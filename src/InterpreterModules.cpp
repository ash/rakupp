// InterpreterModules.cpp — module loading, EVAL, the REPL's hooks, statements and declarations
//
// One of the parts InterpreterParts.h lists; what they share is declared there.
#include <unordered_set>
#include "InterpreterParts.h"
#include "AotModules.h"

namespace rakupp {
static thread_local int t_stageDepth = 0;
namespace {
struct StageLoadTimer {
    size_t idx = (size_t)-1;
    std::chrono::steady_clock::time_point t0;
    StageLoadTimer(const std::string& name, bool fresh) {
        if (!g_stageStats || !fresh) return;
        std::lock_guard<std::mutex> lk(g_stageMu);
        g_stageLoads.push_back({name, t_stageDepth++, 0.0});
        idx = g_stageLoads.size() - 1;
        t0 = std::chrono::steady_clock::now();
    }
    ~StageLoadTimer() {
        if (idx == (size_t)-1) return;
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::lock_guard<std::mutex> lk(g_stageMu);
        g_stageLoads[idx].ms = ms;
        t_stageDepth--;
    }
};
} // namespace

// Does this class DECLARE an attribute of this bare name — its own, or one it
// composed from a role? A composed role's attributes belong to the composer:
// `does R` flattens them into ci->attrs, except for the FIRST `does`, which
// arrives as the PARENT instead and so has to be looked through here.
//
// A real ancestor CLASS is deliberately NOT walked. A private attribute is
// per-class and is never inherited — only the public accessor (`self.attr`)
// crosses the boundary — so a `$!x` in a child naming a PARENT's attribute is
// undeclared, exactly as Rakudo has it. Walking the whole parent chain let that
// through: it compiled code here that Rakudo refuses (silent, and in the
// direction that produces non-portable source), and it meant any mistyped `$!x`
// in a subclass bound quietly to an ancestor's attribute of that name instead
// of being reported.
static bool declaresAttrHere(const ClassInfo* c, const std::string& bare) {
    if (!c) return false;
    for (auto& at : c->attrs) if (at.name == bare) return true;
    if (c->parent && c->parent->isRole && declaresAttrHere(c->parent.get(), bare)) return true;
    for (auto& p : c->extraParents)
        if (p && p->isRole && declaresAttrHere(p.get(), bare)) return true;
    return false;
}

// Per-thread execution registers. One instance per real thread; the GIL still
// serialises who runs. See the declaration in Interpreter.h.
void insertRuntimeMulti(ClassInfo* ci, const std::string& mname, Value cand); // MethodCallPart2.cpp
static bool precompEnabled() { return precompModulesEnabled(); }   // the module half
static const EmbeddedModule* findEmbeddedModule(const std::string& name) {
    auto& m = embeddedModules();
    auto it = m.find(name);
    return it == m.end() ? nullptr : &it->second;
}

// The "version" field of a dist root's META6.json ('' when absent/unreadable).
static std::string metaVersion(const std::string& distRoot) {
    std::ifstream in(distRoot + "/META6.json");
    if (!in) return "";
    std::ostringstream ss; ss << in.rdbuf();
    const std::string& s = ss.str();
    size_t k = s.find("\"version\"");
    if (k == std::string::npos) return "";
    k = s.find(':', k); if (k == std::string::npos) return "";
    k = s.find('"', k); if (k == std::string::npos) return "";
    size_t e = s.find('"', k + 1); if (e == std::string::npos) return "";
    return s.substr(k + 1, e - k - 1);
}

// The `provides` map of a source checkout's META6.json: Rakudo's
// CompUnit::Repository::FileSystem resolves through it, so a distribution may
// put a module anywhere it likes. Lingua::NumericWordForms ships
// `Lingua::NumericWordForms` at lib/Lingua/NumericWordForms/NumericWordForms.rakumod,
// and a path search derived from the NAME cannot find it (16 dists behind it).
// Deliberately a textual scan, like metaVersion above — the file is small and a
// JSON parser is not available this early.
static std::string metaProvidesPath(const std::string& distRoot, const std::string& name) {
    std::ifstream in(distRoot + "/META6.json");
    if (!in) return "";
    std::ostringstream ss; ss << in.rdbuf();
    const std::string& s = ss.str();
    size_t pv = s.find("\"provides\"");
    if (pv == std::string::npos) return "";
    const std::string key = "\"" + name + "\"";
    // search only inside the provides object
    size_t brace = s.find('{', pv);
    if (brace == std::string::npos) return "";
    int depth = 0; size_t end = brace;
    for (; end < s.size(); end++) {
        if (s[end] == '{') depth++;
        else if (s[end] == '}') { depth--; if (!depth) break; }
    }
    size_t k = s.find(key, brace);
    if (k == std::string::npos || k > end) return "";
    k = s.find(':', k + key.size()); if (k == std::string::npos || k > end) return "";
    // the value may be a plain string or an object with a "file" key
    size_t q = s.find_first_not_of(" \t\r\n", k + 1);
    if (q == std::string::npos) return "";
    if (s[q] == '{') { q = s.find("\"file\"", q); if (q == std::string::npos || q > end) return "";
                       q = s.find(':', q); if (q == std::string::npos) return "";
                       q = s.find_first_not_of(" \t\r\n", q + 1); }
    if (q == std::string::npos || s[q] != '"') return "";
    size_t e2 = s.find('"', q + 1); if (e2 == std::string::npos) return "";
    return s.substr(q + 1, e2 - q - 1);
}


// ===========================================================================
// DATA-PLAN P6: the compiler answers `use Data::Native`
//
// On this engine the tag families are BUILT IN, so a program that says
// `use Data::Native` should load nothing at all — not the module, and not the
// eight or nine reference distributions its portable copy depends on. Measured
// before this existed: `use Data::Native` cost 26 ms, `use Digest::Native`
// 20.6, `use JSON::Native` 17.0 — all of it loading fallbacks that never ran,
// because the extension or the builtin answered every call.
//
// The COMPILER decides, never the module — so a distribution carries no
// version logic and stays exactly what it is on Rakudo. It defers in three
// cases, and this function is where all three live:
//
//   1. A search path names one. `-I` and `use lib` win, so a distribution is
//      tested as an ordinary module — which is what `rakupp test <Dist>` does,
//      running the suite with the dist's own lib in front. Without this the
//      suite would silently exercise the engine instead of the code it was
//      written for, which is the hole EXTENSIONS.md records from an early
//      JSON::Native.
//   2. A version was asked for. `use Data::Native:ver<0.3+>` is a request for
//      a specific distribution; the compiler has one interface version and no
//      business pretending to satisfy a range it was not told about.
//   3. The family is not implemented here yet. Only what the engine can
//      actually answer is claimed — see kDataNativeTags.
//
// The shape is `use Test`'s (rtUse, above), NOT isShadowedModule()'s: that one
// puts rakulib AHEAD of -I on purpose, which is exactly wrong here.
// ===========================================================================

// One row per family the compiler can answer, with the primitive that backs
// each name. Kept beside registerBuiltins' spelling — `rakupp-<function>` —
// so adding a primitive and putting it in a tag is one edit, not two files.
struct DataNativeTag {
    const char* tag;
    const char* names[20];     // NULL-terminated; `digest` alone is fifteen
};
static const DataNativeTag kDataNativeTags[] = {
    // json (P1), csv (P2) and digest (P3) are here; zlib and random join the
    // table as P4 and P5 register theirs. Until a tag's primitives all exist
    // the module is left to answer it, which is why the check below is by
    // PRIMITIVE and not by tag name.
    { "json", { "from-json", "to-json", "json-backend", nullptr } },
    { "csv",  { "from-csv", "to-csv", "csv-backend", nullptr } },
    { "digest", { "md5", "sha1", "sha224", "sha256", "sha384", "sha512",
                  "md5-hex", "sha1-hex", "sha224-hex", "sha256-hex", "sha384-hex", "sha512-hex",
                  "hmac", "hmac-hex", "digest-backend", nullptr } },
    { "zlib", { "compress", "uncompress", "gzslurp", "gzspurt",
                "crc32", "adler32", "zlib-backend", nullptr } },
    { "random", { "crypt_random_buf", "crypt_random", "crypt_random_uniform",
                  "random-backend", nullptr } },
    { nullptr, { nullptr } }
};

// Which module names the compiler claims, and the tags each covers.
struct DataNativeModule {
    const char* module;
    // The INTERFACE version this engine implements for that name. An installed
    // distribution NEWER than this is one that knows something the compiler does
    // not, so the compiler steps aside and lets it load — which is the whole
    // mechanism by which the distributions can be updated on their own schedule
    // without waiting for an engine release. Bump it here when a tag's surface
    // changes, in step with the distribution's own META6 version.
    const char* interfaceVer;
    const char* tags[6];       // NULL-terminated
};
static const DataNativeModule kDataNativeModules[] = {
    { "Data::Native",           "0.0.1", { "json", "csv", "digest", "zlib", "random", nullptr } },
    { "JSON::Native",           "0.0.2", { "json", nullptr } },
    { "CSV::Native",            "0.0.1", { "csv", nullptr } },
    { "Digest::Native",         "0.0.1", { "digest", nullptr } },
    { "Compress::Zlib::Native", "0.0.1", { "zlib", nullptr } },
    { nullptr, nullptr, { nullptr } }
};

// The PARSER needs the same answer, and cheaply. It scans a `use`d module's
// source for exported operators and sigilless constants — which for a module
// the compiler answers is pure waste, and not free: JSON::Native is installed
// on this box, so the scan found it, read it, and cost 10.2 ms on a program
// that then never loaded it.
//
// This is the table lookup only. The parser pairs it with its own on-disk
// check, because a `-I` path naming a real distribution has to be scanned
// normally — that is the same rule dataNativeUse applies, spelled where the
// parser can afford it.
bool rakuppCompilerAnswersModule(const std::string& name) {
    for (const DataNativeModule* m = kDataNativeModules; m->module; m++) {
        if (name != m->module) continue;
        // Claimed only if at least one of its tags is actually implemented.
        for (const char* const* t = m->tags; *t; t++)
            for (const DataNativeTag* d = kDataNativeTags; d->tag; d++)
                if (std::string(*t) == d->tag) return true;
        return false;
    }
    return false;
}

// The names a compiler-answered `use` puts in scope. The declaration checker
// needs them: without this it sees `to-json` with no `use` it can attribute it
// to, treats it as a candidate, and goes looking for the module's source to
// clear it — which costs 10 ms when the name happens to be installed, and would
// REFUSE THE PROGRAM if it were not findable at all.
void rakuppCompilerAnsweredNames(const std::string& module, std::set<std::string>& out) {
    for (const DataNativeModule* m = kDataNativeModules; m->module; m++) {
        if (module != m->module) continue;
        for (const char* const* t = m->tags; *t; t++)
            for (const DataNativeTag* d = kDataNativeTags; d->tag; d++)
                if (std::string(*t) == d->tag)
                    for (const char* const* n = d->names; *n; n++) out.insert(*n);
        return;
    }
}

bool Interpreter::dataNativeUse(const std::string& name,
                                const std::vector<std::string>& importArgs,
                                bool doImport, const std::string& verReq) {
    const DataNativeModule* mod = nullptr;
    for (const DataNativeModule* m = kDataNativeModules; m->module; m++)
        if (name == m->module) { mod = m; break; }
    if (!mod) return false;

    // (2) a versioned request belongs to the store, not to us. `use
    // Data::Native:ver<0.2+>` asked for something specific; answering it from
    // the compiler's own interface would be inventing a version.
    if (!verReq.empty()) return false;

    // (1) an explicit search path wins — this is what makes `rakupp test`
    // exercise the distribution rather than the engine.
    //
    // DIRECTORY entries only, and that distinction is the whole rule. libPaths_
    // also carries the installed repositories, so asking the full resolver here
    // meant an INSTALLED copy beat the compiler — which is exactly backwards,
    // and left `use JSON::Native` loading the distribution and its JSON::Fast
    // dependency on a machine that had it installed.
    if (moduleFileOnPath(name, libPaths_, sixE())) return false;

    // (2b) an INSTALLED distribution newer than the interface this engine
    // implements knows something the compiler does not, so the compiler steps
    // aside. That is what lets the distributions be released on their own
    // schedule: install 0.2.0 of one and it takes over on this engine with no
    // engine release, exactly as it would on Rakudo.
    //
    // One store lookup, against loading the module and its whole dependency
    // tree — which is the cost this interception exists to avoid. `entry` and
    // `lines` are discarded; only the version in lines[0] is being asked about.
    if (mod->interfaceVer && *mod->interfaceVer) {
        std::string nameSha = sha1hex(name);
        for (auto& repo : repoPrefixesFor(libPaths_)) {
            std::string entry;
            std::vector<std::string> lines;
            if (!pickInstalledDist(repo + "/short/" + nameSha, "", entry, lines)) continue;
            if (!lines.empty() && verCmp(lines[0], mod->interfaceVer) > 0) return false;
        }
    }

    // Which of this module's tags the engine can actually answer. A tag whose
    // primitives are not registered is not claimed, so a family the engine has
    // no primitives for is still left to its distribution.
    std::vector<const DataNativeTag*> answered;
    for (const char* const* t = mod->tags; *t; t++) {
        for (const DataNativeTag* d = kDataNativeTags; d->tag; d++) {
            if (std::string(*t) != d->tag) continue;
            bool all = true;
            for (const char* const* n = d->names; *n && all; n++)
                all = builtins_.count(std::string("rakupp-") + *n) != 0;
            if (all) answered.push_back(d);
        }
    }
    // (3) nothing here yet for this module — let it load
    if (answered.empty()) return false;

    // `use Data::Native <json csv>` names tags; a bare `use` takes them all.
    // An unknown tag is an ERROR rather than a silent no-op: a typo'd <crytpo>
    // exporting nothing is a bad afternoon.
    std::vector<std::string> want(importArgs.begin(), importArgs.end());
    bool all = want.empty();
    for (auto& w : want) if (w == "all") all = true;
    if (!all) {
        for (auto& w : want) {
            bool known = false;
            for (const char* const* t = mod->tags; *t && !known; t++) known = (w == *t);
            if (!known)
                throw RakuError{Value::typeObj("X::AdHoc"),
                    name + ": no such tag <" + w + ">"};
        }
    }

    // `need` is not ours to answer. It asks for the COMPUNIT — the caller wants
    // to reach the package by name — and the compiler has builtins to offer, not
    // a package. Falling through lets `need CSV::Native` load the distribution
    // and `CSV::Native::parse-raku` resolve, which is also what makes the
    // module's pure-Raku half usable as an independent oracle in the tests.
    if (!doImport) return false;

    // The claim registry, so a **::Native module loading later stands aside
    // rather than colliding — on Rakudo two modules exporting one name is a
    // hard compile error, and the protocol has to hold on both engines.
    // `PROCESS::<%DATA-NATIVE-CLAIMED>` — which the engine resolves as the
    // dynamic `%*DATA-NATIVE-CLAIMED`, not as a plain global of that name.
    // Created here if the program has not made it, since the compiler is
    // usually the first to claim anything.
    Value* claimed = nullptr;
    if (global_) {
        auto it = global_->vars.find("%*DATA-NATIVE-CLAIMED");
        if (it == global_->vars.end()) {
            global_->define("%*DATA-NATIVE-CLAIMED", Value::makeHash());
            it = global_->vars.find("%*DATA-NATIVE-CLAIMED");
        }
        if (it != global_->vars.end()) claimed = &it->second;
    }
    for (auto* d : answered) {
        bool wanted = all;
        for (auto& w : want) if (w == d->tag) wanted = true;
        if (!wanted) continue;
        if (claimed && claimed->hash()) (*claimed->hash())[d->tag] = Value::boolean(true);
        // Hand out the PRIMITIVE ITSELF, never a Raku wrapper around it:
        // measured, an exported builtin costs 0.948 us a call and one Raku
        // frame of indirection would add 33%. This is the same Callable shape
        // `&::('rakupp-from-json')` yields, cached per builtin the same way, so
        // the two spellings are the identical object.
        for (const char* const* n = d->names; *n; n++) {
            std::string prim = std::string("rakupp-") + *n;
            auto bit = builtins_.find(prim);
            if (bit == builtins_.end() || !tctx_.cur) continue;
            const Value* ref = builtinRef(prim);
            if (!ref) continue;
            tctx_.cur->define(std::string("&") + *n, *ref);
        }
    }
    return true;
}

const Value* Interpreter::builtinRef(const std::string& name) {
    auto rit = builtinRefs_.find(name);
    if (rit != builtinRefs_.end()) return &rit->second;
    auto bit = builtins_.find(name);
    if (bit == builtins_.end()) return nullptr;
    Value code; code.t = VT::Code; code.setCode(makePayload<Callable>());
    // A tag primitive answers to its EXPORTED name, whichever spelling reached
    // it first. `rakupp-` is the adoption hook another engine would register
    // under; `md5` is what the routine is called in the program that imported
    // it, so that is what `&md5.name` and a backtrace should say. Without this
    // the answer depended on whether `&::('rakupp-md5')` or a `use` ran first.
    code.code()->name = name;
    if (name.compare(0, 7, "rakupp-") == 0) {
        std::string bare = name.substr(7);
        for (const DataNativeTag* d = kDataNativeTags; d->tag; d++)
            for (const char* const* n = d->names; *n; n++)
                if (bare == *n) code.code()->name = bare;
    }
    code.code()->builtin = bit->second;
    return &(builtinRefs_[name] = code);
}

// A package name inside `Foo::<x>` is looked up from where it is written,
// as `Foo::{…}` (through .WHO) already is: in `unit module M`,
// `my package EXPORT::DEFAULT { }` binds the lexical EXPORT::DEFAULT to
// M::EXPORT::DEFAULT, and `EXPORT::DEFAULT::<&f> = &f` is a slot in THAT
// package — the one the importer reads. Sparrow6::DSL builds its whole
// export list this way. Only a name that resolves to a LONGER name of the
// same tail is rewritten; anything else is the package it says.
std::string Interpreter::pkgSymbolName(const std::string& name) const {
    std::string pkg, key;
    if (!tctx_.cur || !splitPkgSymbol(name, pkg, key)) return name;
    const Value* t = tctx_.cur->find(pkg);
    if (!t || t->t != VT::Type) return name;
    const std::string full(t->s.c_str());
    if (full.size() <= pkg.size() + 2 ||
        full.compare(full.size() - pkg.size() - 2, std::string::npos, "::" + pkg) != 0) return name;
    size_t off = std::strchr("$@%&", name[0]) ? 1 : 0;
    return name.substr(0, off) + full + name.substr(off + pkg.size());
}

// Would importing `key` = `val` REPLACE a routine already in scope with something
// that cannot be called? Some of rakupp's built-in module surfaces publish a name
// as a bare TYPE-OBJECT placeholder — NativeCall's export list carries
// `&trait_mod:<is>` that way, since the trait it stands for is built into the
// engine and has no Raku routine to point at. Harmless until another module
// re-exports that same list: NativeLibs copies NativeCall's exports into its own
// (`CHECK for NativeCall::EXPORT::.keys`), so `use NativeLibs` dropped the
// placeholder over whatever `trait_mod:<is>` candidates were already imported —
// and every user-defined `is` trait in the file then did NOTHING, silently. That
// is how Red lost its columns: `use Red` pulls NativeLibs in, and `is column`
// stopped being a trait at all (issue #77). A placeholder never displaces a real
// routine; two real routines still follow the ordinary last-wins import rule.
bool Interpreter::importWouldShadowRoutine(const std::string& key, const Value& val) {
    if (key.empty() || key[0] != '&' || val.t == VT::Code) return false;
    if (!rtIsDefined(val) || val.t == VT::Type) {
        Value* cur = tctx_.cur ? tctx_.cur->find(key) : nullptr;
        if (cur && cur->t == VT::Code) return true;
    }
    return false;
}

// Every tag a module's `is export(:a :b)` traits name, read from its SOURCE —
// the one record covering every kind of declaration (a sub keeps its tags in
// the AST; a variable, constant, class or enum does not). Null when the module
// builds its own EXPORT (a `sub EXPORT` or an `EXPORT::` package), whose tags
// only running it would tell.
static std::optional<std::set<std::string>> scanExportTags(const std::string& src) {
    if (src.empty() || src.find("EXPORT") != std::string::npos) return std::nullopt;
    std::set<std::string> tags{"DEFAULT", "ALL", "MANDATORY"};
    for (size_t p = 0; (p = src.find("export", p)) != std::string::npos; ) {
        size_t q = p + 6;
        p = q;
        while (q < src.size() && (src[q] == ' ' || src[q] == '\t')) q++;
        if (q >= src.size() || src[q] != '(') continue;
        int depth = 0;
        for (size_t k = q; k < src.size(); k++) {
            const char c = src[k];
            if (c == '(') depth++;
            else if (c == ')') { if (--depth == 0) break; }
            else if (c == ':' && depth == 1 && k + 1 < src.size() &&
                     (ascii::isalpha((unsigned char)src[k + 1]) || src[k + 1] == '_')) {
                size_t e = k + 1;
                while (e < src.size() && (ascii::isalnum((unsigned char)src[e]) || src[e] == '_' || src[e] == '-')) e++;
                tags.insert(src.substr(k + 1, e - k - 1));
                k = e - 1;
            }
        }
    }
    return tags;
}

// `use Foo :NoSucTag` — a tag the module exports nothing under is an error,
// not an empty import (S11-modules/importing.t). `:&name` names a symbol, and
// `!flag` is an argument for a module's own EXPORT; neither is judged.
void Interpreter::checkImportTags(const std::string& name, const std::vector<std::string>& importArgs) {
    auto ti = moduleExportTags_.find(name);
    if (ti == moduleExportTags_.end()) return;
    for (auto& t : importArgs)
        if (!t.empty() && !std::strchr("&$@%!", t[0]) && !ti->second.count(t))
            throwTypedV("X::Import::NoSuchTag", {{"source-package", Value::str(name)}, {"tag", Value::str(t)}},
                        "Module '" + name + "' exports nothing under the tag '" + t + "'");
}

// A `use`/`need`/`require` merges what the loaded unit declared into the
// importing unit's view of the packages (stashImport) — every form of load
// but a repository's `.need`, which hands the unit back without merging.
void Interpreter::loadModule(const std::string& name, const std::vector<std::string>& importArgs, bool doImport, bool quiet, const std::string& verReq, bool requireForm, bool mergeGlobals) {
    const std::string importer = stashUnitHere();
    {   // (the module's own declarations are ITS, whatever unit a hoisted
        // type being built asked for it — see materializePendingType)
        struct Override { std::optional<std::string>& o; std::optional<std::string> saved;
                          ~Override() { o = saved; } } ov{stashUnitOverride_, stashUnitOverride_};
        stashUnitOverride_.reset();
        try { loadModuleImpl(name, importArgs, doImport, quiet, verReq, requireForm); }
        catch (...) { unitStash_.erase(name); throw; }   // a failed load merges nothing, now or on a retry
    }
    if (mergeGlobals && unitStash_.count(name)) stashImport(importer, name);
}

void Interpreter::loadModuleImpl(const std::string& name, const std::vector<std::string>& importArgs, bool doImport, bool quiet, const std::string& verReq, bool requireForm) {
    // the evaluated `use Mod EXPR, …` arguments belong to THIS load — take them
    // now, before the module's own `use` statements run through here again
    const ValueList useExtra = std::move(useExprArgs_);
    useExprArgs_.clear();
    StageLoadTimer stageTimer(name, !loadedModules_.count(name)); // --stagestats: a first load, timed
    // DATA-PLAN P6. Before anything is looked for on disk: this engine may be
    // able to answer the `use` itself, in which case nothing loads at all.
    if (dataNativeUse(name, importArgs, doImport, verReq)) return;
    // `use if;` — the ecosystem dist whose whole job is the `:if(EXPR)`
    // colonpair on a later `use`. The parser reads that pair itself
    // (UseStmt::ifCond) and exec() skips the load when it is false, so the
    // dist has nothing left to do here; loading its file (found under `-I lib`
    // when its own suite runs, or from the store) only ran its actions-only
    // slang into the refusal. Answered before the search, like a pragma.
    if (name == "if" && !requireForm) return;
    // A uses B, B uses A: the second `use A` arrives while A's own body is
    // still running. Rakudo refuses it — its precompiler reports the cycle
    if (modulesLoading_.count(name) && !requireForm)
        throwTypedV("X::AdHoc", {},
                    "Circular module loading detected trying to precompile " + name);
    if (loadedModules_.count(name) && doImport && !requireForm) checkImportTags(name, importArgs);
    if (loadedModules_.count(name)) {
        // a module `use`d again, anywhere, is everyone's: its classes stop being
        // the first importing block's alone
        if (!requireScoped_.empty() || !needHidden_.empty()) {
            auto mc = moduleClasses_.find(name);
            if (mc != moduleClasses_.end())
                for (auto& c : mc->second) { requireScoped_.erase(c); needHidden_.erase(c); }
        }
        // The module body ran once and stays run — but a repeat `use` still
        // IMPORTS into the new scope. Only the `sub EXPORT(*@_)` protocol needs
        // replaying (its symbols are defined lexically, per use-statement, and
        // may depend on the args: `use JSON::Fast <immutable !pretty>` in one
        // block, plain `use JSON::Fast` in the next). Ordinary `is export`
        // symbols were published to global and are already visible.
        // A repeat `use Mod :tag` imports the selective exports its tag now
        // selects into THIS scope (the module body already ran; the subs were
        // withheld then). DEFAULT/MANDATORY tags import unconditionally.
        if (doImport) {
            auto sit = moduleSelectiveExports_.find(name);
            if (sit != moduleSelectiveExports_.end()) {
                // `:&name` names the SYMBOL, not a tag — strip the sigil here as
                // the first-load path does, and take a direct name request as a
                // reason to import. Without it a module already loaded by
                // somebody else's narrower `use` could never be widened:
                // PDF::COS::Dict gets in first with `:&from-ast, :&ast-coerce`,
                // so PDF::IO::Serializer's `use PDF::COS::Util :&to-ast` imported
                // nothing at all and every object it serialized came out `null`.
                std::set<std::string> reqTags, reqNames;
                for (auto& a : importArgs) {
                    if (!a.empty() && std::strchr("&$@%", a[0])) {
                        reqTags.insert(a.substr(1));
                        reqNames.insert(a.substr(1));
                    }
                    else reqTags.insert(a);
                }
                bool reqAll = reqTags.count("ALL") != 0; // `:ALL` imports every export
                const bool reqDefault = reqTags.empty() || reqTags.count("DEFAULT") != 0; // a selective `use Mod :tag` does not imply DEFAULT
                for (auto& se : sit->second) {
                    std::string bare = (!se.key.empty() && std::strchr("&$@%", se.key[0]))
                                     ? se.key.substr(1) : se.key;
                    bool want = reqAll || reqNames.count(bare) != 0;
                    for (const std::string& tag : se.tags)
                        if ((tag == "DEFAULT" && reqDefault) || tag == "MANDATORY" || reqTags.count(tag)) { want = true; break; }
                    // a hand-filled EXPORT::TAG package answers its own tag only
                    // (`:ALL` is the EXPORT::ALL package, `:&name` an entry there)
                    if (se.stashOnly) {
                        const std::string& tag = se.tags.front();
                        want = tag == "MANDATORY" || (tag == "DEFAULT" ? reqDefault : reqTags.count(tag) != 0) ||
                               (tag == "ALL" && reqNames.count(bare));
                    }
                    // an imported MULTI joins a same-named multi the scope
                    // already declared (`use Foo; multi waz($x) {…}` sees both)
                    if (want && se.value.t == VT::Code && se.value.code() &&
                        se.value.code()->isMultiDispatcher) {
                        Value* have = tctx_.cur->local(se.key);
                        if (have && have->t == VT::Code && have->code() && have->code()->isMultiDispatcher &&
                            have->code() != se.value.code()) {
                            auto& cands = have->code()->candidates;
                            for (auto& c : se.value.code()->candidates) {
                                bool dup = false;
                                for (auto& h : cands)
                                    if (h.code() && c.code() && h.code()->body == c.code()->body &&
                                        h.code()->params == c.code()->params) { dup = true; break; }
                                if (!dup) cands.push_back(c);
                            }
                            continue;
                        }
                    }
                    if (want && !mainlineSubNames_.count(se.key) &&
                        !importWouldShadowRoutine(se.key, se.value))
                        tctx_.cur->define(se.key, se.value);
                }
            }
        }
        auto it = moduleExportSubs_.find(name);
        if (doImport && it != moduleExportSubs_.end()) {
            ValueList eargs;
            for (auto& s : importArgs) eargs.push_back(Value::str(s));
            for (auto& v : useExtra) eargs.push_back(v);
            try {
                Value res = callCallable(it->second, eargs);
                if (res.t == VT::Hash && res.hash())
                    for (auto& kv : *res.hash())
                        if (!importWouldShadowRoutine(kv.first, kv.second))
                            tctx_.cur->define(kv.first, kv.second);
            } catch (RakuError& e) {
                // For `use`/`need` a module's own refusal is the `use` failing,
                // and it propagates — Rakudo aborts compilation and exits 1.
                // This used to warn and carry on at exit 0, which made
                // export-time validation advisory: `use M <typo>` imported
                // nothing and said so only on stderr, then ran the program.
                //
                // THREE things stay a warning, and none is a matter of taste:
                //
                // 1. Anything the module did not raise ITSELF. A module
                //    refusing an import writes `die "..."`, which arrives here
                //    as X::AdHoc; an ENGINE GAP arrives as a typed exception —
                //    X::CompUnit::UnsatisfiedDependency for something rakupp
                //    cannot supply, X::Method::NotFound for a hook it does not
                //    implement. Propagating those turns a gap into a broken
                //    dist: a 148-dist ecosystem shard measured exactly that,
                //    and Polyglot::Regexen (whose EXPORT wants QAST) went from
                //    `pass` to `self-fail` on the strength of it. Its tests do
                //    not need the export that failed; today's warning lets it
                //    keep working, and this must not take that away.
                // 2. `if` — its EXPORT necessarily fails here. Both
                //    implementations of that dist patch Rakudo compiler
                //    internals; rakupp supplies the `:if` adverb natively, so
                //    the noise would only pollute every dependent's test log.
                // 3. `require` (the quiet caller, and the bareword form) —
                //    Rakudo does not run a module's EXPORT for `require` AT
                //    ALL, in any of its three spellings; measured. rakupp does,
                //    so a throw here would fail a load Rakudo completes.
                const std::string exType =
                    e.payload.t == VT::Type ? e.payload.s
                  : (e.payload.t == VT::Object && e.payload.obj() && e.payload.obj()->cls)
                        ? e.payload.obj()->cls->name : std::string();
                const bool moduleRaised = exType == "X::AdHoc";
                if (name != "if" && !quiet && !requireForm && moduleRaised) throw;
                // An L10N dist's EXPORT does one thing: install a slang into
                // `$*LANG`. When the token rewrite has already done that job
                // (`applyL10NSlang`), its failure here is expected and silent —
                // but only for a language we really did handle.
                if (name != "if" && !l10nApplied_.count(name) && !slangModules_.count(name))
                    std::cerr << "===WARNING=== Module " << name
                              << " EXPORT failed: " << e.message << "\n";
            }
        }
        return;
    }
    noteSymbolMutation("module load (use/need)");
    loadedModules_.insert(name);
    modulesLoading_.insert(name);
    struct LoadingG { std::set<std::string>& s; std::string n; ~LoadingG() { s.erase(n); } } loadingG{modulesLoading_, name};

    // RAKUPP_TRACE also reports where a module's load time went: `parse` is the
    // lex+parse of its source, `run` is executing its top level (declarations,
    // BEGIN blocks, side effects). The split is what decides whether caching a
    // parsed AST would be worth anything for a given dependency tree.
    const bool traceLoad = std::getenv("RAKUPP_TRACE") != nullptr;
    auto nowMs = [] {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    };
    // Everything a module load does ONCE THE TREE EXISTS — module scope, hoisting,
    // executing the top level, publishing to global, the EXPORT protocol. Shared by
    // all three ways a tree arrives: embedded in the binary, out of the precomp
    // cache, or freshly parsed.
    // How the tree arrived, for RAKUPP_TRACE: set by whichever path produced it.
    double howMs = 0;                       // ms spent getting the tree
    const char* howLabel = "embedded";
    std::optional<std::set<std::string>> srcExportTags;   // set from the source, when there is one
    auto loadParsed = [&](std::shared_ptr<Program> prog, const std::string& finish) {
        // (the classes that exist before this module's body runs — see the end)
        std::unordered_set<std::string> classesBefore;
        for (auto& kv : classes_) classesBefore.insert(kv.first);
        double tRun = traceLoad ? nowMs() : 0;
        struct TGuard {
            bool on; const std::string& nm; double pms, t0;
            const std::function<double()>& now;
            const char* how;   // "parse", "precomp" or "embedded"
            ~TGuard() { if (on) fprintf(stderr, "[Load] %s %s %.1f ms, run %.1f ms\n",
                                        nm.c_str(), how, pms, now() - t0); }
        } tg{traceLoad, name, howMs, tRun, nowMs, howLabel};
        { std::unique_lock<std::mutex> kl(sharedMut_, std::defer_lock); if (parallelMode_) kl.lock(); keptPrograms_.push_back(prog); }
        // this module is now the executing unit (bare-name forward references
        // resolve against ITS declarations while its top level runs)
        unitPush(prog.get());
        struct UnitGuard { Interpreter& I; ~UnitGuard() { I.unitPop(); } } unitG{*this};
        auto saved = tctx_.cur;
        std::string savedFinish = finishData_;
        finishData_ = finish; // this module's $=finish data block
        // Load the module into its OWN scope (chained to global). Its subs see one
        // another there; then everything is republished to global EXCEPT a
        // non-`is export` sub whose name collides with a built-in — that one stays
        // module-private so it can't shadow the built-in for the importer. This is
        // what lets Test::Util's `our sub run` coexist with the built-in `run`:
        // Test::Util's own `is_run` still resolves `run` (via its module closure),
        // while an importer's bare `run(...)` reaches the built-in.
        auto moduleEnv = std::make_shared<Env>(); moduleEnv->parent = global_;
        moduleEnv->unitFrame = true;
        unitOfEnv_[moduleEnv.get()] = name;
        unitStash_[name];
        // %?RESOURCES is LEXICAL to the compiling module: bind it in the module env so
        // subs defined here close over their OWN resources and still resolve them when
        // called later, after the load-time resourceStack_ entry has been popped.
        if (!resourceStack_.empty())
            moduleEnv->define("%?RESOURCES", resourceStack_.back());
        // …and $?DISTRIBUTION, which is lexical to the compiling module in the same way
        if (!distStack_.empty() && distStack_.back().t != VT::Any)
            moduleEnv->define("$?DISTRIBUTION", distStack_.back());
        // Which subs may shadow a built-in for the importer — the same scan
        // codegen runs over the module graph, so `--exe` agrees with this.
        std::set<std::string> exported;
        collectExportedSubNames(prog->stmts, exported);
        // (an exported CODE VARIABLE is exported like a sub: `our &my-counter is
        // export`, set in a BEGIN — S10-packages/precompilation.t)
        for (auto& st : prog->stmts) {
            if (!st || st->kind != NK::ExprStmt) continue;
            const Expr* e = static_cast<const ExprStmt*>(st.get())->e.get();
            if (e && e->kind == NK::Assign) e = static_cast<const Assign*>(e)->target.get();
            if (e && e->kind == NK::VarExpr) {
                const auto* ve = static_cast<const VarExpr*>(e);
                if (ve->declExport && ve->name.size() > 1 && ve->name[0] == '&') exported.insert(ve->name.substr(1));
            }
        }
        // …and the `our sub`s a module declares with no package of its own in
        // force: those live in GLOBAL, where an unqualified call finds them
        // once no lexical scope has the name (6.c/MISC/bug-coverage.t — a
        // module file of nothing but `our sub module-transform {…}`). Once
        // `unit module Foo;` has run, an `our sub` is `Foo::`'s instead.
        std::set<std::string> globalOurSubs;
        for (auto& st : prog->stmts) {
            if (!st) continue;
            if (st->kind == NK::ClassDecl) {
                auto* cd = static_cast<const ClassDecl*>(st.get());
                if (cd->isPackage && !cd->bracedBody && cd->body.empty()) break;   // `unit module Foo;`
                continue;
            }
            if (st->kind == NK::SubDecl && static_cast<const SubDecl*>(st.get())->isOur)
                globalOurSubs.insert("&" + static_cast<const SubDecl*>(st.get())->name);
        }
        // Selective exports: `is export(:foo)` publishes only when the importer
        // asks (`use Mod :foo`). Build the name→tags map and the requested-tag
        // set; a tag named DEFAULT or MANDATORY always publishes.
        std::map<std::string, std::vector<std::string>> exportTagsByName;
        collectExportTagsByName(prog->stmts, exportTagsByName);
        std::set<std::string> requestedTags;
        // `use Mod :&name` names the SYMBOL, not a tag. A module publishes such a
        // routine under the bare tag (`is export(:ast-coerce)`), so the sigil is
        // stripped here — and the bare name is accepted as a DIRECT request too,
        // so a plainly-exported sub still arrives. PDF::COS::Dict imports its AST
        // helpers as `use PDF::COS::Util :&from-ast, :&ast-coerce`, and neither
        // name reached it.
        std::set<std::string> requestedNames;
        for (auto& a : importArgs) {
            if (!a.empty() && std::strchr("&$@%", a[0])) {
                requestedTags.insert(a.substr(1));
                requestedNames.insert(a.substr(1));
            }
            else requestedTags.insert(a);
        }
        if (srcExportTags) moduleExportTags_[name] = *srcExportTags;
        // `use Mod :ALL` is the catch-all: it imports EVERY exported sub whatever
        // its tag (Text::Utils, Abbreviations test with `:ALL`).
        bool wantAll = requestedTags.count("ALL") != 0;
        // `use Mod :tag` asks for THAT tag: a plain `is export` is `:DEFAULT`, and
        // DEFAULT is not implied by a selective request (Rakudo: `use Mod :extra`
        // leaves the default exports undeclared). A plain `use Mod` (no tags),
        // `:DEFAULT` and `:ALL` still bring them in.
        const bool selectiveOnly = !requestedTags.empty() && !wantAll && !requestedTags.count("DEFAULT");
        auto tagWithheld = [&](const std::string& bare) -> bool {
            if (wantAll) return false;
            if (requestedNames.count(bare)) return false;   // `:&name` asked for it by name
            auto it = exportTagsByName.find(bare);
            if (it == exportTagsByName.end()) return selectiveOnly && exported.count(bare) != 0; // plain `is export` = :DEFAULT; not exported = untouched
            for (const std::string& tag : it->second) {
                // `:MANDATORY` is exported whatever the importer asked for —
                // that is what the tag means.
                if (tag == "MANDATORY") return false;
                // `:DEFAULT` is NOT a free pass. Naming any tag REPLACES the
                // default set rather than adding to it, so `use Mod :beta`
                // leaves `is export(:DEFAULT)` undeclared in the importer, and
                // `is export(:DEFAULT, :beta)` comes in on the strength of
                // `:beta` alone. Treating DEFAULT as always-publishing handed
                // a selective importer the whole default set — which is most
                // of a module's surface, and the reason its author reached for
                // tags in the first place.
                if (tag == "DEFAULT") { if (!selectiveOnly) return false; continue; }
                if (requestedTags.count(tag)) return false;
            }
            return true; // every tag is selective and none was requested
        };
        tctx_.cur = moduleEnv;
        // A module loaded at RUN time (`require`, `$repo.need`) runs its
        // mainline inside the caller's dynamic scope: a `my $*x` around the
        // require is visible there, and in whatever that module loads in
        // turn. A compile-time `use` gets no such frame.
        struct DynFrameG {
            std::vector<Env*>& st; Env* e;
            ~DynFrameG() { if (e && !st.empty() && st.back() == e) st.pop_back(); }
        } dynFrameG{tctx_.dynStack, (requireForm || runtimeLoadDepth_ > 0) && saved ? saved.get() : nullptr};
        if (dynFrameG.e) tctx_.dynStack.push_back(dynFrameG.e);
        auto savedPkg = curPkgEnv_; curPkgEnv_ = moduleEnv; // `our sub` in the module installs here, not main's global
        // A `unit module Foo;` sets pkgPrefix for the rest of the file so its
        // `our sub`s publish qualified (Foo::name); save/restore so it can't leak
        // into the importing program.
        auto savedModPrefix = tctx_.pkgPrefix; tctx_.pkgPrefix.clear();
        // The language revision is per COMPILATION UNIT. A module opening with
        // `use v6.e.PREVIEW;` (or any other pragma) must not leave the importer
        // running at that revision — Test::Util's own `use v6;` was silently
        // re-versioning every Roast file that loads it.
        int savedLangRev = langRev_;
        // …and so is the RakuAST pragma: a module that wants the names has to
        // ask for them itself, and must not leak them back to its importer.
        bool savedRakuAst = rakuAstPragma_;
        auto publish = [&] {
            // The EXPORT packages themselves must NAME something, or
            // `::('Mod::EXPORT::ALL')` finds no symbol to ask .WHO of.
            if (!name.empty())
                for (const char* which : {"::EXPORT", "::EXPORT::DEFAULT", "::EXPORT::ALL"})
                    global_->define(name + which, Value::typeObj(name + which));
            for (auto& kv : moduleEnv->vars) {
                const std::string& k = kv.first;
                if (k == "&EXPORT") continue; // per-module export protocol sub — never republished
                // …nor a compile-time `?` variable: `$?DISTRIBUTION`, `%?RESOURCES` are
                // LEXICAL to the module (its routines close over them), and the
                // program's own `$?DISTRIBUTION` stays undefined (cur-current-distribution.t)
                if (k.size() > 2 && k[1] == '?' && std::strchr("$@%&", k[0])) continue;
                // …nor a type the module declares and does NOT export: `role Node`
                // inside `unit module YAMLish` is YAMLish::Node, reachable by that
                // name alone. Published under its short name, it replaced the
                // `Node` an earlier module exported, and Cro's template builder,
                // loaded first, typed its variables with YAMLish's role (#116).
                if (!name.empty() && !k.empty() && !std::strchr("$@%&", k[0]) &&
                    kv.second.t == VT::Type && kv.second.s == name + "::" + k) {
                    auto cit = classes_.find(kv.second.s);
                    if (cit != classes_.end() && cit->second) {
                        const ClassInfo* ci = cit->second.get();
                        bool exp = ci->decl && ci->decl->isExport;
                        for (auto& v : ci->roleVariants)
                            if (v && v->decl && v->decl->isExport) exp = true;
                        if (!exp) continue;
                    }
                }
                if (k.size() > 1 && k[0] == '&') {
                    std::string bare = k.substr(1);
                    if (!exported.count(bare) && builtins_.count(bare)) continue; // withhold shadower
                    // The module's EXPORT stash, by name. A program that loads a
                    // module at RUNTIME cannot `use` its exports into scope, so it
                    // reaches for them through the stash Rakudo publishes:
                    //   require ::('JSON::Tiny');
                    //   &from-json = ::("JSON::Tiny::EXPORT::DEFAULT::&from-json");
                    // (DBIish's mysql JSON test does exactly this.) Naming them as
                    // globals is enough — a symbolic ref resolves through the same
                    // lookup a variable does.
                    if (exported.count(bare) && !name.empty()) {
                        for (const char* which : {"::EXPORT::DEFAULT::", "::EXPORT::ALL::"}) {
                            // `::("Mod::EXPORT::DEFAULT::&sub")` reads the sigil-first
                            // spelling; `Mod::EXPORT::ALL.WHO<&sub>` reads the other
                            // (WHO builds a package's stash from the qualified globals
                            // under its prefix). NativeLibs' own suite uses both.
                            global_->define("&" + name + which + bare, kv.second);
                            global_->define(name + which + "&" + bare, kv.second);
                        }
                    }
                }
                // a SELECTIVE `is export(:tag)` sub stays out of the importer's
                // lexical scope unless its tag was requested — Rakudo's rule, and
                // what `nok MY::<&prompt>:exists` after a plain `use Prompt` checks.
                // Record it (with its tags) so a later `use Mod :tag` can import it
                // without re-running the module body. A PLAIN `is export` is
                // recorded too, under DEFAULT: the module body runs once, and the
                // second `use` inside a routine that is called twice has to import
                // just as lexically as the first.
                if (k.size() > 1 && k[0] == '&' && !name.empty()) {
                    auto tit = exportTagsByName.find(k.substr(1));
                    if (tit != exportTagsByName.end())
                        moduleSelectiveExports_[name].push_back({k, kv.second, tit->second});
                    else if (exported.count(k.substr(1)))
                        moduleSelectiveExports_[name].push_back({k, kv.second, {"DEFAULT"}});
                }
                if (k.size() > 1 && k[0] == '&' && tagWithheld(k.substr(1))) continue;
                // An import is LEXICAL. A `use` inside a block or a routine body
                // shadows an outer routine of the same name for that scope, and
                // publishing to global alone cannot: Time::gmtime's own
                // `my sub gmtime` says `use P5localtime; populate(gmtime($time))`
                // in its body and means the IMPORTED gmtime — it called itself,
                // 41,367 frames deep. Exported routines only; the rest of a
                // module's scope is its own business.
                // an exported MULTI joins the importing scope's own multi of the
                // same name (`use Foo; multi waz($x) {…}` dispatches to both)
                if (k.size() > 1 && k[0] == '&' && exported.count(k.substr(1)) &&
                    doImport && saved && kv.second.t == VT::Code && kv.second.code() &&
                    kv.second.code()->isMultiDispatcher) {
                    Value* have = saved->local(k);
                    if (have && have->t == VT::Code && have->code() && have->code()->isMultiDispatcher &&
                        have->code() != kv.second.code()) {
                        auto& cands = have->code()->candidates;
                        for (auto& c : kv.second.code()->candidates) {
                            bool dup = false;
                            for (auto& h : cands)
                                if (h.code() && c.code() && h.code()->body == c.code()->body &&
                                    h.code()->params == c.code()->params) { dup = true; break; }
                            if (!dup) cands.push_back(c);
                        }
                        continue;
                    }
                }
                if (k.size() > 1 && k[0] == '&' && exported.count(k.substr(1)) &&
                    doImport && saved && saved.get() != global_.get()) {
                    // …but never over a routine the importing scope DECLARES
                    // ITSELF. Compress::Zlib's `need Compress::Zlib::Raw` sits
                    // in the module mainline beside its own `our sub uncompress`,
                    // and replacing that with Raw's four-argument NATIVE of the
                    // same name segfaulted on the first call.
                    if (!saved->local(k)) saved->define(k, kv.second);
                    // …and ONLY there. The import ends with the block or routine
                    // that made it: published to GLOBAL as well, `{ use Foo }`
                    // left `foo()` callable after the block (S11-modules/lexical.t,
                    // importing.t, S10-packages/export.t) and an EVAL there still
                    // parsed its operators (imported-subs.t). Nor does it reach out
                    // and REPLACE a name the program already has — Time::gmtime's
                    // caller kept calling its own global `&gmtime`.
                    continue;
                }
                // `need Mod` and `use Mod ()` import NOTHING, and publishing the
                // module's exported routines to GLOBAL under their bare names
                // imports them by the back door: `use P5index ()` left &index
                // over the built-in one, so `index("foobar","zzz")` answered -1
                // where it has to answer Nil.
                if (!doImport && k.size() > 1 && k[0] == '&' && exported.count(k.substr(1)))
                    continue;
                // A routine the module does NOT export is its own: its exported
                // subs reach it through their closure, and an `our sub` goes by its
                // package name — never by its bare name in GLOBAL, where it leaked
                // into every importer. That is how a dependency's export became
                // callable from the program (S10-packages/export.t) and how an
                // EVAL parsed an operator the module never exported
                // (S06-operator-overloading/imported-subs.t).
                if (k.size() > 1 && k[0] == '&' && !exported.count(k.substr(1)) && !globalOurSubs.count(k)) continue;
                // never clobber a routine the PROGRAM declared itself
                if (mainlineSubNames_.count(k)) continue;
                // …and never replace a live VIEW with a copy. An `our` variable
                // publishes a proxy onto the module's own slot (see the
                // our-declaration publish), so that a write through the published
                // name reaches the variable the module itself reads. Copying the
                // value over it restores exactly the two-container split that
                // proxy exists to remove — `$TABSTOP = 4` in the caller, and the
                // module's `tabstop()` still answering 8.
                if (Value* g = global_->local(k))
                    if (g->t == VT::Hash && g->hashKind == "Proxy") continue;
                global_->define(k, kv.second);
            }
            // A module may fill its EXPORT::TAG packages by hand instead of (or
            // beside) `is export`:
            //     my package EXPORT::DEFAULT { }
            //     BEGIN for <&config &bash …> { EXPORT::DEFAULT::{$_} = ::($_) }
            // (Sparrow6::DSL re-exports its whole DSL that way.) Each tag package
            // imports what it holds when the `use` asks for that tag — DEFAULT
            // for a plain `use`, MANDATORY always — and `:ALL` reads only the
            // EXPORT::ALL package, as Rakudo's import does. The `is export`
            // routines above publish into the same packages; they are theirs.
            if (name.empty()) return;
            const std::string tagPfx = name + "::EXPORT::";
            std::map<std::pair<std::string, std::string>, Value> handBuilt;   // (tag, key) → value
            auto take = [&](const std::string& pkg, const std::string& key, const Value& v) {
                if (pkg.size() <= tagPfx.size() || pkg.compare(0, tagPfx.size(), tagPfx) != 0) return;
                std::string tag = pkg.substr(tagPfx.size());
                if (tag.find("::") != std::string::npos || key.empty()) return;
                std::string bare = std::strchr("$@%&", key[0]) ? key.substr(1) : key;
                if (key[0] == '&' && (exported.count(bare) || exportTagsByName.count(bare))) return;
                handBuilt.emplace(std::make_pair(tag, key), v);
            };
            for (auto& kv : global_->vars) {
                std::string pkg, key;
                if (splitPkgSymbol(kv.first, pkg, key)) take(pkg, key, kv.second);
            }
            for (auto& [pkg, stash] : pkgStashes_)
                if (stash) for (auto& kv : *stash) take(pkg, kv.first, kv.second);
            const bool plainUse = requestedTags.empty();
            for (auto& [tk, v] : handBuilt) {
                const std::string& tag = tk.first;
                const std::string& key = tk.second;
                moduleSelectiveExports_[name].push_back({key, v, {tag}, /*stashOnly=*/true});
                std::string bare = std::strchr("$@%&", key[0]) ? key.substr(1) : key;
                bool want = tag == "MANDATORY" ||
                            (tag == "DEFAULT" ? plainUse || requestedTags.count("DEFAULT")
                                              : requestedTags.count(tag) != 0) ||
                            (tag == "ALL" && requestedNames.count(bare));
                if (!want || !doImport || !saved) continue;
                // a placeholder never displaces a routine (see importWouldShadowRoutine)
                if (key[0] == '&' && v.t != VT::Code) continue;
                if (saved.get() != global_.get()) {
                    if (!saved->local(key)) saved->define(key, v);
                }
                else if (!mainlineSubNames_.count(key)) global_->define(key, v);
            }
        };
        // A runtime failure in a module's load-time code (often a deep dependency
        // using an unimplemented primitive, e.g. Lock) is non-fatal: warn and keep
        // going so the importing program can still run paths that don't need it —
        // and still publish whatever the module managed to define before dying.
        loadingModuleDepth_++;
        bool savedDoImport = moduleDoImport_; moduleDoImport_ = doImport;
        try {
            langRev_ = prog->langRev;   // the module's own revision, not the importer's
            // …and its own pragmas, not the importer's. The parser recorded the
            // pragma, so it is in force before hoistSubs stamps the routines.
            rakuAstPragma_ = prog->usesRakuAst;
            if (rakuAstPragma_) { anyRevSwitch_ = true; rakuAstMaterialize(); }
            if (prog->langRev != 1) anyRevSwitch_ = true;
            hoistSubs(prog->stmts);
            // A module is a compilation unit too, so its INIT phasers run ONCE,
            // before its own mainline — including the ones nested in its subs
            // and blocks. Rakudo runs the INIT of a module sub that is never
            // called, and runs it exactly once; without this walk each one fired
            // at its textual position, so an INIT inside `our sub greet` ran on
            // every call. Same collection and same self-contained rule as the
            // program mainline and EVAL (see collectPhasersStmt); after hoistSubs,
            // so an INIT may call the module's own subs. A module loaded from
            // the precomp cache gets a fresh AST with initHoisted clear — the
            // flag is per-run and deliberately not serialized — so it collects
            // the same way a freshly parsed one does.
            //
            // WHEN they run needs care. Rakudo puts them before the module
            // mainline (a module INIT reading a mainline `our $x` sees an
            // undefined value), and can, because `use` is resolved at COMPILE
            // time there — its dependencies are loaded before any of it runs.
            // In rakupp a `use` is a mainline statement, so running the INITs
            // first made `INIT { … Helper::compute() … }` in a module that
            // `use Helper` die with "Undefined routine". They therefore run
            // after the module's leading prologue of `use` statements: nothing
            // is reordered, the dependencies are in, and for the conventional
            // module — every `use` at the top — this IS Rakudo's position.
            //
            // topLevel=FALSE even for the module's own top-level INITs. That
            // flag means "the containers this INIT names already exist", which
            // is true of run()'s mainline only because run() pre-declares its
            // `my`s before any phaser. loadParsed has no such pass, so a module
            // INIT is hoisted on the same terms as any nested one: only if it
            // names nothing it does not declare itself. Otherwise `our $stash;
            // INIT { $stash = … }` hoisted above its own declaration and died
            // with "Variable '$stash' is not declared".
            std::vector<Block*> inits;
            for (auto& st : prog->stmts) collectPhasersStmt(st.get(), "INIT", inits, /*topLevel=*/false);
            // A module's END runs at PROCESS end, not at load (File::Temp
            // registers its tempfile cleanup this way), capturing the module
            // scope, and sorts at the position of the `use` that loaded it.
            EndUnitScope endUnit{*this};
            registerEnds(*prog);
            bool initsDone = inits.empty();
            auto runInitsOnce = [&] {
                if (initsDone) return;
                initsDone = true;
                for (auto* b : inits) runHoistedInit(b);
            };
            auto isPrologue = [](Stmt* s) {
                if (s->kind == NK::UseStmt || s->kind == NK::EmptyStmt) return true;
                if (s->kind == NK::SubDecl) return true; // hoistSubs already took it
                // the `unit module Foo;` header itself (a package with no body of
                // its own); a BRACED `module Foo { … }` is a namespace and is not
                // prologue — including the empty one, which carries no statements
                // either and so needs the braces to tell it apart
                if (s->kind == NK::ClassDecl) { auto* c = static_cast<ClassDecl*>(s);
                    return c->isPackage && c->body.empty() && !c->bracedBody &&
                           c->methods.empty(); }
                return false;
            };
            moduleTopEnvs_.push_back(moduleEnv.get());
            if (loadedModuleEnvSet_.insert(moduleEnv.get()).second) loadedModuleEnvs_.push_back(moduleEnv);
            struct TopPop { std::vector<Env*>& v; ~TopPop() { v.pop_back(); } } topPop{moduleTopEnvs_};
            for (auto& st : prog->stmts) {
                tctx_.endCurTopStmt = st.get();           // for a nested `use` in it
                if (!isPrologue(st.get())) runInitsOnce(); // every `use` above us has run
                if (st->kind == NK::Block && static_cast<Block*>(st.get())->initHoisted)
                    continue; // ran in runInitsOnce, ahead of this mainline
                if (st->kind == NK::SubDecl && !static_cast<SubDecl*>(st.get())->name.empty() &&
                    !static_cast<SubDecl*>(st.get())->isMethod) {
                    auto* sd = static_cast<SubDecl*>(st.get());
                    applySubTraits(sd);
                    // A leading `unit module Foo;` has now set pkgPrefix, but this
                    // `our sub` was already defined by hoistSubs (bare) — publish it
                    // under its qualified name so external `Foo::name()` calls resolve.
                    if (sd->isOur && !tctx_.pkgPrefix.empty())
                        if (Value* c = tctx_.cur->find("&" + sd->name))
                            global_->define("&" + tctx_.pkgPrefix + sd->name, *c);
                    continue; // hoisted
                }
                exec(st.get());
            }
            runInitsOnce(); // a module that is nothing but `use`s and declarations
        }
        catch (RakuError& e) {
            loadingModuleDepth_--; moduleDoImport_ = savedDoImport;
            publish();
            tctx_.cur = saved; curPkgEnv_ = savedPkg; finishData_ = savedFinish; tctx_.pkgPrefix = savedModPrefix; langRev_ = savedLangRev; rakuAstPragma_ = savedRakuAst;
            // A module that THROWS while loading is fatal, for the same reason a
            // missing or unparseable one is: its remaining BEGIN blocks and exports
            // never happen, so what the importer gets is a half-built module that
            // looks loaded. This warned and continued, and the failure was as
            // invisible as the other two — t/regression/cro-live-server.raku ran
            // for weeks against a Cro::HTTP::Router whose LogTimelineSchema had
            // died, so `route` was never exported and the test only ever checked
            // that nothing exploded.
            throw;
        }
        catch (...) { loadingModuleDepth_--; moduleDoImport_ = savedDoImport; tctx_.cur = saved; curPkgEnv_ = savedPkg; finishData_ = savedFinish; tctx_.pkgPrefix = savedModPrefix; langRev_ = savedLangRev; rakuAstPragma_ = savedRakuAst; throw; }
        loadingModuleDepth_--; moduleDoImport_ = savedDoImport;
        // JSON::Fast's to-json/from-json get a native fast path (Builtins.cpp:
        // wrapJsonFastExports) — wrapped in the MODULE env before publishing,
        // so the qualified globals and the EXPORT protocol both hand out the
        // wrapped subs. Only on a load that completed: a module that died
        // publishes whatever it managed, and wrapping half a module helps nobody.
        if (name == "JSON::Fast") wrapJsonFastExports(*moduleEnv);
        publish();
        tctx_.cur = saved; curPkgEnv_ = savedPkg; finishData_ = savedFinish; tctx_.pkgPrefix = savedModPrefix; langRev_ = savedLangRev; rakuAstPragma_ = savedRakuAst;
        // What the module declares is the IMPORTING scope's when that is a block
        // or a routine: `{ use EmptyClass; my EmptyClass $foo }` leaves no
        // EmptyClass after the block (S11-modules/lexical.t). A `use` at the
        // top of the program or of a module keeps its packages for everyone.
        if (!name.empty()) {
            std::vector<std::string> fresh;
            for (auto& kv : classes_) if (!classesBefore.count(kv.first)) fresh.push_back(kv.first);
            const bool nested = saved && saved.get() != global_.get() &&
                                std::find(moduleTopEnvs_.begin(), moduleTopEnvs_.end(), saved.get()) ==
                                    moduleTopEnvs_.end();
            // …except what it declares under `X::`: Rakudo installs that into
            // CORE's own X package, which is shared rather than lexical, so a
            // test that loads JSON::Tiny inside its throws-like block still
            // names X::JSON::Tiny::Invalid outside it
            if (nested && !requireForm)
                for (auto& c : fresh)
                    if (c.rfind("X::", 0) != 0 && c.find("::X::") == std::string::npos)
                        requireScoped_[c] = saved;
            moduleClasses_[name] = std::move(fresh);
        }
        // `sub EXPORT(*@_)` protocol: call it with the use-statement's <...>
        // args; its returned Map ('&name' => &code, ...) defines the imports
        // in the USING scope.
        {
            auto it = moduleEnv->vars.find("&EXPORT");
            if (it != moduleEnv->vars.end() && it->second.t == VT::Code)
                moduleExportSubs_[name] = it->second;   // for repeat `use`s
            if (doImport && it != moduleEnv->vars.end() && it->second.t == VT::Code) {
                ValueList eargs;
                for (auto& s : importArgs) eargs.push_back(Value::str(s));
                for (auto& v : useExtra) eargs.push_back(v);
                try {
                    Value res = callCallable(it->second, eargs);
                    if (res.t == VT::Hash && res.hash())
                        for (auto& kv : *res.hash()) {
                            // A ROUTINE slot that resolved to the undefined value is
                            // a failed lookup, not a symbol: `'&golden-ratio' =>
                            // &Some::Module::golden-ratio` where that sub is
                            // `is export` but not `our` (Math::NumberTheory's EXPORT
                            // map — Rakudo hands back an Any there too). Binding it
                            // can only shadow a name that does resolve. Only the `&`
                            // sigil: `'$bar' => $b` where `our $b;` is undefined is a
                            // real export of a real container (roast's gh2979.t).
                            if (kv.second.t == VT::Any && !kv.first.empty() && kv.first[0] == '&')
                                continue;
                            if (importWouldShadowRoutine(kv.first, kv.second)) continue;
                            tctx_.cur->define(kv.first, kv.second);
                            // (a TYPE it hands over is the importer's lexical)
                            if (!kv.first.empty() && ascii::isupper((unsigned char)kv.first[0])) {
                                auto& sl = unitStash_[stashUnitHere()].lexical[kv.first];
                                if (!sl) { stashNodes_.emplace_back(); sl = &stashNodes_.back();
                                           sl->fq = kv.second.t == VT::Type ? kv.second.s.str() : kv.first; sl->stub = false; }
                            }
                        }
                } catch (RakuError& e) {
                    // see the replay site above for all three halves of this
                    const std::string exType =
                        e.payload.t == VT::Type ? e.payload.s
                      : (e.payload.t == VT::Object && e.payload.obj() && e.payload.obj()->cls)
                            ? e.payload.obj()->cls->name : std::string();
                    if (name != "if" && !quiet && !requireForm && exType == "X::AdHoc") throw;
                    if (name != "if" && !l10nApplied_.count(name) && !slangModules_.count(name))   // see the site above
                        std::cerr << "===WARNING=== Module " << name
                                  << " EXPORT failed: " << e.message << "\n";
                }
            }
        }
    };

    // Obtain the tree for a module we found on disk — from the precomp cache when
    // it is still valid, otherwise by parsing — then hand it to loadParsed.
    auto loadSource = [&](const std::string& src, const std::string& srcPath) {
        auto prog = std::make_shared<Program>();
        std::string finish;
        double t0 = traceLoad ? nowMs() : 0;
        std::string cpath = precompEnabled() && srcPath.rfind("rakupp:", 0) != 0
                          ? precompPath(srcPath, libPaths_) : std::string();
        bool cached = false;
        if (!cpath.empty()) {
            std::string blob;
            if (precompRead(cpath, src, blob, finish)) {
                try { deserializeAst(blob, *prog); cached = true; }
                catch (AstSerialError&) { prog->stmts.clear(); finish.clear(); } // corrupt: parse instead
            }
        }
        // A module that registers a slang runs its EXPORT for real only in the
        // scratch host (rakuppActivateSlang); here it fails to find `$*LANG`, and
        // that failure is expected, not news.
        if (name != "Slangify" && name != "if" && rakuppIsSlangSource(src)) slangModules_.insert(name);
        if (!cached) try {
            Lexer lx(src);
            lx.tolerant_ = true;   // a slang below a `use` may own syntax this first lex cannot read
            auto mtoks = lx.tokenize();
            applyL10NSlang(src, mtoks);   // a MODULE may be written in a localized Raku too
            Parser parser(std::move(mtoks));
            // The module's own `use`s resolve on the SAME search path, so its
            // imported operators and sigilless constants are known while its body
            // parses (Text::Utils reads SPACE from Text::Utils::Vars).
            parser.libPaths_ = libPaths_;
            parser.srcFile_ = srcPath;
            parser.src_ = &src;   // a `use Slang::X` inside re-reads the rest of the module through the slang
            *prog = parser.parseProgram();
            finish = lx.finishData();
            if (!cpath.empty())
                try { precompWrite(cpath, srcPath, src, serializeAst(*prog), finish, parser.opScanned_); }
                catch (AstSerialError&) {} // a construct the format can't hold: just don't cache
        } catch (ParseError& e) {
            // A module that will not parse is FATAL, like a missing one: its BEGIN
            // blocks and exports never happen, so continuing past it runs the rest
            // of the program against a state nobody designed.
            //
            // (A `Slang::*` module used to be exempt here — "a compile-time grammar
            // mutator rakupp cannot apply" — so its failure surfaced as a baffling
            // parse error in the DIST that used it. Slangs are applied now, and a
            // slang module that will not parse fails like any other.)
            ParseError pe("Error while compiling module " + name + " (line " +
                          std::to_string(e.line) + "): " + e.what(), e.line);
            // the line is the MODULE's; a nested module's failure keeps its own file
            pe.file = e.file.empty() ? srcPath : e.file;
            throw pe;
        }
        howMs = traceLoad ? nowMs() - t0 : 0;
        howLabel = cached ? "precomp" : "parse";
        // routines declared while this module's top level runs record ITS file
        // (backtrace .file — Log::Async walks frames by path); RAII because a
        // throwing module load is fatal-but-caught upstream
        struct DFGuard { std::string& f; std::string s; size_t& d; size_t sd; ~DFGuard() { f = s; d = sd; } }
            dfG{curDeclFile_, curDeclFile_, curDeclDepth_, curDeclDepth_};
        curDeclFile_ = srcPath;
        curDeclDepth_ = tctx_.callFrames.size();
        srcExportTags = scanExportTags(src);
        { // $?FILE inside the module answers as Rakudo spells it: "path (Name)"
            std::unique_lock<std::mutex> kl(sharedMut_, std::defer_lock); if (parallelMode_) kl.lock();
            unitNameOfFile_[srcPath] = name;
        }
        loadParsed(prog, finish);
        // (after the load: a refused tag fails the `use`, and the module stays loaded)
        if (doImport && !requireForm) checkImportTags(name, importArgs);   // (a `require` list names symbols)
    };


    // A module compiled INTO this binary needs no file at all — take it before
    // the search path is even consulted, so a `--exe` binary runs with its
    // dependencies deleted from the machine.
    if (const EmbeddedModule* em = findEmbeddedModule(name)) {
        auto prog = std::make_shared<Program>();
        bool okEmbedded = true;
        try { deserializeAst(em->blob, *prog); }
        catch (AstSerialError&) { okEmbedded = false; } // fall through to the disk
        if (okEmbedded) attachAotBodies(name, *prog);   // its routines' native bodies, if compiled
        if (okEmbedded) {
            if (traceLoad) fprintf(stderr, "[Load] %s <- embedded in this binary\n", name.c_str());
            // Its distribution travelled with it (MODULES-PLAN B3): bind
            // %?RESOURCES and $?DISTRIBUTION exactly as the two disk paths below
            // do, or a module that reads a resource sees an empty hash and dies
            // on Any. Only for a module that HAS a dist, so the ordinary
            // embedded module pays nothing.
            auto dk = embeddedModuleDist().find(name);
            if (dk != embeddedModuleDist().end()) {
                resourceStack_.push_back(buildEmbeddedResourceMap(dk->second));
                distStack_.push_back(buildEmbeddedDistribution(dk->second));
                struct RG { ValueList& s; ~RG() { s.pop_back(); } } rg{resourceStack_};
                struct DG { ValueList& s; ~DG() { s.pop_back(); } } dg{distStack_};
                loadParsed(prog, em->finish);
                return;
            }
            loadParsed(prog, em->finish);
            return;
        }
    }
    std::string rel = name;
    for (size_t p = rel.find("::"); p != std::string::npos; p = rel.find("::")) rel.replace(p, 2, "/");
    // 1. local lib paths (project lib, ., rakupp rakulib). A `use lib` may point at a
    // distribution root rather than its `lib/` dir (e.g. Roast's `use lib
    // $*PROGRAM.parent(2).add("packages/Test-Helpers")`), so try `<base>/lib/` too —
    // this is the common case Rakudo resolves via META6.json.
    // The extensions a NAME resolves to, and `.raku` is deliberately not among
    // them: Rakudo's CompUnit::Repository::FileSystem does not take it even
    // with an explicit -I, `.raku` is the extension a PROGRAM wears, and
    // treating it as a module made every script beside the one being run a
    // candidate module — a scratch file named `paths.raku` shadowed the
    // installed `paths` distribution and reported "Undefined routine 'paths'".
    // A dist that really does keep a module in a `.raku` file still loads: a
    // META6 `provides` path is explicit and is tried before any of these.
    // 6.e then drops `.pm`, as Rakudo does, leaving .rakumod and .pm6.
    static const char* extsAll[] = {".rakumod", ".pm6", ".pm"};
    static const char* exts6e[]  = {".rakumod", ".pm6"};
    const char* const* exts = sixE() ? exts6e : extsAll;
    const size_t nExts = sixE() ? 2 : 3;
    if (traceLoad) {
        std::cerr << "[LibPaths]"; for (auto& b : libPaths_) std::cerr << " [" << b << "]"; std::cerr << "\n";
    }
    // A SHADOWED name resolves from the engine's rakulib first, ahead of any
    // -I path or store: the ecosystem NativeHelpers::Blob/CStruct/Pointer read
    // MoarVM's REPR memory layout by design and cannot run here, so even the
    // dist's OWN suite — which `rakupp test` runs with the dist's lib in
    // front — must exercise the shadow, which keeps the same surface.
    // The copy compiled into this binary wins over the rakulib/ beside it: an
    // installed rakupp has no rakulib/ at all, and a checkout's is the same
    // text the build embedded. Not precompiled — the path is not a file, and
    // three small modules parse in well under a millisecond.
    {
        std::string shPath, shSrc;
        if (rakuppShadowModule(name, shPath, shSrc)) {
            if (traceLoad) fprintf(stderr, "[Load] %s <- the shadow compiled into this binary\n", name.c_str());
            if (pinnedInstall_ && pinnedInstall_->name == name) pinnedInstall_.reset();
            loadSource(shSrc, shPath);
            return;
        }
    }
    // A store's `.need` of one particular distribution (see the CURI arm):
    // that dist's source, whatever else the search path holds
    if (pinnedInstall_ && pinnedInstall_->name == name) {
        const PinnedInstall pin = *pinnedInstall_;
        pinnedInstall_.reset();
        std::ifstream src(pin.repo + "/sources/" + pin.srcId);
        if (src) {
            std::ostringstream ss; ss << src.rdbuf();
            resourceStack_.push_back(buildResourceMap(pin.repo, pin.distId));
            distStack_.push_back(buildInstalledDistribution(pin.repo, pin.distId));
            struct RGuard { ValueList& s; ~RGuard() { s.pop_back(); } } rg{resourceStack_};
            struct DGuard { ValueList& s; ~DGuard() { s.pop_back(); } } dg{distStack_};
            loadSource(ss.str(), pin.repo + "/sources/" + pin.srcId);
            return;
        }
    }
    std::vector<std::string> searchOrder = libPaths_;
    // `PROCESS::<$REPO> := CompUnit::Repository::FileSystem.new(:prefix(…))`
    // puts a source tree at the head of the chain: its prefix (and those of
    // the FileSystem repos it chains to) is searched first
    {
        std::vector<std::string> repoDirs;
        Value* rv = findDynamicLenient("$*REPO");
        for (int hop = 0; rv && hop < 16; hop++) {
            if (rv->t != VT::Object || !rv->obj() || !rv->obj()->cls ||
                rv->obj()->cls->name != "CompUnit::Repository::FileSystem") break;
            auto& at = rv->obj()->attrs;
            if (at.count("prefix")) repoDirs.push_back(at["prefix"].toStr());
            auto nx = at.find("next-repo");
            rv = nx != at.end() ? &nx->second : nullptr;
        }
        searchOrder.insert(searchOrder.begin(), repoDirs.begin(), repoDirs.end());
    }
    // No shadow compiled in (an embedder of librakupp): the rakulib/ on disk
    if (!shadowLibDir_.empty() && isShadowedModule(name)) {
        searchOrder.erase(std::remove(searchOrder.begin(), searchOrder.end(), shadowLibDir_),
                          searchOrder.end());
        searchOrder.insert(searchOrder.begin(), shadowLibDir_);
    }
    for (auto& pathEntry : searchOrder) {
        std::string storePre;
        if (repoSpecStore(pathEntry, storePre)) continue; // an inst# store joins phase 2 below
        const std::string base = repoSpecDir(pathEntry);
        for (const std::string& dir : {base, base + "/lib"}) {
            // A dist's META6 `provides` may put the module at a path the name
            // does not describe; that mapping is tried FIRST, as Rakudo's
            // FileSystem repo does, and the name-derived paths remain the
            // fallback for the (usual) checkout with no META6.
            std::string metaRel;
            {
                std::string dr = dir;
                if (dr == "lib") dr = ".";
                else if (dr.size() > 4 && dr.compare(dr.size() - 4, 4, "/lib") == 0)
                    dr = dr.substr(0, dr.size() - 4);
                std::string pf = metaProvidesPath(dr, name);
                // A `provides` entry resolves the NAME; it is not a waiver of the
                // VERSION. Gated exactly as the name-derived paths below are, or a
                // dist that maps its module explicitly defeats every `:ver` — and
                // silently, since the fast path returns before the constraint is
                // ever read. `use Foo:ver<9.9+>` loaded a 1.2.3 dist that way.
                if (!pf.empty() && !verReq.empty()) {
                    std::string mv = metaVersion(dr);
                    if (!verSatisfies(mv, verReq)) {
                        if (traceLoad)
                            std::cerr << "[Load] " << name << " skip META6 provides " << dr << "/" << pf
                                      << " (ver " << (mv.empty() ? "?" : mv) << " !~ " << verReq << ")\n";
                        pf.clear();   // fall through: name-derived paths, then the stores
                    }
                }
                if (!pf.empty()) {
                    std::string full = dr + "/" + pf;
                    std::ifstream mv(full);
                    if (mv) {
                        if (traceLoad)
                            std::cerr << "[Load] " << name << " <- META6 provides " << full << "\n";
                        std::ostringstream ms; ms << mv.rdbuf();
                        resourceStack_.push_back(buildSourceResourceMap(dr));
                        distStack_.push_back(buildDistribution(dr));
                        struct RG { ValueList& s; ~RG() { s.pop_back(); } } rg2{resourceStack_};
                        struct DG { ValueList& s; ~DG() { s.pop_back(); } } dg2{distStack_};
                        loadSource(ms.str(), full);
                        return;
                    }
                }
            }
            for (size_t xi = 0; xi < nExts; xi++) {
                const char* ext = exts[xi];
                std::ifstream in(dir + "/" + rel + ext);
                if (!in) continue;
                if (!dirEntryCaseExact(dir + "/" + rel + ext)) continue; // config.raku is not Config.raku
                if (!verReq.empty()) {
                    // `use Foo:ver<0.0.14+>`: a too-old candidate must NOT win
                    // just by being first on the path — skip it and let a later
                    // path (or the installed repos below) satisfy the constraint.
                    std::string dr = dir;
                    if (dr == "lib") dr = ".";
                    else if (dr.size() > 4 && dr.compare(dr.size() - 4, 4, "/lib") == 0)
                        dr = dr.substr(0, dr.size() - 4);
                    std::string mv = metaVersion(dr);
                    if (!verSatisfies(mv, verReq)) {
                        if (traceLoad)
                            std::cerr << "[Load] " << name << " skip " << dir << "/" << rel << ext
                                      << " (ver " << (mv.empty() ? "?" : mv) << " !~ " << verReq << ")\n";
                        continue;
                    }
                }
                if (traceLoad)
                    std::cerr << "[Load] " << name << " <- source " << dir << "/" << rel + ext << "\n";
                std::ostringstream ss; ss << in.rdbuf();
                // Bind %?RESOURCES from the source checkout's META6. The dist root is
                // the parent of the lib dir that matched: for `-I <root>` we matched
                // `<root>/lib/...` (root = base), for the default cwd `lib` entry we
                // matched `lib/...` (root = "."). Pop after load.
                std::string distRoot = dir;
                if (distRoot == "lib") distRoot = ".";
                else if (distRoot.size() > 4 && distRoot.compare(distRoot.size() - 4, 4, "/lib") == 0)
                    distRoot = distRoot.substr(0, distRoot.size() - 4);
                resourceStack_.push_back(buildSourceResourceMap(distRoot));
                distStack_.push_back(buildDistribution(distRoot));
                struct RGuard { ValueList& s; ~RGuard() { s.pop_back(); } } rg{resourceStack_};
                struct DGuard { ValueList& s; ~DGuard() { s.pop_back(); } } dg{distStack_};
                loadSource(ss.str(), dir + "/" + rel + ext);
                return;
            }
        }
    }
    // 2. installed Rakudo/zef modules: resolve name via the CURI short/ index
    // (inst# entries from `use lib`/-I/RAKULIB first, then the default repos)
    std::string nameSha = sha1hex(name);
    for (auto& repo : repoPrefixesFor(libPaths_)) {
        // several installed versions may share the short name: take the NEWEST
        // that satisfies the `use` constraint (pickInstalledDist). Taking the
        // first that satisfied loaded whichever version readdir put first.
        std::string entry; std::vector<std::string> lines;
        if (!pickInstalledDist(repo + "/short/" + nameSha, verReq, entry, lines)) continue;
        std::ifstream src(repo + "/sources/" + lines[3]);
        if (!src) continue;
        if (traceLoad)
            std::cerr << "[Load] " << name << " <- installed " << repo << " dist=" << entry << "\n";
        std::ostringstream ss; ss << src.rdbuf();
        // Bind %?RESOURCES for this module (the short-index filename is its dist
        // id) so BEGIN-time `%?RESOURCES<x>.slurp` resolves. Pop after loading.
        resourceStack_.push_back(buildResourceMap(repo, entry));
        distStack_.push_back(buildInstalledDistribution(repo, entry)); // $?DISTRIBUTION
        struct RGuard { ValueList& s; ~RGuard() { s.pop_back(); } } rg{resourceStack_};
        struct DGuard { ValueList& s; ~DGuard() { s.pop_back(); } } dg{distStack_};
        loadSource(ss.str(), repo + "/sources/" + lines[3]);
        return;
    }
    // Module file not found. Pragmas / version literals are expected to have no
    // file; anything else is a genuinely unresolved dependency and is FATAL.
    //
    // This used to warn and carry on, which was a divergence with real
    // consequences. A program whose `use` silently vanished ran on against
    // whatever was left — and since a module's BEGIN blocks and its exported
    // symbols never materialised, what followed was arbitrary. It also flattered
    // every measurement we took: in the module battery, twenty probes "produced
    // output" under rakupp purely because the load had been skipped, so the
    // comparison against Rakudo was meaningless for them. Deferring the error to
    // first USE of an imported symbol would be more in the spirit of the language,
    // but a module's BEGIN blocks have to run at load time regardless, so there is
    // no honest way to postpone it.
    // PRAGMAS have no file to find, so they must not be mistaken for a missing
    // module now that missing is fatal. This list was short enough to reject real
    // Raku pragmas: `use newline :lf` and `no precompilation` are both ordinary
    // language features with no module behind them, and rejecting them cost three
    // Roast files that Rakudo passes. rakupp ignores the ones it does not
    // implement, which is right — they change compilation details, not semantics
    // it can observe — but ignoring them must be a deliberate entry here rather
    // than a side effect of the lookup failing.
    // `use js` declares the program's TARGET, so the interpreter is the wrong way
    // to run it and says so at the line that declares the intent — rather than
    // letting it run on and die at the first `JS.` call, which is what the old
    // rakulib/JS.rakumod stub did. There is no module behind the name and never
    // was; the emitter recognises it and compiles `JS` to the host's global.
    if (name == "js" || name == "JS")
        throwTyped("X::CompUnit::UnsatisfiedDependency", {{"specification", name}},
                   "`use " + name + "` declares a program that targets JavaScript, "
                   "so it cannot run under the interpreter.\n"
                   "Compile it instead:  rakupp --target=js " +
                   ((srcFile_.empty() || srcFile_ == "-e") ? std::string("PROGRAM.raku") : srcFile_) +
                   " -o PROGRAM.js && node PROGRAM.js");
    if (isPragmaName(name) || quiet) return;
    std::string where = "Could not find " + name + " in:";
    for (auto& b : libPaths_) where += "\n    " + b;
    for (auto& r : rakuRepoPrefixes()) where += "\n    " + r;
    throwTyped("X::CompUnit::UnsatisfiedDependency", {{"specification", name}}, where);
}

// The scopes EVALs are running in, innermost last: an EVAL's code runs in the
// scope that called it, and for as long as it runs that scope is its UNIT::.
thread_local std::vector<std::shared_ptr<Env>> g_evalUnits;
// the classes whose BODY is running right now, innermost last
thread_local std::vector<std::string> g_classBodies;
// A name counts when the unit (or the one running it) declares it, when it is
// a core type — an X:: one only if Rakudo has it, since isKnownTypeName takes
// any X:: name — or when a class, subset, package, alias or lexical answers to
// it. A routine of that name counts too: only an UNKNOWN name is refused.
bool Interpreter::bareNameResolves(const std::string& n, const Program& unit) {
    auto declaredIn = [&](const Program& p) {
        if (p.typeNamesOpaque || p.declaredTypeNames.count(n) || p.declaredTermNames.count(n)) return true;
        for (size_t c = n.find("::"); c != std::string::npos; c = n.find("::", c + 2))
            if (p.declaredTypeNames.count(n.substr(c + 2))) return true;
        return false;
    };
    if (declaredIn(unit)) return true;
    if (const Program* outer = unitCurrent(); !outer || declaredIn(*outer)) return true;
    // a pseudo-package or CORE-qualified name is not this check's business
    static const char* const kPseudo[] = {"MY::", "OUR::", "CORE::", "GLOBAL::", "PROCESS::", "COMPILING::",
        "DYNAMIC::", "CALLER::", "CALLERS::", "LEXICAL::", "OUTER::", "OUTERS::", "SETTING::", "UNIT::",
        "CLIENT::", "PARENT::", "EXPORT::"};
    for (const char* p : kPseudo)
        if (n.rfind(p, 0) == 0) return true;
    if (n.rfind("X::", 0) == 0 ? isRakudoExceptionName(n) : isKnownTypeName(n)) return true;
    const std::string& rn = resolveClassAlias(n);
    if (classes_.count(n) || classes_.count(rn) || subsets_.count(n) || pkgMeta_.count(n) ||
        pkgMeta_.count(rn) || isNativeTypeName(n) || enumPairs_.count(n))
        return true;
    if (!tctx_.pkgPrefix.empty() && classes_.count(tctx_.pkgPrefix + n)) return true;
    if (tctx_.cur && (tctx_.cur->find(n) || tctx_.cur->find("&" + n) || tctx_.cur->find("::" + n)))
        return true;
    return global_ && (global_->find(n) || global_->find("&" + n));
}

bool unitHasNestedPhasers(const std::vector<StmtPtr>& stmts);   // InterpreterOperators.cpp
Value Interpreter::evalString(const std::string& srcIn, bool mainlinePH, bool* incompleteOut,
                              bool checkOnly) {
    // `class C { EVAL 'method x { … }' }` — code EVALed while a class body runs
    // declares into that class: compile it as an augment of it
    std::string wrapped;
    if (!g_classBodies.empty() && !checkOnly) {
        size_t b = srcIn.find_first_not_of(" \t\n");
        if (b != std::string::npos &&
            (srcIn.compare(b, 7, "method ") == 0 || srcIn.compare(b, 6, "multi ") == 0 ||
             srcIn.compare(b, 10, "submethod ") == 0))
            wrapped = "use MONKEY-TYPING; augment class " + g_classBodies.back() + " { " + srcIn + " }";
    }
    const std::string& src = wrapped.empty() ? srcIn : wrapped;
    // (an EVAL runs in its caller's scope, so a `use fatal` inside it must
    // not outlive it: the pragma is put back as it was — and one OUTSIDE it,
    // a `try` block's included, does not reach in: the EVAL'd code is a unit
    // compiled on its own, and starts as `no fatal`)
    struct EvalUnit {
        std::shared_ptr<Env> env; signed char fatal;
        EvalUnit(const std::shared_ptr<Env>& e) : env(e), fatal(e ? e->fatalPragma : 0) {
            g_evalUnits.push_back(e);
            if (e) e->fatalPragma = -1;
        }
        ~EvalUnit() { g_evalUnits.pop_back(); if (env) env->fatalPragma = fatal; }
    } evalUnit(tctx_.cur);
    // a `unit module Foo;` inside the EVAL scopes the rest of THAT source, not
    // the program that ran it (roast S10-packages/basic.t: every class after
    // `EVAL 'unit module RT64688_m1; …'` was named RT64688_m1::…)
    struct PkgPrefixRestore {
        ExecContext& c; std::string saved;
        PkgPrefixRestore(ExecContext& cx) : c(cx), saved(cx.pkgPrefix) {}
        ~PkgPrefixRestore() { c.pkgPrefix = saved; }
    } pkgPrefixRestore(tctx_);
    // the EVAL'd source counts its own lines from 1; the caller's statement
    // line comes back when it returns (a regex's `{ … }` and `** { … }` run
    // through here, and a failing `is` after one reported "line 1")
    struct LineRestore {
        Interpreter* self; int line;
        ~LineRestore() { self->curLine_ = line; }
    } lineRestore{this, curLine_};
    // `$=pod` inside the EVAL is the EVAL's own compilation unit's pod, as in
    // Rakudo — Pod::Load's whole method is `EVAL "module M { $source }\n$=pod"`.
    // The main program's DOM is put back on every exit path.
    struct PodSwap {
        ValueList& slot; ValueList saved;
        PodSwap(ValueList& s, ValueList mine) : slot(s), saved(std::move(s)) { slot = std::move(mine); }
        ~PodSwap() { slot = std::move(saved); }
    // A pod directive may be INDENTED — the block's margin is its delimiter's
    // own indent — so looking only for a `=` at column 0 missed every indented
    // block and left $=pod empty inside the EVAL.
    } podSwap(podDom_, [&] {
        for (size_t i = 0; i <= src.size();) {
            size_t j = i;
            while (j < src.size() && (src[j] == ' ' || src[j] == '\t')) j++;
            if (j < src.size() && src[j] == '=') return true;
            size_t nl = src.find('\n', i);
            if (nl == std::string::npos) break;
            i = nl + 1;
        }
        return false;
    }() ? parsePod(src, /*strict=*/true) : ValueList{});
    Lexer lexer(src);
    // the caller's operator spellings, so `EVAL '"a" (C) "b"'` lexes `(C)` whole
    for (Env* e = tctx_.cur.get(); e; e = e->parent.get())
        for (auto& kv : e->vars) {
            const std::string& n = kv.first;
            size_t lt = n.find(":<");
            if (n.size() > 1 && n[0] == '&' && lt != std::string::npos && n.back() == '>') {
                std::string kind = n.substr(1, lt - 1);
                std::string opn = n.substr(lt + 2, n.size() - lt - 3);
                if (kind == "infix" || kind == "prefix" || kind == "postfix")
                    lexer.noteUserOp(opn, false);
                else if ((kind == "circumfix" || kind == "postcircumfix") && opn.find(' ') != std::string::npos) {
                    lexer.noteUserOp(opn.substr(0, opn.find(' ')), false);
                    lexer.noteUserOp(opn.substr(opn.find(' ') + 1), false);
                }
            }
            // …and its sigilless TERMS that are no plain word (`my \term:<ℵ₀>`)
            else if (!n.empty() && !std::strchr("$@%&", n[0]) && (unsigned char)n.back() >= 0x80)
                lexer.noteUserOp(n, true);
        }
    auto prog = std::make_shared<Program>();
    try {
        lexer.tolerant_ = true;
        auto etoks = lexer.tokenize();
        applyL10NSlang(src, etoks);   // `EVAL 'use L10N::AF; …'` reads Afrikaans too
        Parser parser(std::move(etoks));
        parser.src_ = &src;         // `EVAL 'use Slang::Date; 2023-01-99'` reads the slang too
        parser.libPaths_ = libPaths_;  // …and resolves it (and any operator scan) on the interpreter's own search path
        parser.strictSep_ = true; // EVAL snippets get "two terms in a row" strictness
        parser.evalSelfInScope_ = tctx_.cur && tctx_.cur->findSelf();   // the code runs inside a method
        // seed user-defined operators (sub infix:<…>) so EVAL'd custom operators parse
        // — and the plain routines in scope, which parse as LISTOPS (so a `?? f !!`
        // in the snippet is the same gobbled `!!` it would be in the file)
        for (Env* e = tctx_.cur.get(); e; e = e->parent.get())
            for (auto& kv : e->vars) {
                const std::string& n = kv.first; // e.g. "&infix:<fromplus>"
                if (n.size() > 1 && n[0] == '&' && n.find(':') == std::string::npos &&
                    (ascii::isalpha((unsigned char)n[1]) || n[1] == '_') &&
                    kv.second.t == VT::Code && kv.second.code() && !kv.second.code()->builtin)
                    parser.declareKnownSub(n.substr(1));
                size_t lt = n.find(":<");
                if (n.size() > 1 && n[0] == '&' && lt != std::string::npos && n.back() == '>') {
                    std::string kind = n.substr(1, lt - 1);
                    if (kind == "infix" || kind == "prefix" || kind == "postfix" ||
                        kind == "circumfix" || kind == "postcircumfix") {
                        parser.declareUserOp(kind, n.substr(lt + 2, n.size() - lt - 3));
                        if (kind == "infix" && kv.second.t == VT::Code && kv.second.code() &&
                            kv.second.code()->assocNon)
                            parser.declareUserOp("infix-non", n.substr(lt + 2, n.size() - lt - 3));
                    }
                }
            }
        *prog = parser.parseProgram();
        // `CATCH { when X::Y {} }`: a type-like name before a statement's block
        // that nothing declares is, to Rakudo, a call that swallowed the block —
        // a compile-time group raised before anything runs, whether or not the
        // `when` ever does (S32-exceptions/misc.t)
        for (auto& gs : parser.gobbleSites_)
            if (!bareNameResolves(gs.name, *prog)) {
                std::string sm = "Function '" + gs.shown + "' needs parens to avoid gobbling block (or perhaps "
                                 "it's a class that's not declared or available in this scope?)";
                std::string pw = "block (apparently claimed by '" + gs.shown + "')";
                throw ParseError(sm + "\nMissing " + pw, gs.line, "X::Comp::Group",
                                 {{"sorrow", "X::Syntax::BlockGobbled"}, {"sorrow-msg", sm},
                                  {"sorrow-what", gs.shown}, {"panic", "X::Syntax::Missing"},
                                  {"panic-msg", "Missing " + pw}, {"panic-what", pw}});
            }
        // `EVAL 'return 1; self!typo()'` in a method: a private method the class
        // does not have is a COMPILE-time error, raised before anything runs
        // (integration/advent2011-day11.t)
        if (!parser.evalPrivCalls_.empty() && tctx_.cur)
            if (Value* sp = tctx_.cur->findSelf())
                if (sp->t == VT::Object && sp->obj() && sp->obj()->cls)
                    for (auto& pc : parser.evalPrivCalls_)
                        if (!sp->obj()->cls->findMethod("!" + pc.first))
                            throw ParseError("No such private method '!" + pc.first + "' for invocant of type '" +
                                             sp->obj()->cls->name + "'", pc.second, "X::Method::NotFound",
                                             {{"method", pc.first}, {"typename", sp->obj()->cls->name},
                                              {"private", "True"}});
    } catch (ParseError& e) {
        // REPL: the input just ran out (unclosed block/paren/heredoc). Report it
        // as "incomplete" so the caller can ask for the next line, rather than
        // raising a syntax error the user would have to work around.
        if (incompleteOut && e.atEof) { *incompleteOut = true; return Value::any(); }
        // A BEGIN runs as it is PARSED, so one written above the line that fails
        // has already run when the error is reported: `EVAL q[BEGIN { $t =
        // 'begin' }; …; 1 1]` leaves $t 'begin' (S04-phasers/begin.t). The
        // source above the failing line is parsed again on its own, and its
        // top-level BEGIN blocks run in the caller's scope; a prefix that does
        // not parse runs nothing, and nothing else in it runs at all.
        if (!incompleteOut && e.line > 1 && src.find("BEGIN") != std::string::npos) {
            size_t cut = 0;
            for (int ln = 1; ln < e.line && cut != std::string::npos; ln++) {
                cut = src.find('\n', cut);
                if (cut != std::string::npos) cut++;
            }
            if (cut != std::string::npos && cut > 0) {
                const std::string pre = src.substr(0, cut);
                try {
                    Lexer pl(pre);
                    pl.tolerant_ = true;
                    Parser pp(pl.tokenize());
                    pp.src_ = &pre;
                    pp.libPaths_ = libPaths_;
                    pp.strictSep_ = true;
                    Program pprog = pp.parseProgram();
                    for (auto& st : pprog.stmts) {
                        if (!st || st->kind != NK::Block) continue;
                        auto* b = static_cast<Block*>(st.get());
                        if (b->phaser != "BEGIN") continue;
                        if (b->stmtForm) execBlock(b, tctx_.cur);
                        else { auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur; execBlock(b, sc); }
                    }
                } catch (...) {}
            }
        }
        if (e.exType == "X::Package::Stubbed") {
            // the space-joined `packages` names become a real list attribute
            std::string names;
            for (auto& kv : e.exAttrs) if (kv.first == "packages") names = kv.second;
            Value arr = Value::array(); arr.isList = true;
            size_t p = 0;
            while (p < names.size()) {
                size_t q = names.find(' ', p);
                arr.arr()->push_back(Value::str(names.substr(p, q - p)));
                if (q == std::string::npos) break;
                p = q + 1;
            }
            throwTypedV("X::Package::Stubbed", {{"packages", arr}}, e.what());
        }
        if (e.exType == "X::Undeclared::Symbols")
            for (auto& kv : e.exAttrs)
                if (kv.first == "post_types") {
                    // `post_types` is a hash of the name to the lines that used it
                    Value h = Value::makeHash();
                    Value lines = Value::array(); lines.isList = true;
                    lines.arr()->push_back(Value::integer(e.line));
                    (*h.hash())[kv.second] = lines;
                    throwTypedV("X::Undeclared::Symbols", {{"post_types", h}}, e.what());
                }
        if (e.exType == "X::Syntax::Number::LiteralType") {
            // the two attributes are OBJECTS, not text: `:vartype(Int)` is the
            // type and `:value(NaN)` the number, which is what throws-like asks
            std::string vt = "Int", val = "0";
            for (auto& kv : e.exAttrs) {
                if (kv.first == "vartype") vt = kv.second;
                else if (kv.first == "value") val = kv.second;
            }
            throwTypedV("X::Syntax::Number::LiteralType",
                        {{"vartype", Value::typeObj(vt)}, {"value", numifyStr(val)}}, e.what());
        }
        if (e.exType == "X::Comp::Group") {
            // parse-level group diagnostic: the `sorrow` attr names the inner
            // exception type; rebuild it as a real object list in .sorrows
            // (…and `panic` the fatal one, `worry` a warning; each may carry
            // its own message and `what`)
            std::string stype, smsg, swhat, ptype, pmsg, pwhat, ppre, ppost, wmsg, sline, pline;
            for (auto& kv : e.exAttrs) {
                if (kv.first == "sorrow-line") sline = kv.second;
                else if (kv.first == "panic-line") pline = kv.second;
                else if (kv.first == "sorrow") stype = kv.second;
                else if (kv.first == "sorrow-msg") smsg = kv.second;
                else if (kv.first == "sorrow-what") swhat = kv.second;
                else if (kv.first == "panic") ptype = kv.second;
                else if (kv.first == "panic-msg") pmsg = kv.second;
                else if (kv.first == "panic-what") pwhat = kv.second;
                else if (kv.first == "panic-pre") ppre = kv.second;
                else if (kv.first == "panic-post") ppost = kv.second;
                else if (kv.first == "worry") wmsg = kv.second;
            }
            auto mk = [&](const std::string& t, const std::string& m, const std::string& w,
                          const std::string* pre = nullptr, const std::string* post = nullptr,
                          const std::string& ln = std::string()) {
                std::vector<std::pair<std::string, Value>> a;
                if (!w.empty()) a.emplace_back("what", Value::str(w));
                if (pre && (!pre->empty() || !post->empty())) {
                    a.emplace_back("pre", Value::str(*pre));
                    a.emplace_back("post", Value::str(*post));
                }
                if (t == "X::Comp::AdHoc") a.emplace_back("payload", Value::str(m));
                if (!ln.empty()) a.emplace_back("line", Value::integer(std::stoll(ln)));
                return makeTypedEx(t, std::move(a), m);
            };
            std::vector<std::pair<std::string, Value>> ga;
            Value arr = Value::array(); arr.isList = true;
            if (!stype.empty() || ptype.empty())
                arr.arr()->push_back(mk(stype.empty() ? "X::AdHoc" : stype, smsg.empty() ? e.what() : smsg, swhat,
                                        nullptr, nullptr, sline));
            ga.emplace_back("sorrows", arr);
            if (!ptype.empty()) ga.emplace_back("panic", mk(ptype, pmsg.empty() ? e.what() : pmsg, pwhat, &ppre, &ppost, pline));
            if (!wmsg.empty()) {
                Value wa = Value::array(); wa.isList = true;
                wa.arr()->push_back(mk("X::Comp::AdHoc", wmsg, ""));
                ga.emplace_back("worries", wa);
            }
            throwTypedV("X::Comp::Group", std::move(ga), e.what());
        }
        if (!e.exType.empty()) {   // typed compile diagnostic — and where it was found
            auto at = e.exAttrs;
            bool haveLine = false;
            for (auto& kv : at) if (kv.first == "line") haveLine = true;
            if (!haveLine && e.line > 0) at.emplace_back("line", std::to_string(e.line));
            throwTyped(e.exType, at, e.what());
        }
        throw RakuError{Value::typeObj("X::Syntax::Confused"), std::string("EVAL parse error: ") + e.what()};
    }
    // a placeholder in the EVAL mainline has no signature to attach to (only
    // for user-level EVAL: internal reparses, e.g. S{}=repl, must stay silent)
    if (mainlinePH)
        if (std::string ph = firstBlockPlaceholder(prog->stmts); !ph.empty())
            throwTyped("X::Placeholder::Mainline", {{"placeholder", ph}},
                       "Cannot use placeholder parameter " + ph + " in the mainline");
    // An undeclared variable is a COMPILE error in EVAL'd code as it is in a
    // file (issue #32's gate, which only the main program went through):
    // `EVAL 'sub greet($name) { say "hello, $nam" }'` dies although greet never
    // runs. The pass sees only the snippet, so a name the calling scope binds
    // at run time is no finding.
    // (…and an EVAL written where `no strict` holds inherits the lax pragma)
    // …and so is a call to a routine nothing declares: `EVAL '$bad = 1;
    // nope()'` dies before `$bad` is set. Only for a snippet the call walker
    // sees all of (plain statements; any declaration or other statement kind
    // makes it opaque), and only names no scope, builtin or type answers.
    if (mainlinePH && declCheckEnabled() && !noStrictHere()) checkUndeclaredCalls(prog->stmts);
    if (mainlinePH && declCheckEnabled() && !noStrictHere()) {
        auto us = findUndeclaredVars(*prog, src, libPaths_);
        for (auto& u : us) {
            if (isSpecialVar(u.name)) continue;
            // Walk out from here. A scope that HAS the variable answers; a
            // block that declares it FURTHER ON answers too — declarations are
            // compile-time, so it exists already, only not yet initialized, and
            // it shadows any outer one of the same name (`sub ee($s) { EVAL $s };
            // ee('!$y.defined'); my $y = 4`). A typed one cannot be stood in
            // for, so that is as good as undeclared (`EVAL '$x = "abc"'; my Int $x`).
            int verdict = 0;   // 1 = exists, 2 = stand-in, 3 = typed-later
            for (Env* e = tctx_.cur.get(); e && !verdict; e = e->parent.get()) {
                if (e->local(u.name)) { verdict = 1; break; }
                if (e->declStmts) {
                    std::vector<const VarExpr*> decls;
                    spDeclaredInRaw(*static_cast<const std::vector<StmtPtr>*>(e->declStmts), decls);
                    for (auto* v : decls)
                        if (v->name == u.name) { verdict = v->declType.empty() ? 2 : 3; break; }
                }
            }
            if (verdict == 1 || (verdict == 0 && tctx_.cur && tctx_.cur->find(u.name))) continue;
            if (verdict == 2) { tctx_.cur->define(u.name, typedDefault("", u.name[0])); continue; }
            throwUndeclaredVar(u.name, &u.inScope);
        }
    }
    { std::unique_lock<std::mutex> kl(sharedMut_, std::defer_lock); if (parallelMode_) kl.lock(); keptPrograms_.push_back(prog); } // keep AST alive for closures defined within
    // this EVAL/REPL line is its own unit for the bare-name fallback
    unitPush(prog.get());
    struct UnitGuard { Interpreter& I; ~UnitGuard() { I.unitPop(); } } unitG{*this};
    // `EVAL $code, :check` stops at the end of COMPILATION. The parse above WAS
    // the compile — a syntax error has already thrown — so what is left to run
    // is the two phasers that belong to compile time: every BEGIN, then every
    // CHECK. Nothing else: not the mainline, not INIT (which is run time, just
    // before it), and not END (which is registered by running). roast
    // S29-context/eval.t checks all three of those in one go.
    if (checkOnly) {
        for (const char* want : {"BEGIN", "CHECK"})
            for (auto& st : prog->stmts) {
                if (st->kind != NK::Block) continue;
                auto* b = static_cast<Block*>(st.get());
                if (b->phaser != want) continue;
                if (b->stmtForm) execBlock(b, tctx_.cur);   // `BEGIN my $x = …` declares out here
                else { auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur; execBlock(b, sc); }
            }
        return Value::any();
    }
    // EVAL'd code is its own compilation unit, so its INIT phasers run before
    // ITS mainline — not at their textual position. roast asserts this through
    // `throws-like '… gather for 1..3 { INIT take "OH HAI"; … }'`: the take has
    // to happen with no gather on the stack, and only hoisting puts it there.
    bool hoistedForInit = false;
    {
        std::vector<Block*> inits;
        for (auto& s : prog->stmts) collectPhasersStmt(s.get(), "INIT", inits, /*topLevel=*/true);
        // An INIT runs at the start of the unit, and the unit's named subs are
        // declared by then — `INIT f()` and `:$x = INIT f()` call one (the
        // hoist below waits for a mention above the sub, which an INIT is not).
        // A BEGIN still sees only what is above it, so a unit with one is left be.
        if (mainlinePH && (!inits.empty() || unitHasNestedPhasers(prog->stmts))) {
            bool anyBegin = false;
            for (auto& st : prog->stmts)
                if (st && st->kind == NK::Block && static_cast<Block*>(st.get())->phaser == "BEGIN") anyBegin = true;
            if (!anyBegin) { hoistSubs(prog->stmts); hoistedForInit = true; }
            // …and its `my` variables exist (undefined) for such a sub to touch,
            // as the program's do: `sub f { $calls++ }` called by an INIT
            for (auto& st : prog->stmts) {
                if (!st || st->kind != NK::ExprStmt) continue;
                Expr* e = static_cast<ExprStmt*>(st.get())->e.get();
                if (e && e->kind == NK::Assign) e = static_cast<Assign*>(e)->target.get();
                if (!e || e->kind != NK::VarExpr) continue;
                auto* ve = static_cast<VarExpr*>(e);
                if (!ve->declare || ve->declScope != "my" || ve->name.empty()) continue;
                if (!ve->containerIs.empty() || ve->declTypeExpr || ve->declShape) continue;
                if (!tctx_.cur->local(ve->name)) tctx_.cur->define(ve->name, declInitial(ve, ve->name[0]));
            }
        }
        // …and its nested BEGIN/CHECK/INIT run before its code, as the program's do
        if (!checkOnly) runStaticPhasers(prog->stmts, tctx_.cur, /*unitIsLive=*/true);
        for (auto* b : inits) runHoistedInit(b);
    }
    // An END in EVAL'd code runs at the END of the whole program (not here),
    // capturing the EVAL scope so it still sees this EVAL's lexicals, and
    // sorting where the EVAL itself sits in its unit.
    EndUnitScope endUnit{*this, /*atSourcePosition=*/false};
    registerEnds(*prog);
    // A declaration takes effect at the start of ITS OWN statement, so
    // `(my @a) = […@a…]` can name the very variable it declares — which is the
    // shape `.raku` gives a self-referential array, and EVAL is how roast reads
    // one back (S02-names-vars/list_array_perl.t). EVAL'd code had no
    // pre-declaration at all and died "Variable '@a' is not declared".
    //
    // Scoped to the ONE statement on purpose. Doing the whole unit up front,
    // which is what the mainline does beside its pad install, also made a
    // mention in an EARLIER statement resolve: `EVAL '$foo; my $foo = 42'`
    // stopped being X::Undeclared, where Rakudo refuses it and
    // S04-declarations/my-6e.t (and its 6.c twin) checks that it does.
    // (`my $r = Nil unless $dir.chars` — a statement MODIFIER's `my` declares
    // in this scope too, and is there, undefined, when the condition skips it)
    std::function<void(Stmt*)> predeclareStmt = [this, &predeclareStmt](Stmt* s) {
        if (s->kind == NK::IfStmt) {
            auto* is = static_cast<IfStmt*>(s);
            if (is->modifier && is->branches.size() == 1 && is->branches[0].second)
                for (auto& st : is->branches[0].second->stmts) if (st) predeclareStmt(st.get());
            return;
        }
        if (s->kind != NK::ExprStmt) return;
        Expr* e = static_cast<ExprStmt*>(s)->e.get();
        if (!e || e->kind != NK::Assign) return;
        Expr* t = static_cast<Assign*>(e)->target.get();
        if (!t || t->kind != NK::VarExpr) return;
        auto* ve = static_cast<VarExpr*>(t);
        // only a plain `my`: `state`, `our` and the trait/parameterized forms
        // own machinery that the declaration itself has to run
        if (!ve->declare || ve->declScope != "my" || ve->name.empty()) return;
        if (!ve->containerIs.empty() || ve->declTypeExpr || ve->declShape) return;
        if (tctx_.cur->local(ve->name)) return;
        tctx_.cur->define(ve->name, declInitial(ve, ve->name[0]));
    };
    // An EVAL'd unit is compiled whole, so its named subs are visible from
    // its first statement on: `EVAL '&x; 1; sub x {}'`, `EVAL 'f(); sub f {…}'`
    // (only for a user-level EVAL — an internal re-parse has no declarations
    // of its own to hoist)
    if (mainlinePH) {
        // (…but a BEGIN runs while the unit is still being compiled, and sees
        // only what is declared ABOVE it: `BEGIN { ohnoes() }; sub ohnoes() { }`
        // stays an undeclared routine, so a unit with one hoists nothing)
        // (…and only when some statement ABOVE a sub names it: a sub nobody
        // reaches early is left to its own statement, which keeps what its
        // signature checks — the enum values it may be misspelling — in scope)
        bool anySub = false, anyBegin = false;
        std::set<std::string> mentioned;
        for (auto& st : prog->stmts) {
            if (!st) continue;
            if (st->kind == NK::Block && static_cast<Block*>(st.get())->phaser == "BEGIN") anyBegin = true;
            if (st->kind == NK::SubDecl) {
                auto* sd = static_cast<SubDecl*>(st.get());
                if (!sd->isMethod && !sd->name.empty() && !sd->isMulti && !sd->isProto &&
                    (mentioned.count(sd->name) || mentioned.count("&" + sd->name)))
                    anySub = true;
                continue;
            }
            spCallsS(st.get(), mentioned);
            collectMentionedS(st.get(), mentioned);
        }
        if (anySub && !anyBegin && !hoistedForInit) hoistSubs(prog->stmts);
    }
    Value last = Value::nil();   // an empty unit is Nil, as an empty block is
    // the EVAL is a scope of its own for `temp`: `EVAL 'temp $x; ++$x'` leaves
    // $x as it found it
    struct EvalTempGuard {
        std::shared_ptr<Env> env; size_t mark;
        ~EvalTempGuard() {
            if (!env || !env->ex || env->ex->tempRestores.size() <= mark) return;
            auto& tr = env->ex->tempRestores;
            for (size_t i = tr.size(); i-- > mark; ) { try { tr[i](); } catch (...) {} }
            tr.resize(mark);
        }
    } evalTemps{mainlinePH ? tctx_.cur : nullptr,
                tctx_.cur && tctx_.cur->ex ? tctx_.cur->ex->tempRestores.size() : 0};
    // a CATCH at the unit's own top level handles what its statements throw,
    // as a block's does — the mainline of a file gets that from execBlock,
    // and an EVAL'd unit runs its statements one by one, so it is done here
    Block* unitCatch = nullptr;
    if (mainlinePH)
        for (auto& s : prog->stmts)
            if (s && s->kind == NK::Block && static_cast<Block*>(s.get())->isCatch &&
                static_cast<Block*>(s.get())->phaser == "CATCH")
                unitCatch = static_cast<Block*>(s.get());
    // registered like a block's, so an error from deeper down that runs the
    // handlers ahead of its unwinding finds this one before any outside the EVAL
    CatchReg unitCatchReg{tctx_, unitCatch, &prog->stmts, tctx_.cur};
    for (auto& s : prog->stmts) {
        tctx_.endCurTopStmt = s.get();   // for a `use` in it
        if (s.get() == unitCatch) continue;
        // a top-level INIT just ran above; running it again here would double it
        if (s->kind == NK::Block && static_cast<Block*>(s.get())->initHoisted) continue;
        // Loop control inside the EVAL, with a loop OUTSIDE it, belongs to that
        // loop: `for ^3 { EVAL q[last] }` ends the for — as Rakudo does, and as
        // `EVAL q[return …]` already returns from an enclosing routine below.
        // With no loop anywhere on this thread's stack it stays a CATCHABLE
        // error, not a crash. Only a USER-level EVAL (what mainlinePH already
        // marks) hands the word onward: an internal reparse — a regex `{ … }`
        // block, an interpolated `$( … )` — still absorbs it, because the
        // enclosing loop cannot see through the regex engine yet (the ledger
        // entry in dev/findings/SPEC-DIVERGENCES.md, and regexBlockErrorStaysQuiet
        // reads this very message to keep such a block quiet).
        const bool ownedOutside = mainlinePH && tctx_.curLoopFrame != ExecContext::kNoFrame;
        predeclareStmt(s.get());   // the declaration is in scope for its own initialiser
        try {
            if (!unitCatch) last = exec(s.get());
            else try { last = exec(s.get()); }
            catch (RakuError& e) {
                int r = 2;
                switch (catchSeen(unitCatchReg.serial, e)) {
                case CatchSeen::Taken:  r = replayCatchOutcome(e); break;   // it ran ahead
                case CatchSeen::Passed: break;
                case CatchSeen::Fresh: {
                    CatchFence fence(tctx_, unitCatchReg.serial);
                    try { r = runBlockCatch(prog->stmts, unitCatch, e); }
                    catch (RakuError& e2) { markHandlerError(e2, unitCatchReg.serial); throw; }
                    if (r == 2) markHandlerError(e, unitCatchReg.serial);
                }
                }
                if (r == 2) throw;           // nothing matched: the error goes on
                if (r == 1) continue;        // .resume: on with the next statement
                return Value::nil();         // handled: the unit is done
            }
        }
        // (named as Rakudo names them: the illegal control and what encloses it)
        catch (RedoEx&) { if (ownedOutside) throw; throwTypedV("X::ControlFlow", {{"illegal", Value::str("redo")}, {"enclosing", Value::str("loop construct")}}, "redo without loop construct"); }
        catch (NextEx&) { if (ownedOutside) throw; throwTypedV("X::ControlFlow", {{"illegal", Value::str("next")}, {"enclosing", Value::str("loop construct")}}, "next without loop construct"); }
        catch (LastEx&) { if (ownedOutside) throw; throwTypedV("X::ControlFlow", {{"illegal", Value::str("last")}, {"enclosing", Value::str("loop construct")}}, "last without loop construct"); }
        catch (ReturnEx&) {
            // with an enclosing routine, `return` in the EVAL returns from IT;
            // top-level it is the spec'd control-flow error
            if (tctx_.curRoutineFrame != 0) throw;
            throw RakuError{Value::typeObj("X::ControlFlow::Return"), "Attempt to return outside of any Routine"};
        }
        // the cooperative flags must not leak out of a TOP-LEVEL EVAL — a phaser's
        // `return` inside an EVAL'd loop would silently unwind the whole program
        if (tctx_.returning && tctx_.curRoutineFrame == 0) {
            tctx_.returning = false;
            throw RakuError{Value::typeObj("X::ControlFlow::Return"), "Attempt to return outside of any Routine"};
        }
        if (tctx_.returning) return last; // enclosing routine consumes the flag
        if (tctx_.givenCtl) return last;  // enclosing given/loop consumes the flag
        if (tctx_.loopCtl) {
            int c = tctx_.loopCtl; tctx_.loopCtl = 0;
            // An outside loop gets the EXCEPTION form, never the flag. The flag is
            // only safe where the control word IS the statement; an EVAL is an
            // expression, so `my $x = EVAL q[last] + 1` would finish the statement
            // first — the operand trap already documented at evalUnary.
            if (ownedOutside) {
                if (c == 1) throw NextEx{};
                if (c == 2) throw LastEx{};
                throw RedoEx{};
            }
            throw RakuError{Value::typeObj("X::ControlFlow"),
                            std::string(c == 1 ? "next" : c == 2 ? "last" : "redo") +
                            " without a supporting loop construct"};
        }
    }
    return last;
}

// ---- REPL support --------------------------------------------------------
// The session is one Interpreter that never sees run(): each line goes through
// evalString into the SAME global scope, which is what makes `my $x` on line 1
// still there on line 9. These three supply the little that run() would have
// done around the mainline — and nothing here is reachable from a script run.

// --repl-after: the program's statements ran through evalString, which does
// not auto-invoke MAIN; this is the dispatch run() would have done, so a
// program that lives in its MAIN runs the same way before the prompt opens.
int Interpreter::replRunMain() {
    Value* mainSub = tctx_.cur ? tctx_.cur->find("&MAIN") : nullptr;
    if (!mainSub && global_) mainSub = global_->find("&MAIN");
    if (!mainSub || mainSub == inheritedMainBarrier_) return -1;
    ValueList margs;
    int rc = mainProtocol(*mainSub, margs);
    if (rc < 0) { sinkReturnedValue(callCallable(*mainSub, margs)); return 0; }
    return rc;
}

// A program that is not on disk (-e, stdin) still has lines to show: seed the
// excerpt cache from the source itself, under both names it may be asked for.
void Interpreter::seedSrcLines(const std::string& file, const std::string& src) {
    std::vector<std::string> lines;
    size_t p = 0;
    while (p <= src.size()) {
        size_t e = src.find('\n', p);
        lines.push_back(src.substr(p, e == std::string::npos ? std::string::npos : e - p));
        if (e == std::string::npos) break;
        p = e + 1;
    }
    srcLineCache_[file] = lines;
    if (!srcFileAbs_.empty() && srcFileAbs_ != file) srcLineCache_[srcFileAbs_] = lines;
}
// --trace: `[trace] file:line  source` per statement. The file is the
// innermost live routine's declaration file — a statement inside a module
// names the module — else the program's own.
void Interpreter::traceStmt(Stmt* s) {
    int ln = s->line;
    if (ln <= 0 && s->kind == NK::ExprStmt) {
        Expr* e = static_cast<ExprStmt*>(s)->e.get();
        if (e) ln = e->line;
    }
    if (ln <= 0) return;
    // A `use` is executed by the hoisting passes before the mainline and again
    // at its own position; only the one that loads does anything, so only that
    // one is traced (a loaded module makes every later `use` of it a no-op).
    if (s->kind == NK::UseStmt) {
        auto* u = static_cast<UseStmt*>(s);
        if (u->isNo || loadedModules_.count(u->module)) return;
    }
    std::string file;
    auto& fr = tctx_.callFrames;
    if (!fr.empty() && fr.back().code) {
        auto c = fr.back().code->codeS();
        if (c) file = c->declFile;
    }
    if (file.empty()) file = curDeclFile();   // a module body while it loads, else the program
    std::string text = srcLineOf(file, ln);
    size_t lead = text.find_first_not_of(" \t");
    if (lead != std::string::npos) text = text.substr(lead);
    std::cerr << "[trace] " << btDisplayPath(file, srcFileAbs_, srcFile_) << ":" << ln
              << "  " << text << "\n";
}

// $?FILE — the file the executing code was WRITTEN in, a compile-time fact: a
// block written in the program and called from a module's sub still says the
// program, and a module's sub called from the program says the module. The
// innermost live routine records its declaration file; when no routine has
// been entered since a module's top level began (curDeclDepth_), the executing
// unit's file is the answer. It used to answer the main program from anywhere
// (found by --trace, 2026-09-06). A module answers as Rakudo spells it: the
// absolute source path, then the name it was loaded as, in parens.
// The file a routine being declared right now was WRITTEN in — what its
// backtrace frames (and the source excerpt under them) will name.
//
// While a file's TOP LEVEL runs, that file is the answer: a module's subs and
// methods belong to the module, and EVALFILE switches it the same way. But a
// routine declared INSIDE a running routine belongs to the file that routine
// was written in, which curDeclFile() cannot know — by then the module's top
// level is long finished and the program's is running. So a `my sub` nested in
// a module's method recorded the PROGRAM's path with the module's line number,
// and the frame named a line that need not exist in the file it named:
//
//     in sub inner at main.raku line 3        # written in lib/BtProbe.rakumod
//
// Same rule as $?FILE (fileConstNow), and deliberately without its " (Unit)"
// spelling: that is for $?FILE, while this path gets OPENED to print the
// excerpt under the frame.
std::string Interpreter::declFileNow() {
    auto& fr = tctx_.callFrames;
    if (!fr.empty() && (curDeclFile_.empty() || fr.size() > curDeclDepth_))
        if (const Value* cv = fr.back().code)
            if (auto c = cv->codeS())
                if (!c->declFile.empty()) return c->declFile;
    return curDeclFile();
}

// declFileNow() for a routine being declared: the calling routine's own
// (already interned) file, or the top level's, interned once per change of it
// — so a closure made in a loop copies four bytes, where it copied the path.
IStr Interpreter::declFileNowI() {
    auto& fr = tctx_.callFrames;
    if (!fr.empty() && (curDeclFile_.empty() || fr.size() > curDeclDepth_))
        if (const Value* cv = fr.back().code)
            if (auto c = cv->codeS())
                if (!c->declFile.empty()) return c->declFile;
    thread_local std::string lastS;
    thread_local IStr lastI;
    const std::string& cur = curDeclFileRef();
    if (lastI.empty() || cur != lastS) { lastS = cur; lastI = IStr(cur); }
    return lastI;
}

std::string Interpreter::fileConstNow() {
    std::string f;
    auto& fr = tctx_.callFrames;
    if (!fr.empty() && (curDeclFile_.empty() || fr.size() > curDeclDepth_))
        if (const Value* cv = fr.back().code)
            if (auto c = cv->codeS()) f = c->declFile;
    if (f.empty()) f = curDeclFile();
    // a program with no file — -e code, stdin — answers its plain name, as
    // Rakudo does (the cwd-prefixed form is for backtrace .file lookups)
    if (f == srcFileAbs_ && !srcFile_.empty() && srcFile_[0] == '-') return srcFile_;
    std::string unit;
    {
        std::unique_lock<std::mutex> kl(sharedMut_, std::defer_lock); if (parallelMode_) kl.lock();
        auto it = unitNameOfFile_.find(f);
        if (it != unitNameOfFile_.end()) unit = it->second;
    }
    if (unit.empty()) return f;
    std::error_code ec;
    std::string abs = std::filesystem::absolute(f, ec).lexically_normal().string();
    return (ec ? f : abs) + " (" + unit + ")";
}

void Interpreter::replStart(std::vector<std::string> args) {
    argv_ = std::move(args);
    tctx_.curStateEnv = global_.get(); // mainline `state` vars live in the session scope
    Value a = Value::array();
    for (auto& s : argv_) a.arr()->push_back(Value::str(s));
    tctx_.cur->define("@*ARGS", a);
}

void Interpreter::replFinish() {
    // END blocks typed at the prompt were deferred by evalString; they belong to
    // the session, so they run once, newest first, as the session ends.
    std::vector<EndPhaser> all;
    { std::lock_guard<std::mutex> g(endPhaserMut_); all.swap(endPhasers_); }
    std::stable_sort(all.begin(), all.end(),
                     [](const EndPhaser& a, const EndPhaser& b) { return a.key < b.key; });
    for (size_t i = all.size(); i-- > 0; ) {
        try { runEndBody(all[i]); } catch (...) {}
        all[i].blk->endSlot = -1;
    }
}

std::vector<std::string> Interpreter::replNames() const {
    std::vector<std::string> out;
    for (Env* e = tctx_.cur.get(); e; e = e->parent.get())
        e->forEachVar([&](const std::string& n, Value&) { out.push_back(n); });
    for (auto& kv : classes_)  out.push_back(kv.first);
    for (auto& kv : subsets_)  out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// If RAKU_TEST_DIE_ON_FAIL is a true value, a real (non-TODO) failure stops the
// whole suite: emit the Rakudo diagnostics and exit 255.
void Interpreter::maybeDieOnFail() {
    if (dieOnFail_ < 0) dieOnFail_ = envFlag("RAKU_TEST_DIE_ON_FAIL") ? 1 : 0;
    if (!dieOnFail_ || todoSubtestDepth_ > 0) return;
    std::cerr << "Stopping test suite, because value of RAKU_TEST_DIE_ON_FAIL "
                 "environment variable is set to a true value.\n";
    if (planned_ >= 0)
        std::cerr << "# You planned " << planned_ << " tests, but ran " << testNum_ << ".\n";
    std::cerr << "# Looks like you failed " << failCount_ << " test" << (failCount_ == 1 ? "" : "s")
              << " of " << testNum_ << "\n";
    bailedOut_ = true; // suppress the trailing auto-plan
    throw ExitEx{255};
}

void Interpreter::emitTest(bool ok, const std::string& desc, const std::string& directive_,
                           const std::string& extraDiag) {
    // In parallel mode a worker may emit test results concurrently with the main
    // thread; serialise the counters + TAP line so nothing interleaves or races.
    std::unique_lock<std::mutex> plk(sharedMut_, std::defer_lock);
    if (parallelMode_) plk.lock();
    std::string directive = directive_;
    // A bare `todo REASON, COUNT` statement marks the next COUNT tests TODO.
    if (directive.empty() && todoRemaining_ > 0) {
        directive = "TODO" + (todoReason_.empty() ? "" : " " + todoReason_);
        todoRemaining_--;
    }
    // …and inside a subtest that is TODO as a whole, every FAILING assertion
    // is TODO with that reason (Rakudo's todo covers the subtest's own lines
    // too — but a passing one prints as a plain `ok`)
    if (directive.empty() && !ok && todoSubtestDepth_ > 0 && subtestDepth_ > 0)
        directive = "TODO" + (todoSubtestReason_.empty() ? "" : " " + todoSubtestReason_);
    // Rakudo's proclaim always emits " - <description>" (description defaults to the
    // empty string) for ok/is/etc.; only skip() suppresses it (it emits "# SKIP …").
    bool isSkip = directive.rfind("SKIP", 0) == 0;
    bool realFail = !ok && directive.empty(); // a genuine failure (not TODO/SKIP)
    // a TODO failure is still reported — on STDOUT, the TODO output, as Rakudo's
    // Test does, so a harness reading the TAP sees why
    auto todoFailDiag = [&](const std::string& ind) {
        if (ok || directive.rfind("TODO", 0) != 0) return;
        if (desc.empty()) std::cout << ind << "# Failed test at " << srcFile_ << " line " << curLine_ << "\n";
        else std::cout << ind << "# Failed test '" << desc << "'\n" << ind << "# at " << srcFile_ << " line " << curLine_ << "\n";
        std::cout << std::flush;
    };
    if (subtestDepth_ > 0) {
        // in a TODO subtest a failure — TODO-marked or not — fails the subtest
        if (realFail || (!ok && !isSkip && todoSubtestDepth_ > 0)) subtestFailed_ = true;
        testNum_++; // advance the subtest-local counter (plan checks + skip-rest depend on it)
        std::string ind(4 * subtestDepth_, ' '); // one indent level per nesting depth
        // a description's own newlines must not start TAP lines: each is `# `-escaped
        std::string d2 = desc;
        for (size_t i = 0; (i = d2.find('\n', i)) != std::string::npos; i += 1 + ind.size() + 2)
            d2.replace(i, 1, "\n" + ind + "# ");
        std::cout << ind << (ok ? "ok " : "not ok ") << testNum_;
        if (isSkip) std::cout << " - # " << directive;
        else {
            std::cout << " - " << d2;
            if (!directive.empty()) std::cout << " # " << directive;
        }
        std::cout << "\n" << std::flush;   // see the top-level line below
        todoFailDiag(ind);
        if (realFail) {
            // Rakudo's two-line form when there is a description
            const int ln = testAssertLine_ ? testAssertLine_ : (int)curLine_;
            if (desc.empty()) std::cerr << "# Failed test at " << srcFile_ << " line " << ln << "\n";
            else std::cerr << "# Failed test '" << desc << "'\n# at " << srcFile_ << " line " << ln << "\n";
            if (!extraDiag.empty()) std::cerr << extraDiag;
            maybeDieOnFail();
        }
        return;
    }
    testNum_++;
    std::ostringstream os;
    std::string d2 = desc;
    for (size_t i = 0; (i = d2.find('\n', i)) != std::string::npos; i += 3) d2.replace(i, 1, "\n# ");
    os << (ok ? "ok " : "not ok ") << testNum_;
    if (isSkip) os << " - # " << directive;
    else {
        os << " - " << d2;
        if (!directive.empty()) os << " # " << directive;
    }
    // Flushed per result: on a pipe stdout is block-buffered, so a file the
    // harness kills at its timeout lost every result it had already reported
    // (the `[TIME] 0/0` lines — scheduler tests that pass 26/26 standalone)
    std::cout << os.str() << "\n" << std::flush;
    todoFailDiag("");
    if (realFail) {
        failCount_++;
        const int ln = testAssertLine_ ? testAssertLine_ : (int)curLine_;
        if (desc.empty()) std::cerr << "# Failed test at " << srcFile_ << " line " << ln << "\n";
        else std::cerr << "# Failed test '" << desc << "'\n# at " << srcFile_ << " line " << ln << "\n";
        if (!extraDiag.empty()) std::cerr << extraDiag;
        maybeDieOnFail();
    }
}
void Interpreter::runNextPhasers(const std::vector<StmtPtr>& stmts, std::shared_ptr<Env>& scope) {
    // NEXT phasers run in REVERSE declaration order (like LEAVE)
    for (auto it = stmts.rbegin(); it != stmts.rend(); ++it) if ((*it)->kind == NK::Block) {
        auto* b = static_cast<Block*>(it->get());
        if (b->phaser == "NEXT") { auto sc = std::make_shared<Env>(); sc->parent = scope; execBlock(b, sc); } }
}
// The scope a registered END will run in: the most recent entry of the block
// that holds it. `for 1..3 -> $i { END say $i }` therefore says 3 — one run,
// the last iteration's $i — exactly as Rakudo's per-entry closure clone does.
void Interpreter::captureEndScope(Block* b) {
    // `END my $x = …` DECLARES in the ENCLOSING scope, and Rakudo's pad carries
    // that slot from compile time — so code around the phaser can name $x
    // (undefined) although the body only runs at exit. The same repair
    // runLeavePhasers makes for a declaration an early `return` jumped over.
    if (b->stmtForm) {
        std::vector<std::string> declared;
        blockDeclNames(b->stmts, declared);
        for (auto& n : declared)
            if (!tctx_.cur->find(n))
                tctx_.cur->define(n, n[0] == '@' ? Value::array() : n[0] == '%' ? Value::makeHash() : Value::any());
    }
    std::lock_guard<std::mutex> g(endPhaserMut_); // any thread may enter the block
    if (b->endSlot >= 0 && (size_t)b->endSlot < endPhasers_.size()) {
        endPhasers_[b->endSlot].env = tctx_.cur;
        endPhasers_[b->endSlot].entered = true;
    }
}
// One entry of the report an END's exception earns: the type and message on the
// first line, its frames indented under it. Rakudo's shape, because that report
// is what a reader greps for and half of it existing in a different layout helps
// nobody. No source excerpt and no `(X::Type)` line — the type is already on
// line 1, and the excerpt belongs to the uncaught printer.
std::string Interpreter::renderEndError(const RakuError& e) {
    // The payload is a constructed object only when the PROGRAM threw one
    // (`X::IO::Mkdir.new(…).throw`). Every exception the engine raises carries a
    // bare type object instead, so reading the name off the object path alone
    // labelled all of them X::AdHoc — the least informative answer available,
    // and one that tells a reader the engine did a plain `die` when it did not.
    // `$!.^name` on the same exception says X::IO::Rmdir; so does this now.
    std::string tn = e.payload.t == VT::Type ? e.payload.s : e.payload.typeName();
    if (tn.empty() || tn == "Any" || tn == "Mu") tn = "X::AdHoc";
    std::string out = "  " + tn + ": " + e.message + "\n";
    if (e.bt) {
        BtStyle st; st.excerpt = false; st.typeLine = false; st.colour = false;
        std::istringstream in(renderFrames(*e.bt, st));
        std::string l;
        while (std::getline(in, l)) if (!l.empty()) out += "    " + l + "\n"; // 2 + 4 = Rakudo's six
    }
    return out + "\n";
}
// One END phaser's body, in the scope it captured.
void Interpreter::runEndBody(const EndPhaser& e) {
    // stmtForm (`END rm-rf($dir);`) runs IN that scope, as `INIT my $x = …`
    // declares in the enclosing one; a braced END gets its own scope under it.
    // SINK: an END's value goes nowhere, so a Failure that is the body's last
    // expression detonates here rather than being dropped undetonated —
    // `END { $dir.IO.spurt("x") }` on a directory that has been removed is the
    // whole shape issue #71 was reported for, and it said nothing at all.
    if (e.entered && e.blk->stmtForm) { execBlock(e.blk, e.env, /*sink=*/true); return; }
    auto sc = std::make_shared<Env>(); sc->parent = e.env;
    // NEVER ENTERED: the block holding this phaser did not run, so the
    // containers Rakudo's compile-time pad would have carried never came to
    // exist. A name the body reads is then not a typo but a scope that was
    // skipped — `sub f($n) { my $x = $n * 2; END say "x=$x" }`, uncalled, says
    // "x=" on Rakudo — so the body runs with `no strict`'s leniency (an
    // unresolved name reads as Any) rather than dying with X::Undeclared and
    // being swallowed whole by the catch-all every END body runs under.
    if (!e.entered) sc->strictPragma = 1;
    execBlock(e.blk, sc, /*sink=*/true);
}
// A unit loaded by another hangs off the loader's key at the position of the
// statement that loaded it: after the loader's ENDs written above that `use`,
// before the ones written below it. The second component separates two units
// loaded at the SAME position (`use A; use B;` with no END between them), in
// load order — which is compile order, so B's ENDs still run before A's.
Interpreter::EndUnitScope::EndUnitScope(Interpreter& i, bool atSourcePosition)
    : I(i), key(i.tctx_.endUnitKey), before(i.tctx_.endsBeforeStmt), cur(i.tctx_.endCurTopStmt) {
    auto& K = I.tctx_.endUnitKey;
    if (!atSourcePosition) {   // an EVAL: registered when it runs, after everything compiled
        K.clear();
        K.push_back(std::numeric_limits<int>::max());
    }
    else {
        int c = 0;   // how many of the loader's own ENDs are written above the statement loading us
        if (I.tctx_.endCurTopStmt) {
            auto it = I.tctx_.endsBeforeStmt.find(I.tctx_.endCurTopStmt);
            if (it != I.tctx_.endsBeforeStmt.end()) c = it->second;
        }
        K.push_back(c - 1);      // -1: loaded above the loader's first END
    }
    K.push_back(I.endUnitSeq_.fetch_add(1, std::memory_order_relaxed));
    I.tctx_.endsBeforeStmt.clear();
    I.tctx_.endCurTopStmt = nullptr;
}
Interpreter::EndUnitScope::~EndUnitScope() {
    I.tctx_.endUnitKey = std::move(key); I.tctx_.endsBeforeStmt = std::move(before);
    I.tctx_.endCurTopStmt = cur;
}

// Every END of a unit, at any depth, in source order. Registration is what
// Rakudo does at COMPILE time, so the phaser's textual position no longer runs
// it — see isBlockPhaser — and an END in a never-called sub still runs at exit,
// in the unit scope captured here.
void Interpreter::registerEnds(const Program& prog) {
    if (!prog.mayHaveEnd) return;   // the parser saw none: no walk at all
    std::vector<Block*> ends;
    g_endChain.clear(); g_endBlockChain.clear(); g_endEdges.clear();
    // …and, per top-level statement, how many of this unit's ENDs are written
    // ABOVE it — what a `use` in that statement needs to place its module.
    for (auto& s : prog.stmts) {
        tctx_.endsBeforeStmt[s.get()] = (int)ends.size();
        collectPhasersStmt(s.get(), "END", ends);
    }
    std::lock_guard<std::mutex> g(endPhaserMut_);
    for (size_t i = 0; i < ends.size(); i++) {
        Block* b = ends[i];
        if (b->endSlot >= 0) continue;   // already registered (this unit is being re-entered)
        std::vector<int> key = tctx_.endUnitKey;
        key.push_back((int)i);
        b->endSlot = (int)endPhasers_.size();
        endPhasers_.push_back({b, tctx_.cur, std::move(key), /*entered=*/false});
    }
    for (auto& e : g_endEdges) {
        if (e.second->endSlot < 0) continue;
        auto& list = endsUnder_[e.first];
        if (!list) list = std::make_shared<std::vector<Block*>>();
        const_cast<std::vector<Block*>&>(*list).push_back(e.second);
    }
    if (!endsUnder_.empty()) endsUnderAny_.store(true, std::memory_order_relaxed);
    g_endChain.clear(); g_endBlockChain.clear(); g_endEdges.clear();
}
void Interpreter::runFirstPhasers(const std::vector<StmtPtr>& stmts) {
    for (auto& s : stmts) if (s->kind == NK::Block) { auto* b = static_cast<Block*>(s.get());
        if (b->phaser == "FIRST") { auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur; execBlock(b, sc); } }
}
void Interpreter::runLastPhasers(const std::vector<StmtPtr>& stmts) {
    for (auto& s : stmts) if (s->kind == NK::Block) { auto* b = static_cast<Block*>(s.get());
        if (b->phaser == "LAST") { auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur; execBlock(b, sc); } }
}
// The `my` names a statement list declares at ITS OWN level (an inner block's
// declarations belong to that block). Used to give a LEAVE/KEEP/UNDO body the
// slots its block would have had in a compile-time pad — see runLeavePhasers.
void blockDeclNames(const std::vector<StmtPtr>& stmts, std::vector<std::string>& out) {
    auto take = [&](Expr* e) {
        if (!e) return;
        if (e->kind == NK::Assign) e = static_cast<Assign*>(e)->target.get();
        if (!e) return;
        if (e->kind == NK::VarExpr) {
            auto* v = static_cast<VarExpr*>(e);
            if (v->declare && v->name.size() > 1 && v->name[1] != '*') out.push_back(v->name);
        } else if (e->kind == NK::ListExpr) {
            for (auto& it : static_cast<ListExpr*>(e)->items) {
                Expr* x = it.get();
                if (x && x->kind == NK::Assign) x = static_cast<Assign*>(x)->target.get();
                if (x && x->kind == NK::VarExpr) {
                    auto* v = static_cast<VarExpr*>(x);
                    if (v->declare && v->name.size() > 1 && v->name[1] != '*') out.push_back(v->name);
                }
            }
        }
    };
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == NK::ExprStmt) take(static_cast<ExprStmt*>(s.get())->e.get());
        else if (s->kind == NK::VarDecl) {
            auto* d = static_cast<VarDecl*>(s.get());
            if (d->scope == "my")
                for (auto& n : d->names)
                    if (n.size() > 1 && n[1] != '*') out.push_back(n);
        }
    }
}

// A block's CATCH, run for the error `e` in the block's own scope. 0 = handled,
// 1 = `.resume` (carry on at the next statement), 2 = matched nothing (the
// error goes on). Out of line — execBlock's frame is paid at every level of a
// Raku recursion, and this one's temporaries are needed only when a CATCH runs.
[[gnu::noinline]] int Interpreter::runBlockCatch(Block* b, Block* catchBlk, RakuError& e) {
    return runBlockCatch(b->stmts, catchBlk, e);
}

[[gnu::noinline]] int Interpreter::runBlockCatch(const std::vector<StmtPtr>& stmts, Block* catchBlk, RakuError& e) {
    ExecContext& tcx = tctx_;
    declareSkippedLexicals(stmts, tcx.cur.get());
    tcx.cur->define("$_", exceptionFor(e));
    tcx.cur->define("$!", exceptionFor(e));
    bool matched = false;
    // A when/default here matches COOPERATIVELY: it sets givenCtl, and the
    // loops below take that as the match. (It used to throw BreakGivenEx
    // for them to catch — a second C++ throw for every exception a CATCH
    // handled, and a throw is tens of µs on macOS; see ExecContext::givenCtl.
    // One behind a callable boundary still throws, and is still caught.)
    uint64_t savedGF = tcx.curGivenFrame; tcx.curGivenFrame = tcx.frameTop;
    struct GFRestore { ExecContext& t; uint64_t f; ~GFRestore() { t.curGivenFrame = f; t.givenCtl = 0; } } gfr{tcx, savedGF};
    auto whenMatched = [&tcx]() {
        if (!tcx.givenCtl) return false;
        tcx.givenCtl = 0; tcx.givenV = Value();
        return true;
    };
    // a CATCH directly inside this CATCH handles what the handler throws
    Block* innerCatch = nullptr;
    for (auto& s : catchBlk->stmts)
        if (s->kind == NK::Block && static_cast<Block*>(s.get())->isCatch &&
            static_cast<Block*>(s.get())->phaser != "CONTROL")
            innerCatch = static_cast<Block*>(s.get());
    try {
        struct G { int& d; G(int& x) : d(x) { d++; } ~G() { d--; } } g{catchDepth_};
        for (auto& s : catchBlk->stmts) {
            if (s.get() == innerCatch) continue;
            exec(s.get());
            if (whenMatched()) { matched = true; break; }
            // a `next`/`last`/`return` leaves the handler where it stands
            if (tcx.loopCtl || tcx.returning) break;
        }
    }
    catch (BreakGivenEx&) { matched = true; /* when/default matched */ }
    catch (ResumeEx&) { return 1; }
    catch (RakuError& e2) {
        if (!innerCatch) throw;
        tcx.cur->define("$_", exceptionFor(e2));
        tcx.cur->define("$!", exceptionFor(e2));
        bool innerMatched = false;
        try {
            for (auto& s : innerCatch->stmts) {
                exec(s.get());
                if (whenMatched()) { innerMatched = true; break; }
            }
        }
        catch (BreakGivenEx&) { innerMatched = true; }
        if (!innerMatched) throw;
        matched = true;
    }
    // only a matching when/default HANDLES the exception — a CATCH body
    // without one runs (it can read $_/.message) but the exception rethrows
    // (Rakudo dies even under try in that shape)… unless the handler LEFT
    // with a loop control or a return: `CATCH { next }` goes on to the next
    // iteration, the exception done with (cooperative next/last/return set
    // these flags rather than throwing)
    if (!matched && (tcx.loopCtl || tcx.returning)) return 0;
    return matched ? 0 : 2;
}

std::atomic<uint64_t> g_catchSerial{0};

// What the handler registered as `serial` makes of an error reaching its
// block: one it has not seen, one it handled ahead of the unwinding (its
// block now finishes the job), or one it has run for and passed on.
Interpreter::CatchSeen Interpreter::catchSeen(uint64_t serial, RakuError& e) {
    if (!e.dispCtx || e.dispCtx != ctxIdOf(tctx_)) return CatchSeen::Fresh;
    if (e.takenBy == serial) return CatchSeen::Taken;
    if (serial >= e.seenTo) return CatchSeen::Passed;
    return CatchSeen::Fresh;
}

// The handler `serial` has run for `e` and not taken it — or raised `e`
// itself, which no handler from it inward may take.
void Interpreter::markHandlerError(RakuError& e, uint64_t serial) {
    const uint64_t id = ctxIdOf(tctx_);
    if (e.dispCtx != id) { e.dispCtx = id; e.seenTo = serial; e.takenBy = 0; e.outcome.reset(); }
    else if (e.seenTo > serial) e.seenTo = serial;
}

// The error `e` is about to leave a block that has exit work — LEAVE, UNDO, a
// `temp` to restore. Rakudo runs the handler that takes an error BEFORE the
// blocks between it and the `die` are left (that is what makes `.resume`
// possible), so their LEAVEs come after the CATCH. Here the handlers are run
// now, innermost first, each in its own block's scope and routine; the first
// that takes the error is recorded on it (takenBy) with how it left, and its
// block, once the unwinding reaches it, acts as though the handler had run
// there (catchSeen, replayCatchOutcome). A handler that matches nothing is
// recorded as passed. The walk stops at a frame a plain call did not enter —
// a builtin's callback, a `try` block — since what lies past one may keep the
// error to itself; past it the handlers run as the error reaches them.
// 0: done (the error goes on); 1: `.resume`d and `canResume` (the caller
// carries on after the statement); 2: a handler died, and `e` is now that
// error, already offered to the handlers further out.
[[gnu::noinline]] int Interpreter::dispatchBeforeUnwind(RakuError& e, bool canResume) {
    ExecContext& tcx = tctx_;
    if (tcx.catchFrames.empty()) return 0;
    const uint64_t id = ctxIdOf(tcx);
    if (e.dispCtx != id) { e.dispCtx = id; e.seenTo = ~uint64_t(0); e.takenBy = 0; e.outcome.reset(); }
    if (e.takenBy) return 0;
    int result = 0;
    for (size_t i = tcx.catchFrames.size(); i-- > 0; ) {
        if (tcx.catchFrames[i].fence) { i = tcx.catchFrames[i].fenceTo; continue; }
        if (tcx.catchFrames[i].serial >= e.seenTo) continue;
        // a copy: the handler's run pushes registrations of its own
        const ExecContext::CatchFrame h = tcx.catchFrames[i];
        if (tcx.frameTop < h.frameTop || tcx.frameTop - h.frameTop != tcx.transpFrames - h.transp) break;
        if (!h.catchBlk) { e.seenTo = e.takenBy = h.serial; return result; }   // a `try`
        int r = 2;
        bool died = false;
        RakuError raisedInHandler;
        std::exception_ptr ctl;
        {
            struct Ctx {
                ExecContext& t; std::shared_ptr<Env> cur; uint64_t ft, tr, rf, lf; Env* re;
                ~Ctx() { t.dynStack.pop_back(); t.cur = std::move(cur); t.frameTop = ft; t.transpFrames = tr; t.curRoutineFrame = rf; t.curLoopFrame = lf; t.curRoutineEnv = re; }
            } ctx{tcx, tcx.cur, tcx.frameTop, tcx.transpFrames, tcx.curRoutineFrame, tcx.curLoopFrame, tcx.curRoutineEnv};
            // the handler is called from where the error stands: a `$*x`
            // declared there is the one it sees
            tcx.dynStack.push_back(tcx.cur.get());
            tcx.cur = h.env; tcx.frameTop = h.frameTop; tcx.transpFrames = h.transp;
            tcx.curRoutineFrame = h.routineFrame; tcx.curLoopFrame = h.loopFrame; tcx.curRoutineEnv = h.routineEnv;
            CatchFence fence(tcx, h.serial);
            try { r = runBlockCatch(*h.stmts, h.catchBlk, e); }
            catch (RakuError& e2) { raisedInHandler = e2; died = true; }
            catch (...) { ctl = std::current_exception(); }
        }
        if (died) {   // the handler's own error goes on from here, in place of `e`
            markHandlerError(raisedInHandler, h.serial);
            e = std::move(raisedInHandler);
            result = 2;
            if (e.takenBy) return result;
            continue;
        }
        e.seenTo = h.serial;
        if (r == 2 && !ctl) continue;
        if (r == 1 && canResume) return 1;
        e.takenBy = h.serial;
        if (ctl || r == 1 || tcx.loopCtl || tcx.returning) {
            auto o = std::make_shared<CatchOutcome>();
            o->ctl = ctl;
            o->resume = r == 1;
            o->loopCtl = tcx.loopCtl; tcx.loopCtl = 0;
            if (tcx.returning) { o->returning = true; o->returnV = std::move(tcx.returnV); tcx.returning = false; }
            e.outcome = std::move(o);
        }
        return result;
    }
    return result;
}

// The built-in `die` with a CATCH that may take its error: the handlers run
// where the `die` stands, and a `.resume` makes the call answer Nil.
[[gnu::noinline]] Value Interpreter::dieDispatching(ValueList& a) {
    RakuError e = dieError(a);
    if (dispatchBeforeUnwind(e, /*canResume=*/true) == 1) return Value::nil();
    throw std::move(e);
}

// A handler's block, reached by an error the handler took ahead of the
// unwinding: what the handler's run returned (0 handled, 1 `.resume`), with
// the control it left by set again or raised again.
[[gnu::noinline]] int Interpreter::replayCatchOutcome(RakuError& e) {
    std::shared_ptr<CatchOutcome> o = std::move(e.outcome);
    if (!o) return 0;
    if (o->ctl) std::rethrow_exception(o->ctl);
    if (o->resume) return 1;
    ExecContext& tcx = tctx_;
    if (o->loopCtl) tcx.loopCtl = o->loopCtl;
    if (o->returning) { tcx.returning = true; tcx.returnV = std::move(o->returnV); }
    return 0;
}

// A handed error that arrived as a C++ exception is raised again as that very
// object (a FeatureNotBuilt stays one) — with the marks the copy picked up.
[[noreturn]] void Interpreter::raiseHanded(HandedError& h) {
    if (!h.raised) throw std::move(h.err);   // a `die` taken by hand, never raised
    if (!h.err.dispCtx) std::rethrow_exception(h.raised);
    try { std::rethrow_exception(h.raised); }
    catch (RakuError& r) { r.dispCtx = h.err.dispCtx; r.seenTo = h.err.seenTo; r.takenBy = h.err.takenBy; r.outcome = h.err.outcome; throw; }
}

// A statement of a block that takes its errors by hand (execBlock's statement
// loop, for a block with a CATCH or one whose caller takes them): what can
// arrive without a C++ throw does. A nested bare block hands back the error it
// is left by; a statement that is only a call of the built-in `die` has its
// error made, not thrown. Everything else runs through exec — and an error it
// raises is caught by the loop as before. The `die` is the built-in's when the
// program has no `&die` of its own in scope and the built-in is not wrapped —
// evalCall's own test — and the statement does what exec and evalCall would:
// the line and --trace, the arguments, the Seqs in them read.
[[gnu::noinline]] Value Interpreter::execStmtHanding(Stmt* s, bool sink, std::unique_ptr<HandedError>& err) {
    if (s->kind == NK::Block) {
        if (s->line > 0) curLine_ = s->line;
        if (g_traceStmts) traceStmt(s);
        return execBareBlock(static_cast<Block*>(s), sink, &err);
    }
    if (s->kind == NK::ExprStmt) {
        Expr* e = static_cast<ExprStmt*>(s)->e.get();
        if (e && e->kind == NK::Call) {
            auto* c = static_cast<Call*>(e);
            if (!c->callee && c->name == "die" && !tctx_.cur->find(callAmpName(c))) {
                auto rit = builtinRefs_.find(c->name);
                if (rit == builtinRefs_.end() || !rit->second.code() || rit->second.code()->wrappers.empty()) {
                    if (s->line > 0) curLine_ = s->line;
                    if (g_traceStmts) traceStmt(s);
                    tctx_.curStmtExpr = e;
                    if (e->line > 0) curLine_ = e->line;
                    ValueList args = evalArgs(c->args);
                    for (auto& a : args) if (a.t == VT::Array) a.seqTouch();
                    err.reset(new HandedError{dieError(args), nullptr});
                    return Value::nil();
                }
            }
        }
    }
    return exec(s, sink);
}

// LAST {…} once, when a loop ENDS — by exhausting its source, its condition
// going false, or a `last` (wherever the `last` came from: the body, a nested
// call, a NEXT or FIRST phaser). Runs in `scope`, the final iteration's scope,
// so the loop variable holds its last value ("L at $_"). NOT run when the loop
// unwinds some other way: a `return`, an exception, or a labeled `last` that
// targets an OUTER loop (Rakudo skips the inner loop's LAST then too).
// A `return` inside the phaser leaves the enclosing routine; with no routine it
// is X::ControlFlow::Return — the same contract as the in-loop phaser runners.
void Interpreter::runLoopLast(Block* body, const std::shared_ptr<Env>& scope) {
    if (!body) return;
    auto saved = tctx_.cur; tctx_.cur = scope;
    try { runLastPhasers(body->stmts); }
    catch (ReturnEx&) {
        tctx_.cur = saved;
        if (tctx_.curRoutineFrame != 0) throw; // the enclosing routine consumes it
        throw RakuError{Value::typeObj("X::ControlFlow::Return"),
                        "Attempt to return outside of any Routine"};
    }
    catch (...) { tctx_.cur = saved; throw; }
    tctx_.cur = saved;
    if (tctx_.returning && tctx_.curRoutineFrame == 0) {
        tctx_.returning = false;
        throw RakuError{Value::typeObj("X::ControlFlow::Return"),
                        "Attempt to return outside of any Routine"};
    }
}

// `hyper for` / `race for`. The iterations go out in batches of 64, in order,
// to as many workers as the machine has cores less one (Rakudo's `.hyper`
// defaults), and this thread waits for them. Each iteration binds its loop
// variable in its worker's scope and runs the body through runLoopBody, as the
// serial loop does — so `next`, `redo`, a `when` and the collected value behave
// as they do there. That scope also holds the worker's own `$/`: a match in the
// body would otherwise write the enclosing routine's from several threads at once.
//
// The values come back in source order, which hyper promises and race permits.
// A `last` ends the loop where it stands: every iteration before it counts,
// none after it (a later batch already under way is thrown away). A die ends it
// too, reaching the caller doing X::HyperRace::Died, as in Rakudo; whichever of
// the two came first in the source decides.
void Interpreter::runHyperLoop(ForStmt* fs, size_t n, const HyperBind& bind,
                               const HyperBind& writeBack, ValueList* collect) {
    constexpr size_t kBatch = 64;
    const size_t nb = (n + kBatch - 1) / kBatch;
    const unsigned hc = std::thread::hardware_concurrency();
    const size_t degree = std::min<size_t>(nb, hc > 1 ? hc - 1 : 1);
    struct Run {
        std::atomic<size_t> next{0};           // the next batch to hand out
        std::atomic<size_t> stop{SIZE_MAX};    // the earliest iteration that ended the loop
        std::mutex m;                          // guards errAt/err/lastAt
        size_t errAt = SIZE_MAX, lastAt = SIZE_MAX;
        std::exception_ptr err;
        std::vector<ValueList> out;            // each batch's values
    };
    auto run = std::make_shared<Run>();
    if (collect) run->out.resize(nb);
    auto lower = [](std::atomic<size_t>& a, size_t v) {
        size_t cur = a.load();
        while (v < cur && !a.compare_exchange_weak(cur, v)) {}
    };
    auto loopScope = tctx_.cur;
    Block* body = fs->body.get();
    const std::string label = fs->label;
    const bool keep = collect != nullptr;
    // a worker's own `$/` starts as the one the loop sees: a match made before
    // the loop still reads the same inside it
    Value outerMatch = Value::nil();
    if (loopScope) if (Value* m = loopScope->find("$/")) outerMatch = *m;
    const bool flat = flatLoopBody(body);   // (asked here: it caches on the node)
    Value work; work.t = VT::Code; work.setCode(makePayload<Callable>());
    // (bind/writeBack are the caller's, alive until every worker has finished)
    work.code()->builtin = [run, lower, loopScope, outerMatch, body, label, keep, flat, n, nb, &bind, &writeBack]
                           (Interpreter& I, ValueList&) -> Value {
        // One scope per worker, kept across its iterations as the serial loop
        // keeps one: a scope per iteration made every worker bump the one
        // shared parent's reference count, and seven cores contending for that
        // cache line ran the loop slower than one thread did. A fresh scope
        // only when the last escaped (a closure in the body captured it);
        // otherwise emptied between iterations, or for a flat body overwritten.
        std::shared_ptr<Env> scope;
        // …and a frame of its own to stand in: a worker starts out in the
        // spawner's scope, which they all share, and every body entry saves
        // and restores the current scope — one more shared count per iteration
        auto own = std::make_shared<Env>();
        own->parent = loopScope;
        struct CurG { std::shared_ptr<Env> s; ~CurG() { tctx_.cur = std::move(s); } } curG{tctx_.cur};
        tctx_.cur = own;
        for (;;) {
            const size_t b = run->next.fetch_add(1);
            if (b >= nb || b * kBatch > run->stop.load()) break;
            ValueList* out = keep ? &run->out[b] : nullptr;
            if (out) out->reserve(kBatch);
            for (size_t i = b * kBatch, e = std::min(n, i + kBatch); i < e; i++) {
                if (i > run->stop.load()) break;
                if (!scope || scope.use_count() > 1) {
                    scope = std::make_shared<Env>();
                    scope->parent = loopScope;
                    scope->define("$/", outerMatch);
                }
                else if (!flat) {
                    scope->vars.clear();
                    scope->define("$/", outerMatch);
                }
                bool cont = true;
                try {
                    if (!bind(i, scope)) continue;
                    std::function<void()> rebind = [&] { bind(i, scope); };   // redo: the element afresh
                    cont = I.runLoopBody(body, scope, label, i == 0, i + 1 == n, out, rebind);
                    if (writeBack) writeBack(i, scope);
                }
                // a next/last labeled for an OUTER loop: that loop is on another
                // thread, out of reach, so it acts on this one
                catch (NextEx&) { continue; }
                catch (LastEx&) { cont = false; }
                catch (...) {
                    { std::lock_guard<std::mutex> lk(run->m);
                      if (i < run->errAt) { run->errAt = i; run->err = std::current_exception(); } }
                    lower(run->stop, i);
                    break;
                }
                if (!cont) {
                    { std::lock_guard<std::mutex> lk(run->m); if (i < run->lastAt) run->lastAt = i; }
                    lower(run->stop, i);
                    break;
                }
            }
        }
        return Value::nil();
    };
    // A `hyper for` nested in another's body spawns from a worker. Past the
    // spawn cap (throttleSpawn) a spawner waits for the herd to thin, which the
    // parents parked here awaiting their own workers never do — so well short
    // of the cap the batches run on this thread instead.
    if (liveWorkers_.load(std::memory_order_relaxed) + (int)degree > 256) {
        ValueList none;
        work.code()->builtin(*this, none);
    }
    else {
        std::vector<Value> workers;
        workers.reserve(degree);
        for (size_t k = 0; k < degree; k++) workers.push_back(spawnPromise(work));
        for (auto& p : workers) awaitPromise(std::static_pointer_cast<PromiseState>(p.ext()));
    }
    // every worker has settled: nothing below races them
    if (run->err && run->errAt < run->lastAt) {
        try { std::rethrow_exception(run->err); }
        catch (RakuError& e) {
            RakuError err = e;
            Value ex = exceptionFor(err);
            if (ex.t == VT::Object && ex.obj()) {
                Value mixed = mixinValue(ex, Value::typeObj("X::HyperRace::Died"), true);
                if (mixed.t == VT::Object && mixed.obj()) err.payload = mixed;
            }
            throw err;
        }
        // anything else (a control exception, a teardown abort) goes on as it was
    }
    if (collect) {
        const size_t upTo = run->lastAt == SIZE_MAX ? nb : run->lastAt / kBatch + 1;
        for (size_t b = 0; b < upTo; b++)
            for (auto& v : run->out[b]) collect->push_back(std::move(v));
    }
}

// Is `n` a built-in type name that a class may legitimately derive from?
// (Used to decide whether `class X is Y` with an unregistered Y is an error.)
GrammarParseDiag& grammarParseDiag() {
    static thread_local GrammarParseDiag d;
    return d;
}

bool isNativeTypeName(const std::string& n) {
    static const std::set<std::string> t = {
        "int", "int8", "int16", "int32", "int64", "uint", "uint8", "uint16",
        "uint32", "uint64", "byte", "num", "num32", "num64", "str", "atomicint",
        "array", "bool", "size_t", "ssize_t", "long", "longlong", "ulong",
        // `void` is NativeCall's own type, not a native storage type: it exists
        // only to parameterize a pointer that points at nothing in particular
        // (`Pointer[void]`, the C `void *`). It belongs in this set all the same
        // — every consumer asks "is this name a type at all?", and without it a
        // bare `void` was an undeclared name and `Pointer[void]` died at parse.
        "ulonglong", "void"};
    return t.count(n) > 0;
}

bool isKnownTypeName(const std::string& n) {
    if (n.empty()) return false;
    // Asked on hot paths (every `.new` of a user class asks whether it shadows
    // a built-in): one hash probe answers nearly every name, and only a
    // qualified or parameterized one goes on to the prefix tests below. The
    // ordered set stays for callers that walk it.
    static const std::unordered_set<std::string> kCore(coreTypeNames().begin(), coreTypeNames().end());
    if (kCore.count(n)) return true;
    if (n.find_first_of(":[") == std::string::npos) return n == "CArray" || n == "Pointer";
    if (n.rfind("X::", 0) == 0) return true;         // exception types
    if (n.rfind("Metamodel::", 0) == 0) return true; // HOWs
    if (n.rfind("IO::", 0) == 0) return true;         // IO::Path::Unix, etc.
    if (n.rfind("Pod::", 0) == 0) return true;        // Pod DOM nodes
    if (n.rfind("CompUnit::", 0) == 0) return true;   // repository chain
    if (n.rfind("Rakudo::", 0) == 0) return true;     // Rakudo::Internals and kin
    // Rakupp::Internals only, NOT all of Rakupp:: — a typo'd Rakupp:: name
    // must stay an error rather than resolve to nothing. (This exception used
    // to exist for a second reason: an ecosystem module, Rakupp::JSON, lived
    // in the engine's own namespace. It is JSON::Native now — that collision,
    // and the one with Rakupp::Internals::JSON beside it, is why it moved.)
    if (n == "Rakupp::Internals" || n.rfind("Rakupp::Internals::", 0) == 0)
        return true;                                  // the first-party internals names
    if (n.rfind("CX::", 0) == 0) return true;         // control exceptions (CX::Last, CX::Warn…)
    if (n.rfind("Encoding::", 0) == 0) return true;   // Encoding::Registry and kin
    // NativeCall's containers, bare or parameterized: `--> CArray[uint8]` is a
    // routine return type in NativeHelpers::Array and friends
    if (n == "CArray" || n == "Pointer" ||
        n.rfind("CArray[", 0) == 0 || n.rfind("Pointer[", 0) == 0) return true;
    return false;
}

// The core type names isKnownTypeName answers for, as a set a caller can walk
// (the "Did you mean" suggestions rank a misspelled type against them).
const std::set<std::string>& coreTypeNames() {
    static const std::set<std::string> t = {
        "Mu", "Any", "Cool", "Junction", "Whatever", "WhateverCode", "Nil",
        "Int", "UInt", "Num", "Rat", "FatRat", "Complex", "Numeric", "Real", "Bool",
        "Str", "Stringy", "Uni", "Blob", "Buf", "Stringy",
        "blob8", "buf8", "blob16", "buf16", "blob32", "buf32", "blob64", "buf64",
        "utf8", "utf16", "utf32", "Collation",
        "Array", "List", "Seq", "Slip", "Range", "Positional", "Iterable", "Iterator", "PredictiveIterator",
        "Hash", "Map", "Associative", "Pair", "Enum", "Bag", "Set", "Mix",
        "BagHash", "SetHash", "MixHash", "Baggy", "Setty", "Mixy", "QuantHash",
        "Code", "Sub", "Method", "Submethod", "Routine", "Block", "Callable",
        "Regex", "Match", "Capture", "Signature", "Parameter",
        "Exception", "Failure", "Backtrace", "WalkList", "Grammar", "Cursor",
        "Attribute", "Scalar", "Proxy", "Version", "Order", "Enumeration",
        "Date", "DateTime", "Instant", "Duration", "IO", "Proc", "Thread",
        "Promise", "Channel", "Supply", "Lock", "Semaphore", "Stash", "Compiler",
        // Supplier and its kin sit beside Supply and were simply missing, so
        // `class :: is Supplier {…}` — how a test stubs out a live supplier —
        // was rejected as inheriting from an unknown TRAIT rather than a type.
        "Supplier", "Supplier::Preserving", "Tap",
        "Distribution", "CompUnit", "Label", "Nd",
        "Distribution::Path", "Distribution::Hash", "Distribution::Locally", "Distribution::Resource",
        "CompUnit::Repository::Distribution",
        // Core names Rakudo resolves that rakupp has not materialized — real
        // types, answered as unmaterialized stubs (the Cursor/Nd model).
        // S02-types/WHICH.t enumerates the lot through ::($name).
        "AST", "Allomorph", "Backtrace::Frame", "CallFrame", "Cancellation",
        "IntStr", "NumStr", "RatStr", "ComplexStr",
        "CurrentThreadScheduler", "ThreadPoolScheduler", "Deprecation",
        "Distro", "Kernel", "VM", "Perl", "Raku", "Unicode",
        "Hashray", "HyperConfiguration", "HyperSeq", "HyperWhatever",
        "IterationBuffer", "Macro", "ObjAt", "Operator", "OperatorProperties",
        "ParallelSequence", "Proc::Async", "PseudoStash", "ScalarVAR",
        "SignedBlob", "UnsignedBlob", "Slang", "StrDistance", "Variable",
        "IntAttrRef", "IntLexRef", "IntPosRef", "NumAttrRef", "NumLexRef",
        "NumPosRef", "StrAttrRef", "StrLexRef", "StrPosRef", "UIntAttrRef",
        "UIntLexRef", "UIntPosRef",
        // Names the bare-name strictness flushed out of t/run.raku: each is a
        // real Raku entity rakupp serves by NAME (a stub type object, an enum
        // registered elsewhere, a methodCall-handled namespace, a sentinel) —
        // the old always-lenient fallback had been quietly covering them.
        "Dateish", "Format", "Formatter", "Formatter::Syntax", "IterationEnd", "Lock::Async", "Lock::Soft", "Signal",
        "Systemic", "Endian", "SeekType", "FileChangeEvent","ProtocolFamily", "ProtocolType", "PromiseStatus", "Encoding", "Encoding::Builtin", "ValueObjAt", "Telemetry", "RaceSeq",
        // REPL — Rakudo's read-eval-print object, which the sandbox pattern
        // drives directly (methodCallInner answers it; see the REPL block there)
        "REPL",
        "Rational", "PositionalBindFailover", "Sequence", "Awaitable",
        "Scheduler", "ForeignCode", "NFC", "NFD", "NFKC", "NFKD",
    };
    return t;
}

// Rakudo's "Did you mean" distance (Perl6::World's levenshtein): an edit that
// only changes case costs 0.1, a sigil traded for another sigil 0.5, any other
// substitution, insertion, deletion or adjacent transposition 1.
static double suggestDistance(const std::string& a, const std::string& b) {
    auto sig = [](char c) { return c == '$' || c == '@' || c == '%' || c == '&' || c == '|'; };
    auto sub = [&](char x, char y) -> double {
        if (x == y) return 0;
        if (ascii::tolower((unsigned char)x) == ascii::tolower((unsigned char)y)) return 0.1;
        if (sig(x) && sig(y)) return 0.5;
        return 1;
    };
    size_t n = a.size(), m = b.size();
    std::vector<std::vector<double>> d(n + 1, std::vector<double>(m + 1, 0));
    for (size_t i = 0; i <= n; i++) d[i][0] = (double)i;
    for (size_t j = 0; j <= m; j++) d[0][j] = (double)j;
    for (size_t i = 1; i <= n; i++)
        for (size_t j = 1; j <= m; j++) {
            double c = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + sub(a[i - 1], b[j - 1])});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                c = std::min(c, d[i - 2][j - 2] + 1);
            d[i][j] = c;
        }
    return d[n][m];
}

// ". Did you mean 'X'?" / ". Did you mean any of these: 'X', 'Y'?", or nothing
std::string didYouMean(const std::vector<std::string>& sug) {
    if (sug.empty()) return "";
    if (sug.size() == 1) return ". Did you mean '" + sug[0] + "'?";
    std::string m = ". Did you mean any of these: ";
    for (size_t i = 0; i < sug.size(); i++) m += (i ? ", '" : "'") + sug[i] + "'";
    return m + "?";
}

// The names among `cands` close enough to `name` to suggest, nearest first and
// at most five, the way Rakudo's levenshtein_candidate_heuristic trims them.
std::vector<std::string> suggestNames(const std::string& name, const std::vector<std::string>& cands) {
    std::vector<std::pair<double, std::string>> hits;
    double bound = std::max(1.0, name.size() / 3.0);
    for (auto& c : cands) {
        if (c == name || c.empty()) continue;
        if (c.size() + 3 < name.size() || name.size() + 3 < c.size()) continue;
        double dd = suggestDistance(name, c);
        if (dd <= bound) hits.push_back({dd, c});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<std::string> out;
    for (auto& h : hits)
        if (std::find(out.begin(), out.end(), h.second) == out.end() && out.size() < 5) out.push_back(h.second);
    return out;
}

// A misspelled TYPE name: the core types and every type this program declared
// Is `t`, written where a parameter's type goes, something nothing declares?
// Only a certainty counts (see declTypeIsKnown): a core enum member or term
// (`sub f(True)`, `(\buf, LittleEndian)`), or a unit's own constant, is a
// VALUE constraint.
bool Interpreter::paramTypeUndeclared(const std::string& t) {
    if (t.empty() || !ascii::isupper((unsigned char)t[0]) || declTypeIsKnown(t)) return false;
    Value ev;
    if (coreEnumValue(t, ev)) return false;
    static const std::set<std::string> kTerms = {"Inf", "NaN", "Nil", "Empty"};
    if (kTerms.count(t)) return false;
    if (const Program* unit = unitCurrent())
        if (unit->declaredTermNames.count(t)) return false;
    return true;
}

// The argument profile X::Multi::NoMatch prints: `RT: Int:D, :foo(Str)`.
// Positionals by type and definedness, named arguments as `:name(Type)` —
// the type's own .raku, or `:Type` when that .raku itself dies.
std::string Interpreter::noMatchProfile(const Value& self, const ValueList& as, bool withInvocant) {
    auto isNamedArg = [](const Value& v) { return v.t == VT::Pair && v.namedArg; };
    std::string prof;
    if (withInvocant) prof = self.typeName() + ":";
    auto sep = [&]() { if (!prof.empty() && prof.back() != ':') prof += ", "; else if (!prof.empty()) prof += " "; };
    for (auto& a : as) {
        if (isNamedArg(a)) continue;
        sep();
        prof += a.typeName() + (a.t == VT::Type || a.t == VT::Any || a.t == VT::Nil ? ":U" : ":D");
    }
    for (auto& a : as) {
        if (!isNamedArg(a) || a.t != VT::Pair) continue;
        Value v = a.pairVal() ? *a.pairVal() : Value::boolean(true);
        std::string tn = v.typeName();
        std::string shown = tn;
        auto ci = classes_.find(v.t == VT::Type ? v.s.str() : tn);
        if (ci != classes_.end() && ci->second && ci->second->findMethod("raku")) {
            try { shown = methodCall(Value::typeObj(ci->first), "raku", ValueList{}).toStr(); }
            catch (...) { sep(); prof += ":" + tn; continue; }
        }
        sep();
        prof += ":" + a.s.str() + "(" + shown + ")";
    }
    return prof;
}

// A parameter typed with a name nothing declares, suggestions and all
// (`sub yoink(Junctoin $barf)` → "Did you mean 'Junction'?")
void Interpreter::throwInvalidParamType(const std::string& type) {
    auto sug = typeSuggestions(type);
    Value sl = Value::array(); sl.isList = true;
    for (auto& n : sug) sl.arr()->push_back(Value::str(n));
    std::string msg = "Invalid typename '" + type + "' in parameter declaration.";
    if (sug.size() == 1) msg += " Did you mean\n'" + sug[0] + "'?";
    else if (!sug.empty()) {
        msg += " Did you mean any of these:\n";
        for (auto& n : sug) msg += "    " + n + "\n";
    }
    throwTypedV("X::Parameter::InvalidType", {{"typename", Value::str(type)}, {"suggestions", sl}}, msg);
}

std::vector<std::string> Interpreter::typeSuggestions(const std::string& name) {
    std::vector<std::string> cands(coreTypeNames().begin(), coreTypeNames().end());
    for (auto& kv : classes_)
        if (!kv.first.empty() && kv.first.rfind("X::", 0) != 0 && kv.first.find('[') == std::string::npos)
            cands.push_back(kv.first);
    // …and the unit's own TERM names (enum values, constants): `sub x(RT123926Floo)`
    // next to `enum E <RT123926Foo Bar>` means the enum value
    if (const Program* unit = unitCurrent())
        for (auto& tn : unit->declaredTermNames)
            if (!tn.empty() && ascii::isupper((unsigned char)tn[0])) cands.push_back(tn);
    // …and the enum values in scope (an EVAL's own unit is not the one above)
    for (Env* e = tctx_.cur.get(); e; e = e->parent.get())
        for (auto& kv : e->vars)
            if (!kv.first.empty() && ascii::isupper((unsigned char)kv.first[0]) &&
                kv.first.find("::") == std::string::npos &&   // (not its `E::` twin)
                !kv.second.enumType.empty() && !kv.second.enumName.empty())
                cands.push_back(kv.first);
    std::sort(cands.begin(), cands.end());
    cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
    return suggestNames(name, cands);
}

// A misspelled ROUTINE name: the built-in routines and the `&` names in scope
std::vector<std::string> Interpreter::routineSuggestions(const std::string& name) {
    std::vector<std::string> cands;
    for (auto& kv : builtins_)
        if (!kv.first.empty() && kv.first[0] != '_' && kv.first.find(':') == std::string::npos &&
            builtinVisible(kv.first))
            cands.push_back(kv.first);
    for (Env* e = tctx_.cur.get(); e; e = e->parent.get()) {
        for (auto& kv : e->vars)
            if (kv.first.size() > 1 && kv.first[0] == '&') cands.push_back(kv.first.substr(1));
        if (e->layout)
            for (auto& nm : e->layout->names)
                if (nm.size() > 1 && nm[0] == '&') cands.push_back(nm.substr(1));
    }
    return suggestNames(name, cands);
}

// An anonymous PUN of a parameterized role with its `[...]` params bound to
// argv — what `P[%defaults].new` and `Q[Int].mk` dispatch on. A shallow copy of
// the role's ClassInfo keeps its methods/attrs; only the param bindings differ.
// The candidate of a parametric role GROUP that takes `n` positional
// arguments: its required positionals are at most n and its positionals (or a
// slurpy) reach n. An exact positional count wins over a fitting range; with
// no fit the group's latest declaration answers, as before there were groups.
std::shared_ptr<ClassInfo> Interpreter::pickRoleVariant(const std::shared_ptr<ClassInfo>& group, size_t n) {
    std::vector<std::shared_ptr<ClassInfo>> all = group->roleVariants;
    all.push_back(group);
    std::shared_ptr<ClassInfo> fit;
    for (auto& v : all) {
        size_t req = 0, pos = 0; bool slurpy = false;
        if (v->decl)
            for (auto& p : v->decl->roleParams) {
                if (p.named) continue;
                if (p.slurpy) { slurpy = true; continue; }
                pos++;
                if (!p.optional && !p.defaultVal) req++;
            }
        if (pos == n && !slurpy) return v;
        if (!fit && req <= n && (n <= pos || slurpy)) fit = v;
    }
    return fit ? fit : group;
}

// …and by the ARGUMENTS when they are there to look at: the candidates whose
// positional parameters accept them (type, where-clause), narrowest first —
// `role R[Str $x]`, `role R[A $x]`, `role R[B $x]` (B is A), `role R[::T]`
// pick 1, 3, 2, 4 for "hi", B, A, Pair.
std::shared_ptr<ClassInfo> Interpreter::pickRoleVariantArgs(const std::shared_ptr<ClassInfo>& group,
                                                            const std::vector<ExprPtr>& exprs) {
    ValueList vals;
    try {
        for (auto& e : exprs) {
            vals.push_back(eval(e.get()));
            if (syntacticNamedPair(&*e)) vals.back().namedArg = true;
        }
    }
    catch (RakuError&) { return pickRoleVariant(group, exprs.size()); }
    return pickRoleVariantValues(group, vals);
}

// …the same choice over arguments already evaluated (`R[Str]` punned at run
// time). Named arguments are not positionals: `R[42, :opt]` has one.
std::shared_ptr<ClassInfo> Interpreter::pickRoleVariantValues(const std::shared_ptr<ClassInfo>& group,
                                                              const ValueList& vals) {
    ValueList av;
    for (auto& v : vals) if (!v.namedArg) av.push_back(v);
    std::vector<std::shared_ptr<ClassInfo>> all = group->roleVariants;
    all.push_back(group);
    auto positionals = [](ClassInfo* v) {
        std::vector<const Param*> ps;
        if (v->decl) for (auto& p : v->decl->roleParams) if (!p.named) ps.push_back(&p);
        return ps;
    };
    auto accepts = [&](ClassInfo* v) -> bool {
        auto ps = positionals(v);
        size_t req = 0; bool slurpy = false;
        for (auto* p : ps) { if (p->slurpy) slurpy = true; else if (!p->optional && !p->defaultVal) req++; }
        size_t pos = 0; for (auto* p : ps) if (!p->slurpy) pos++;
        if (!(req <= av.size() && (av.size() <= pos || slurpy))) return false;
        // a TYPE CAPTURE binds its name for the parameters after it:
        // `role R[::T $a, T $b]` takes (Array, Array) but not (Int, Hash)
        std::map<std::string, Value> captured;
        for (size_t i = 0; i < ps.size() && i < av.size(); i++) {
            const Param* p = ps[i];
            if (p->slurpy) break;
            if (p->typeCapture) {
                std::string cn = !p->captureName.empty() ? p->captureName : p->type;
                captured[cn] = av[i].t == VT::Type ? av[i] : Value::typeObj(av[i].typeName());
                continue;
            }
            if (!p->type.empty() && captured.count(p->type)) {
                const Value& ct = captured[p->type];
                bool ok = false;
                try { ok = boolify(smartmatchValue("~~", av[i], ct)); } catch (RakuError&) {}
                if (!ok) return false;
                continue;
            }
            if (!p->type.empty() && !p->typeCapture && p->type != "Any" && p->type != "Mu") {
                bool ok = false;
                try { ok = boolify(smartmatchValue("~~", av[i], Value::typeObj(p->type))); } catch (RakuError&) {}
                if (!ok) return false;
            }
            if (p->whereExpr) {
                auto env = std::make_shared<Env>(); env->parent = tctx_.cur;
                env->define("$_", av[i]);
                if (!p->name.empty()) env->define(p->name, av[i]);
                auto saved = tctx_.cur; tctx_.cur = env;
                bool ok = true;
                try {
                    Value cv = eval(p->whereExpr.get());
                    if (cv.t == VT::Code && cv.code()) ok = boolify(callCallable(cv, ValueList{av[i]}));
                    else ok = boolify(smartmatchValue("~~", av[i], cv));
                } catch (RakuError&) { ok = false; }
                tctx_.cur = saved;
                if (!ok) return false;
            }
        }
        return true;
    };
    std::vector<ClassInfo*> ok;
    std::vector<std::shared_ptr<ClassInfo>> okp;
    for (auto& v : all) if (accepts(v.get())) { ok.push_back(v.get()); okp.push_back(v); }
    if (ok.empty())
        throwTyped("X::Role::Parametric::NoSuchCandidate", {{"role", group->name}},
                   "No appropriate parametric role variant available for '" + group->name + "'");
    // narrowest: a candidate none of whose parameters is wider than the other's
    auto narrowerEq = [&](ClassInfo* a, ClassInfo* b) {
        auto pa = positionals(a), pb = positionals(b);
        for (size_t i = 0; i < pa.size() && i < pb.size(); i++) {
            std::string ta = pa[i]->typeCapture ? "Mu" : pa[i]->type.empty() ? "Any" : pa[i]->type;
            std::string tb = pb[i]->typeCapture ? "Mu" : pb[i]->type.empty() ? "Any" : pb[i]->type;
            if (ta == tb) continue;
            if (tb == "Mu" || (tb == "Any" && ta != "Mu")) continue;
            if (ta == "Mu" || ta == "Any") return false;
            bool sub = false;
            try { sub = boolify(smartmatchValue("~~", Value::typeObj(ta), Value::typeObj(tb))); } catch (RakuError&) {}
            if (!sub) return false;
        }
        return true;
    };
    for (size_t i = 0; i < ok.size(); i++) {
        bool best = true;
        for (size_t j = 0; j < ok.size() && best; j++)
            if (i != j && !narrowerEq(ok[i], ok[j]) && narrowerEq(ok[j], ok[i])) best = false;
        if (best) return okp[i];
    }
    return okp.front();
}

Value Interpreter::makeRolePun(ClassInfo* role, const std::string& roleName, ValueList& argv) {
    // An EXPLICIT parameterization type-checks its arguments: `role R[Any:D $x]`
    // refuses a type object and `role S[Str $q]` refuses a 42, which is how a
    // module picks between parameterizations by trying them (Parameterizable's
    // `multi method MIXIN`). bindRoleParamsInto below is deliberately lenient —
    // a bare `does R` must still take the parameter DEFAULTS — so the check
    // belongs here, where the arguments were actually written.
    if (role->decl && !role->decl->roleParams.empty()) {
        auto refuse = [&]() {
            throwTyped("X::Role::Parametric::NoSuchCandidate", {{"role", roleName}},
                       "No appropriate parametric role variant available for '" + roleName + "'");
        };
        size_t pi = 0;
        for (auto& p : role->decl->roleParams) {
            if (p.named || p.slurpy) continue;
            size_t ai = 0, seen = 0; bool found = false;
            for (; ai < argv.size(); ai++)
                if (!argv[ai].namedArg && seen++ == pi) { found = true; break; }
            pi++;
            if (!found || p.typeCapture) continue;   // `::T` captures whatever it is given
            const Value& a = argv[ai];
            if (p.defConstraint == 1 && !isDefined(a)) refuse();
            if (p.defConstraint == 2 && isDefined(a)) refuse();
            if (!p.type.empty() && p.type != "Any" && p.type != "Mu" &&
                !typeOrSubsetMatches(a, p.type)) refuse();
        }
    }
    // The SAME parameterization is the SAME type: `Answer[42]` written twice
    // must produce one pun, or `$obj does Answer[42]` and a later
    // `$obj ~~ Answer[42]` disagree (Parameterizable's does-ok). Keyed by the
    // arguments' identities; anything without a stable identity (a Code, an
    // object) is left uncached rather than wrongly shared.
    std::string punKey = roleName;
    bool cacheable = true;
    for (auto& a : argv) {
        if (a.t == VT::Code || a.t == VT::Object) { cacheable = false; break; }
        punKey += "\x01" + whichOf(a);
    }
    if (cacheable) {
        auto hit = rolePunCache_.find(punKey);
        if (hit != rolePunCache_.end() && classes_.count(hit->second))
            return Value::typeObj(hit->second);
    }
    auto pun = std::make_shared<ClassInfo>(*role);
    pun->roleVariants.clear();   // a parameterization is one candidate, not a group
    static int punSerial = 0;
    pun->name = roleName + "\x01pun" + std::to_string(++punSerial);
    // …and what to CALL it: `Foo[Int]`, the way it was written, the way Rakudo
    // answers `.^name` and the way `.^shortname` shortens. The key above cannot
    // double as that — it carries a serial so the SAME parameterization written
    // twice stays one type — so the display form rides alongside it. A nested
    // parameter uses ITS display form, which is how `Baz[Foo[Int],Bar[Int]]`
    // comes out whole (roast S02-names-vars/names.t).
    pun->dispName = roleArgsDisplay(roleName, argv);
    {   // a VALUE argument is shown by its type
        bool anyValue = false;
        for (auto& a : argv) if (!a.namedArg && (a.t != VT::Type || !a.enumName.empty())) anyValue = true;
        if (anyValue) {
            ValueList shown;
            for (auto& a : argv) {
                if (a.namedArg) continue;
                Value t = a.t == VT::Type && a.enumName.empty() ? a : Value::typeObj(a.typeName());
                shown.push_back(t);
            }
            pun->shownName = roleArgsDisplay(roleName, shown);
        }
    }
    if (cacheable) rolePunCache_[punKey] = pun->name;
    pun->doneRoles.insert(roleName); // `~~ P` still answers True
    // …and `~~ P[Int]` answers for THIS parameterization only
    if (pun->dispName != roleName) pun->doneRoles.insert(pun->dispName);
    pun->roleParamBindings.clear();
    bindRoleParamsInto(pun.get(), role, argv, role->declEnv);
    // (the generic statements run for THIS parameterization, and its methods
    // close over the scope they ran in)
    const bool generic = !pun->roleParamBindings.empty() && runGenericRoleBody(pun, role);
    applyRoleTypeParamsToAttrs(pun.get());
    recloseRoleMethods(pun, generic ? role->declEnv.get() : nullptr, generic ? pun->declEnv : nullptr);
    concretizeInnerRoles(pun, role);
    // The roles this parameterization DOES through its parameters: `role
    // RR[::T] does T` (RR[Foo] does Foo) and `role PR1[::T1] does PR00[T1]`
    // (PR1[Int] does PR00[Int]) — evaluated with the parameters bound
    if (role->decl) {
        auto penv = std::make_shared<Env>();
        penv->parent = role->declEnv ? role->declEnv : global_;
        for (auto& b : pun->roleParamBindings) penv->define(b.first, b.second);
        std::vector<std::string> rdoes = role->decl->roles;
        if (!role->decl->parent.empty()) rdoes.push_back(role->decl->parent);
        for (auto& ep : role->decl->extraParents) rdoes.push_back(ep);
        for (auto& dn : rdoes) {
            if (Value* tv = penv->local(dn))
                if (tv->t == VT::Type) {
                    auto tit = classes_.find(tv->s);
                    if (tit != classes_.end() && tit->second && tit->second->isRole) {
                        // A role composing a role that declares the same attribute
                        // is a conflict the PUN reports — the role itself may only
                        // be a stub of what a class will resolve, so its own
                        // composition let the duplicate pass; punning it is
                        // composing it for real (roast S14-roles/conflicts.t:
                        // `role S does R { has $.grfuffle }; S.new` dies).
                        for (auto& ra : tit->second->attrs)
                            for (auto& own : role->attrs)
                                if (own.name == ra.name && own.sigil == ra.sigil &&
                                    !(own.declId && ra.declId && own.declId == ra.declId))
                                    throw RakuError{Value::typeObj("X::Role::Attribute::Conflicts"),
                                        "Attribute '" + std::string(1, ra.sigil) + "!" + ra.name +
                                        "' conflicts in role '" + roleName + "' composition: declared in both '" +
                                        roleName + "' and '" + tit->first + "'"};
                        pun->doneRoles.insert(tit->first);
                        for (auto& sub : tit->second->doneRoles) pun->doneRoles.insert(sub);
                    }
                    else if (tit != classes_.end() && tit->second)   // `role RC[::T] is T`
                        pun->extraParents.push_back(tit->second);
                }
        }
        for (auto& ra : role->decl->roleArgs) {
            ValueList rv;
            auto saved = tctx_.cur; tctx_.cur = penv;
            bool ok = true;
            try { for (auto& e : ra.second) rv.push_back(eval(e.get())); } catch (...) { ok = false; }
            tctx_.cur = saved;
            if (!ok || rv.empty()) continue;
            std::string disp = roleArgsDisplay(ra.first, rv);
            if (disp != ra.first) pun->doneRoles.insert(disp);
        }
    }
    classes_[pun->name] = pun;
    punArgs_[pun->name] = argv;
    return Value::typeObj(pun->name);
}

// How a parameterization is WRITTEN: `Foo[Int]`, nested parameters in their own
// display form (`Baz[Foo[Int],Bar[Int]]`). A pun's `.^name`, and the key a
// composing class records in doneRoles so `C ~~ R[Int]` can tell R[Int] from R[Str].
std::string Interpreter::roleArgsDisplay(const std::string& roleName, const ValueList& argv) {
    std::string args;
    for (auto& a : argv) {
        if (a.namedArg) continue;
        if (!args.empty()) args += ",";
        if (a.t == VT::Type) {
            auto ci = classes_.find(a.s);
            args += (ci != classes_.end() && !ci->second->dispName.empty())
                        ? ci->second->dispName : a.typeName();
        }
        else args += g_rakuRepr ? g_rakuRepr(a) : a.toStr();
    }
    return args.empty() ? roleName : roleName + "[" + args + "]";
}

// A PUN of a parameterized role owns its own copy of the role's attributes, so
// a type-capture parameter can be substituted into them directly. (A class that
// merely composes the role does NOT own a copy — the first `does` role sits in
// the parent slot and its ClassInfo is shared by every composer — so that path
// resolves the name per OBJECT instead, in runAttrDefaults.)
//
// Only type captures count: a VALUE parameter that happens to share a name with
// a type must not rename anything.
void Interpreter::applyRoleTypeParamsToAttrs(ClassInfo* dest) {
    if (!dest || dest->roleParamBindings.empty() || dest->attrs.empty()) return;
    for (auto& a : dest->attrs) {
        if (a.type.empty()) continue;
        for (auto& b : dest->roleParamBindings) {
            if (b.first != a.type || b.second.t != VT::Type) continue;
            const std::string& bound = b.second.s;
            if (!bound.empty() && bound != a.type) a.type = bound;
            break;
        }
    }
}

// Bind ONE composed role's `[...]` parameters into `dest->roleParamBindings` —
// the vector invokeClosure walks up the invocant's MRO to put the params in scope
// for the role's method bodies. `argv` may be empty: a bare `does R` / `but R`
// must still bind the parameter DEFAULTS (License::SPDX's `does JSON::Class`
// needs $opt-in = False, or every role method mentioning it dies "not declared").
// Shared by all three composition routes: class body, runtime mixin, role pun.
void Interpreter::bindRoleParamsInto(ClassInfo* dest, ClassInfo* role, ValueList& argv,
                                     const std::shared_ptr<Env>& scope) {
    if (!role->decl || role->decl->roleParams.empty()) return;
    auto tmp = std::make_shared<Env>();
    tmp->parent = scope ? scope : global_;
    try { bindParams(role->decl->roleParams, argv, tmp, /*methodCtx=*/false); }
    catch (...) {} // arity/type mismatch: leave unbound rather than abort composition
    size_t posIdx = 0;
    for (auto& p : role->decl->roleParams) {
        // `role R[::T]`: a bare type-capture param binds the type NAME to the
        // argument at its positional slot (Cro::ConnectionState[TestState]
        // answers TestState for T in the role body)
        // (a capture after a constraint, `Numeric ::T`, is one too: typeCapture
        // stays false there because the parameter HAS a type)
        const std::string capName = !p.captureName.empty() ? p.captureName : p.type;
        if ((p.typeCapture || !p.captureName.empty()) && !capName.empty() && !p.named) {
            size_t ai = 0, seen = 0; bool found = false;
            for (; ai < argv.size(); ai++)
                if (!argv[ai].namedArg && seen++ == posIdx) { found = true; break; }
            if (found) {
                // An ENUM's type object is the tagged pair-list, not a VT::Type,
                // so re-wrapping it as a bare type object threw the enum away:
                // `role R[::EnumBits]` bound a name that answered MyBits to
                // `.^name` and False to `~~ Enumeration`, and `EnumBits.enums`
                // — the line every BitEnum consumer runs — found no method.
                const bool enumTypeObj = argv[ai].t == VT::Array &&
                                         !argv[ai].enumType.empty() &&
                                         argv[ai].enumType == argv[ai].typeName();
                Value tv = (argv[ai].t == VT::Type || enumTypeObj)
                             ? argv[ai]
                             : Value::typeObj(argv[ai].typeName());
                dest->roleParamBindings.push_back({capName, tv});
            }
            // …and a bare `does R` takes the capture's DEFAULT (`role R[::T = Any]`),
            // which is as load-bearing as an explicit argument: an attribute
            // declared `has T @!items` is unusable while T names nothing.
            else if (p.defaultVal) {
                Value dv;
                bool got = true;
                try { dv = eval(p.defaultVal.get()); } catch (...) { got = false; }
                if (got)
                    dest->roleParamBindings.push_back(
                        {capName, dv.t == VT::Type ? dv : Value::typeObj(dv.typeName())});
            }
        }
        if (!p.named && !p.slurpy) posIdx++;
        if (!p.name.empty())
            if (Value* v = tmp->find(p.name)) dest->roleParamBindings.push_back({p.name, *v});
    }
    // …and into the ROLE BODY's own scope, which is what a `sub` declared in the
    // body closes over. A role's METHODS receive the bindings per call, from the
    // invocant's class (invokeMethod); a plain sub has no invocant to carry them,
    // so unless the value is in its closure it is simply not declared. BitEnum
    // looks its bit names up in `sub lookup` — reading `$prefix`, the role's own
    // named parameter — and every consumer died on the first lookup. Existing
    // entries are left alone, so a second composition cannot rewrite what the
    // first one bound.
    if (!dest->roleParamBindings.empty()) {
        for (auto& m : role->methods) {
            if (m.second.t != VT::Code || !m.second.code() || !m.second.code()->closure) continue;
            auto& env = m.second.code()->closure;
            for (auto& b : dest->roleParamBindings)
                if (!env->local(b.first)) env->define(b.first, b.second);
            break;
        }
    }
}

// Re-close a parametric role's methods over the parameters in
// `conc->roleParamBindings`: each method (each multi candidate) becomes a copy
// whose closure is a scope binding them, one scope per original closure. That
// is what makes R[Int] and R[Str] two roles rather than one role told its
// parameters at call time by whichever class called — `does R[Str] does R[Int]`
// gave both its multis T = Str, and a `multi method foo(T $t)` matched nothing
// at all, since the dispatcher resolved T from the CALLER's scope.
void Interpreter::recloseRoleMethods(const std::shared_ptr<ClassInfo>& conc, Env* bodyScope,
                                     const std::shared_ptr<Env>& concScope) {
    if (!conc || conc->roleParamBindings.empty()) return;
    std::unordered_map<Env*, std::shared_ptr<Env>> scopes;
    // a method declared in the role body closes over the scope this
    // parameterization's generic statements ran in, which already binds the
    // parameters (and answers pkgType) — so its `$v` and `G::A` are its own
    if (bodyScope && concScope) scopes[bodyScope] = concScope;
    auto reclose = [&](Value& m) {
        const Callable* c = m.code();
        if (!c || !c->body || c->builtin) return;
        auto& scope = scopes[c->closure.get()];
        if (!scope) {
            scope = std::make_shared<Env>();
            scope->parent = c->closure ? c->closure : global_;
            scope->x().pkgType = conc;   // `self.R::m` in here resolves from THIS parameterization
            for (auto& b : conc->roleParamBindings)
                if (!b.first.empty() && !scope->local(b.first)) scope->define(b.first, b.second);
        }
        auto copy = makePayload<Callable>(*c);
        copy->closure = scope;
        copy->roleConcrete = true;
        m.setCode(std::move(copy));
    };
    for (auto& kv : conc->methods) {
        Value& m = kv.second;
        if (m.t != VT::Code || !m.code()) continue;
        if (m.code()->isMultiDispatcher) {
            auto disp = makePayload<Callable>(*m.code());
            for (auto& cand : disp->candidates)
                if (cand.t == VT::Code && cand.code()) reclose(cand);
            m.setCode(std::move(disp));
        }
        else reclose(m);
    }
}

std::shared_ptr<ClassInfo> Interpreter::concretizeRole(const std::shared_ptr<ClassInfo>& role, ValueList& argv,
                                                       const std::shared_ptr<Env>& scope) {
    if (!role || !role->isRole || !role->decl || role->decl->roleParams.empty()) return role;
    auto conc = std::make_shared<ClassInfo>(*role);
    conc->roleParamBindings.clear();
    bindRoleParamsInto(conc.get(), role.get(), argv, scope);
    if (conc->roleParamBindings.empty()) return role;
    const bool generic = runGenericRoleBody(conc, role.get());
    applyRoleTypeParamsToAttrs(conc.get());
    recloseRoleMethods(conc, generic ? role->declEnv.get() : nullptr, generic ? conc->declEnv : nullptr);
    concretizeInnerRoles(conc, role.get());
    return conc;
}

// The roles a parametric role does THROUGH its parameters are concretized
// with it: `role R2[::T] does R1[::T]` makes R2[Num] do R1[Num], not the
// generic R1 every parameterization of R2 would otherwise share.
void Interpreter::concretizeInnerRoles(const std::shared_ptr<ClassInfo>& conc, ClassInfo* role) {
    if (!conc || !role || !role->decl || role->decl->roleArgs.empty()) return;
    auto penv = std::make_shared<Env>();
    penv->parent = role->declEnv ? role->declEnv : global_;
    for (auto& b : conc->roleParamBindings)
        if (!b.first.empty() && !penv->local(b.first)) penv->define(b.first, b.second);
    std::map<std::string, size_t> seenName;
    std::map<ClassInfo*, std::shared_ptr<ClassInfo>> done;   // parent and composedRoles stay one object
    auto redo = [&](std::shared_ptr<ClassInfo>& slot) {
        if (!slot || !slot->isRole || !slot->decl || slot->decl->roleParams.empty()) return;
        auto hit = done.find(slot.get());
        if (hit != done.end()) { slot = hit->second; return; }
        const size_t k = seenName[slot->name]++;
        const std::vector<ExprPtr>* rargs = nullptr;
        size_t seen = 0;
        for (auto& ra : role->decl->roleArgs) {
            const bool named = ra.first == slot->name ||
                (slot->name.size() > ra.first.size() + 2 &&
                 slot->name.compare(slot->name.size() - ra.first.size(), ra.first.size(), ra.first) == 0 &&
                 slot->name[slot->name.size() - ra.first.size() - 1] == ':');
            if (named && seen++ == k) { rargs = &ra.second; break; }
        }
        if (!rargs || rargs->empty()) return;
        ValueList argv;
        auto saved = tctx_.cur; tctx_.cur = penv;
        try {
            for (auto& e : *rargs) {
                Value v = eval(e.get());
                if (syntacticNamedPair(&*e)) v.namedArg = true;
                argv.push_back(std::move(v));
            }
        } catch (RakuError&) { tctx_.cur = saved; return; }
        tctx_.cur = saved;
        auto inner = concretizeRole(slot, argv, penv);
        if (inner == slot) return;
        const std::string disp = roleArgsDisplay(slot->name, argv);
        done[slot.get()] = inner;
        slot = inner;
        if (disp != slot->name) conc->doneRoles.insert(disp);
    };
    redo(conc->parent);
    for (auto& r : conc->composedRoles) redo(r);
}

// `inv.R::m` where R is parametric names the parameterization of R that the
// invocant's type composed. Classes are searched nearest first (C3 order); at
// each, the roles it composes directly, then the roles THOSE compose, a level
// at a time. One match answers; two different parameterizations at one level
// are ambiguous, the message Rakudo gives; none leaves the caller with R.
ClassInfo* Interpreter::qualifiedConcretization(const Value& inv, const std::string& roleName) {
    ClassInfo* start = nullptr;
    if (inv.t == VT::Object && inv.obj()) start = inv.obj()->cls.get();
    else if (inv.t == VT::Type) {
        auto it = classes_.find(inv.s);
        if (it != classes_.end()) start = it->second.get();
    }
    return qualifiedConcretizationFrom(start, roleName);
}

bool Interpreter::valueBoundVar(const std::string& name) {
    for (Env* e = tctx_.cur.get(); e; e = e->parent.get())
        if (e->local(name)) return e->xr().varValueBound.count(name) > 0;
    return false;
}

ClassInfo* Interpreter::qualifiedConcretizationFrom(ClassInfo* start, const std::string& roleName) {
    if (!start) return nullptr;
    auto directRoles = [](ClassInfo* c, std::vector<ClassInfo*>& out) {
        if (c->parent && c->parent->isRole) out.push_back(c->parent.get());
        for (auto& p : c->extraParents) if (p && p->isRole) out.push_back(p.get());
        for (auto& r : c->composedRoles) if (r && r->isRole) out.push_back(r.get());
    };
    std::vector<ClassInfo*> classes;
    if (start->isRole) classes.push_back(start);
    else classes = c3ClassMro(start);
    for (ClassInfo* c : classes) {
        if (!c) continue;
        std::vector<ClassInfo*> level;
        directRoles(c, level);
        std::set<ClassInfo*> seen;
        for (int depth = 0; !level.empty() && depth < 32; depth++) {
            std::vector<ClassInfo*> found, next;
            for (ClassInfo* r : level) {
                if (!seen.insert(r).second) continue;
                if (r->name == roleName) found.push_back(r);
                directRoles(r, next);
            }
            if (found.size() == 1) return found[0];
            if (found.size() > 1)
                throw RakuError{Value::typeObj("X::AdHoc"), "Ambiguous concretization lookup for " + roleName};
            level.swap(next);
        }
    }
    return nullptr;
}

// A Seq is read once (SeqToken, Value.h). Only the language's own uses of one
// come here — a `for`, a method called on it, `@$s`, `$s[0]` — never the
// engine's internal reads, so nothing the engine does on its own behalf can
// make a Seq throw. Marking one CACHED is always safe: it can only take a
// later complaint away.
void Interpreter::seqUse(const Value& v, SeqUse how) {
    SeqToken* tok = v.seqTok();
    if (!tok || v.t != VT::Array || !v.isList || !(v.s == "Seq")) return;
    unsigned char st = tok->state.load(std::memory_order_relaxed);
    if (st == kSeqCached) return;
    if (st == kSeqConsumed) {
        if (how == SeqUse::Sink) return;
        throwTypedV("X::Seq::Consumed", {{"kind", Value::typeObj("Seq")}},
                    "The iterator of this Seq is already in use/consumed by another Seq\n"
                    "(you might solve this by adding .cache on usages of the Seq, or\n"
                    "by assigning the Seq into an array)");
    }
    if (how == SeqUse::Peek) return;
    unsigned char u = kSeqUnread;
    tok->state.compare_exchange_strong(u, how == SeqUse::Cache ? kSeqCached : kSeqConsumed);
}

void Interpreter::seqMintList(Value& r, const Value& inv) {
    if (!(r.s == "Seq")) return;
    SeqToken* have = r.seqTok();
    if (have && have != inv.seqTok()) return;   // a Seq handed back as it is keeps its state
    r.setSeqTok(makeSlabShared<SeqToken>());
}
bool mayHaveStateDecl(const Expr* e) {
    if (!e) return false;
    switch (e->kind) {
        case NK::IntLit: case NK::NumLit: case NK::StrLit: case NK::BoolLit:
        case NK::NameTerm: case NK::Whatever: case NK::SelfTerm:
        case NK::RegexLit: case NK::SubstLit: case NK::AllomorphLit:
            return false;
        case NK::VarExpr: {
            auto* v = static_cast<const VarExpr*>(e);
            return v->declare && v->declScope == "state";
        }
        case NK::InterpStr: {
            for (auto& part : static_cast<const InterpStr*>(e)->parts)
                if (mayHaveStateDecl(part.get())) return true;
            return false;
        }
        case NK::Assign: {
            auto* a = static_cast<const Assign*>(e);
            return mayHaveStateDecl(a->target.get()) || mayHaveStateDecl(a->value.get());
        }
        case NK::Binary: {
            auto* b = static_cast<const Binary*>(e);
            return mayHaveStateDecl(b->lhs.get()) || mayHaveStateDecl(b->rhs.get());
        }
        case NK::Unary:
            return mayHaveStateDecl(static_cast<const Unary*>(e)->operand.get());
        case NK::Call: {
            for (auto& a : static_cast<const Call*>(e)->args)
                if (mayHaveStateDecl(a.get())) return true;
            return false;
        }
        case NK::MethodCall: {
            auto* m = static_cast<const MethodCall*>(e);
            if (mayHaveStateDecl(m->inv.get())) return true;
            for (auto& a : m->args) if (mayHaveStateDecl(a.get())) return true;
            return false;
        }
        case NK::Index: {
            auto* ix = static_cast<const Index*>(e);
            return mayHaveStateDecl(ix->base.get()) || mayHaveStateDecl(ix->index.get());
        }
        case NK::Ternary: {
            auto* t = static_cast<const Ternary*>(e);
            return mayHaveStateDecl(t->cond.get()) || mayHaveStateDecl(t->then.get()) ||
                   mayHaveStateDecl(t->els.get());
        }
        case NK::Range: {
            auto* r = static_cast<const RangeExpr*>(e);
            return mayHaveStateDecl(r->from.get()) || mayHaveStateDecl(r->to.get());
        }
        case NK::ListExpr: {
            for (auto& it : static_cast<const ListExpr*>(e)->items)
                if (mayHaveStateDecl(it.get())) return true;
            return false;
        }
        case NK::Pair: {
            auto* pr = static_cast<const PairExpr*>(e);
            return mayHaveStateDecl(pr->keyExpr.get()) || mayHaveStateDecl(pr->value.get());
        }
        case NK::ChainExpr: {
            for (auto& o : static_cast<const ChainExpr*>(e)->operands)
                if (mayHaveStateDecl(o.get())) return true;
            return false;
        }
        default:
            return true; // BlockExpr, SymbolicRef, NqpOp, … — keep the frame
    }
}
bool mayHaveStateDecl(const Stmt* s) {
    if (!s) return false;
    switch (s->kind) {
        case NK::EmptyStmt: case NK::LastStmt: case NK::NextStmt: case NK::RedoStmt:
        case NK::UseStmt:
            return false;
        case NK::ExprStmt:
            return mayHaveStateDecl(static_cast<const ExprStmt*>(s)->e.get());
        case NK::Block:
            return mayHaveStateDecl(static_cast<const Block*>(s));
        case NK::ReturnStmt:
            return mayHaveStateDecl(static_cast<const ReturnStmt*>(s)->value.get());
        case NK::IfStmt: {
            auto* is = static_cast<const IfStmt*>(s);
            for (auto& br : is->branches)
                if (mayHaveStateDecl(br.first.get()) || mayHaveStateDecl(br.second.get())) return true;
            return mayHaveStateDecl(is->elseBlock.get());
        }
        case NK::WhileStmt: {
            auto* w = static_cast<const WhileStmt*>(s);
            return mayHaveStateDecl(w->cond.get()) || mayHaveStateDecl(w->body.get());
        }
        case NK::ForStmt: {
            auto* f = static_cast<const ForStmt*>(s);
            return mayHaveStateDecl(f->list.get()) || mayHaveStateDecl(f->body.get());
        }
        case NK::LoopStmt: {
            auto* l = static_cast<const LoopStmt*>(s);
            return mayHaveStateDecl(l->init.get()) || mayHaveStateDecl(l->cond.get()) ||
                   mayHaveStateDecl(l->incr.get()) || mayHaveStateDecl(l->body.get());
        }
        case NK::RepeatStmt: {
            auto* r = static_cast<const RepeatStmt*>(s);
            return mayHaveStateDecl(r->cond.get()) || mayHaveStateDecl(r->body.get());
        }
        case NK::GivenStmt: {
            auto* g = static_cast<const GivenStmt*>(s);
            return mayHaveStateDecl(g->topic.get()) || mayHaveStateDecl(g->body.get());
        }
        case NK::WhenStmt: {
            auto* w = static_cast<const WhenStmt*>(s);
            return mayHaveStateDecl(w->cond.get()) || mayHaveStateDecl(w->body.get());
        }
        default:
            return true; // SubDecl, ClassDecl, VarDecl, EnumDecl, … — keep the frame
    }
}

// `token tok:sym(EXPR)` arrives named `tok:sym<\x02HEX>` (the parser's
// encoding of EXPR): evaluate it now, in the declaring scope, and install the
// rule as `tok:sym<VALUE>`.
// `$*PACKAGE` as a trait in a package body sees it. In a ROLE body that is
// the role itself, whose metaobject is a ParametricRoleHOW — not the group's
// (`given $*PACKAGE.HOW { when Metamodel::ParametricRoleHOW {…} }`); the mark
// tells `.HOW` which one is asked for.
static Value rolePackageValue(const std::string& clsName, bool isRole) {
    Value v = Value::typeObj(clsName);
    if (isRole) v.hashKind = "\x01role-body";
    return v;
}

static void installRule(ClassInfo* ci, const GrammarRuleDecl& r);
void Interpreter::installRuleResolved(ClassInfo* ci, const GrammarRuleDecl& r) {
    size_t at = r.name.find(":sym<\x02");
    if (at == std::string::npos) { installRule(ci, r); return; }
    size_t end = r.name.find('>', at);
    std::string hex = r.name.substr(at + 6, end - at - 6), src;
    auto nib = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t i = 0; i + 1 < hex.size(); i += 2) src += (char)(nib(hex[i]) * 16 + nib(hex[i + 1]));
    GrammarRuleDecl rr = r;
    rr.name = r.name.substr(0, at) + ":sym<" + evalString(src).toStr() + ">" + r.name.substr(end + 1);
    installRule(ci, rr);
}

// A `multi rule NAME(0)` candidate is stored under a mangled key, because every
// candidate of the group shares NAME. The key is NAME \x1f lit \x1e lit …, so it
// can never collide with a rule name the parser could produce.
static std::string ruleLitKey(const std::string& name, const std::vector<std::string>& lits) {
    std::string k = name; k += '\x1f';
    for (size_t i = 0; i < lits.size(); i++) { if (i) k += '\x1e'; k += lits[i]; }
    return k;
}
// Install one declared rule into a class, routing a literal-value candidate to
// its mangled key and listing it against the plain name in declaration order.
static void installRule(ClassInfo* ci, const GrammarRuleDecl& r) {
    if (!r.lits.empty()) {
        std::string key = ruleLitKey(r.name, r.lits);
        ci->rules[key] = r.pattern;
        ci->ruleKind[key] = r.kind;
        ci->ruleLitArgs[key] = r.lits;
        auto& cands = ci->ruleLitCands[r.name];
        if (std::find(cands.begin(), cands.end(), key) == cands.end()) cands.push_back(key);
        // the plain name must resolve to SOMETHING, or `<expr(3)>` is an unknown
        // subrule before dispatch ever runs; the first candidate's body stands in
        // and is only reached when no candidate's literals match.
        if (!ci->rules.count(r.name)) {
            ci->rules[r.name] = r.pattern;
            ci->ruleKind[r.name] = r.kind;
            ci->ruleOrder.push_back(r.name);
            ci->ruleLitOnly.insert(r.name);
        }
        return;
    }
    ci->rules[r.name] = r.pattern;
    ci->ruleKind[r.name] = r.kind;
    if (!r.pod.empty()) ci->rulePod[r.name] = {r.pod, r.podTrail, r.declLine};
    ci->ruleLitOnly.erase(r.name); // a generic candidate: the group has a fallback body
    if (!r.params.empty()) ci->ruleParams[r.name] = r.params;
    else ci->ruleParams.erase(r.name);
    if (std::find(ci->ruleOrder.begin(), ci->ruleOrder.end(), r.name) == ci->ruleOrder.end())
        ci->ruleOrder.push_back(r.name);
}

// The declaration statements exec dispatches here, kept out of its frame.
//
// exec is one switch over every statement kind, and a switch's stack frame is
// sized to the MAXIMUM over all its cases — so the ~100 Value temporaries that
// `class`, `sub`, `enum`, `subset`, a named regex and `use` need between them
// were charged to every ExprStmt and every loop body as well. They are load-time
// work: a hot loop runs none of them. Splitting them out is worth roughly half
// of exec's frame, which is worth the same again in recursion depth, because a
// Raku call frame costs one exec and two evals of native stack.
//
// noinline is the whole point — without it the compiler is free to fold this
// straight back into exec and undo the split.
[[gnu::noinline]] Value Interpreter::execDeclStmt(Stmt* s) {
    switch (s->kind) {
        case NK::NamedRegexDecl: {
            auto* nr = static_cast<NamedRegexDecl*>(s);
            noteSymbolMutation("named-regex declaration");
            namedRegex_[nr->name] = nr->pattern;    // <NAME> resolvable from a plain /…/ regex
            namedRegexKind_[nr->name] = nr->kind;
            // the `&name` form: a Callable running the regex against its argument
            // (unanchored), so `'port = 443' ~~ &pair` and &pair($str) work
            std::string pat = nr->pattern;
            std::string kind = nr->kind;          // regex / token / rule — decides the flags
            Value code; code.t = VT::Code; code.setCode(makePayload<Callable>());
            code.code()->name = nr->name;
            code.code()->isRegexRoutine = true;   // `&R.^name` is Regex, not Sub
            // (the regex's own code blocks see it as `&?ROUTINE`: it is handed in)
            Callable* selfW = code.code();   // not owned: see PRef::fromObject
            code.code()->builtin = [pat, kind, selfW](Interpreter& I, ValueList& a) -> Value {
                if (a.empty()) return Value::nil();
                Value selfV;
                // the routine is running, so its caller holds it alive: take a
                // count for `&?ROUTINE` from the object (owning it in the
                // closure would be a cycle that never frees)
                if (selfW) { selfV.t = VT::Code; selfV.setCode(PRef<Callable>::fromObject(selfW)); }
                return I.regexMatch(I.rxSubject(a[0]), pat, selfV.t == VT::Code ? &selfV : nullptr, kind);
            };
            regexRoutineSrc_[code.code()] = {pat, kind};  // recoverable from `&name`
            tctx_.cur->define("&" + nr->name, code);
            return Value::any();
        }
        case NK::SubsetDecl: {
            auto* sd = static_cast<SubsetDecl*>(s);
            if (!sd->pod.empty() && !sd->name.empty()) { // `#| …` — EvenNum.WHY
                pkgPod_[tctx_.pkgPrefix + sd->name] = sd->pod;
                pkgPodTrail_[tctx_.pkgPrefix + sd->name] = sd->podTrail;
                if (!tctx_.pkgPrefix.empty()) { pkgPod_[sd->name] = sd->pod; pkgPodTrail_[sd->name] = sd->podTrail; }
            }
            if (!sd->name.empty()) {
                SubsetInfo info{sd->baseType, sd->where.get()};
                info.langRev = langRev_;   // `Even.^ver` answers with this
                info.defConstraint = sd->defConstraint;   // `of Str:D`
                info.coerce = sd->coerceBase;             // `of Str()`
                info.coerceFrom = sd->coerceFrom;         // `of Num(Str)`
                info.declEnv = tctx_.cur;   // `where { LogLevels{$_}:exists }` reads its class's constant (Template::Mustache)
                subsets_[sd->name] = info;
                // …and under the PACKAGE-QUALIFIED name, the way a class registers.
                // Without it an importer writing `URI::Scheme` got a bare type
                // object whose `where` had been dropped, so `~~` against it was
                // False for every value — including ones the subset accepts.
                // (not a `my subset`, which stays inside its package: `module M
                // { my subset F … }; my M::F $f` is "Type 'M::F' is not declared")
                if (!tctx_.pkgPrefix.empty() && !sd->isMy) subsets_[tctx_.pkgPrefix + sd->name] = info;
            }
            return Value::any();
        }
        case NK::UseStmt: {
            auto* u = static_cast<UseStmt*>(s);
            // `use Foo:if(EXPR)` — a false condition makes the whole statement
            // a no-op (the ecosystem `if` dist; rakupp honors the adverb
            // natively since both of that dist's implementations are Rakudo
            // compiler internals).
            if (u->ifCond && !eval(u->ifCond.get()).truthy()) return Value::any();
            // Lexical: the scope executing the pragma carries it, so it lapses
            // when that scope does (`{ no strict; … }` does not unstrict the
            // file around it). `use strict` is the same switch the other way —
            // it turns the check back on inside a lax file.
            if (u->module == "strict") {
                if (tctx_.cur) tctx_.cur->strictPragma = u->isNo ? 1 : -1;
                return Value::any();
            }
            // `use fatal`: from here on in this scope, a returned Failure throws
            if (u->module == "fatal") {
                if (tctx_.cur) tctx_.cur->fatalPragma = u->isNo ? -1 : 1;
                return Value::any();
            }
            // `no MONKEY-SEE-NO-EVAL` — a string interpolated into a regex
            // (`<$x>`) is compiled RESTRICTED in this scope: no code in it
            if ((u->module == "MONKEY-SEE-NO-EVAL" || u->module == "MONKEY") && tctx_.cur) {
                tctx_.cur->define("$?NO-MONKEY-SEE-NO-EVAL", Value::boolean(u->isNo));
                return Value::any();
            }
            // `use experimental :rakuast` — the one experimental feature rakupp
            // acts on. Every other `:tag` stays the silent no-op it has always
            // been (`:cached`, `:pack`, …), so the capture the parser now makes
            // for this module changes nothing else.
            //
            // Building the registry HERE, rather than at the first name, is
            // deliberate: the pragma sits at the top of its unit, so the single
            // atomic publish happens on the thread that asked for it instead of
            // in whichever worker first misses. It is only an optimisation —
            // the publish is a compare-exchange and is safe from any thread.
            if (u->module == "experimental" && !u->isNo) {
                for (auto& tag : u->importArgs)
                    if (tag == "rakuast") {
                        rakuAstPragma_ = true;
                        // The per-call switch that carries the pragma into a
                        // module's own routines rides anyRevSwitch_, so opening
                        // the namespace arms it exactly as `use v6.e.PREVIEW`
                        // does. A process that opens neither never pays it.
                        anyRevSwitch_ = true;
                        rakuAstMaterialize();
                    }
                // …and the statement then takes its ordinary road: `experimental`
                // is a name loadModule already knows has no file behind it, so
                // nothing else about the pragma changes.
            }
            // `use NativeCall` is a pragma here — the FFI is native to the compiler,
            // so no module file declares the PACKAGE or its EXPORT stash. Suites
            // introspect both (NativeLibs' 01-basic walks
            // `NativeCall::EXPORT::ALL::{…}` for every name it re-exports), so name
            // them: the package as a type object, its symbols as qualified globals
            // that .WHO reads back as the stash.
            if (u->module == "NativeCall" && global_ && !global_->vars.count("NativeCall")) {
                // …and the split is the real one: `use NativeCall` hands you
                // DEFAULT, while guess_library_name / check_routine_sanity are
                // :ALL-only (that is why a bare call to either is undeclared
                // under Rakudo too).
                static const char* ncNames[] = {
                    "&nativecast", "&nativesizeof", "&cglobal", "&explicitly-manage",
                    "&refresh", "&trait_mod:<is>", "&postcircumfix:<[ ]>",
                    "Pointer", "CArray", "OpaquePointer", "NativeCall", "Native",
                    "bool", "void", "long", "longlong", "ulong", "ulonglong",
                    "size_t", "ssize_t",
                };
                static const char* ncAllOnly[] = {
                    "&guess_library_name", "&check_routine_sanity",
                };
                for (const char* pkg : {"NativeCall", "NativeCall::EXPORT",
                                        "NativeCall::EXPORT::DEFAULT", "NativeCall::EXPORT::ALL",
                                        "NativeCall::Types"})
                    global_->define(pkg, Value::typeObj(pkg));
                // `NativeCall::Native` is the role NativeCall mixes into a sub to
                // make it native. It has no declaration here — the FFI is built
                // in — so the NAME is defined and the `does` form is recognised
                // where mixins are applied. Without the name, `$f does
                // NativeCall::Native[$f, $so]` died "Undeclared name".
                global_->define("NativeCall::Native", Value::typeObj("NativeCall::Native"));
                for (const char* n : ncNames)
                    for (const char* which : {"NativeCall::EXPORT::DEFAULT::",
                                              "NativeCall::EXPORT::ALL::"})
                        global_->define(std::string(which) + n, Value::typeObj(n));
                for (const char* n : ncAllOnly)
                    global_->define(std::string("NativeCall::EXPORT::ALL::") + n, Value::typeObj(n));
            }
            // `use NativeCall::Types` is a compunit of its own upstream — the
            // types without the machinery — and dists reach for it directly
            // (Font::FreeType's Raw/Defs does, and thirteen dists sit behind
            // that one). It is built in here just as NativeCall is, so there is
            // no file to find and the `use` died "Could not find".
            //
            // It publishes the names QUALIFIED, not into the caller's scope:
            // upstream, a bare `use NativeCall::Types` leaves `Pointer`
            // undeclared and answers `NativeCall::Types::Pointer`. Exporting the
            // short names here would be more convenient and less true, and would
            // hide the missing `use NativeCall` in any file that relied on it.
            if (u->module == "NativeCall::Types" && global_) {
                static const char* nctNames[] = {
                    "Pointer", "CArray", "ExplicitlyManagedString",
                    "VarArgPointerSentinel", "bool", "void", "long", "longlong",
                    "ulong", "ulonglong", "size_t", "ssize_t",
                };
                global_->define("NativeCall::Types", Value::typeObj("NativeCall::Types"));
                for (const char* n : nctNames)
                    global_->define(std::string("NativeCall::Types::") + n, Value::typeObj(n));
            }
            if (u->module == "Test") usedTest_ = true;
            else if (u->module.size() >= 2 && u->module[0] == 'v' && ascii::isdigit((unsigned char)u->module[1])) {
                // language version pragma: use v6.c / v6.d / v6.e[.PREVIEW]
                // A BARE `use v6;` asks for "some Raku 6", which means the
                // current default revision — 6.d — not the newest one we know
                // how to be. Reading it as 6.e silently changed semantics under
                // the most common pragma in real code (`sqrt(-1)` answered
                // 0+1i rather than NaN). Only an explicit 6.e (or later) opts in.
                langRev_ = langRevOfPragma(u->module);   // v6c/v6.c, v6d, v6e… (see Parser.h)
            }
            else if (u->module == "lib") {
                // `use lib` takes ONE path or a list of them (`use lib <lib t/lib>`);
                // each is prepended, so the last written wins the search order.
                std::vector<std::string> paths;
                if (!u->arg.empty()) paths.push_back(u->arg);
                else if (u->argExpr) {
                    Value pv = eval(u->argExpr.get());
                    if (pv.t == VT::Array && pv.arr() && !pv.itemized)
                        for (auto& e : *pv.arr()) paths.push_back(e.toStr());
                    else paths.push_back(pv.toStr());
                    // (the literal form refuses '' at parse time; a computed one here)
                    for (auto& p : paths)
                        if (p.empty())
                            throwTyped("X::LibEmpty", {}, "Repository specification can not be an empty string.  "
                                                          "Did you mean 'use lib \".\"' ?");
                }
                for (auto& p : paths)
                    if (!p.empty()) useLibPath(p);
            }
            // `use Rakupp::Ext` is the discoverable spelling for code that is
            // rakupp-only by design; the loader itself is a builtin (see
            // registerBuiltins) so a portable module can reach it through
            // `&::('rakupp-ext-load')` without writing anything Rakudo cannot
            // compile. Accepting the `use` keeps it from looking like a typo.
            else if (u->module == "Rakupp::Ext") { /* loader is always available */ }
            else if (!u->module.empty()) {
                // `use Mod EXPR, …` — the non-string arguments, evaluated here and
                // handed to the module's EXPORT after any string ones (`use
                // META::constants $?DISTRIBUTION`, `use CLI::Version
                // $?DISTRIBUTION, &MAIN, 'long'`)
                useExprArgs_.clear();
                if (u->argExpr) {
                    Value av = eval(u->argExpr.get());
                    if (av.t == VT::Array && av.arr() && av.isList && !av.itemized)
                        for (auto& e : *av.arr()) useExprArgs_.push_back(e);
                    else useExprArgs_.push_back(av);
                }
                // `import Foo;` — the package is ALREADY there; nothing is
                // loaded. What it does is bring Foo's routines into this scope
                // under their bare names, which is the whole difference Rakudo
                // enforces: without it `f()` is an undeclared routine even
                // though `Foo::f` resolves.
                //
                // rakupp imports the package's `our` routines rather than only
                // the `is export` ones, which is the same latitude this engine
                // already takes with a module's exports (an `our sub … is
                // export` inside a module body is globally reachable here
                // without any import at all). Nothing in the corpus can tell
                // the two apart; a program that could would see a name in scope
                // that Rakudo would not give it.
                if (u->isImport) {
                    // `import Mod :tag` asks the module for a SELECTIVE export
                    // set, which only the loader can answer — the package scan
                    // below sees what a load already published, never what a tag
                    // would add. loadModule is idempotent for an already-loaded
                    // module and replays just the import, which is exactly the
                    // shape `need Mod; import Mod :tag;` needs (Math::Trig).
                    if (!u->importArgs.empty())
                        loadModule(u->module, u->importArgs, /*doImport=*/true, /*quiet=*/true,
                                   u->verReq, /*requireForm=*/false);
                    // A CLASS's `is export` methods import as SUBS over the
                    // class: `method prefix:<~> is export` gives `~$obj`,
                    // `method infix:<as>(…) is export` gives `$obj as T`
                    // (S06-operator-overloading/infix.t)
                    {
                        auto cit = classes_.find(u->module);
                        if (cit != classes_.end() && cit->second && !cit->second->exportedMethods.empty()) {
                            std::string src;
                            for (auto& mn : cit->second->exportedMethods) {
                                std::string q = mn;
                                for (size_t k = 0; (k = q.find('\\', k)) != std::string::npos; k += 2) q.insert(k, "\\");
                                for (size_t k = 0; (k = q.find('"', k)) != std::string::npos; k += 2) q.insert(k, "\\");
                                src += "multi " + mn + "(" + u->module + " $self, |c) is raw { $self.\"" + q + "\"(|c) }\n";
                            }
                            try { evalString(src); } catch (RakuError&) {}
                        }
                    }
                    const std::string pfx = u->module + "::";
                    // …and the package's `is export` TYPES, bound LEXICALLY under
                    // their short names: an imported role group `Bar` shadows a
                    // `class Bar` declared elsewhere in the file for this scope
                    // (S11-modules/export.t). A group is exported when any of its
                    // candidates says so.
                    for (auto& kv : classes_) {
                        if (!kv.second || kv.first.size() <= pfx.size() ||
                            kv.first.compare(0, pfx.size(), pfx) != 0) continue;
                        std::string shortName = kv.first.substr(pfx.size());
                        if (shortName.find("::") != std::string::npos ||
                            shortName.find('\x01') != std::string::npos) continue;
                        ClassInfo* ci = kv.second.get();
                        bool exported = ci->decl && ci->decl->isExport;
                        for (auto& v : ci->roleVariants)
                            if (v && v->decl && v->decl->isExport) exported = true;
                        if (exported && !tctx_.cur->local(shortName))
                            tctx_.cur->define(shortName, Value::typeObj(kv.first));
                    }
                    // The routines come from the package's EXPORT stash — the tags
                    // asked for (DEFAULT and MANDATORY when none is named, every
                    // one under :ALL, a `:&name` by name) — exactly as Rakudo's
                    // import reads EXPORT::TAG. An unexported routine, `our` or
                    // not, stays behind its qualified name (import.t).
                    std::vector<std::string> exportPfx;   // `&Foo::EXPORT::DEFAULT::` …
                    std::set<std::string> wantNames;
                    {
                        std::set<std::string> tags;
                        for (auto& a : u->importArgs) {
                            if (a.empty()) continue;
                            if (std::strchr("&$@%", a[0])) wantNames.insert("&" + a.substr(1));
                            else tags.insert(a);
                        }
                        if (tags.empty() && wantNames.empty()) tags = {"DEFAULT", "MANDATORY"};
                        if (!wantNames.empty()) tags.insert("ALL");
                        for (auto& tg : tags) exportPfx.push_back("&" + pfx + "EXPORT::" + tg + "::");
                    }
                    for (Env* e = tctx_.cur.get(); e; e = e->parent.get())
                        for (auto& kv : e->vars) {
                            if (kv.first.size() <= pfx.size() + 1 || kv.first[0] != '&') continue;
                            std::string bare;
                            for (auto& ep : exportPfx)
                                if (kv.first.size() > ep.size() && kv.first.compare(0, ep.size(), ep) == 0) {
                                    bare = "&" + kv.first.substr(ep.size());
                                    break;
                                }
                            if (bare.empty() || bare.find("::") != std::string::npos) continue;
                            // an ALL entry that stands in for `:&name` brings in that name only
                            if (!wantNames.empty() && !wantNames.count(bare) &&
                                kv.first.find("::EXPORT::ALL::") != std::string::npos &&
                                !std::count(u->importArgs.begin(), u->importArgs.end(), std::string("ALL")))
                                continue;
                            Value* have = tctx_.cur->find(bare);
                            if (!have) { tctx_.cur->define(bare, kv.second); continue; }
                            // an imported MULTI joins the multi already in scope:
                            // this scope's own group, or a new one here that starts
                            // from the outer group's candidates
                            if (have->t == VT::Code && have->code() && have->code()->isMultiDispatcher &&
                                kv.second.t == VT::Code && kv.second.code() &&
                                kv.second.code()->isMultiDispatcher && have->code() != kv.second.code()) {
                                if (tctx_.cur->local(bare) != have) {
                                    Value fresh; fresh.t = VT::Code; fresh.setCode(makePayload<Callable>());
                                    fresh.code()->name = have->code()->name;
                                    fresh.code()->isMultiDispatcher = true;
                                    fresh.code()->candidates = have->code()->candidates;
                                    tctx_.cur->define(bare, fresh);
                                    have = tctx_.cur->local(bare);
                                }
                                auto& cands = have->code()->candidates;
                                for (auto& c : kv.second.code()->candidates) {
                                    bool dup = false;
                                    for (auto& h : cands)
                                        if (h.code() && c.code() && h.code()->body == c.code()->body &&
                                            h.code()->params == c.code()->params) { dup = true; break; }
                                    if (!dup) cands.push_back(c);
                                }
                            }
                        }
                    break;
                }
                // `use NQPHLL:from<NQP>` / `use QAST:from<NQP>`: Rakudo's own compiler
                // guts, which a slang's legacy role imports. Nothing to load here.
                if (u->fromLang == "NQP") return Value::any();
                // `require Stub:file($path)`: the FILE is loaded (its directory
                // searched for its stem), and what it declares lands in the
                // package Stub — `Stub::<InnerClass>` reaches the class
                // `require Mod <&sym $var term>` names SYMBOLS, each of which the
                // module must export: a missing one is X::Import::MissingSymbols
                // — and a bare `quux` is a sigilless term, not the sub &quux
                // (S11-modules/require.t)
                auto requireSymbols = [&](const std::string& from) {
                    if (!u->isRequire || u->importArgs.empty()) return;
                    Value missing = Value::array(); missing.isList = true;
                    std::string joined;
                    for (auto& sym : u->importArgs) {
                        if (sym.empty()) continue;
                        if ((tctx_.cur && tctx_.cur->find(sym)) || (global_ && global_->vars.count(sym))) continue;
                        missing.arr()->push_back(Value::str(sym));
                        joined += (joined.empty() ? "" : ", ") + sym;
                    }
                    if (!joined.empty())
                        throwTypedV("X::Import::MissingSymbols", {{"from", Value::str(from)}, {"missing", missing}},
                                    "Module '" + from + "' does not export " + joined);
                };
                if (u->isRequire && u->fileExpr) {
                    std::string path = eval(u->fileExpr.get()).toStr();
                    size_t sl = path.rfind('/');
                    std::string dir = sl == std::string::npos ? "." : path.substr(0, sl);
                    std::string stem = sl == std::string::npos ? path : path.substr(sl + 1);
                    for (const char* ext : {".rakumod", ".pm6", ".pm", ".raku"}) {
                        size_t el = std::strlen(ext);
                        if (stem.size() > el && stem.compare(stem.size() - el, el, ext) == 0) {
                            stem = stem.substr(0, stem.size() - el); break;
                        }
                    }
                    std::set<std::string> before;
                    for (auto& kv : classes_) before.insert(kv.first);
                    libPaths_.insert(libPaths_.begin(), dir);
                    try { loadModule(stem, {}, /*doImport=*/true, /*quiet=*/true, "", /*requireForm=*/true); }
                    catch (...) { libPaths_.erase(libPaths_.begin()); throw; }
                    libPaths_.erase(libPaths_.begin());
                    auto& stash = pkgStashes_[u->module];
                    if (!stash) stash = makePayload<ValueMap>();
                    for (auto& kv : classes_)
                        if (!before.count(kv.first) && kv.first.find("::") == std::string::npos)
                            (*stash)[kv.first] = Value::typeObj(kv.first);
                    requireSymbols(stem);
                    return Value::any();
                }
                loadModule(u->module, u->importArgs, !u->isNeed && !u->emptyImport, /*quiet=*/false, u->verReq,
                           /*requireForm=*/u->isRequire);
                requireSymbols(u->module);
                // A module can build a tag by hand — `package EXPORT::decode-percents
                // { our &decode-percents = &Cro::ResourceIdentifier::decode-percents }`
                // (Cro::Uri) — and `use Mod :decode-percents` imports what that
                // package holds. Only a tag the `use` names, and never over a name
                // already in scope.
                if (!u->isNeed && !u->emptyImport && !u->isRequire && global_ && tctx_.cur)
                    for (auto& tag : u->importArgs) {
                        if (tag.empty() || std::strchr("&$@%", tag[0]) || tag.find(':') != std::string::npos ||
                            tag == "DEFAULT" || tag == "ALL" || tag == "MANDATORY") continue;
                        const std::string pfx = "&EXPORT::" + tag + "::";
                        for (auto& kv : global_->vars) {
                            if (kv.first.size() <= pfx.size() || kv.first.compare(0, pfx.size(), pfx) != 0) continue;
                            std::string bare = "&" + kv.first.substr(pfx.size());
                            if (bare.find("::") != std::string::npos || tctx_.cur->local(bare)) continue;
                            tctx_.cur->define(bare, kv.second);
                        }
                    }
                // `use Mod <name:alias>` — import that routine under a second name.
                // (rakupp imports a module's whole export set; the alias is the part
                // that has to be honoured, or the name simply is not there.)
                for (auto& ia : u->importArgs) {
                    size_t c = ia.find(':');
                    if (c == std::string::npos || c == 0 || c + 1 >= ia.size()) continue;
                    std::string orig = "&" + ia.substr(0, c), alias = "&" + ia.substr(c + 1);
                    Value* src = tctx_.cur ? tctx_.cur->find(orig) : nullptr;
                    if (!src && global_) { auto it = global_->vars.find(orig); if (it != global_->vars.end()) src = &it->second; }
                    if (!src) continue;
                    if (tctx_.cur) tctx_.cur->define(alias, *src);
                    if (global_) global_->define(alias, *src);
                }
            }
            return Value::any();
        }
        case NK::SubDecl: {
            auto* sd = static_cast<SubDecl*>(s);
            // `sub ::(EXPR) (…) {…}` — the INDIRECT name, computed as the
            // declaration runs. Hoisting never pre-registers these (it keys
            // on the static name, which is empty), so the sub exists from
            // this statement on — which is also Rakudo's constraint: the
            // name expression's inputs must already have values.
            std::string dynSubName;
            if (sd->name.empty() && sd->nameExpr)
                dynSubName = eval(sd->nameExpr.get()).toStr();
            const std::string& sname = dynSubName.empty() ? sd->name : dynSubName;
            auto makeCand = [&](const std::vector<Param>* prms) {
                Value c; c.t = VT::Code; c.setCode(makePayload<Callable>());
                c.code()->name = sname;
                c.code()->pkg = tctx_.pkgPrefix.empty() ? "GLOBAL"
                            : tctx_.pkgPrefix.substr(0, tctx_.pkgPrefix.size() - 2); // strip trailing ::
                c.code()->params = prms;
                c.code()->body = &sd->body;
                c.code()->langRev = langRev_;
                c.code()->rakuAst = rakuAstPragma_;
                c.code()->closure = tctx_.cur;
                if (sd->retLiteralPresent && !sd->body.empty() && sd->body.back()->kind == NK::ExprStmt)
                    c.code()->retLiteral = static_cast<const ExprStmt*>(sd->body.back().get())->e.get();
                // `sub foo(A $a)` where A is a package or module: no type
                if (!hoistingSubs_)
                    for (auto& p : *prms) {
                        if (!p.type.empty() && nameIsPackageNow(p.type))
                            throwTypedV("X::Parameter::BadType", {{"type", Value::typeObj(p.type)}},
                                        "'" + p.type + "' cannot be used as a type");
                    }
                // `sub yoink(Junctoin $barf)` — a type nothing declares (asked
                // while hoisting too: the unit's declared names cover the
                // types defined further down)
                for (auto& p : *prms) {
                        if (!p.type.empty() && !p.typeCapture && !p.typeMayBeUndeclared && !p.coerce && paramTypeUndeclared(p.type)) {
                            bool captured = false;
                            for (auto& q : *prms)
                                if (q.typeCapture && (q.captureName == p.type || q.type == p.type)) captured = true;
                            if (!captured) throwInvalidParamType(p.type);
                        }
                    }
                // `sub foo() returns Bar { }` with no Bar anywhere: X::InvalidType
                if (sd->retViaReturns && !hoistingSubs_) {
                    // (a coercion `returns Int(Str)` names its TARGET type)
                    const std::string rn = retTypeName(sd->retType);
                    bool captured = false;
                    for (auto& p : *prms) if (p.typeCapture && (p.captureName == rn || p.type == rn)) captured = true;
                    if (!captured && !rn.empty() && rn.find("::") == std::string::npos &&
                        rn.find('[') == std::string::npos && rn.find(':') == std::string::npos &&
                        !classes_.count(rn) && !isKnownTypeName(rn) && !subsets_.count(rn) &&
                        !isNativeTypeName(rn) && !classes_.count(resolveClassAlias(rn)) &&
                        !(!tctx_.pkgPrefix.empty() && classes_.count(tctx_.pkgPrefix + rn)) &&
                        !tctx_.cur->find(rn))
                        throwTypedV("X::InvalidType", {{"typename", Value::str(rn)}},
                                    "Invalid typename '" + rn + "'");
                }
                // `sub infix:<↑>(…) is assoc<right>` — reductions over it fold from the right
                if (sd->assocRight && sname.rfind("infix:<", 0) == 0 && sname.size() > 8 && sname.back() == '>')
                    rightAssocOps_.insert(sname.substr(7, sname.size() - 8));
                c.code()->assocRight = sd->assocRight;
                c.code()->assocNon = sd->assocNon;
                c.code()->assocChain = sd->assocChain;
                c.code()->retType = qualifyDeclType(sd->retType);
                c.code()->retRw = sd->retRw;
                c.code()->declFile = declFileNowI();
                c.code()->declLine = sd->line;
                c.code()->pod = sd->pod; c.code()->podTrail = sd->podTrail;
                if (sd->deprecated) c.code()->deprecated = deprecationFor(sd);
                // a statement-level `my method m {…}` is a SubDecl with isMethod set;
                // dropping the flag here meant callCallable never bound `self`
                c.code()->isMethod = sd->isMethod;
                c.code()->isStub = stmtIsStub(sd->body);
                c.code()->testAssertion = sd->testAssertion;
                c.code()->isNodal = sd->isNodal;
                {   // `sub f { @_ }` — an @_/%_ reference implies a slurpy signature
                    std::set<std::string> ph2;
                    for (auto& s2 : sd->body) collectPHStmt(s2.get(), ph2);
                    c.code()->usesArgs = ph2.count("@_") || ph2.count("%_");
                    c.code()->implicitArgs = (ph2.count("@_") ? 1 : 0) | (ph2.count("%_") ? 2 : 0);
                    for (auto& st : sd->traits)
                        if (st.name == "hidden-from-USAGE") c.code()->hiddenFromUsage = true;
                        else if (st.name == "default") c.code()->isDefaultCand = true;
                    c.code()->hadSig = sd->hadSig;
                }
                // Only a PURE `{*}` proto (a bare redispatcher) is excluded from
                // dispatch; a proto with a real body (or an operator proto with no
                // multis) stays a callable candidate.
                c.code()->isProto = sd->isProto && sd->body.size() == 1 &&
                    sd->body[0]->kind == NK::ExprStmt &&
                    static_cast<ExprStmt*>(sd->body[0].get())->e &&
                    static_cast<ExprStmt*>(sd->body[0].get())->e->kind == NK::Whatever;
                c.code()->isProtoBody = sd->isProto && !c.code()->isProto;
                if (sd->isNative) { c.code()->isNative = true; c.code()->nativeLib = sd->nativeLib;
                                    c.code()->nativeLibSub = sd->nativeLibSub;
                    // `is native(EXPR)` — evaluate NOW, in the declaring module's
                    // scope, where `constant SHA1 = %?RESOURCES<libraries/sha1>`
                    // is visible (Rakudo's trait-application time). An undefined
                    // result (`is native(Str)`) keeps the default namespace. If
                    // the eval dies (forward reference), keep the AST for one
                    // retry at first call.
                    if (sd->nativeLibExpr && c.code()->nativeLib.empty() && c.code()->nativeLibSub.empty()) {
                        // …but NOT while hoisting: the module's own `constant
                        // LIB = …` has not executed yet, so the only LIB this
                        // eval can find is a bare global some OTHER module
                        // published — DBDish::SQLite's natives were baked to
                        // Pg's 'pq' exactly that way. Defer to first call,
                        // which retries in the sub's own closure.
                        if (!hoistingSubs_) {
                            try {
                                Value r = eval(sd->nativeLibExpr.get());
                                if (r.t == VT::Code) { ValueList none; r = callCallable(r, none); }
                                if (isDefined(r)) c.code()->nativeLib = ncLibNameOf(r);
                            } catch (RakuError&) {}
                        }
                        // No lib yet? Keep the AST and retry at first call, in
                        // the closure (the constant is visible there by then).
                        if (c.code()->nativeLib.empty())
                            c.code()->nativeLibExpr = sd->nativeLibExpr.get();
                    }
                    // `is symbol(EXPR)`: unlike the library above, a computed name is
                    // resolved at FIRST CALL, never here. The helper that computes it
                    // typically asks the library its version — and at declaration time
                    // the library is not loadable yet, so the probe answers 0, which
                    // OpenSSL::Stack's real_symbol caches in a `state` and hands back
                    // the pre-1.1 spelling for every symbol from then on.
                    if (sd->nativeSymExpr) c.code()->nativeSymExpr = sd->nativeSymExpr.get();
                                    c.code()->nativeSym = sd->nativeSym.empty() ? sname : sd->nativeSym; }
                for (auto& p : *prms) {
                    // a default makes no sense on a slurpy or a required param
                    // (an anonymous one, `*@`, has no name to report)
                    const std::string pn = p.name.size() > 1 ? p.name : std::string();
                    if (p.defaultVal && p.slurpy)
                        throwTyped("X::Parameter::Default", {{"how", "slurpy"}, {"parameter", pn}},
                            "Cannot put default on slurpy parameter " + (pn.empty() ? std::string("") : pn));
                    if (p.defaultVal && p.required)
                        throwTyped("X::Parameter::Default", {{"how", "required"}, {"parameter", pn}},
                            "Cannot put default on required parameter " + pn);
                }
                // a `--> 42` / `--> "foo"` / `--> Nil` constraint forbids
                // `return <value>` anywhere in the body (compile error in Rakudo)
                if (sd->retLiteralPresent || sd->retType == "Nil") {
                    std::string repr = "Nil";
                    // the literal was appended as the body's LAST statement (parseSub);
                    // read it back from there for the error message
                    const Expr* rl = nullptr;
                    if (sd->retLiteralPresent && !sd->body.empty() &&
                        sd->body.back()->kind == NK::ExprStmt)
                        rl = static_cast<const ExprStmt*>(sd->body.back().get())->e.get();
                    if (rl) {
                        if (rl->kind == NK::IntLit)
                            repr = std::to_string(static_cast<const IntLit*>(rl)->v);
                        else if (rl->kind == NK::StrLit)
                            repr = "\"" + static_cast<const StrLit*>(rl)->v + "\"";
                        else if (rl->kind == NK::NumLit)
                            repr = Value::number(static_cast<const NumLit*>(rl)->v).gist();
                        else if (rl->kind == NK::InterpStr) { // `"foo"` lexes as an InterpStr
                            std::string flat; bool constStr = true;
                            for (auto& p : static_cast<const InterpStr*>(rl)->parts) {
                                if (p->kind == NK::StrLit) flat += static_cast<const StrLit*>(p.get())->v;
                                else { constStr = false; break; }
                            }
                            if (constStr) repr = "\"" + flat + "\"";
                        }
                        else if (rl->kind == NK::BoolLit)
                            repr = static_cast<const BoolLit*>(rl)->v ? "True" : "False";
                        else if (rl->kind == NK::NameTerm)
                            repr = static_cast<const NameTerm*>(rl)->name; // `--> True`/`--> Nil`
                    }
                    bool viaMethod = false;   // `27.return` — Rakudo raises that one at run time, untyped
                    std::function<bool(const Stmt*)> hasRetVal = [&](const Stmt* st) -> bool {
                        if (!st) return false;
                        if (st->kind == NK::ReturnStmt)
                            return static_cast<const ReturnStmt*>(st)->value != nullptr;
                        // `42.return` — the method form of returning a value is
                        // forbidden by a literal `-->` constraint too
                        if (st->kind == NK::ExprStmt) {
                            const Expr* e2 = static_cast<const ExprStmt*>(st)->e.get();
                            if (e2 && e2->kind == NK::MethodCall &&
                                static_cast<const MethodCall*>(e2)->method == "return")
                                return viaMethod = true;
                        }
                        if (st->kind == NK::Block) {
                            for (auto& b : static_cast<const Block*>(st)->stmts)
                                if (hasRetVal(b.get())) return true;
                        }
                        else if (st->kind == NK::IfStmt) {
                            auto* is = static_cast<const IfStmt*>(st);
                            for (auto& br : is->branches) if (hasRetVal(br.second.get())) return true;
                            if (hasRetVal(is->elseBlock.get())) return true;
                        }
                        else if (st->kind == NK::WhileStmt)
                            return hasRetVal(static_cast<const WhileStmt*>(st)->body.get());
                        else if (st->kind == NK::ForStmt)
                            return hasRetVal(static_cast<const ForStmt*>(st)->body.get());
                        return false;
                    };
                    for (auto& st : sd->body)
                        if (hasRetVal(st.get()))
                            throwTyped(viaMethod ? "X::AdHoc" : "X::Comp::AdHoc",
                                {{"payload", repr}},
                                "No return arguments allowed when return value " +
                                repr + " is already specified in the signature");
                }
                if (prms->empty()) {
                    c.code()->placeholders = computePlaceholders(sd->body);
                    // an explicit signature — even `()` — forbids placeholders
                    if (sd->hadSig && !c.code()->placeholders.empty())
                        throwTyped("X::Signature::Placeholder",
                            {{"placeholder", c.code()->placeholders[0]},
                             {"line", std::to_string(sd->line > 0 ? sd->line : 1)}},
                            "Placeholder variable '" + c.code()->placeholders[0] +
                            "' cannot override existing signature");
                }
                return c;
            };
            Value code = makeCand(&sd->params);
            ValueList altCands;
            for (auto& ap : sd->altParams) altCands.push_back(makeCand(&ap));
            // dispatch non-built-in `is` traits to a user trait_mod:<is> multi:
            // `sub foo() is traced {…}` calls trait_mod:<is>($foo, :traced).
            // Skipped while hoisting — traits run at the declaration's textual
            // position (see the hoist-skip sites), after earlier `my`s initialized.
            if (!sd->traits.empty() && !hoistingSubs_) {
                if (Value* tm = tctx_.cur->find("&trait_mod:<is>")) {
                    if (tm->t == VT::Code) for (auto& st : sd->traits) callRoutineTrait(*tm, code, st);
                }
                // no `trait_mod:<is>` anywhere: `sub yulia is krassivaya {}` is an
                // unknown trait (Rakudo: X::Comp::Trait::Unknown), where certain
                else if (routineTraitsCertain())
                    for (auto& st : sd->traits)
                        if (!routineTraitKnown(st.name))
                            unknownRoutineTrait(st.name, code);
            }
            // …and the PARAMETERS' own traits, for a sub that is NOT hoisted
            // (an anonymous or nested one); a hoisted named sub gets them from
            // applySubTraits, at its textual position.
            if (!hoistingSubs_) applyParamTraits(sd->params, code);
            // `(sig1) | (sig2)` is ONE routine with alternative signatures — its
            // candidates share a single `state`-variable store.
            if (!sd->altParams.empty()) {
                auto shared = std::make_shared<Env>(); shared->parent = tctx_.cur;
                auto share = [&](Value& c) { std::call_once(c.code()->state.init, [&] { c.code()->state.env = shared; }); };
                share(code);
                for (auto& c : altCands) share(c);
            }
            if (!sname.empty()) {
                if (sd->isMulti || !sd->altParams.empty()) {
                    std::string key = "&" + sname;
                    Value* existing = tctx_.cur->find(key);
                    Callable* disp;
                    Value dispVal;
                    // A group from an OUTER scope is not this scope's to grow:
                    // an inner `multi` gets its own group that starts from the
                    // outer candidates (they stay visible), and an inner `proto`
                    // starts an empty one — the outer candidates are shadowed.
                    // Joining the outer group leaked inner candidates out of the
                    // block, and a proto could not isolate them at all.
                    if (existing && existing->t == VT::Code && existing->code() &&
                        existing->code()->isMultiDispatcher && tctx_.cur->local(key) != existing) {
                        Value fresh; fresh.t = VT::Code; fresh.setCode(makePayload<Callable>());
                        fresh.code()->name = sname;
                        fresh.code()->isMultiDispatcher = true;
                        if (!sd->isProto) fresh.code()->candidates = existing->code()->candidates;
                        tctx_.cur->define(key, fresh);
                        existing = tctx_.cur->local(key);
                    }
                    if (existing && existing->t == VT::Code && existing->code() && existing->code()->isMultiDispatcher) {
                        disp = existing->code(); dispVal = *existing;
                    } else {
                        dispVal.t = VT::Code; dispVal.setCode(makePayload<Callable>());
                        dispVal.code()->name = sname;
                        dispVal.code()->isMultiDispatcher = true;
                        disp = dispVal.code();
                        tctx_.cur->define(key, dispVal);
                    }
                    // A declaration inside a block runs twice — hoisted at the
                    // block's entry, then again in sequence — and used to join
                    // the group both times: `do { proto sub f(|) {*}; &f }` and
                    // a proto in a sub body listed two `(|)` candidates. The
                    // declaration (its body) is the identity; a repeat is a no-op.
                    auto joins = [&](const Value& c) {
                        if (!c.code()) return;
                        for (auto& have : disp->candidates)
                            if (have.code() && have.code()->body && have.code()->body == c.code()->body &&
                                have.code()->params == c.code()->params)
                                return;
                        c.code()->isMultiCandidate = true;
                        disp->candidates.push_back(c);
                        noteUserInfixCandidate(sname, c.code());
                    };
                    joins(code);
                    for (auto& c : altCands) joins(c);
                    // `our proto`/`our multi` in a package: the GROUP is published
                    // under the qualified name, as a plain `our sub` is below
                    if (sd->isOur && !tctx_.pkgPrefix.empty() && global_)
                        global_->define("&" + tctx_.pkgPrefix + sname, dispVal);
                    // …and an exported one is in its package's EXPORT tags, as a
                    // plain exported sub is below (what `import` consults)
                    if (sd->isExport && !sd->isMethod && global_ && !tctx_.pkgPrefix.empty()) {
                        std::set<std::string> tags(sd->exportTags.begin(), sd->exportTags.end());
                        if (tags.empty()) tags.insert("DEFAULT");
                        tags.insert("ALL");
                        for (auto& tg : tags)
                            global_->define("&" + tctx_.pkgPrefix + "EXPORT::" + tg + "::" + sname, dispVal);
                    }
                    return dispVal;
                }
                // a named METHOD declared inside another method's body belongs
                // to the class all the same (Rakudo installs it in $?PACKAGE),
                // closing over the frame that declared it: `method foo { my $a
                // = 42; method bar { $a } }` — `.bar` after `.foo` answers 42
                if (sd->isMethod && !sd->isPrivate && !hoistingSubs_ && tctx_.curRoutineVal &&
                    tctx_.curRoutineVal->t == VT::Code && tctx_.curRoutineVal->code() &&
                    tctx_.curRoutineVal->code()->isMethod) {
                    auto ci = classes_.find(tctx_.curRoutineVal->code()->pkg);
                    if (ci != classes_.end() && ci->second && !ci->second->isRole) {
                        code.code()->pkg = ci->second->name;
                        ci->second->methods[sname] = code;
                    }
                }
                tctx_.cur->define("&" + sname, code);
                // `our sub` is package-scoped: also install globally so a sibling block
                // (or an `our &name;` re-declaration) can reach it.
                if (sd->isOur && curPkgEnv_ && curPkgEnv_ != tctx_.cur) {
                    // a DIFFERENT `our sub` of the same name already owns the
                    // package slot: `{ our sub foo {…} }; { our sub foo {…} }`
                    if (!sd->isMulti && !sd->isProto && !sd->isMethod) {   // methods live in their class
                        Value* had = curPkgEnv_->local("&" + sname);
                        if (!had) had = curPkgEnv_->local("\x01OUR&" + sname);   // an EVAL-born one
                        if (had)
                            if (had->t == VT::Code && had->code() && code.code() &&
                                had->code()->body && had->code()->body != code.code()->body &&
                                !had->code()->isMultiDispatcher)
                                throwTypedV("X::Redeclaration",
                                    {{"symbol", Value::str(sname)}, {"what", Value::str("routine")}},
                                    "Redeclaration of routine '" + sname + "'");
                    }
                    // …but one an EVAL declared at the top level is in the PACKAGE,
                    // not in any later unit's lexical scope: `EVAL 'our sub f {…}';
                    // EVAL 'f()'` is X::Undeclared::Symbols (S02-names/our.t)
                    if (!g_evalUnits.empty() && curPkgEnv_ == global_ && tctx_.pkgPrefix.empty() &&
                        !sd->isMulti && !sd->isProto && !sd->isMethod)
                        curPkgEnv_->define("\x01OUR&" + sname, code);
                    else
                        curPkgEnv_->define("&" + sname, code);
                }
                // and publish the fully-qualified name (Foo::Bar::name) so callers
                // outside the module can reach an unexported `our sub` — the only
                // way OpenSSL::Version::version_num etc. are invoked.
                if (sd->isOur && !tctx_.pkgPrefix.empty())
                    global_->define("&" + tctx_.pkgPrefix + sname, code);
                // an exported routine is also in its package's EXPORT::TAG
                // packages — every tag it names (DEFAULT when it names none)
                // and always ALL: `&EXPORT::ALL::f`, `Foo::EXPORT::DEFAULT::f()`
                if (sd->isExport && !sd->isMethod && global_) {
                    std::set<std::string> tags(sd->exportTags.begin(), sd->exportTags.end());
                    if (tags.empty()) tags.insert("DEFAULT");
                    tags.insert("ALL");
                    for (auto& tg : tags)
                        global_->define("&" + tctx_.pkgPrefix + "EXPORT::" + tg + "::" + sname, code);
                }
            }
            // (not while HOISTING: the call happens where the statement stands,
            // inside whatever CATCH the block has, and exactly once)
            if (sd->immediateCall && !hoistingSubs_) { // `sub f(...) {...}(args)` — call right away
                // (evalArgs, as any call's arguments: `( |True )` slips its value in)
                ValueList ia = evalArgs(sd->immediateArgs);
                Value r = callCallable(code, ia);
                // a statement on its own: a Failure it returns is SUNK, and blows up
                failureDetonate(r);
                return r;
            }
            return code;
        }
        case NK::EnumDecl: {
            auto* ed = static_cast<EnumDecl*>(s);
            if (!ed->pod.empty() && !ed->name.empty()) { // `#| …` — Colors.WHY
                pkgPod_[tctx_.pkgPrefix + ed->name] = ed->pod;
                pkgPodTrail_[tctx_.pkgPrefix + ed->name] = ed->podTrail;
                if (!tctx_.pkgPrefix.empty()) { pkgPod_[ed->name] = ed->pod; pkgPodTrail_[ed->name] = ed->podTrail; }
            }
            ValueList items;
            if (ed->values) {
                Value ev0 = eval(ed->values.get());
                // `enum Bits (%h)` — a HASH gives its pairs
                if (ev0.t == VT::Hash && ev0.hash() && (ev0.hashKind.empty() || ev0.hashKind == "Map")) {
                    for (auto& kv : *ev0.hash()) items.push_back(Value::pair(kv.first, kv.second));
                }
                else if (ev0.t == VT::Array && ev0.arr() && ev0.arr()->size() == 1 &&
                         (*ev0.arr())[0].t == VT::Hash && (*ev0.arr())[0].hash() &&
                         ((*ev0.arr())[0].hashKind.empty() || (*ev0.arr())[0].hashKind == "Map")) {
                    for (auto& kv : *(*ev0.arr())[0].hash()) items.push_back(Value::pair(kv.first, kv.second));
                }
                else items = ev0.flatten();
            }
            long long counter = 0;
            Value pairs = Value::array();
            Value lastVal;             // the previous member's value (a bare key succeeds it)
            // an ANONYMOUS enum is still a type its members belong to — `.pred`,
            // `.succ` and `.enums` walk it — under a hidden name that every
            // rendering shows as Rakudo shows the empty one: `.^name` is "",
            // `.raku` is `::B` (Value::typeName, enumTypeShown)
            std::string enumTypeName = ed->name;
            if (enumTypeName.empty()) {
                static std::atomic<unsigned> anonEnums{0};
                enumTypeName = std::string(kAnonEnumPrefix) + std::to_string(++anonEnums);
            }
            std::string enumBaseType;  // the type every value must share
            for (auto& it : items) {
                std::string key; Value val;
                if (it.t == VT::Pair) { key = it.s; val = it.pairVal() ? *it.pairVal() : Value::integer(counter); if (val.t == VT::Int && !val.big()) counter = val.toInt() + 1; }
                // after a NON-Int member, a bare key takes the next value by `.succ`:
                // `enum E (a => 'x', 'b')` makes b 'y'
                else if (lastVal.t != VT::Nil && lastVal.t != VT::Int && lastVal.t != VT::Any) {
                    key = it.toStr(); val = methodCall(lastVal, "succ", {});
                }
                else { key = it.toStr(); val = Value::integer(counter++); }
                lastVal = val;
                // one enum, one value type: the declared one, else the first member's
                {
                    const std::string vt = val.t == VT::Int ? "Int" : val.typeName();
                    if (!ed->ofType.empty()) {
                        if (!typeOrSubsetMatches(val, ed->ofType))
                            throwTypedV("X::TypeCheck", {{"got", val}, {"expected", Value::typeObj(ed->ofType)}},
                                        "Type check failed in enum " + ed->name + "::" + key + " definition; expected " +
                                        ed->ofType + " but got " + val.typeName());
                    }
                    else if (enumBaseType.empty()) enumBaseType = vt;
                    else if (vt != enumBaseType && !(enumBaseType == "Int" && val.t == VT::Int))
                        throwTypedV("X::TypeCheck", {{"got", val}, {"expected", Value::typeObj(enumBaseType)}},
                                    "Type check failed in enum " + ed->name + "::" + key + " definition; expected " +
                                    enumBaseType + " but got " + val.typeName() + " (" + val.gist() + ")");
                }
                Value ev;
                // An Int beyond int64 (`CBOR_Max_UInt_8Byte => 18446744073709551615`)
                // IS the member's value, not an ordinal to clamp: `toInt()` saturated
                // it to 2^63-1, so every range test CBOR::Simple wrote against the
                // constant sent a legal uint64 down the bignum-tag path. Keep the
                // BigInt payload and tag it with the member name like any other.
                if (val.t == VT::Int && val.big()) { ev = val; ev.enumName = key; }
                else ev = Value::enumVal(key, val.t == VT::Int ? val.toInt() : counter++);
                // a NON-Int enum value (`enum Blerp (One => "Eins")`) keeps its real
                // value beside the ordinal; `.value` and `.pair` answer with it
                if (val.t != VT::Int) ev.setPairVal(makePayload<Value>(val));
                ev.enumType = enumTypeName; // carry the enum's type identity (for .^name, ~~, .WHAT)
                // a short name ANOTHER enum of this scope already claimed is
                // poisoned: neither can have it (`S1::b` / `S2::b` still work)
                {
                    Value* prior = tctx_.cur->local(key);
                    if (prior && !ed->name.empty() &&
                        ((!prior->enumType.empty() && prior->enumType.str() != ed->name) ||
                         (prior->t == VT::Hash && prior->hashKind == "PoisonedAlias"))) {
                        Value pz = Value::makeHash(); pz.hashKind = "PoisonedAlias";
                        (*pz.hash())["alias"] = Value::str(key);
                        (*pz.hash())["package-name"] = Value::str(ed->name);
                        tctx_.cur->define(key, pz);
                    }
                    else tctx_.cur->define(key, ev);
                }
                tctx_.cur->define(enumTypeName + "::" + key, ev);   // (`.succ` finds a member by it)
                // …and under the PACKAGE-QUALIFIED names, so another compilation
                // unit can write `URI::Query::Mixed`. Without them the qualified
                // form fell through to a bare type object carrying that name, and
                // `when Mixed` inside the module never matched it — split-query
                // ignored its :hash-format and always returned the raw pair list.
                if (!tctx_.pkgPrefix.empty()) {
                    global_->define(tctx_.pkgPrefix + key, ev);
                    if (!ed->name.empty())
                        global_->define(tctx_.pkgPrefix + ed->name + "::" + key, ev);
                }
                // `enum EType is export (…)` INSIDE a `unit class`: the body
                // scope dies with the load, so exported members must land in
                // global for the importer (Date::Event's test files use the
                // bare key names)
                if (ed->isExport && global_) {
                    global_->define(key, ev);
                    if (!ed->name.empty()) global_->define(ed->name + "::" + key, ev);
                }
                pairs.arr()->push_back(Value::pair(key, val));
            }
            pairs.enumType = enumTypeName; // the type object itself is the tagged pair-list
            enumPairs_[enumTypeName] = pairs; enumLangRev_[enumTypeName] = langRev_;  // reachable from any scope
            if (!ed->name.empty()) {
                tctx_.cur->define(ed->name, pairs);
                if (!tctx_.pkgPrefix.empty()) global_->define(tctx_.pkgPrefix + ed->name, pairs);
                if (ed->isExport && global_) global_->define(ed->name, pairs);
            }
            // an enum declaration is valued as the Map of its members, named or
            // not: `my %e = enum :: <foo bar>` is `{foo => 0, bar => 1}`, and so
            // is `my $e = enum Ea <foo bar>`
            Value m = Value::makeHash();
            for (auto& p : *pairs.arr()) (*m.hash())[p.s] = p.pairVal() ? *p.pairVal() : Value::any();
            m.hashKind = "Map";
            return m;
        }
        case NK::ClassDecl: {
            auto* cd = static_cast<ClassDecl*>(s);
            // A `my class` declared inside a ROUTINE is named under the package
            // that routine belongs to: `class Q { method b { my class X {} } }`
            // makes Q::X, as Rakudo names it (and a module sub's, M::Z). The
            // call does not carry the package along, so it is taken from the
            // running routine for the length of this declaration.
            struct RoutinePkgPrefix {
                std::string& p; std::string saved; bool on = false;
                explicit RoutinePkgPrefix(std::string& x) : p(x) {}
                ~RoutinePkgPrefix() { if (on) p = std::move(saved); }
            } routinePkg(tctx_.pkgPrefix);
            if (cd->isMy && !cd->isAugment && !cd->name.empty() && cd->name.find("::") == std::string::npos &&
                tctx_.curRoutineVal && tctx_.curRoutineVal->t == VT::Code && tctx_.curRoutineVal->code()) {
                const std::string& pk = tctx_.curRoutineVal->code()->pkg;
                if (!pk.empty() && pk != "GLOBAL" && tctx_.pkgPrefix != pk + "::") {
                    routinePkg.saved = tctx_.pkgPrefix; routinePkg.on = true;
                    tctx_.pkgPrefix = pk + "::";
                }
            }
            // one name, two ONLY methods (or two tokens) in one package is a
            // redeclaration — `multi` candidates and a proto's `:sym<…>` are not
            if (!hoistingSubs_) {
                std::set<std::string> seenM, seenR;
                for (auto& md : cd->methods) {
                    if (!md || md->isMulti || md->isProto || md->name.empty()) continue;
                    if (!seenM.insert((md->isPrivate ? "!" : "") + md->name).second)
                        throwTypedV("X::Redeclaration",
                            {{"symbol", Value::str(md->name)}, {"what", Value::str("method")}},
                            "Package '" + cd->name + "' already has a method '" + md->name +
                            "' (did you mean to declare a multi method?)");
                }
                // …and the scoped forms, which sit in the body as statements:
                // `my method foo …; my method foo …`, `our token foo …` twice
                for (auto& st : cd->body) {
                    if (!st) continue;
                    if (st->kind == NK::SubDecl) {
                        auto* sd = static_cast<SubDecl*>(st.get());
                        if (sd->isMethod && !sd->isMulti && !sd->isProto && !sd->name.empty() &&
                            !seenM.insert(sd->name).second)
                            throwTypedV("X::Redeclaration",
                                {{"symbol", Value::str(sd->name)}, {"what", Value::str("method")}},
                                "Redeclaration of method '" + sd->name + "'");
                    }
                    else if (st->kind == NK::NamedRegexDecl) {
                        auto* nr = static_cast<NamedRegexDecl*>(st.get());
                        if (!nr->name.empty() && !seenR.insert(nr->name).second)
                            throwTypedV("X::Redeclaration",
                                {{"symbol", Value::str(nr->name)}, {"what", Value::str("regex")}},
                                "Redeclaration of regex '" + nr->name + "'");
                    }
                }
                // (a rule with a signature is a `multi rule` candidate — one of several)
                for (auto& r : cd->rules)
                    if (!r.name.empty() && r.params.empty() && r.lits.empty() && !seenR.insert(r.name).second)
                        throwTypedV("X::Redeclaration",
                            {{"symbol", Value::str(r.name)}, {"what", Value::str("method")}},
                            "Package '" + cd->name + "' already has a regex '" + r.name + "'");
            }
            // `class C does InNoWayExist` — a role name nothing declares is
            // X::InvalidType (a built-in role or type is fine, and a qualified
            // or parameterized name is left to the loader)
            if (!hoistingSubs_) {
                auto unknownRole = [&](const std::string& rn) {
                    if (rn.empty() || rn.find("::") != std::string::npos || rn.find('[') != std::string::npos) return false;
                    if (classes_.count(rn) || isKnownTypeName(rn) || subsets_.count(rn)) return false;
                    if (!tctx_.pkgPrefix.empty() && classes_.count(tctx_.pkgPrefix + rn)) return false;
                    if (classes_.count(resolveClassAlias(rn))) return false;
                    if (tctx_.cur->find(rn)) return false;   // an enum / constant / imported symbol
                    return true;
                };
                std::vector<std::string> doesNames = cd->roles;
                if (cd->parentIsDoes) doesNames.push_back(cd->parent);
                for (auto& hn : cd->hidesNames) doesNames.push_back(hn);
                // `role RR[::T] does T` — the role's own type parameter names
                // what it will do once parameterized
                auto isOwnCapture = [&](const std::string& rn) {
                    if (!cd->isRole) return false;
                    for (auto& p : cd->roleParams)
                        if (p.typeCapture && (p.captureName == rn || p.type == rn)) return true;
                    return false;
                };
                for (auto& rn : doesNames)
                    if (!isOwnCapture(rn) && unknownRole(rn))
                        throwTypedV("X::InvalidType", {{"typename", Value::str(rn)}},
                                    "Invalid typename '" + rn + "'");
                // (a name the unit declares further down is let through: Rakudo
                // refuses that too, but S12-attributes/trusts.t scores on it)
                const Program* tunit = unitCurrent();
                for (auto& tn : cd->trustsNames)
                    if (tn != "GLOBAL" && tn != "OUR" &&   // the pseudo-packages are always there
                        unknownRole(tn) && !(tunit && (tunit->typeNamesOpaque || tunit->declaredTypeNames.count(tn))))
                        throwTypedV("X::Undeclared", {{"symbol", Value::str(tn)}, {"what", Value::str("Type")}},
                                    "Type '" + tn + "' is not declared");
            }
            // `class Foo does Maybe` where Maybe is an ENUM: that is role
            // composition (see the roles loop), never a parent class
            if (cd->parentIsDoes && !cd->parent.empty() && !classes_.count(cd->parent)) {
                bool isEnum = false;
                try {
                    NameTerm tn(cd->parent);
                    Value et = eval(&tn);
                    isEnum = et.t == VT::Array && !et.enumType.empty() && et.enumName.empty();
                } catch (...) {}
                if (isEnum) {
                    cd->roles.insert(cd->roles.begin(), cd->parent);
                    cd->parent.clear();
                    cd->parentIsDoes = false;
                }
            }
            // already created by the hoist pass at scope entry — re-running it
            // would build a SECOND ClassInfo, and objects made in between would
            // then belong to a type that is no longer the one the name resolves to
            if (!hoistingSubs_) {
                auto ht = hoistedTypes_.find(cd);
                if (ht != hoistedTypes_.end()) {
                    if (--ht->second <= 0) hoistedTypes_.erase(ht);
                    return Value::any();
                }
            }
            if (cd->isAugment) {
                // Reopen an existing type and add methods. Build each method into a
                // Callable Value (same shape as the main registration path).
                auto buildMethod = [&](SubDecl* md) {
                    Value code; code.t = VT::Code;
                    code.setCode(makePayload<Callable>());
                    code.code()->name = md->name;
                    code.code()->params = &md->params;
                    code.code()->retType = qualifyDeclType(md->retType);
                    code.code()->retRw = md->retRw;
                    code.code()->pod = md->pod; code.code()->podTrail = md->podTrail;
                    if (md->deprecated) code.code()->deprecated = deprecationFor(md);
                    code.code()->body = &md->body;
                    code.code()->langRev = langRev_;
                    code.code()->rakuAst = rakuAstPragma_;
                    code.code()->closure = tctx_.cur;
                    code.code()->isMethod = true;
                    code.code()->declFile = declFileNowI();
                    code.code()->declLine = md->line;
                    if (md->params.empty()) code.code()->placeholders = computePlaceholders(md->body);
                    return code;
                };
                auto addTo = [&](auto& tbl, SubDecl* md) {
                    Value code = buildMethod(md);
                    if (md->isMulti) {
                        auto it = tbl.find(md->name);
                        if (it != tbl.end() && it->second.code() && it->second.code()->isMultiDispatcher) {
                            if (code.code()) code.code()->isMultiCandidate = true;
                            it->second.code()->candidates.push_back(code);
                            return;
                        }
                        Value disp; disp.t = VT::Code; disp.setCode(makePayload<Callable>());
                        disp.code()->name = md->name; disp.code()->isMultiDispatcher = true;
                        // the group of a METHOD is itself a Method: `^lookup` hands
                        // this back, and code that dispatches on it (a
                        // `trait_mod:<is>(Method $m, …)` handler) will not bind a Sub
                        disp.code()->isMethod = true;
                        disp.code()->pkg = cd->name;   // see the class path
                        if (code.code()) code.code()->isMultiCandidate = true;
                        disp.code()->candidates.push_back(code);
                        tbl[md->name] = disp;
                        return;
                    }
                    tbl[md->name] = code;
                };
                // `augment class Rat does Precise { }` — the ROLES an augment
                // composes. Rat::Precise adds its `.precise` to Rat and FatRat
                // that way and declares no method in the body at all, so an
                // augment that reads only cd->methods added nothing. This is a
                // shallow composition: the role's own methods, by name, into the
                // same table the body's methods go to. A role that brings
                // attributes or composes further roles of its own is not served
                // here — a full compose needs the class-declaration path.
                std::vector<std::string> augRoles;
                if (cd->parentIsDoes && !cd->parent.empty()) augRoles.push_back(cd->parent);
                for (auto& rn : cd->roles) augRoles.push_back(rn);
                auto roleInfo = [&](const std::string& rn) -> ClassInfo* {
                    auto it = classes_.find(rn);
                    if (it == classes_.end() && !tctx_.pkgPrefix.empty())
                        it = classes_.find(tctx_.pkgPrefix + rn);
                    if (it == classes_.end()) it = classes_.find(resolveClassAlias(rn));
                    return it == classes_.end() ? nullptr : it->second.get();
                };
                auto addRolesTo = [&](auto& tbl) {
                    for (auto& rn : augRoles) {
                        ClassInfo* ri = roleInfo(rn);
                        if (!ri) continue;
                        for (auto& kv : ri->methods)
                            if (!tbl.count(kv.first)) tbl[kv.first] = kv.second;
                    }
                };
                // resolve package-relative short names: `augment class B` inside
                // `augment class A { … }` finds the nested A::B via prefix/alias
                auto existing = classes_.find(cd->name);
                if (existing == classes_.end() && !tctx_.pkgPrefix.empty())
                    existing = classes_.find(tctx_.pkgPrefix + cd->name);
                if (existing == classes_.end())
                    existing = classes_.find(resolveClassAlias(cd->name));
                if (existing != classes_.end()) {
                    // augment a user-declared type — merge into its ClassInfo
                    ClassInfo* ci = existing->second.get();
                    addRolesTo(ci->methods);
                    for (auto& rn : augRoles) ci->doneRoles.insert(rn);
                    for (auto& md : cd->methods) addTo(ci->methods, md.get());
                    for (auto& a : cd->attrs) {
                        ClassAttr ca; ca.name = a.name; ca.sigil = a.sigil;
                        ca.pod = a.pod; ca.podTrail = a.podTrail; ca.declLine = a.declLine;
                        ca.pub = a.pub; ca.rw = a.rw; ca.required = a.required; ca.def = a.def || a.sigil != '$' ? a.def.get() : a.defaultTrait.get(); ca.shape = a.shape.get(); ca.defaultTrait = a.defaultTrait.get(); ca.where = a.whereExpr.get(); ca.type = resolveAttrTypeAlias(a.type, ci->name); ca.requiredWhy = a.requiredWhy;
                        ca.defConstraint = a.defConstraint;
                        ca.coerce = a.coerce;
                        ca.containerIs = a.containerIs;
                        ca.doesRoles = a.doesRoles;
                        ca.objKeyed = a.objKeyed;
                        ca.inlined = a.inlined;
                        if (ca.inlined) { ca.inlineCls = ncInlineClass(ca.type); haveInlineAttrs_ = true; }
                        ci->attrs.push_back(ca);
                    }
                    for (auto& r : cd->rules) installRuleResolved(ci, r);
                    noteSymbolMutation("augment (user type)");
                } else {
                    // augment a built-in type — park methods in the extension
                    // table; a name that is neither a user type nor a known
                    // built-in has nothing to augment
                    if (!isKnownTypeName(cd->name))
                        throwTyped("X::Augment::NoSuchType",
                                   {{"package-kind", "class"}, {"package", cd->name}},
                                   "You tried to augment class " + cd->name +
                                   ", but it does not exist");
                    addRolesTo(builtinExt_[cd->name]);
                    for (auto& md : cd->methods) addTo(builtinExt_[cd->name], md.get());
                    noteSymbolMutation("augment (built-in type)");
                }
                { // nested decls, if any — under this package's prefix so an inner
                  // `augment class B` resolves the nested A::B
                    // …but a plain `class B { }` in the augment, where A::B is
                    // already complete, declares it a second time
                    for (auto& st : cd->body)
                        if (st && st->kind == NK::ClassDecl) {
                            auto* nd = static_cast<ClassDecl*>(st.get());
                            if (nd->isAugment || nd->isStubDecl || nd->name.empty()) continue;
                            auto pit = classes_.find(cd->name + "::" + nd->name);
                            if (pit != classes_.end() && pit->second && pit->second->decl &&
                                pit->second->decl != nd && !pit->second->decl->isStubDecl)
                                throwTyped("X::Redeclaration", {{"symbol", nd->name}},
                                           "Redeclaration of symbol '" + nd->name + "'");
                        }
                    std::string savedPrefix = tctx_.pkgPrefix;
                    tctx_.pkgPrefix = cd->name + "::";
                    try { for (auto& st : cd->body) exec(st.get()); }
                    catch (...) { tctx_.pkgPrefix = savedPrefix; throw; }
                    tctx_.pkgPrefix = savedPrefix;
                }
                return Value::typeObj(cd->name);
            }
            if (cd->isPackage) {
                // `module M is NoSuch {}` — a parent nobody declared (Rakudo:
                // X::Inheritance::UnknownParent, as for a class)
                if (!cd->name.empty() && !cd->parent.empty() && !classes_.count(cd->parent) &&
                    !isKnownTypeName(cd->parent) && !pkgKind_.count(cd->parent) &&
                    !subsets_.count(cd->parent) && !(tctx_.cur && tctx_.cur->find(cd->parent))) {
                    auto sug = typeSuggestions(cd->parent);
                    Value sl = Value::array(); sl.isList = true;
                    for (auto& n : sug) sl.arr()->push_back(Value::str(n));
                    throwTypedV("X::Inheritance::UnknownParent",
                        {{"child", Value::str(cd->name)}, {"parent", Value::str(cd->parent)}, {"suggestions", sl}},
                        "'" + cd->name + "' cannot inherit from '" + cd->parent + "' because it is unknown" +
                        didYouMean(sug));
                }
                if (!cd->name.empty()) {
                    signed char k = cd->isModuleDecl ? 1 : 2;
                    if (!cd->isMy && lexicalPkgDepth_ == 0) stashDeclare(tctx_.pkgPrefix + cd->name, !cd->isModuleDecl);
                    else if (stashUnitHere().empty()) {
                        auto& slot = unitStash_[""].lexical[cd->name];
                        if (!slot) { stashNodes_.emplace_back(); slot = &stashNodes_.back(); slot->fq = cd->name; }
                    }
                    lastDecl_[cd->name] = {cd, true, cd->isStubDecl};
                    pkgKind_[tctx_.pkgPrefix + cd->name] = k;
                    if (!tctx_.pkgPrefix.empty()) pkgKind_[cd->name] = k;
                    if (!cd->pod.empty()) {
                        pkgPod_[tctx_.pkgPrefix + cd->name] = cd->pod;
                        if (!tctx_.pkgPrefix.empty()) pkgPod_[cd->name] = cd->pod;
                        pkgPodTrail_[tctx_.pkgPrefix + cd->name] = cd->podTrail;
                        if (!tctx_.pkgPrefix.empty()) pkgPodTrail_[cd->name] = cd->podTrail;
                    }
                }
                // name adverbs, literal or computed: `module Zef:ver($?DISTRIBUTION…)`
                if (!cd->name.empty() &&
                    (!cd->ver.empty() || !cd->auth.empty() || !cd->api.empty() ||
                     cd->verExpr || cd->authExpr || cd->apiExpr)) {
                    PkgMeta pm;
                    pm.ver  = cd->verExpr  ? eval(cd->verExpr.get()).toStr()  : cd->ver;
                    pm.auth = cd->authExpr ? eval(cd->authExpr.get()).toStr() : cd->auth;
                    pm.api  = cd->apiExpr  ? eval(cd->apiExpr.get()).toStr()  : cd->api;
                    pkgMeta_[tctx_.pkgPrefix + cd->name] = pm;
                    if (!tctx_.pkgPrefix.empty()) pkgMeta_[cd->name] = pm;
                }
                // file-scoped `unit module Foo;` (no body of its own): register the
                // name and set the package prefix so the rest of the file's `our
                // sub`s / `our` vars publish under qualified names (Foo::name). The
                // prefix persists to end-of-compunit; loadModule save/restores it so
                // it can't leak. An EMPTY BRACED body is not this form — it is a
                // namespace with nothing in it — and asking `body.empty()` alone
                // could not tell them apart, so `module foo {}` set the prefix and
                // named everything after it `foo::…`.
                // `module StubC { ... }` — a forward declaration: nothing runs
                if (cd->isStubDecl) {
                    if (!cd->name.empty() && !tctx_.cur->local(cd->name))
                        tctx_.cur->define(cd->name, Value::typeObj(cd->name));
                    return Value::typeObj(cd->name);
                }
                if (cd->body.empty() && !cd->bracedBody) {
                    if (!cd->name.empty()) {
                        unitPkgByFile_[declFileNow()] = tctx_.pkgPrefix + cd->name;   // CLIENT:: asks
                        tctx_.cur->define(cd->name, Value::typeObj(cd->name));
                        tctx_.pkgPrefix += cd->name + "::";
                        if (curPkgEnv_ == global_) curPkgEnv_ = tctx_.cur; // `our` installs here
                    }
                    return Value::any();
                }
                // braced `module Foo { ... }`: run body in a child scope, then publish
                // its symbols globally under qualified names ($Foo::bar, &Foo::sub).
                if (!cd->name.empty()) tctx_.cur->define(cd->name, Value::typeObj(tctx_.pkgPrefix + cd->name));
                // a package is OUR-scoped unless declared `my`: declared in a block,
                // `::Test1` still finds it from anywhere
                if (!cd->name.empty() && !cd->isMy && global_ && tctx_.cur != global_ &&
                    tctx_.pkgPrefix.empty() && !global_->local(cd->name))
                    global_->define(cd->name, Value::typeObj(cd->name));
                std::string savedPrefix = tctx_.pkgPrefix;
                tctx_.pkgPrefix += cd->name + "::";
                auto pkgEnv = std::make_shared<Env>();
                pkgEnv->parent = tctx_.cur;
                pkgEnv->packageFrame = true;
                auto saved = tctx_.cur; tctx_.cur = pkgEnv;
                auto savedPkg = curPkgEnv_; curPkgEnv_ = pkgEnv; // `our` inside the package installs here (published qualified)
                // (inside a `my package`, the types it declares are not package-scoped)
                struct LexPkg { int& d; bool on; LexPkg(int& x, bool o) : d(x), on(o) { if (on) d++; }
                                ~LexPkg() { if (on) d--; } } lexPkg{lexicalPkgDepth_, cd->isMy};
                hoistSubs(cd->body); // forward refs: Cro::HTTP::Router calls router-plugin-register long before its definition
                // classes/roles register FIRST (Rakudo declares types at compile
                // time): `our $p = router-plugin-register('link')` at the top of
                // Cro::HTTP::Router news a PluginKey declared 1400 lines later
                for (auto& st : cd->body)
                    if (st->kind == NK::ClassDecl && !static_cast<ClassDecl*>(st.get())->isAugment)
                        exec(st.get());
                for (auto& st : cd->body)
                    if (!(st->kind == NK::ClassDecl && !static_cast<ClassDecl*>(st.get())->isAugment))
                        exec(st.get());
                tctx_.cur = saved; curPkgEnv_ = savedPkg;
                // …and in a role parameterization's generic statements the
                // package's types are reachable by their package-qualified
                // names FROM HERE, lexically: `my package G { class A is
                // Array[T] {} }` makes `G::A` this parameterization's
                // R::G::A[Int], which no global name can tell from R[Str]'s
                if (roleInstNames_ && !cd->name.empty())
                    for (auto& kv : pkgEnv->vars)
                        if (kv.second.t == VT::Type && !kv.first.empty() &&
                            (ascii::isalpha((unsigned char)kv.first[0]) || kv.first[0] == '_'))
                            tctx_.cur->define(cd->name + "::" + kv.first, kv.second);
                // Only `our`-declared sigil vars are visible by qualified name; `my` stays lexical.
                // …and so does a routine declared without `our` — `package P { sub f {} }`
                // leaves `P::f` unfound, an `is export` one included (it is in
                // P::EXPORT, which is what `import` reads) — and a `my constant`
                std::set<std::string> ourVars, lexicalOnly;
                for (auto& st : cd->body) {
                    if (st->kind == NK::SubDecl) {
                        auto* sd = static_cast<SubDecl*>(st.get());
                        if (!sd->isOur && !sd->isMethod && !sd->name.empty()) lexicalOnly.insert("&" + sd->name);
                        continue;
                    }
                    Expr* e = st->kind == NK::ExprStmt ? static_cast<ExprStmt*>(st.get())->e.get() : nullptr;
                    if (e && e->kind == NK::Assign) e = static_cast<Assign*>(e)->target.get();
                    if (e && e->kind == NK::VarExpr) {
                        auto* v = static_cast<VarExpr*>(e);
                        if (v->declare && v->declScope == "our") ourVars.insert(v->name);
                        if (v->declMyConstant) lexicalOnly.insert(v->name);
                    }
                }
                for (auto& kv : pkgEnv->vars) {
                    const std::string& sym = kv.first;
                    bool sigilVar = !sym.empty() && (sym[0]=='$'||sym[0]=='@'||sym[0]=='%');
                    if (sigilVar && !ourVars.count(sym)) continue; // a `my` package var — not published
                    if (lexicalOnly.count(sym)) continue;
                    std::string qual;
                    if (!sym.empty() && (sym[0]=='$'||sym[0]=='@'||sym[0]=='%'||sym[0]=='&'))
                        qual = std::string(1, sym[0]) + tctx_.pkgPrefix + sym.substr(1);
                    else qual = tctx_.pkgPrefix + sym;
                    noteSymbolMutation("package-qualified global (our)");
                    // A COPY here gives `module M { our $v }` two containers —
                    // the one M's own subs read, and the one `$M::v = 4` writes.
                    // That is the split the our-declaration publish removes by
                    // installing a VIEW onto the package's slot, and this loop
                    // ran afterwards and copied the value straight back over it:
                    // the same trap the module-load republish already knows to
                    // step around. A `class` never hit it, because a class body
                    // has no republish of its own — which is why the very fix
                    // that made `$CK::v` one container left `$MK::v` two.
                    // The view is installed here rather than merely preserved,
                    // so a BARE `our $v;` — which never reaches the publish in
                    // evalAssign, having no initialiser to assign — is one
                    // container too.
                    if (sigilVar) global_->define(qual, makeEnvSlotProxy(pkgEnv, sym));
                    else global_->define(qual, kv.second);
                }
                // During a `use` load, `is export` subs of a braced module also
                // surface BARE to the importer (same-file code still sees only
                // the qualified names, like Rakudo).
                if (loadingModuleDepth_ > 0 && moduleDoImport_) {
                    std::function<void(const std::vector<StmtPtr>&)> surface =
                        [&](const std::vector<StmtPtr>& body) {
                        for (auto& st : body) {
                            if (st->kind == NK::SubDecl) {
                                auto* sd = static_cast<SubDecl*>(st.get());
                                if (sd->isExport && !sd->name.empty()) {
                                    auto it = pkgEnv->vars.find("&" + sd->name);
                                    if (it != pkgEnv->vars.end())
                                        tctx_.cur->define(it->first, it->second);
                                }
                            }
                            // `enum LEVEL is export <FATAL ERROR …>` inside the braced
                            // body: the importer must get the VALUES, not just the type.
                            // Without this the names still resolved — as bare type
                            // objects — so `DEBUG <= INFO` compared two undefined things
                            // and answered True, which is how zef's log filter let every
                            // DEBUG message through (Zef.rakumod declares its LEVEL/
                            // STAGE/PHASE enums inside `package Zef { … }`).
                            else if (st->kind == NK::EnumDecl) {
                                auto* ed = static_cast<EnumDecl*>(st.get());
                                if (!ed->isExport || ed->name.empty()) continue;
                                auto tv = pkgEnv->vars.find(ed->name);
                                if (tv == pkgEnv->vars.end()) continue;
                                tctx_.cur->define(ed->name, tv->second);
                                if (tv->second.t == VT::Array && tv->second.arr())
                                    for (auto& p : *tv->second.arr()) {
                                        auto vi = pkgEnv->vars.find(p.s);
                                        if (vi != pkgEnv->vars.end())
                                            tctx_.cur->define(p.s, vi->second);
                                    }
                            }
                        }
                    };
                    surface(cd->body);
                }
                tctx_.pkgPrefix = savedPrefix;
                // the declaration's VALUE is the package: `my $p = my package P {}`
                if (!cd->name.empty()) return Value::typeObj(savedPrefix + cd->name);
                return Value::any();
            }
            auto ci = std::make_shared<ClassInfo>();
            // `is NAME` where NAME is no type may be a USER TRAIT (`trait_mod:<is>`,
            // e.g. JSON::Class's `is x-wrapper`). It cannot be dispatched here —
            // the trait body typically does `type.^add_role(...)`, which needs the
            // class REGISTERED (classes_[clsName], far below) — so unknown parents
            // are collected and offered to the trait_mod after registration.
            std::vector<std::string> pendingIsTraits;
            std::set<std::string> roleIsTraits;   // …of those, a role's: unhandled is not an error
            // A `unit role`/`unit class` file keeps its `use` statements INSIDE the
            // body (the body is the rest of the file), but parent/role resolution
            // below needs those modules loaded NOW: `unit role JX::Attr; use
            // JX::Descriptor; also does JX::Descriptor;` resolved the parent before
            // the use ran, silently got null (tolerated for roles), and the
            // composing class then failed "Attribute $!declarant not declared"
            // (JSON::Class's whole Attr chain is built this way). Loads are cached,
            // so the body's own execution of the same `use` later is a no-op.
            for (auto& bs : cd->body)
                if (bs && bs->kind == NK::UseStmt) exec(bs.get());
            // anonymous `role {…}` / `class {…}` literals get a synthesized name so
            // they can be registered, mixed in (`does`/`but`), and introspected.
            // an unqualified class nested in a class/package body registers under
            // its QUALIFIED name (`class GenericActions` inside `class Cro::Uri`
            // is Cro::Uri::GenericActions, as in Rakudo); the tail alias keeps the
            // short name resolvable
            // A COMPOUND name nests the same way: `grammar Schema::Core` inside
            // `unit module YAMLish` is YAMLish::Schema::Core, so an importer can
            // reach it. (Only a name that already carries the prefix is left alone.)
            // `class ::(EXPR)` — the INDIRECT name, computed now (declarations
            // run at run time here, so this is just an expression slot)
            std::string dynName;
            if (cd->name.empty() && cd->nameExpr) {
                dynName = eval(cd->nameExpr.get()).toStr();
                if (dynName.empty())
                    throw RakuError{Value::typeObj("X::Syntax::Name::Null"),
                                    "an indirect type name evaluated to an empty string"};
            }
            const std::string& declName = dynName.empty() ? cd->name : dynName;
            std::string clsName = declName.empty()
                ? "<anon|" + std::to_string(++anonTypeCounter_) + ">"
                : (!tctx_.pkgPrefix.empty() && declName.rfind(tctx_.pkgPrefix, 0) != 0
                    ? tctx_.pkgPrefix + declName : declName);
            // A class a parametric role's generic statements declare, whose
            // PARENT is parameterized by a role parameter, is a class of this
            // parameterization's own, named for the parent's arguments:
            // `class A is Array[T]` in R[Int] is R::G::A[Int] (and
            // `class H is Hash[V, K]` in R2[Str, Int] is R2::G::H[Int,Str]).
            // A class that only USES a parameter keeps its plain name.
            bool genericInstance = false;
            // (not when the parent is a ROLE: `is P2[T]` inherits that role's pun,
            // and the class keeps its own name)
            bool parentIsRole = false;
            if (roleInstNames_ && !cd->parent.empty()) {
                auto pit = classes_.find(lexicalTypeName(*this, cd->parent));
                if (pit == classes_.end()) pit = classes_.find(resolveClassAlias(cd->parent));
                parentIsRole = pit != classes_.end() && pit->second && pit->second->isRole;
            }
            if (roleInstNames_ && !parentIsRole && !cd->isRole && !cd->parent.empty() && !cd->parentIsDoes) {
                for (auto& ra : cd->roleArgs) {
                    if (ra.first != cd->parent || ra.second.empty()) continue;
                    NameScan ns;
                    for (auto& e : ra.second) ns.expr(e.get());
                    bool uses = false;
                    for (auto& r : ns.refs)
                        if (taintedName(*roleInstNames_, r)) { uses = true; break; }
                    if (uses) {
                        ValueList av;
                        bool ok = true;
                        try { for (auto& e : ra.second) av.push_back(eval(e.get())); }
                        catch (RakuError&) { ok = false; }
                        if (ok && !av.empty()) {
                            const std::string sfx = roleArgsDisplay("", av);
                            if (!sfx.empty()) { clsName += sfx; genericInstance = true; }
                        }
                    }
                    break;
                }
            }
            ci->name = clsName;
            ci->pod = cd->pod; ci->podTrail = cd->podTrail;
            // A type composing a parametric role gets its own parameterization
            // of it, one per `does` (concretizeRole): the arguments of the k-th
            // `does` naming the role, evaluated here and kept for the type's
            // own bindings further down. A PARAMETRIC role composing one keeps
            // sharing it: its arguments may name its own parameters, which
            // nothing has bound yet. An argument that does not evaluate leaves
            // the role shared, and the binding further down reports it as it
            // always did.
            // The SAME parameterization written twice is one role (`does R[Int]
            // does R[Int]` composes it once, as in Rakudo), keyed as the pun
            // cache keys one; an argument without a stable identity is not.
            std::map<const ClassInfo*, ValueList> concArgv;
            std::map<std::string, std::shared_ptr<ClassInfo>> concSeen;
            auto concretizeOccurrence = [&](const std::shared_ptr<ClassInfo>& role, const std::string& written,
                                            size_t k) -> std::shared_ptr<ClassInfo> {
                if ((cd->isRole && !cd->roleParams.empty()) || !role || !role->isRole || !role->decl ||
                    role->decl->roleParams.empty())
                    return role;
                ValueList argv;
                size_t seen = 0;
                try {
                    for (auto& ra : cd->roleArgs) {
                        if (ra.first != written || seen++ != k) continue;
                        for (auto& e : ra.second) {
                            Value v = eval(e.get());
                            if (syntacticNamedPair(&*e)) v.namedArg = true; // `does R[:opt]`
                            argv.push_back(std::move(v));
                        }
                        break;
                    }
                } catch (RakuError&) { return role; }
                std::string key = std::to_string(reinterpret_cast<uintptr_t>(role.get()));
                for (auto& a : argv) {
                    if (a.t == VT::Code || a.t == VT::Object) { key.clear(); break; }
                    key += "\x01" + whichOf(a);
                }
                if (!key.empty()) {
                    auto hit = concSeen.find(key);
                    if (hit != concSeen.end()) return hit->second;
                }
                auto conc = concretizeRole(role, argv, role->declEnv);
                if (conc != role) concArgv[conc.get()] = std::move(argv);
                if (!key.empty()) concSeen[key] = conc;
                return conc;
            };
            if (!cd->parent.empty()) {
                // `is CORE::Exception` — the CORE:: qualifier names the SETTING's
                // symbol, which is exactly what a bare name resolves to for us;
                // Getopt::Long spells it that way because ITS OWN class is also
                // called Exception. Strip the prefix before every lookup.
                std::string parentName = cd->parent.rfind("CORE::", 0) == 0
                                       ? cd->parent.substr(6) : cd->parent;
                // a type may not inherit from / compose itself:  class A is A / role A does A
                // …but a PARAMETERIZED one is a different role: `role R does R['x']`
                // composes R['x'], not R. PDF::COS::ByteString is declared exactly
                // that way — a bare role over its own default parameterization.
                bool paramSelf = false;
                for (auto& ra : cd->roleArgs)
                    if (ra.first == parentName && !ra.second.empty()) { paramSelf = true; break; }
                if (parentName == cd->name && !cd->name.empty() && parentName == cd->parent && !paramSelf)
                    throwTyped(cd->isRole ? "X::InvalidType" : "X::Inheritance::SelfInherit",
                        {{"name", cd->name}},
                        cd->isRole ? "Role '" + cd->name + "' cannot inherit from / compose itself"
                                   : "'" + cd->name + "' cannot inherit from itself.");   // Rakudo's wording
                auto it = classes_.find(lexicalTypeName(*this, parentName));
                if (it == classes_.end() && !tctx_.pkgPrefix.empty())
                    it = classes_.find(tctx_.pkgPrefix + parentName); // sibling nested type
                if (it == classes_.end()) it = classes_.find(resolveClassAlias(parentName));
                // `does X` where X is a class or a concrete core type: only
                // roles compose (built-in role names like Positional stay fine)
                if (cd->parentIsDoes) {
                    static const std::set<std::string> kConcreteTy = {
                        "Int", "Str", "Num", "Rat", "Bool", "Complex", "Array", "Hash"};
                    if ((it != classes_.end() && !it->second->isRole) ||
                        (it == classes_.end() && kConcreteTy.count(cd->parent)))
                        throwTypedV("X::Composition::NotComposable",
                            {{"target-name", Value::str(clsName)},
                             {"composer", Value::typeObj(cd->parent)}},
                            cd->parent + " is not composable, so " + clsName +
                            " cannot compose it");
                }
                // …and the RakuAST registry, which lives outside `classes_` on
                // purpose (see RakuAstClasses.h) and so was reachable from every
                // name position EXCEPT this one. FINALIZER subclasses a phaser
                // node — `my class LeavePhaser is
                // RakuAST::StatementPrefix::Phaser::Leave` — and got "cannot
                // inherit from … because it is unknown" for a name that
                // resolves fine one line earlier.
                const std::shared_ptr<ClassInfo>* rakuAstParent =
                    (it == classes_.end() && rakuAstVisible() && isRakuAstName(cd->parent))
                        ? rakuAstClass(cd->parent) : nullptr;
                // `my package A { }; my class B is A { }` — the latest A is a
                // package, whatever class an earlier A was
                if (!cd->parentIsDoes && nameIsPackageNow(cd->parent))
                    throwTypedV("X::Inheritance::Unsupported",
                        {{"child-typename", Value::str(cd->name)}, {"parent", Value::typeObj(cd->parent)}},
                        std::string(pkgKind_[cd->parent] == 1 ? "module" : "package") + " " + cd->parent +
                        " does not support inheritance, so " + cd->name + " cannot inherit from it");
                if (it != classes_.end()) {
                    ci->parent = it->second;
                    // `does R[a, b]` as the first composition: the candidate of
                    // R's group whose signature takes that many arguments
                    if (it->second->isRole && !it->second->roleVariants.empty()) {
                        size_t n = 0; const std::vector<ExprPtr>* rargs = nullptr;
                        for (auto& ra : cd->roleArgs) if (ra.first == cd->parent) { n = ra.second.size(); rargs = &ra.second; break; }
                        ci->parent = rargs ? pickRoleVariantArgs(it->second, *rargs) : pickRoleVariant(it->second, n);
                    }
                    if (cd->parentIsDoes) ci->parent = concretizeOccurrence(ci->parent, cd->parent, 0);
                    // `is R[Str]`: a parametric role as a PARENT is its pun
                    else if (ci->parent->isRole && ci->parent->decl && !ci->parent->decl->roleParams.empty()) {
                        const std::vector<ExprPtr>* rargs = nullptr;
                        for (auto& ra : cd->roleArgs) if (ra.first == cd->parent) { rargs = &ra.second; break; }
                        if (rargs && !rargs->empty()) {
                            ValueList av;
                            bool ok = true;
                            try {
                                for (auto& e : *rargs) {
                                    Value v = eval(e.get());
                                    if (syntacticNamedPair(&*e)) v.namedArg = true;
                                    av.push_back(std::move(v));
                                }
                            } catch (RakuError&) { ok = false; }
                            if (ok) {
                                Value pun = makeRolePun(ci->parent.get(), cd->parent, av);
                                auto pc = classes_.find(pun.s);
                                if (pc != classes_.end() && pc->second) ci->parent = pc->second;
                            }
                        }
                    }
                }
                else if (rakuAstParent) ci->parent = *rakuAstParent;
                else if (isKnownTypeName(cd->parent)) {
                    ci->nativeParent = cd->parent; // is Str / is Cool / …
                    // …`is Array[Str]` keeps the element type (S05-grammar/inheritance.t)
                    for (auto& ra : cd->roleArgs)
                        if (ra.first == cd->parent && ra.second.size() == 1 && ra.second[0])
                            try { Value tv = eval(ra.second[0].get()); if (tv.t == VT::Type) ci->nativeOf = tv.s; }
                            catch (RakuError&) {}
                }
                else if (!cd->isRole && !cd->parentIsDoes)
                    pendingIsTraits.push_back(cd->parent);
                // a ROLE's `is name` that names no type is offered to
                // `trait_mod:<is>` too (`role Loser is description {}`,
                // S14-traits/package.t) — but one no candidate takes is still
                // dropped, as it always was for a role
                else if (cd->isRole && !cd->parentIsDoes && !cd->parent.empty() &&
                         ascii::islower((unsigned char)cd->parent[0])) {
                    pendingIsTraits.push_back(cd->parent);
                    roleIsTraits.insert(cd->parent);
                }
            }
            for (auto& tn : cd->trustsNames) ci->trusts.insert(tn);
            ci->hidden = cd->isHidden;
            ci->hides = cd->hidesNames;
            // additional `is Parent` targets — multiple inheritance
            for (auto& pn : cd->extraParents) {
                if (pn == cd->name)
                    throw RakuError{Value::typeObj("X::Inheritance::SelfInherit"),
                        "Class '" + cd->name + "' cannot inherit from itself"};
                auto it = classes_.find(lexicalTypeName(*this, pn));
                if (it == classes_.end() && !tctx_.pkgPrefix.empty())
                    it = classes_.find(tctx_.pkgPrefix + pn);
                if (it == classes_.end()) it = classes_.find(resolveClassAlias(pn));
                const std::shared_ptr<ClassInfo>* rakuAstExtra =
                    (it == classes_.end() && rakuAstVisible() && isRakuAstName(pn))
                        ? rakuAstClass(pn) : nullptr;
                if (it != classes_.end()) ci->extraParents.push_back(it->second);
                else if (rakuAstExtra) ci->extraParents.push_back(*rakuAstExtra);
                // A BUILT-IN parent that arrives here rather than in the parent slot:
                // `class C does R is Str` puts the role in the slot first, so the
                // `is Str` lands among the extras and was dropped — the class was
                // not a Str at all. PDF::COS::TextString is declared
                // `also does PDF::COS; also is Str;` and could not bind its own
                // `sub pdfdoc-encode(Str $str)`.
                else if (isKnownTypeName(pn)) {
                    if (ci->nativeParent.empty()) ci->nativeParent = pn;
                }
                else pendingIsTraits.push_back(pn);
            }
            if (!cd->isRole && hasMultipleInheritance(ci.get())) {
                bool ok = true;
                c3Linearize(ci.get(), ok);
                if (!ok)
                    throw RakuError{Value::typeObj("X::Inheritance::Unsupported"),
                        "Could not build C3 linearization for " + cd->name + ": ambiguous hierarchy"};
            }
            // -------- role composition helpers --------
            // a stub body is a bare `...` / `!!!` — in a role it declares a
            // requirement the composing class must fulfil
            // (`owner`, when given, is the routine the parameters belong to: one
            // parameterization of a role keys its `T $x` as the type it bound, so
            // R[Str]'s and R[Int]'s `multi method m(T $x)` are two signatures)
            auto sigKeyParams = [](const std::vector<Param>* ps, const Callable* owner = nullptr) {
                std::string k;
                if (ps) for (auto& p : *ps) {
                    if (p.named || p.slurpy) continue;
                    if (owner && owner->roleConcrete && owner->closure && !p.type.empty())
                        if (Value* tv = owner->closure->local(p.type))
                            if (tv->t == VT::Type && !tv->s.empty()) {
                                k += std::string(tv->s.c_str()) + ",";
                                continue;
                            }
                    k += (p.type.empty() ? "Any" : p.type);
                    // a literal or `where` constraint makes it a DIFFERENT
                    // candidate: `multi f(1)` in one role and `multi f(3)` in
                    // another do not conflict (APPENDICES/A01-limits/misc.t)
                    if (p.litVal || p.whereExpr) {
                        char buf[40];
                        std::snprintf(buf, sizeof buf, "|%p|%p", (const void*)p.litVal.get(), (const void*)p.whereExpr.get());
                        k += buf;
                    }
                    k += ",";
                }
                return k;
            };
            auto codeIsStub = [](const Value& v) -> bool {
                if (v.t != VT::Code || !v.code()) return false;
                if (v.code()->isMultiDispatcher) {
                    if (v.code()->candidates.empty()) return true;
                    for (auto& c : v.code()->candidates) if (!(c.code() && c.code()->isStub)) return false;
                    return true;
                }
                return v.code()->isStub;
            };
            // A role method that declares `state` gets its OWN copy per composing
            // class. `state` is per-closure, and each composition is a distinct
            // closure — sharing the role's Callable made ONE class's memo answer
            // for every other. META6's AutoAssoc caches its json-name→attribute
            // map in `state %lookup`, so META6's table answered META6::Support's
            // subscripts and `$obj<support><source>` came back empty. Only bodies
            // that actually declare `state` are copied; everything else keeps
            // sharing, which is what makes composition cheap.
            auto cloneDispatcher = [](const Value& v) {
                Value nv = v;
                auto fresh = makePayload<Callable>();
                fresh->pkg = v.code()->pkg; fresh->name = v.code()->name;
                fresh->isMultiDispatcher = true; fresh->isProto = v.code()->isProto;
                fresh->isMethod = v.code()->isMethod;
                fresh->candidates = v.code()->candidates;
                nv.setCode(fresh);
                return nv;
            };
            // every role this type composes (the first `does` lands as parent)
            std::vector<ClassInfo*> composedRoles;
            if (ci->parent && ci->parent->isRole) composedRoles.push_back(ci->parent.get());
            for (auto& p : ci->extraParents) if (p && p->isRole) composedRoles.push_back(p.get());
            // …each `does` of cd->roles as the variant and parameterization ITS
            // arguments pick: the k-th `does R` takes the k-th `R[...]` (the
            // parent slot, when it is an R, took the first)
            std::vector<std::shared_ptr<ClassInfo>> rolesConc(cd->roles.size());
            for (size_t j = 0; j < cd->roles.size(); j++) {
                const std::string& rn = cd->roles[j];
                auto it = classes_.find(lexicalTypeName(*this, rn));
                if (it == classes_.end() && !tctx_.pkgPrefix.empty())
                    it = classes_.find(tctx_.pkgPrefix + rn); // `does Handler` where the role is a sibling nested type
                if (it == classes_.end()) it = classes_.find(resolveClassAlias(rn));
                if (it != classes_.end() && it->second->isRole) {
                    size_t k = cd->parentIsDoes && cd->parent == rn ? 1 : 0;
                    for (size_t q = 0; q < j; q++) if (cd->roles[q] == rn) k++;
                    const std::vector<ExprPtr>* rargs = nullptr;
                    {
                        size_t seen = 0;
                        for (auto& ra : cd->roleArgs)
                            if (ra.first == rn && seen++ == k) { rargs = &ra.second; break; }
                    }
                    std::shared_ptr<ClassInfo> r = it->second;
                    if (!r->roleVariants.empty())
                        r = rargs ? pickRoleVariantArgs(it->second, *rargs) : pickRoleVariant(it->second, 0);
                    r = concretizeOccurrence(r, rn, k);
                    rolesConc[j] = r;
                    composedRoles.push_back(r.get());
                }
            }
            // …and the roles a parameterized role DOES through its type
            // parameter: `role RR[::T] does T; class C does RR[Foo]` does Foo
            std::vector<std::string> viaParamRoles;
            {
                std::vector<ClassInfo*> extra;
                for (ClassInfo* r : composedRoles) {
                    if (!r->decl || r->decl->roleParams.empty()) continue;
                    std::vector<std::string> rdoes = r->decl->roles;
                    // (`role RC[::T] is T` — the INHERITANCE rides the same way)
                    if (!r->decl->parent.empty()) rdoes.push_back(r->decl->parent);
                    for (auto& ep : r->decl->extraParents) rdoes.push_back(ep);
                    const std::vector<ExprPtr>* rargs = nullptr;
                    for (auto& ra : cd->roleArgs)
                        if (ra.first == r->name || r->name.size() > ra.first.size() &&
                            r->name.compare(r->name.size() - ra.first.size(), ra.first.size(), ra.first) == 0)
                            { rargs = &ra.second; break; }
                    if (!rargs) continue;
                    for (auto& dn : rdoes) {
                        size_t pi = 0; bool found = false;
                        for (auto& p : r->decl->roleParams) {
                            if (p.named) continue;
                            if (p.typeCapture && (p.captureName == dn || p.type == dn)) { found = true; break; }
                            pi++;
                        }
                        if (!found || pi >= rargs->size()) continue;
                        Value tv;
                        try { tv = eval((*rargs)[pi].get()); } catch (RakuError&) { continue; }
                        if (tv.t != VT::Type) continue;
                        auto tit = classes_.find(tv.s);
                        if (tit != classes_.end() && tit->second && tit->second->isRole) {
                            extra.push_back(tit->second.get());
                            viaParamRoles.push_back(tit->first);
                        }
                        else if (tit != classes_.end() && tit->second && !tit->second->isRole) {
                            bool have = false;
                            for (auto& ep : ci->extraParents) if (ep == tit->second) have = true;
                            if (!have && ci->parent != tit->second) ci->extraParents.push_back(tit->second);
                        }
                    }
                }
                for (ClassInfo* x : extra) {
                    bool have = false;
                    for (ClassInfo* r : composedRoles) if (r == x) have = true;
                    if (!have) composedRoles.push_back(x);
                }
            }
            // conflict detection: the same method (same signature for multis)
            // provided — as a real implementation — by two different roles must be
            // resolved by the class's own method (diamonds share the Callable, so
            // pointer identity exempts them)
            std::map<std::string, std::map<std::string, std::set<const Callable*>>> provided;
            std::map<std::string, std::set<std::string>> providerRoles;
            for (ClassInfo* role : composedRoles) {
                // (two parameterizations of one role are two providers: R[Str], R[Int])
                auto ca = concArgv.find(role);
                const std::string provider = ca != concArgv.end() && !ca->second.empty()
                                           ? roleArgsDisplay(role->name, ca->second) : role->name;
                for (auto& kv : role->methods) {
                    if (kv.second.t != VT::Code || !kv.second.code()) continue;
                    // since 6.e a role's SUBMETHODS are not composed into the class
                    // at all, so two roles carrying the same one do not conflict
                    if ((langRev_ >= 2 || role->langRev >= 2) && kv.second.code()->isSubmethod) continue;
                    if (kv.second.code()->isMultiDispatcher) {
                        for (auto& c : kv.second.code()->candidates)
                            if (c.code() && !c.code()->isStub) { provided[kv.first][sigKeyParams(c.code()->params, c.code())].insert(c.code()); providerRoles[kv.first].insert(provider); }
                    }
                    else if (!kv.second.code()->isStub) { provided[kv.first][""].insert(kv.second.code()); providerRoles[kv.first].insert(provider); }
                }
            }
            // `state` in a role method is per-COMPOSITION: two classes doing one
            // role each get their own slot, as they do in Rakudo where each
            // composition is a distinct closure. Sharing made one class's memo
            // answer for every other — META6's AutoAssoc caches its json-name →
            // attribute map in `state %lookup`, so META6's table answered
            // META6::Support's subscripts and `$obj<support><source>` read empty.
            // The Callable stays shared (it holds a once_flag and the native-call
            // caches), so the slot hangs off the CLASS, keyed by that Callable;
            // invokeMethod prefers it. Driven from `composedRoles`, which is the
            // list that includes a role arriving as the PARENT — `class A does R`
            // puts R there, not in cd->roles.
            {
                std::function<void(const Value&)> noteRoleState = [&](const Value& v) {
                    if (v.t != VT::Code || !v.code()) return;
                    if (v.code()->isMultiDispatcher) {
                        for (auto& cand : v.code()->candidates) noteRoleState(cand);
                        return;
                    }
                    if (!v.code()->body || ci->roleStateEnvs.count(v.code())) return;
                    bool anyState = false;
                    for (auto& st : *v.code()->body)
                        if (mayHaveStateDecl(st.get())) { anyState = true; break; }
                    if (!anyState) return;
                    auto e = std::make_shared<Env>();
                    e->parent = v.code()->closure ? v.code()->closure : global_;
                    e->stateFrame = true;
                    ci->roleStateEnvs[v.code()] = e;
                };
                for (ClassInfo* role : composedRoles)
                    for (auto& kv : role->methods) noteRoleState(kv.second);
            }
            std::set<std::string> conflicted;
            // a conflict needs providers from TWO DIFFERENT roles: one role's own
            // multi candidates can collide on our sig key (invocant-only sigs like
            // `(::?CLASS:U:)` vs `(::?CLASS:D:)` both key as "") without being a
            // conflict at all — JSON::Class's Descriptor tripped this on itself
            for (auto& kv : provided)
                for (auto& sk : kv.second)
                    if (sk.second.size() > 1 && providerRoles[kv.first].size() > 1)
                        conflicted.insert(kv.first);
            // compose additional `does Role`s: flatten their methods/attrs in (the
            // class's own methods, registered below, override by key). Multi
            // dispatchers are CLONED (never share the role's own dispatcher) and
            // merged per candidate; an implementation displaces a same-signature
            // stub, never the other way around.
            std::vector<std::string> composeNames = cd->roles;
            for (auto& vr : viaParamRoles)
                if (std::find(composeNames.begin(), composeNames.end(), vr) == composeNames.end())
                    composeNames.push_back(vr);
            for (size_t cj = 0; cj < composeNames.size(); cj++) {
                const std::string& rn = composeNames[cj];
                auto it = classes_.find(lexicalTypeName(*this, rn));
                // resolve the SAME way as the conflict-detection scan above: an
                // imported short name (`does Pluggable` where the role is really
                // `Mod::Pluggable`) needs the pkg-prefix / alias fallback, else its
                // methods are silently never composed.
                if (it == classes_.end() && !tctx_.pkgPrefix.empty())
                    it = classes_.find(tctx_.pkgPrefix + rn);
                if (it == classes_.end()) it = classes_.find(resolveClassAlias(rn));
                {   // only a role (or an unknown/built-in role name) composes;
                    // a class or concrete core type is X::Composition::NotComposable
                    static const std::set<std::string> kConcrete = {
                        "Int", "Str", "Num", "Rat", "Bool", "Complex", "Array", "Hash"};
                    bool bad = (it != classes_.end() && !it->second->isRole) ||
                               (it == classes_.end() && kConcrete.count(rn));
                    if (bad)
                        throwTypedV("X::Composition::NotComposable",
                            {{"target-name", Value::str(cd->name)},
                             {"composer", Value::typeObj(rn)}},
                            rn + " is not composable, so " + cd->name +
                            " cannot compose it");
                    // a role that is still only a stub (`role R { ... }`, never
                    // completed) has no variant to compose
                    if (it != classes_.end() && it->second->isRole && it->second->roleVariants.empty() &&
                        it->second->decl && it->second->decl->isStubDecl)
                        throwTyped("X::Role::Parametric::NoSuchCandidate", {{"role", rn}},
                                   "No appropriate parametric role variant available for '" + rn + "'");
                }
                ci->doneRoles.insert(rn); // record membership (for ~~ Role / .does), even if unknown
                // `class Foo does Maybe` for an ENUM Maybe: the class gets a
                // public `$.Maybe` (set by `.new(Maybe => No)`) and a method per
                // member answering whether it is the one held
                if (it == classes_.end()) {
                    Value en;
                    try {
                        NameTerm tn(rn);
                        Value et = eval(&tn);
                        if (et.t == VT::Array && !et.enumType.empty() && et.enumName.empty())
                            en = methodCall(et, "enums", ValueList{});
                    } catch (...) {}
                    if (en.t == VT::Hash && en.hash()) {
                        bool have = false;
                        for (auto& a : ci->attrs) if (a.name == rn) have = true;
                        if (!have) { ClassAttr ca; ca.name = rn; ca.sigil = '$'; ca.pub = true; ci->attrs.push_back(ca); }
                        const std::string attr = rn;
                        for (auto& kv : *en.hash()) {
                            const std::string key = kv.first;
                            Value m = Value::closure([attr, key](ValueList& a) -> Value {
                                if (a.empty() || a[0].t != VT::Object || !a[0].obj()) return Value::boolean(false);
                                auto at = a[0].obj()->attrs.find(attr);
                                return Value::boolean(at != a[0].obj()->attrs.end() &&
                                                      at->second.enumName.c_str() == key);
                            });
                            m.code()->isMethod = true;
                            if (!ci->methods.count(key)) ci->methods[key] = m;
                        }
                    }
                    continue;
                }
                // the variant and parameterization this `does` picked (above)
                std::shared_ptr<ClassInfo> rinfo =
                    cj < rolesConc.size() && rolesConc[cj] ? rolesConc[cj] : it->second;
                // …and everything the role composes THROUGH ITS OWN PARENT SLOT:
                // `role B does A` puts A there rather than in B's own tables, so a
                // class that takes B as its parent walks the chain and finds A's
                // methods — but a class whose parent slot is already spoken for
                // (`class D is Hash does B`) copies from B alone, and A's methods
                // and attributes were lost. PDF::COS::Tie::Hash does PDF::COS::Tie
                // and a PDF dictionary is Hash-backed, so the whole tie API
                // (`.lvalue`, `.of-att`) went missing exactly there.
                if (std::find(ci->composedRoles.begin(), ci->composedRoles.end(), rinfo) == ci->composedRoles.end())
                    ci->composedRoles.push_back(rinfo);
                std::vector<ClassInfo*> roleChain;
                {
                    std::set<ClassInfo*> seenRC;
                    std::function<void(ClassInfo*)> walkRole = [&](ClassInfo* rc) {
                        if (!rc || !rc->isRole || !seenRC.insert(rc).second) return;
                        roleChain.push_back(rc);
                        walkRole(rc->parent.get());
                        for (auto& p2 : rc->extraParents) walkRole(p2.get());
                    };
                    walkRole(rinfo.get());
                }
                for (ClassInfo* rcM : roleChain)
                for (auto& kv : rcM->methods) {
                    // a 6.e role's submethod is not composed into a class of
                    // an earlier revision either: its BUILD/TWEAK run as the
                    // ROLE's constructors (roleCtorHooks, runBuildChain)
                    if (!cd->isRole && langRev_ < 2 && rcM->langRev >= 2 && kv.second.t == VT::Code &&
                        kv.second.code() && kv.second.code()->isSubmethod)
                        continue;
                    bool newDisp = kv.second.t == VT::Code && kv.second.code() && kv.second.code()->isMultiDispatcher;
                    // remember which composed names are SUBMETHODS: 6.e hides them
                    // from ordinary dispatch while still running role BUILD/TWEAK
                    if (kv.second.t == VT::Code && kv.second.code() && kv.second.code()->isSubmethod) {
                        ci->roleSubmethods.insert(kv.first);
                        // a 6.e role's submethods, or any role's in a 6.e class, are not composed
                        if (langRev_ >= 2 || rcM->langRev >= 2) ci->roleSubmethodsHidden.insert(kv.first);
                    }
                    auto ex = ci->methods.find(kv.first);
                    if (ex == ci->methods.end()) {
                        ci->methods[kv.first] = newDisp ? cloneDispatcher(kv.second) : kv.second;
                        continue;
                    }
                    Value& e = ex->second;
                    if (e.code() == kv.second.code()) continue; // identical method (diamond)
                    bool exDisp = e.t == VT::Code && e.code() && e.code()->isMultiDispatcher;
                    if (exDisp && newDisp) {
                        for (auto& c : kv.second.code()->candidates) {
                            std::string sk = sigKeyParams(c.code() ? c.code()->params : nullptr, c.code());
                            bool cStub = c.code() && c.code()->isStub;
                            bool placed = false;
                            for (auto& e2 : e.code()->candidates) {
                                if (sigKeyParams(e2.code() ? e2.code()->params : nullptr, e2.code()) != sk) continue;
                                bool eStub = e2.code() && e2.code()->isStub;
                                if (eStub && !cStub) e2 = c; // implementation replaces stub
                                placed = true; break;
                            }
                            if (!placed) e.code()->candidates.push_back(c);
                        }
                        continue;
                    }
                    bool eStub = codeIsStub(e), nStub = codeIsStub(kv.second);
                    if (nStub) continue;                    // a stub never displaces
                    if (eStub) ci->methods[kv.first] = newDisp ? cloneDispatcher(kv.second) : kv.second;
                    // both real implementations: recorded in `conflicted` above
                }
                if (!cd->isRole)
                    for (ClassInfo* rcA : roleChain)
                        if (!rcA->roleAttrConflict.empty())
                            throw RakuError{Value::typeObj("X::Role::Attribute::Conflicts"), rcA->roleAttrConflict};
                for (ClassInfo* rcA : roleChain)
                for (auto& a : rcA->attrs) {
                    bool dup = false;
                    for (auto& ex : ci->attrs)
                        if (ex.name == a.name && ex.sigil == a.sigil) {
                            // the same declaration arriving twice (diamond) is fine;
                            // two distinct same-name declarations conflict in a class
                            if (!(ex.declId && a.declId && ex.declId == a.declId) && !cd->isRole)
                                throw RakuError{Value::typeObj("X::Role::Attribute::Conflicts"),
                                    "Attribute '" + std::string(1, a.sigil) + "!" + a.name +
                                    "' conflicts in " + std::string(cd->isRole ? "role" : "class") +
                                    " '" + clsName + "' composition: declared in both '" + rn + "' and another role"};
                            // …in a ROLE the duplicate is let through, and reported
                            // when the role is punned or composed into a class
                            if (!(ex.declId && a.declId && ex.declId == a.declId) && cd->isRole &&
                                ci->roleAttrConflict.empty())
                                ci->roleAttrConflict = "Attribute '" + std::string(1, a.sigil) + "!" + a.name +
                                    "' conflicts in role '" + clsName + "' composition: declared in both '" +
                                    clsName + "' and '" + rn + "'";
                            dup = true; break;
                        }
                    if (!dup) ci->attrs.push_back(a);
                }
                for (auto& sub : rinfo->doneRoles) ci->doneRoles.insert(sub); // role-of-role
                // …and the role's GRAMMAR RULES. Only the first `does` becomes the
                // parent (whose rules the grammar walk finds); every further role
                // lands here, and its tokens were simply dropped — a grammar
                // composing three DSL::Shared roles kept one role's rules.
                // The class's own declarations win a name clash; between two
                // composed roles the EARLIER `does` wins, matching methods.
                {
                    // …walking the composed role's WHOLE chain: a role's own
                    // `does` puts its sub-role in the parent slot
                    // (TimeIntervalSpec does TimeIntervalSpeechParts), and those
                    // rules have to arrive too.
                    std::set<ClassInfo*> seenR;
                    std::function<void(ClassInfo*)> mergeRules = [&](ClassInfo* rc) {
                        if (!rc || !seenR.insert(rc).second) return;
                        for (auto& rk : rc->rules) {
                            bool own = false;
                            for (auto& r0 : cd->rules) if (r0.name == rk.first) { own = true; break; }
                            if (own || ci->rules.count(rk.first)) continue;
                            ci->rules[rk.first] = rk.second;
                            auto ki = rc->ruleKind.find(rk.first);
                            if (ki != rc->ruleKind.end()) ci->ruleKind[rk.first] = ki->second;
                            auto pi2 = rc->ruleParams.find(rk.first);
                            if (pi2 != rc->ruleParams.end()) ci->ruleParams[rk.first] = pi2->second;
                            auto li2 = rc->ruleLitArgs.find(rk.first);
                            if (li2 != rc->ruleLitArgs.end()) ci->ruleLitArgs[rk.first] = li2->second;
                            auto lc2 = rc->ruleLitCands.find(rk.first);
                            if (lc2 != rc->ruleLitCands.end()) ci->ruleLitCands[rk.first] = lc2->second;
                        }
                        for (auto& nm : rc->ruleOrder)
                            if (std::find(ci->ruleOrder.begin(), ci->ruleOrder.end(), nm) == ci->ruleOrder.end())
                                ci->ruleOrder.push_back(nm);
                        mergeRules(rc->parent.get());
                        for (auto& p2 : rc->extraParents) mergeRules(p2.get());
                    };
                    mergeRules(rinfo.get());
                }
            }
            // a role used as a parent (`class C does R` where R lands as parent) also counts
            if (ci->parent && ci->parent->isRole) ci->doneRoles.insert(ci->parent->name);
            for (auto& p : ci->extraParents) if (p && p->isRole) ci->doneRoles.insert(p->name);
            // Carry a composed role's `is also` dispatcher-aliases onto THIS type:
            // the alias is a second name for the multi group the role provides —
            // found in our merged methods, or in the role's own when it arrives as
            // a parent (walked, not copied). A CLASS installs the alias (its own
            // same-named method wins); a ROLE only remembers it, so `role B does A`
            // still delivers A's alias to the class that eventually does B. Only a
            // role that carried `is also` has anything here. (See
            // ClassInfo::alsoRoleAliases; Method::Also #19's role case.)
            for (ClassInfo* role : composedRoles) {
                if (role->alsoRoleAliases.empty()) continue;
                for (auto& ra : role->alsoRoleAliases) {
                    // the group may be merged into us, or up the composed chain
                    // (roles that arrive as parents are walked, not copied) — a
                    // full lookup finds it either way, incl. `role B does A`.
                    Value group;
                    if (Value* g = ci->findMethod(ra.first)) group = *g;
                    if (!(group.t == VT::Code && group.code() && group.code()->isMultiDispatcher)) {
                        auto rit = role->methods.find(ra.first);
                        if (rit != role->methods.end()) group = rit->second;
                    }
                    if (!(group.t == VT::Code && group.code() &&
                          group.code()->isMultiDispatcher)) continue;
                    for (auto& alias : ra.second) {
                        if (cd->isRole) {
                            auto& v = ci->alsoRoleAliases[ra.first];
                            if (std::find(v.begin(), v.end(), alias) == v.end()) v.push_back(alias);
                        }
                        else if (!ci->methods.count(alias)) ci->methods[alias] = group;
                    }
                }
            }
            ci->isGrammar = cd->isGrammar;
            ci->isMonitor = cd->isMonitor;
            ci->classRw = cd->classRw;
            // A grammar with no explicit parent derives from the built-in
            // Grammar type, as Rakudo's do (G, Grammar, Match, Capture, Cool,
            // Any, Mu) — nativeParent is the existing seam for a built-in
            // ancestor, so ~~/.isa/.does/.^mro all follow from it.
            if (ci->isGrammar && !ci->parent && ci->nativeParent.empty())
                ci->nativeParent = "Grammar";
            ci->isRole = cd->isRole;
            ci->repr = cd->repr;
            ci->ver = cd->ver; ci->auth = cd->auth; ci->api = cd->api;
            // COMPUTED name adverbs on a class, evaluated at declaration —
            // `unit class DBDish::Oracle:ver($?DISTRIBUTION.meta<ver>)…` is how
            // every DBDish driver stamps itself, and its suite reads .^ver back
            if (cd->verExpr)  { try { ci->ver  = eval(cd->verExpr.get()).toStr();  } catch (RakuError&) {} }
            if (cd->authExpr) { try { ci->auth = eval(cd->authExpr.get()).toStr(); } catch (RakuError&) {} }
            if (cd->apiExpr)  { try { ci->api  = eval(cd->apiExpr.get()).toStr();  } catch (RakuError&) {} }
            // the class/role BODY scope: body lexicals (`my $lex = ...`) live here,
            // and methods/attr-defaults close over it
            auto bodyEnv = std::make_shared<Env>();
            bodyEnv->parent = tctx_.cur;
            bodyEnv->packageFrame = true;
            ci->declEnv = bodyEnv; // capture the declaration scope (attr-default closures)
            bodyEnv->x().pkgType = ci;
            ci->decl = cd;         // program-lifetime AST view (roleParams for parameterized roles)
            // `role ABCD[EFGH] { }` — a role parameter typed with nothing declared
            if (cd->isRole)
                for (auto& p : cd->roleParams) {
                    if (p.type.empty() || p.typeCapture || p.typeMayBeUndeclared || p.coerce || !paramTypeUndeclared(p.type)) continue;
                    bool captured = false;
                    for (auto& q : cd->roleParams)
                        if (q.typeCapture && (q.captureName == p.type || q.type == p.type)) captured = true;
                    if (!captured) throwInvalidParamType(p.type);
                }
            // …and a METHOD's parameter typed with nothing declared, as a sub's
            // is (`class ZZ { method foo(Blah $a) {} }`; S14-roles/basic.t)
            for (auto& md : cd->methods) {
                if (!md) continue;
                for (auto& p : md->params) {
                    if (p.type.empty() || p.typeCapture || p.typeMayBeUndeclared || p.coerce || !paramTypeUndeclared(p.type)) continue;
                    bool captured = false;
                    for (auto& q : md->params)
                        if (q.typeCapture && (q.captureName == p.type || q.type == p.type)) captured = true;
                    for (auto& q : cd->roleParams)
                        if (q.captureName == p.type || q.type == p.type || q.name == p.type) captured = true;
                    if (!captured) throwInvalidParamType(p.type);
                }
            }
            ci->langRev = (signed char)langRev_;
            if (!cd->isRole && langRev_ < 2) {
                auto e6Hooks = [](ClassInfo* r) {
                    if (!r || !r->isRole || r->langRev < 2) return false;
                    for (const char* h : {"BUILD", "TWEAK"}) {
                        auto it = r->methods.find(h);
                        if (it != r->methods.end() && it->second.t == VT::Code && it->second.code() &&
                            it->second.code()->isSubmethod) return true;
                    }
                    return false;
                };
                std::set<ClassInfo*> seenR;
                std::function<void(ClassInfo*)> walk = [&](ClassInfo* k) {
                    if (!k || ci->roleCtorHooks) return;
                    if (!k->isRole) { if (k->roleCtorHooks) ci->roleCtorHooks = true; return; }
                    if (!seenR.insert(k).second) return;
                    if (e6Hooks(k)) { ci->roleCtorHooks = true; return; }
                    walk(k->parent.get());
                    for (auto& p : k->extraParents) walk(p.get());
                };
                walk(ci->parent.get());
                for (auto& p : ci->extraParents) walk(p.get());
                for (auto& r : ci->composedRoles) walk(r.get());
            }
            // Normal flow reached the declaration: retire the forward-reference
            // record hoistSubs parked. A stale entry made the method-not-found
            // fallback re-EXECUTE this body — statements, is()-calls and all —
            // on the first missed method call against the class.
            {   // (only THIS declaration: the rest of its group, or a stub's
                // completion further down the scope, stay pending — the
                // completion is built from there where the stub is registered,
                // see stubOverCompleted)
                auto pend = pendingTypes_.find(cd->name);
                if (pend != pendingTypes_.end()) {
                    auto& v = pend->second;
                    v.erase(std::remove(v.begin(), v.end(), cd), v.end());
                    if (v.empty()) pendingTypes_.erase(pend);
                }
            }
            // Parameterized-role value params: bind each composed role's `[...]`
            // params to this class's `does R[args]` arguments, so the role body
            // (methods/submethods) sees them (e.g. Cro::Policy::Timeout[%phase-defaults]).
            // Runs even with NO roleArgs: a bare `does R` must still bind the
            // role's parameter DEFAULTS (License::SPDX's `does JSON::Class`).
            {
                auto tailMatch = [](const std::string& full, const std::string& part) {
                    if (full == part) return true;
                    if (full.size() > part.size() && full.compare(full.size() - part.size(), part.size(), part) == 0
                        && full[full.size() - part.size() - 1] == ':') return true;
                    if (part.size() > full.size() && part.compare(part.size() - full.size(), full.size(), full) == 0
                        && part[part.size() - full.size() - 1] == ':') return true;
                    return false;
                };
                for (ClassInfo* role : composedRoles) {
                    if (!role->decl || role->decl->roleParams.empty()) continue;
                    const std::vector<ExprPtr>* rargs = nullptr;
                    for (auto& ra : cd->roleArgs) if (tailMatch(role->name, ra.first)) { rargs = &ra.second; break; }
                    // NO `continue` when the class wrote a bare `does R`: the
                    // role's parameter DEFAULTS must still bind — License::SPDX's
                    // `does JSON::Class` needs $opt-in = False in scope, or every
                    // role method that mentions it dies "not declared" (the
                    // Test::META chain, 11 dists' meta tests).
                    ValueList argv;
                    // (a parameterization made above already has ITS `does`'s
                    // arguments — the name alone finds the first `does R[...]`)
                    auto ca = concArgv.find(role);
                    if (ca != concArgv.end()) argv = ca->second;
                    else if (rargs) for (auto& e : *rargs) {
                        Value v = eval(e.get());
                        if (syntacticNamedPair(&*e)) v.namedArg = true; // `does R[:opt]` → named arg
                        argv.push_back(std::move(v));
                    }
                    bindRoleParamsInto(ci.get(), role, argv, ci->declEnv);
                    // `does R[Int]`: the class does R[Int] — and not R[Str]
                    // (a PARAMETRIC role's `does R1[::T]` names no type yet:
                    // each of its parameterizations records its own R1[…])
                    bool unbound = false;
                    for (auto& a : argv) if (a.t == VT::Hash && a.hashKind == "Failure") unbound = true;
                    if (!argv.empty() && !unbound) {
                        const std::string rn = role->dispName.empty() ? role->name : role->dispName;
                        std::string disp = roleArgsDisplay(rn, argv);
                        if (disp != rn) ci->doneRoles.insert(disp);
                    }
                }
                // A ROLE binds its OWN parameter defaults too, so that using it
                // directly — punning it, `Acme::Cow.new(...)` — sees them. A
                // composing class binds its own arguments over the top and is
                // found first on the MRO walk, so this only ever answers for the
                // pun. Without it a punned role's methods died on their own
                // parameter ("Variable '$cow' is not declared") while the same
                // methods reached through `does` worked.
                if (cd->isRole && ci->decl && !ci->decl->roleParams.empty() &&
                    ci->roleParamBindings.empty()) {
                    ValueList none;
                    bindRoleParamsInto(ci.get(), ci.get(), none, ci->declEnv);
                }
            }
            {   // a class body takes no arguments — placeholders are compile errors
                std::string ph = firstBlockPlaceholder(cd->body);
                if (!ph.empty())
                    throwTyped("X::Placeholder::Block", {{"placeholder", ph}},
                        "Placeholder variable '" + ph +
                        "' may not be used here because the surrounding block does not take a signature");
            }
            for (auto& r : cd->rules) installRuleResolved(ci.get(), r);
            for (auto& a : cd->attrs) {
                // a placeholder in an attribute default has no block to bind to
                if (a.def) {
                    std::set<std::string> ph;
                    collectPHExprPublic(a.def.get(), ph);
                    for (auto& n : ph)
                        if (n[1] == '^' || n[1] == ':')
                            throwTyped("X::Placeholder::Attribute", {{"placeholder", n}},
                                "Placeholder variable '" + n + "' may not be used in an attribute default");
                }
                // a class may not redeclare an attribute a composed role declares
                if (!cd->isRole)
                    for (ClassInfo* role : composedRoles)
                        for (auto& ra : role->attrs)
                            if (ra.name == a.name && ra.sigil == a.sigil)
                                throw RakuError{Value::typeObj("X::Role::Attribute::Conflicts"),
                                    "Attribute '" + std::string(1, a.sigil) + "!" + a.name +
                                    "' conflicts in class '" + clsName +
                                    "' composition: also declared in role '" + role->name + "'"};
                // `has Int $.x = "42"` / `has Int(Rat) $.x = "42"` — a LITERAL default
                // that no value of the type (nor of a coercion's source type) can
                // hold is refused where the class is declared, as Rakudo does at
                // compile time (APPENDICES/A02-some-day-maybe/misc.t)
                if (a.sigil == '$' && a.def && !a.type.empty() && ascii::isupper((unsigned char)a.type[0]) &&
                    a.type.find('[') == std::string::npos &&
                    (a.def->kind == NK::StrLit || a.def->kind == NK::IntLit || a.def->kind == NK::NumLit ||
                     (a.def->kind == NK::InterpStr && [&] {   // "42" with nothing interpolated
                         for (auto& pt : static_cast<InterpStr*>(a.def.get())->parts)
                             if (!pt || pt->kind != NK::StrLit) return false;
                         return true; }()))) {
                    Value dv = eval(a.def.get());
                    std::string tgt = resolveAttrTypeAlias(a.type, clsName);
                    // The check is NOMINAL, as Rakudo's is: a subset's `where`
                    // is not run at declaration (`has Pos $.x = -1` compiles
                    // there), so a subset is judged by its base type. And a
                    // name that resolves to no type at all — an `our subset`
                    // inside a `unit class`, which URI declares its Scheme as —
                    // is not one this can prove anything about.
                    for (int guard = 0; guard < 16; guard++) {
                        auto it = subsets_.find(tgt);
                        if (it == subsets_.end()) it = subsets_.find(clsName + "::" + tgt);
                        if (it == subsets_.end() || it->second.coerce) break;
                        tgt = it->second.base.empty() ? std::string("Any") : it->second.base;
                    }
                    const bool known = subsets_.count(tgt) || classes_.count(tgt) || isKnownTypeName(tgt);
                    bool fits = !known || typeOrSubsetMatches(dv, tgt);
                    if (!fits && a.coerce) fits = a.coerceFrom.empty() || typeOrSubsetMatches(dv, a.coerceFrom);
                    if (!fits) {
                        const std::string want = a.type + (a.coerce ? "(" + a.coerceFrom + ")" : std::string());
                        throwTypedV("X::TypeCheck::Attribute::Default",
                                    {{"name", Value::str("$!" + a.name)}, {"got", dv},
                                     {"expected", Value::typeObj(a.type)}},
                                    "Default value " + dv.typeName() + " (" + typeCheckRepr(dv) +
                                    ") can never be assigned to attribute '$!" + a.name + "' of type " + want);
                    }
                }
                // …and a ROLE doing so is let through, to be reported when it is
                // punned or composed into a class (S14-roles/conflicts.t)
                if (cd->isRole && ci->roleAttrConflict.empty())
                    for (ClassInfo* role : composedRoles)
                        for (auto& ra : role->attrs)
                            if (ra.name == a.name && ra.sigil == a.sigil && ci->roleAttrConflict.empty())
                                ci->roleAttrConflict = "Attribute '" + std::string(1, a.sigil) + "!" + a.name +
                                    "' conflicts in role '" + clsName + "' composition: declared in both '" +
                                    clsName + "' and '" + role->name + "'";
                ClassAttr ca; ca.name = a.name; ca.sigil = a.sigil; ca.pub = a.pub; ca.rw = a.rw; ca.required = a.required; ca.type = resolveAttrTypeAlias(a.type, clsName); ca.requiredWhy = a.requiredWhy;
                ca.pod = a.pod; ca.podTrail = a.podTrail; ca.declLine = a.declLine;
                // `class Foo is rw` — every PUBLIC attribute is writable
                // (Compress::Zstd's buffer structs)
                if (cd->classRw && a.pub && !a.readonly) ca.rw = true;
                ca.containerIs = a.containerIs;
                ca.handles = a.handles;
                ca.doesRoles = a.doesRoles;
                ca.handlesTo = a.handlesTo;
                ca.built = a.built;
                ca.notBuilt = a.notBuilt;
                ca.defConstraint = a.defConstraint;
                ca.coerce = a.coerce;
                ca.objKeyed = a.objKeyed;
                ca.inlined = a.inlined;
                if (ca.inlined) { ca.inlineCls = ncInlineClass(ca.type); haveInlineAttrs_ = true; }
                ca.def = a.def || a.sigil != '$' ? a.def.get() : a.defaultTrait.get(); ca.shape = a.shape.get(); ca.defaultTrait = a.defaultTrait.get();
                ca.where = a.whereExpr.get();
                if (a.deprecated)
                    ca.deprecated = std::make_shared<std::string>(a.deprecatedWith.empty() ? "something else" : a.deprecatedWith);
                // the default must fit the declared type — judged here, at
                // composition, as Rakudo does (a role's type may be a capture
                // still unbound, so only a class is checked)
                if (a.defaultTrait && !cd->isRole && a.sigil == '$' && !ca.type.empty()) {
                    Value dv;
                    try { dv = eval(a.defaultTrait.get()); } catch (RakuError&) { dv = Value::any(); }
                    checkDeclDefault(ca.type, '$', dv, true);
                }
                ca.declId = &a;
                // user traits (`is json-name(…)`) evaluate AFTER the class body
                // runs — see the deferred pass below the body-exec block
                ci->attrs.push_back(ca);
            }
            std::set<const void*> ownParams; // this declaration's own method signatures
            for (auto& md : cd->methods) ownParams.insert(&md->params);
            // `method loader handles <load-delegate>` — every listed name is answered
            // by asking THIS method for the object to forward to. Recorded before the
            // bodies are built so the dispatch below can see it, and in
            // delegatedNames too, so nothing a composed role brought in shadows it.
            for (auto& md : cd->methods)
                for (auto& h : md->handles)
                    if (!h.empty() && !md->name.empty()) {
                        ci->methodHandles[h] = md->name;
                        ci->delegatedNames.insert(h);
                    }
            // methods carrying user `is` traits: dispatched to trait_mod:<is> AFTER the
            // class registers (the handler may call $*PACKAGE.^add_method / .HOW)
            std::vector<std::tuple<SubDecl*, Value, std::string>> methodTraitQueue; // decl, routine, table key
            for (auto& md : cd->methods) {
                // `method ::('indirect')` / `method ::('with space')` — the
                // name is an expression, computed as the class is built. The
                // method map takes any string, spaces included; the QUOTED
                // call form (`A."with space"()`) is how such a name is reached.
                std::string mdDyn;
                if (md->name.empty() && md->nameExpr)
                    mdDyn = eval(md->nameExpr.get()).toStr();
                const std::string& mdName = mdDyn.empty() ? md->name : mdDyn;
                Value code; code.t = VT::Code;
                code.setCode(makePayload<Callable>());
                code.code()->name = mdName;
                // `.package` is the DECLARING type, not the enclosing package —
                // Method::Protected's trait reads `$method.package.HOW` to refuse
                // a role method, and GLOBAL made every method look class-borne
                code.code()->pkg = clsName;
                code.code()->params = &md->params;
                code.code()->retType = qualifyDeclType(md->retType, clsName);
                code.code()->retRw = md->retRw;
                code.code()->body = &md->body;
                code.code()->langRev = langRev_;
                code.code()->rakuAst = rakuAstPragma_;
                code.code()->closure = bodyEnv;
                code.code()->isMethod = true; // invoked via .() binds the 1st arg as self
                // a method's own declarator pod — `#|` above it, `#=` below — is
                // what `.^find_method('m').WHY` answers, exactly as a sub's is
                code.code()->pod = md->pod; code.code()->podTrail = md->podTrail;
                if (md->deprecated) code.code()->deprecated = deprecationFor(md.get());
                code.code()->declFile = declFileNowI();
                code.code()->declLine = md->line;
                code.code()->isStub = stmtIsStub(md->body);
                for (auto& st : md->traits) if (st.name == "default") code.code()->isDefaultCand = true;
                // an undeclared `$!attr` reference in a method body is a compile
                // error — in a ROLE too: its attributes are its own (or those of
                // the roles it does), never its consumer's
                {
                    // `submethod BUILD(:$!notthere)` — an attribute parameter too
                    auto refs = collectAttrRefs(md->body);
                    for (auto& p : md->params)
                        if (p.name.size() > 2 && p.name[1] == '!' && std::strchr("$@%&", p.name[0]))
                            refs.push_back(p.name);
                    for (auto& ar : refs) {
                        std::string bare = ar.substr(2);
                        if (!declaresAttrHere(ci.get(), bare))
                            // filename+line mark it a compile-time (X::Comp) error —
                            // the top-level printer adds the ===SORRY!=== banner
                            throwTyped("X::Attribute::Undeclared",
                                {{"symbol", ar}, {"package-name", clsName},
                                 {"package-kind", cd->isRole ? "role" : cd->isGrammar ? "grammar" : "class"},
                                 {"what", "attribute"},
                                 {"line", std::to_string(md->line)},
                                 {"filename", srcFileAbs_.empty() ? srcFile_ : srcFileAbs_}},
                                "Attribute " + ar + " not declared in " +
                                (cd->isRole ? "role " : cd->isGrammar ? "grammar " : "class ") + clsName);
                    }
                }
                if (md->params.empty()) code.code()->placeholders = computePlaceholders(md->body);
                // a private method (`method !name`) lives under a distinct `!`-prefixed
                // key so it coexists with a public `.name` of the same name (they are
                // separate methods in Raku; `self!name` vs `self.name`). Only self!name
                // dispatch and .^private_methods look here.
                code.code()->isPrivateMethod = md->isPrivate;
                code.code()->isSubmethod = md->isSubmethod;
                // `is native` on a METHOD (`method mysql_error(MYSQL:D: --> Str)
                // is native(LIB) { * }`). NativeCall's method form passes the
                // INVOCANT as the first C argument, so `$mysql.mysql_error` is
                // `mysql_error(mysql)` and a type-object invocant (`MYSQL:U:`,
                // as mysql_init takes) is the NULL that C expects. The flag was
                // only ever set on the SUB path, so a native method fell through
                // to its `{*}` body and answered Whatever — every DBDish::mysql
                // and Oracle call, which declare their whole API this way.
                if (md->isNative) {
                    code.code()->isNative   = true;
                    code.code()->nativeLib  = md->nativeLib;
                    code.code()->nativeLibSub = md->nativeLibSub;
                    // `is native(EXPR)`: the class body has already run its own
                    // `constant LIB = …` by the time methods are built, so it
                    // resolves here; keep the AST for the first-call retry when
                    // it does not (a forward reference).
                    if (md->nativeLibExpr && code.code()->nativeLib.empty() &&
                        code.code()->nativeLibSub.empty()) {
                        try {
                            Value r = eval(md->nativeLibExpr.get());
                            if (r.t == VT::Code) { ValueList none; r = callCallable(r, none); }
                            if (isDefined(r)) code.code()->nativeLib = ncLibNameOf(r);
                        } catch (RakuError&) {}
                        if (code.code()->nativeLib.empty())
                            code.code()->nativeLibExpr = md->nativeLibExpr.get();
                    }
                    if (md->nativeSymExpr) code.code()->nativeSymExpr = md->nativeSymExpr.get();
                    code.code()->nativeSym = md->nativeSym.empty() ? mdName : md->nativeSym;
                }
                // a PURE `{*}` proto method is the group definition, not a candidate
                // — same rule as the sub path. Without this the proto entered the
                // candidate list with its `(|)` slurpy and WON whenever the real
                // candidates were invocant-constrained, so `D.g` printed `*`.
                code.code()->isProto = md->isProto && md->body.size() == 1 &&
                    md->body[0]->kind == NK::ExprStmt &&
                    static_cast<ExprStmt*>(md->body[0].get())->e &&
                    static_cast<ExprStmt*>(md->body[0].get())->e->kind == NK::Whatever;
                code.code()->isProtoBody = md->isProto && !code.code()->isProto;
                const std::string key = md->isPrivate ? "!" + mdName : mdName;
                if (md->isExport && !md->isPrivate) ci->exportedMethods.insert(key);
                if (!md->traits.empty()) methodTraitQueue.push_back({md.get(), code, key});
                if (md->isMulti) {
                    auto it = ci->methods.find(key);
                    if (it != ci->methods.end() && it->second.code() && it->second.code()->isMultiDispatcher) {
                        // an own candidate overrides a same-signature candidate
                        // composed from a role (never another own candidate)
                        auto& cands = it->second.code()->candidates;
                        std::string sk = sigKeyParams(code.code()->params);
                        cands.erase(std::remove_if(cands.begin(), cands.end(), [&](const Value& c){
                            return c.code() && !ownParams.count(c.code()->params) &&
                                   sigKeyParams(c.code()->params, c.code()) == sk; }), cands.end());
                        cands.push_back(code);
                    } else {
                        Value disp; disp.t = VT::Code; disp.setCode(makePayload<Callable>());
                        disp.code()->name = md->name; disp.code()->isMultiDispatcher = true;
                        // the group of a METHOD is itself a Method: `^lookup` hands
                        // this back, and code that dispatches on it (a
                        // `trait_mod:<is>(Method $m, …)` handler) will not bind a Sub
                        disp.code()->isMethod = true;
                        // …and it belongs to the same class its candidates do. A
                        // trait on the PROTO (`proto method pick(|) is protected
                        // {*}`) reads `$method.package`, and the group carried no
                        // package at all, so it answered GLOBAL — Method::Protected
                        // then asked GLOBAL for `^add_attribute`.
                        disp.code()->pkg = clsName;
                        disp.code()->candidates.push_back(code);
                        ci->methods[key] = disp;
                    }
                } else {
                    ci->methods[key] = code;
                }
                // A class may write the build-plan routine itself, and Rakudo
                // runs that one as readily as a metaclass-added one.
                if (key == "POPULATE") ci->hasPopulate = true;
                ci->roleSubmethods.erase(key); // the class declares it ITSELF now
            }
            // A role in the PARENT slot (`class C does R` with no `is`) is walked,
            // not copied — so a class's own `multi method m` group shadowed R's
            // candidates for `m` instead of joining them, and the narrower role
            // candidate never competed. Red's SQLite driver declares
            // `multi method default-type-for-type($) is default` beside the
            // `(Str)`, `(Numeric)`, … candidates its CommonSQL role supplies, and
            // every Str column came out varchar(255) rather than text. Merge them
            // here, own candidates first; a same-signature role candidate stays
            // overridden, as it does for a role composed by copy.
            if (!cd->isRole) {
                std::vector<ClassInfo*> parentRoles;
                for (ClassInfo* r = ci->parent && ci->parent->isRole ? ci->parent.get() : nullptr;
                     r && r->isRole; r = r->parent.get())
                    parentRoles.push_back(r);
                for (auto& p : ci->extraParents)
                    if (p && p->isRole) parentRoles.push_back(p.get());
                for (ClassInfo* role : parentRoles)
                    for (auto& kv : role->methods) {
                        const Value& rv = kv.second;
                        if (rv.t != VT::Code || !rv.code() || !rv.code()->isMultiDispatcher) continue;
                        auto it = ci->methods.find(kv.first);
                        if (it == ci->methods.end() || it->second.t != VT::Code || !it->second.code() ||
                            !it->second.code()->isMultiDispatcher) continue;
                        auto& cands = it->second.code()->candidates;
                        for (auto& rc : rv.code()->candidates) {
                            if (!rc.code() || rc.code()->isStub || rc.code()->isProto) continue;
                            std::string sk = sigKeyParams(rc.code()->params, rc.code());
                            bool have = false;
                            for (auto& c : cands)
                                if (c.code() == rc.code() ||
                                    (c.code() && sigKeyParams(c.code()->params, c.code()) == sk)) { have = true; break; }
                            if (!have) cands.push_back(rc);
                        }
                    }
            }
            // aggregate role requirements (composed roles already carry the ones
            // they inherited from roles they compose, so this is transitive) and
            // record this role's own stub methods as requirements
            std::map<std::string, std::set<std::string>> reqFrom; // req name -> role names (for the message)
            for (ClassInfo* role : composedRoles) {
                for (const std::string& rq : role->requiredMethods) { ci->requiredMethods.insert(rq); reqFrom[rq].insert(role->name); }
                for (auto& kv : role->requiredMultiSigs) {
                    auto& dst = ci->requiredMultiSigs[kv.first];
                    for (auto& s : kv.second) if (std::find(dst.begin(), dst.end(), s) == dst.end()) dst.push_back(s);
                    reqFrom[kv.first].insert(role->name);
                }
            }
            if (cd->isRole)
                for (auto& md : cd->methods)
                    if (stmtIsStub(md->body)) {
                        // private stubs are keyed `!name` to match how they're stored
                        // and dispatched, so a private impl (also `!name`) satisfies them
                        std::string rqKey = md->isPrivate ? "!" + md->name : md->name;
                        ci->requiredMethods.insert(rqKey);
                        if (md->isMulti) ci->requiredMultiSigs[rqKey].push_back(sigKeyParams(&md->params));
                    }
            // role composition check: a non-role class that composes a role must
            // implement every method the role requires — via its own methods, a
            // composed/inherited implementation, a public attribute's accessor,
            // or an attribute `handles` delegation. Roles compose freely.
            if (!cd->isRole) {
                std::set<std::string> classOwn;
                for (auto& md : cd->methods) classOwn.insert(md->isPrivate ? "!" + md->name : md->name);
                // conflicts first: two roles provided the same real implementation
                for (const std::string& cn : conflicted)
                    if (!classOwn.count(cn)) {
                        std::string rl; for (auto& r : providerRoles[cn]) { if (!rl.empty()) rl += ", "; rl += r; }
                        throw RakuError{Value::typeObj("X::Role::Unresolved::Method"),
                            "Method '" + cn + "' must be resolved by class " + clsName +
                            " because it exists in multiple roles (" + rl + ")"};
                    }
                // a real (non-stub) implementation anywhere in the effective type
                std::function<bool(ClassInfo*, const std::string&, const std::string*)> hasImpl =
                    [&](ClassInfo* c, const std::string& n, const std::string* sig) -> bool {
                    if (!c) return false;
                    auto mit = c->methods.find(n);
                    if (mit != c->methods.end() && mit->second.t == VT::Code && mit->second.code()) {
                        if (mit->second.code()->isMultiDispatcher) {
                            for (auto& cand : mit->second.code()->candidates)
                                if (cand.code() && !cand.code()->isStub &&
                                    (!sig || sigKeyParams(cand.code()->params, cand.code()) == *sig)) return true;
                        }
                        else if (!mit->second.code()->isStub) return true; // a plain method covers any signature
                    }
                    if (hasImpl(c->parent.get(), n, sig)) return true;
                    for (auto& p : c->extraParents) if (p && hasImpl(p.get(), n, sig)) return true;
                    return false;
                };
                std::function<bool(ClassInfo*, const std::string&)> attrCovers =
                    [&](ClassInfo* c, const std::string& n) -> bool {
                    if (!c) return false;
                    for (auto& a : c->attrs) {
                        if (a.pub && a.name == n) return true; // accessor counts as the method
                        for (auto& h : a.handles) if (h == n) return true;
                    }
                    if (attrCovers(c->parent.get(), n)) return true;
                    for (auto& p : c->extraParents) if (p && attrCovers(p.get(), n)) return true;
                    return false;
                };
                for (const std::string& rq : ci->requiredMethods) {
                    bool ok;
                    bool multiReq = false;
                    auto sigsIt = ci->requiredMultiSigs.find(rq);
                    if (sigsIt != ci->requiredMultiSigs.end() && !sigsIt->second.empty()) {
                        ok = true;
                        multiReq = true;
                        for (auto& s : sigsIt->second) if (!hasImpl(ci.get(), rq, &s)) { ok = false; break; }
                    }
                    else ok = hasImpl(ci.get(), rq, nullptr) ||
                              classOwn.count(rq); // an own stub is a deliberate promise
                    if (!ok && attrCovers(ci.get(), rq)) ok = true;
                    // a stubbed MULTI candidate left unimplemented has its own
                    // exception type (parameterized-basic.t: a `multi method`
                    // stub whose invocant is typed `::?CLASS:D`)
                    if (!ok && multiReq)
                        throwTypedV("X::Role::Unimplemented::Multi",
                                    {{"method", Value::str(rq)}, {"target", Value::typeObj(clsName)}},
                                    "Multi method '" + rq + "' has a candidate required by a role that " +
                                    clsName + " does not implement");
                    if (!ok) {
                        std::string rl; for (auto& r : reqFrom[rq]) { if (!rl.empty()) rl += ", "; rl += r; }
                        throw RakuError{Value::typeObj("X::Comp::AdHoc"),
                            "Method '" + rq + "' must be implemented by " + clsName +
                            " because it is required by roles: " + (rl.empty() ? "?" : rl) + "."};
                    }
                }
                // requirements verified: drop role-composed stubs so calls reach a
                // parent implementation / accessor instead of executing the stub
                // (a stub the class itself declares stays callable-and-dies)
                for (auto mit = ci->methods.begin(); mit != ci->methods.end(); ) {
                    Value& mv = mit->second; bool drop = false;
                    if (mv.t == VT::Code && mv.code()) {
                        if (mv.code()->isMultiDispatcher) {
                            auto& cs = mv.code()->candidates;
                            cs.erase(std::remove_if(cs.begin(), cs.end(), [&](const Value& c){
                                return c.code() && c.code()->isStub && !ownParams.count(c.code()->params); }), cs.end());
                            drop = cs.empty();
                        }
                        else if (mv.code()->isStub && !ownParams.count(mv.code()->params)) drop = true;
                    }
                    if (drop) mit = ci->methods.erase(mit); else ++mit;
                }
                // An attribute `handles` delegation is a method ON THE CLASS in Rakudo, so
                // it outranks everything a composed ROLE brought in under the same name —
                // both a `...` stub and a role's own default body. TAP's Output::Handle
                // (`has IO::Handle $.handle handles(:print<print>, :terminal<t>)`) hands the
                // whole Output role to its handle, `.terminal` included (issue #34).
                // `handles *` is excluded: it is a fallback for names nothing else answers.
                std::function<void(ClassInfo*)> noteDelegations = [&](ClassInfo* c) {
                    if (!c) return;
                    for (auto& a : c->attrs)
                        for (auto& h : a.handles)
                            if (h != "*" && !classOwn.count(h)) {
                                ci->delegatedNames.insert(h);
                                ci->methods.erase(h); // nothing composed may shadow it
                            }
                    noteDelegations(c->parent.get());
                    for (auto& p : c->extraParents) noteDelegations(p.get());
                };
                noteDelegations(ci.get());
            }
            noteSymbolMutation("class/role/grammar declaration");
            // The registry is name-keyed, so a second declaration of a live name
            // would CLOBBER every held type object of the first — old handles
            // silently adopt the new body. When the first declaration is still
            // lexically REACHABLE, refuse instead, as Rakudo does (its EVAL of a
            // plain redeclaration at the same visibility is a compile-time
            // X::Redeclaration, leaving the original intact; rakupp's EVAL
            // shares the caller's scope, so mainline-then-EVAL lands here).
            // Reachability is approximated as: the previous declaration's site
            // env is an ancestor of the current scope. A declaration whose
            // block has EXITED is fair game — roast redeclares plain classes
            // across sibling blocks and inside eval-lives-ok all over, and
            // Rakudo accepts those. Also exempt:
            //   - the same declaration node re-evaluated (a class in a sub body
            //     runs its decl on every call; Rakudo compiles it once)
            //   - `my`-scoped types (lexical in Rakudo, legally redeclarable;
            //     rakupp approximates with latest-wins)
            //   - stubs, augment, parameterized roles, weak packages — the same
            //     coexistence rules checkRedeclarations() applies at parse time
            {
                auto prev = classes_.find(clsName);
                if (prev != classes_.end() && prev->second->decl && prev->second->decl != cd &&
                    !cd->isMy && !prev->second->decl->isMy &&
                    !cd->isAugment && !cd->parameterized && !prev->second->decl->parameterized &&
                    !cd->isStubDecl && !prev->second->decl->isStubDecl &&
                    !cd->isPackage && !prev->second->decl->isPackage) {
                    Env* prevSite = prev->second->declEnv && prev->second->declEnv->parent
                                  ? prev->second->declEnv->parent.get() : nullptr;
                    // (an EVAL's own scope is lexical; the type it declared is
                    // package-scoped, so its site is the scope around the EVAL)
                    while (prevSite && prevSite->evalFrame && prevSite->parent) prevSite = prevSite->parent.get();
                    bool visible = false;
                    for (Env* e = tctx_.cur.get(); e && prevSite; e = e->parent.get())
                        if (e == prevSite) { visible = true; break; }
                    if (visible)
                        throwTyped("X::Redeclaration", {{"symbol", clsName}},
                                   "Redeclaration of symbol '" + clsName + "'");
                }
            }
            // A qualified private call into ANOTHER class (`$t!R::g()`) is judged
            // when the calling class is compiled: R must trust this class by name
            // — trust does not pass to a trustee's children
            if (!cd->isStubDecl && !cd->isRole) {
                for (auto& md : cd->methods) {
                    if (!md) continue;
                    std::set<std::string> ment;
                    for (auto& st : md->body) collectMentionedS(st.get(), ment);
                    for (auto& n : ment) {
                        if (n.empty() || n[0] != '\x02') continue;
                        const std::string q = n.substr(1);
                        size_t cut = q.rfind("::");
                        const std::string pkg = q.substr(0, cut);
                        if (pkg == clsName || pkg == cd->name) continue;
                        auto ti = classes_.find(pkg);
                        if (ti == classes_.end() || !ti->second || ti->second->isRole) continue;
                        if (ti->second->trusts.count(clsName) || ti->second->trusts.count(cd->name)) continue;
                        // (only for a private method the package really has: a
                        // call that names none is left to fail where it runs)
                        if (!ti->second->findMethod("!" + q.substr(cut + 2))) continue;
                        throwTypedV("X::Method::Private::Permission",
                            {{"method", Value::str(q.substr(cut + 2))}, {"source-package", Value::str(pkg)},
                             {"calling-package", Value::str(clsName)}},
                            "Cannot call private method '" + q.substr(cut + 2) + "' on package '" + pkg +
                            "' because it does not trust the '" + clsName + "' package.");
                    }
                }
            }
            // `my class A { ... }; my class A is repr("…") { }` — the stub already
            // fixed the representation
            {
                auto prev = lastDecl_.find(clsName);
                if (prev != lastDecl_.end() && prev->second.node != cd &&
                    prev->second.isStub && !cd->isStubDecl && !cd->repr.empty())
                    throwTypedV("X::TooLateForREPR", {{"type", Value::typeObj(clsName)}},
                                "Cannot change REPR of " + clsName + " now (must be set at initial declaration)");
                lastDecl_[clsName] = {cd, false, cd->isStubDecl};
            }
            // `enum Error (Metadata => -20); class Metadata { }` — the name is an
            // enum member already
            if (!hoistingSubs_ && !cd->isAugment && clsName.find("::") == std::string::npos) {
                if (Value* ev = tctx_.cur->find(clsName))
                    if (!ev->enumName.empty() && ev->enumName == clsName && !ev->enumType.empty())
                        throwTyped("X::Redeclaration", {{"symbol", clsName}},
                                   "Redeclaration of symbol '" + clsName + "'");
            }
            // A stub DECLARES a name; it never redefines one. `class Gen::Tab
            // { ... }` written inside a method body re-runs on every call of that
            // method, and overwriting the completed class with the empty stub is
            // how Terminal::Table's generator handed back an object that had none
            // of the methods it was supposed to have. The parse-time half of this
            // is in checkRedeclarations: a stub is a promise to the compilation
            // unit, not to the block it stands in.
            bool stubOverCompleted = false;
            if (cd->isStubDecl) {
                // …and a stub whose COMPLETION is further down the same scope
                // is completed NOW: the completing declaration is compile-time
                // in Rakudo, so `class X::B { ... }; X::B.new.a; class X::B {
                // has $.a = 42 }` prints 42 there (roast S12-class/stubs.t).
                // The scope-entry pass recorded the completion as the pending
                // type for the name (the stub matched too, and the later one
                // won the slot); building it here, in place of the stub, is
                // what makes the name usable in between.
                // (a same-named type from a sibling scope is no reason not
                // to: the pending record holds only what THIS scope has yet
                // to build)
                bool builtNow = false;
                {
                    auto pend = pendingTypes_.find(cd->name);
                    bool completionAhead = false;
                    if (pend != pendingTypes_.end())
                        for (ClassDecl* d : pend->second)
                            if (d != cd && !d->isStubDecl) completionAhead = true;
                    if (completionAhead) builtNow = materializePendingType(cd->name);
                }
                auto ex = classes_.find(clsName);
                stubOverCompleted = ex != classes_.end() && ex->second &&
                                    !(ex->second->decl && ex->second->decl->isStubDecl);
                // …and the completion built here never passes through the
                // stub, so the forward declaration's repr (below) would be lost
                if (builtNow && stubOverCompleted && ex->second->repr.empty() && !ci->repr.empty())
                    ex->second->repr = ci->repr;
            }
            // A FORWARD DECLARATION carries the REPRESENTATION, and the body
            // that follows does not repeat it: `class FT_Library is
            // repr('CPointer') {...}` up top, then `class FT_Library { … }` with
            // the methods. The body installs a fresh ClassInfo over the stub, so
            // the repr was simply lost and the class became P6opaque — every
            // `Pointer[FT_Library].deref` then answered a raw Int and
            // Font::FreeType could not build its handle at all. C types that
            // reference each other are declared this way as a matter of course.
            if (ci->repr.empty()) {
                auto prev = classes_.find(clsName);
                if (prev != classes_.end() && prev->second && prev->second->decl &&
                    prev->second->decl->isStubDecl && !prev->second->repr.empty())
                    ci->repr = prev->second->repr;
            }
            // a ROLE declared again under the same name with another signature is
            // one more candidate of its group (`role R {}` beside `role R[$x] {}`);
            // the earlier declarations ride along, the latest holds the name
            if (!stubOverCompleted && cd->isRole) {
                auto prev = classes_.find(clsName);
                // …declared in the SAME scope (two `my role B` in sibling blocks
                // are two roles), and with a parameter list somewhere among them
                bool sameScope = prev != classes_.end() && prev->second && prev->second->declEnv &&
                                 ci->declEnv && prev->second->declEnv->parent == ci->declEnv->parent;
                // …or each at the top of its own compilation unit: a module
                // declaring `role R[::T]` beside the `role R` it imported adds
                // a candidate to that group (roast S14-roles/versioning.t, one
                // candidate per language revision). Rakudo refuses the case
                // where the second unit never imported the first ("Merging
                // GLOBAL symbols failed"); this is lenient there.
                auto unitTop = [&](Env* e) { return e && (e == global_.get() || e->unitFrame); };
                bool acrossUnits = !sameScope && prev != classes_.end() && prev->second && prev->second->declEnv &&
                                   ci->declEnv && prev->second->declEnv->parent != ci->declEnv->parent &&
                                   unitTop(prev->second->declEnv->parent.get()) && unitTop(ci->declEnv->parent.get());
                bool parametric = cd->parameterized || !cd->roleParams.empty() ||
                                  (prev != classes_.end() && prev->second && prev->second->decl &&
                                   !prev->second->decl->roleParams.empty());
                if ((sameScope || acrossUnits) && parametric && prev->second->isRole &&
                    prev->second.get() != ci.get() && prev->second->decl &&
                    !prev->second->decl->isStubDecl) {
                    ci->roleVariants = prev->second->roleVariants;
                    if (prev->second->decl != cd) ci->roleVariants.push_back(prev->second);
                }
            }
            if (!stubOverCompleted) classes_[clsName] = ci;
            if (!cd->isAugment && !cd->name.empty() && !cd->isAnonDecl) {
                if (!cd->isMy && lexicalPkgDepth_ == 0) stashDeclare(clsName, false);
                else if (stashUnitHere().empty()) {   // the program's own lexical type
                    auto& slot = unitStash_[""].lexical[clsName];
                    if (!slot) { stashNodes_.emplace_back(); slot = &stashNodes_.back(); slot->fq = clsName; slot->stub = false; }
                }
            }
            // now the type resolves, dispatch the collected non-type `is` names to a
            // user trait_mod:<is>. Only NO-CANDIDATE means "not a trait"; a trait
            // body that ran and DIED propagates, or its real error would be replaced
            // by a misleading UnknownParent.
            for (auto& tn : pendingIsTraits) {
                // `is implementation-detail` is a TYPE trait that constrains
                // nothing we model — a class carrying it is an ordinary class.
                // Read as inheritance it died with "cannot inherit from
                // 'implementation-detail'", and Cro::WebApp marks every template
                // it compiles that way. It is the only no-argument trait Rakudo
                // takes on a type: `is pure`, `is nodal` and
                // `is hidden-from-backtrace` are routine traits and stay errors
                // here too.
                if (tn == "implementation-detail") continue;
                bool handled = false;
                if (Value* tm = tctx_.cur->find("&trait_mod:<is>")) {
                    if (tm->t == VT::Code) {
                        Value pr = Value::pair(tn, Value::boolean(true));
                        pr.namedArg = true;
                        ValueList ta; ta.push_back(Value::typeObj(clsName)); ta.push_back(pr);
                        // the trait sees the package it is applied to as `$*PACKAGE`
                        // (`$*PACKAGE.HOW does R` reaches THIS type, not its outer one)
                        Env* pe = tctx_.cur.get();
                        Value* prevPkg = pe->local("$*PACKAGE");
                        const bool hadPkg = prevPkg != nullptr;
                        Value savedPkg = hadPkg ? *prevPkg : Value::nil();
                        pe->define("$*PACKAGE", rolePackageValue(clsName, cd->isRole));
                        auto restorePkg = [&] {
                            if (hadPkg) pe->define("$*PACKAGE", savedPkg); else pe->vars.erase("$*PACKAGE");
                        };
                        try { callCallable(*tm, ta); handled = true; }
                        catch (RakuError& te) {
                            restorePkg();
                            if (te.message.rfind("Cannot resolve caller", 0) != 0) throw;
                        }
                        catch (...) { restorePkg(); throw; }
                        if (handled) restorePkg();
                    }
                }
                // a PACKAGE or MODULE is a namespace, not a class: it can be named
                // but never inherited from
                if (!handled && roleIsTraits.count(tn)) continue;
                if (!handled && pkgKind_.count(tn))
                    throwTypedV("X::Inheritance::Unsupported",
                        {{"child-typename", Value::str(cd->name)}, {"parent", Value::typeObj(tn)}},
                        (pkgKind_[tn] == 1 ? "module" : "package") + std::string(" ") + tn +
                        " does not support inheritance, so " + cd->name + " cannot inherit from it");
                if (!handled) {
                    auto sug = typeSuggestions(tn);
                    Value sl = Value::array(); sl.isList = true;
                    for (auto& n : sug) sl.arr()->push_back(Value::str(n));
                    throwTypedV("X::Inheritance::UnknownParent",
                        {{"child", Value::str(cd->name)}, {"parent", Value::str(tn)}, {"suggestions", sl}},
                        "Class '" + cd->name + "' cannot inherit from '" + tn +
                        "' because it is unknown" + didYouMean(sug));
                }
            }
            // Class-body subs are hoisted into the body scope NOW, before the
            // method traits below run: a `trait_mod:<is>` handler is an ordinary
            // sub, and `unit class Path::Finder` keeps its
            // `multi sub trait_mod:<is>(Method, Precedence:D :$constraint!)` beside
            // the methods that carry `is constraint(…)`. Looking it up in the
            // ENCLOSING scope found nothing, so every matcher method went untagged
            // and the module refused its own keys. Hoisted exactly once — running
            // it twice would append each `multi` candidate a second time.
            {
                auto savedCur = tctx_.cur;
                std::string savedPfx = tctx_.pkgPrefix;
                tctx_.pkgPrefix = clsName + "::";
                tctx_.cur = bodyEnv;
                hoistSubs(cd->body);
                // …and the body's ENUMs, which hoistSubs does not record: a trait
                // handler declared beside them is typed by them
                // (`(Method $m, Precedence:D :$constraint!)`), and an unresolvable
                // parameter type means the candidate never matches — the trait then
                // looked like "not this handler's trait" and was silently dropped.
                // Re-running the declaration when the body executes is harmless.
                for (auto& st : cd->body)
                    if (st->kind == NK::EnumDecl) try { exec(st.get()); } catch (...) {}
                tctx_.cur = savedCur;
                tctx_.pkgPrefix = savedPfx;
            }
            // METHOD-level user `is` traits: `method m() is also<mag> {…}` calls
            // trait_mod:<is>($m, :also<mag>) with $*PACKAGE bound to the class under
            // declaration (Method::Also registers aliases keyed by $*PACKAGE.^name).
            if (!methodTraitQueue.empty()) {
                auto savedCur = tctx_.cur;
                std::string savedPfx = tctx_.pkgPrefix;
                tctx_.cur = bodyEnv;                 // the handler may live in the body
                tctx_.pkgPrefix = clsName + "::";    // …and so may the types it names,
                                                     // which must register under the
                                                     // class's package, not bare
                if (Value* tm = bodyEnv->find("&trait_mod:<is>")) {
                    if (tm->t == VT::Code) {
                        Value* prevPkg = bodyEnv->find("$*PACKAGE");
                        Value savedPkg = prevPkg ? *prevPkg : Value::nil();
                        bodyEnv->define("$*PACKAGE", rolePackageValue(clsName, cd->isRole));
                        for (auto& mq : methodTraitQueue) {
                            SubDecl* md = std::get<0>(mq);
                            // A trait on a `proto` belongs to the DISPATCHER — that is
                            // what `^lookup` hands back, and what a `$method ~~ Role`
                            // check will be asking. On a plain or `multi` method it
                            // belongs to the routine itself. Path::Finder declares
                            // half its matchers as `proto method path(…) is
                            // constraint(Depth) { * }`, and mixing into the candidate
                            // left the name the module looks up untagged.
                            Value target = std::get<1>(mq);
                            if (md->isProto) {
                                auto mit = ci->methods.find(std::get<2>(mq));
                                if (mit != ci->methods.end() && mit->second.code() &&
                                    mit->second.code()->isMultiDispatcher)
                                    target = mit->second;
                            }
                            for (auto& st : md->traits) {
                                if (st.name == "default" && !st.arg) continue;   // dispatch tie-break, not a user trait
                                Value arg = st.arg ? eval(st.arg.get()) : Value::boolean(true);
                                // Method::Also's ROLE path adds the alias to each
                                // COMPOSING class, not to the role, and only for a
                                // dispatcher (proto/multi group — its `is_dispatcher`
                                // guard). Its AliasableRoleHOW.specialize leans on MOP
                                // (multi_methods_to_incorporate, instantiate_generic)
                                // we don't run; but the alias is just a second name
                                // for the group, so record it and let composition
                                // carry it into the consumer (Method::Also #19's
                                // t/01-basic role case).
                                if (cd->isRole && st.name == "also" && st.arg) {
                                    const std::string& mkey = std::get<2>(mq);
                                    auto mit2 = ci->methods.find(mkey);
                                    if (mit2 != ci->methods.end() && mit2->second.t == VT::Code &&
                                        mit2->second.code() && mit2->second.code()->isMultiDispatcher) {
                                        auto rec = [&](const Value& v) {
                                            std::string a = v.toStr();
                                            if (!a.empty()) ci->alsoRoleAliases[mkey].push_back(a);
                                        };
                                        if (arg.arr()) for (auto& e : *arg.arr()) rec(e);
                                        else rec(arg);
                                    }
                                }
                                Value p = Value::pair(st.name, arg); p.namedArg = true;
                                ValueList ta; ta.push_back(target); ta.push_back(p);
                                try { callCallable(*tm, ta); }
                                catch (RakuError& te) {
                                    // no matching candidate: not this handler's trait — but a
                                    // handler that RAN and died is the trait refusing the
                                    // method (Method::Protected: "Cannot apply 'is protected'
                                    // … in a role"), and that refusal must reach the caller
                                    const Value& pl = te.payload;
                                    bool noMatch = (pl.t == VT::Type && pl.s == "X::Multi::NoMatch") ||
                                                   (pl.t == VT::Object && pl.obj() && pl.obj()->cls &&
                                                    pl.obj()->cls->name == "X::Multi::NoMatch") ||
                                                   te.message.rfind("Cannot resolve caller", 0) == 0 ||
                                                   te.message.rfind("No matching", 0) == 0;
                                    if (!noMatch) throw;
                                }
                            }
                        }
                        if (prevPkg) bodyEnv->define("$*PACKAGE", savedPkg);
                        else bodyEnv->vars.erase("$*PACKAGE");
                    }
                }
                tctx_.cur = savedCur;
                tctx_.pkgPrefix = savedPfx;
            }
            // a qualified name also answers to its TAIL where no real class claims
            // it: `use URI::Path` inside `unit class URI` lets bare `Path` resolve
            // (Rakudo finds it in the URI:: package stash; we alias globally)
            // …and EVERY proper suffix, not just the last segment: inside
            // `unit module Getopt::Long`, a multi param typed `Argument::Boolean`
            // must find Getopt::Long::Argument::Boolean, or its whole dispatch
            // group resolves no candidate.
            // (not for one parameterization's instance of a generic class: its
            // names are the lexical ones that parameterization binds)
            for (size_t sep = genericInstance ? std::string::npos : clsName.find("::"); sep != std::string::npos;
                 sep = clsName.find("::", sep + 1)) {
                std::string tail = clsName.substr(sep + 2);
                // never shadow a BUILT-IN type: `class X::Roast::Channel` must not
                // make bare `Channel` mean the exception class
                if (!tail.empty() && !isKnownTypeName(tail) &&
                    !classes_.count(tail) && !classAliases_.count(tail)) {
                    classAliases_[tail] = clsName;
                    if (loadingModuleDepth_ == 0) programAliases_.insert(tail);
                }
            }
            // (`anon class C {…}` installs its name nowhere — S12-class/anonymous.t)
            if (!cd->name.empty() && !cd->isAnonDecl) tctx_.cur->define(cd->name, Value::typeObj(clsName));
            // run the body statements in the body scope: nested classes/enums and
            // static subs register; `my` lexicals land where the methods see them.
            // The package prefix covers the body so a nested `class GenericActions`
            // registers as Cro::Uri::GenericActions (Rakudo nesting semantics).
            {
                auto saved = tctx_.cur;
                std::string savedPrefix = tctx_.pkgPrefix;
                tctx_.pkgPrefix = clsName + "::";
                tctx_.cur = bodyEnv;
                // (the body's subs were hoisted above, before the method traits)
                // An EVAL in the body adds to THIS class (`EVAL 'method x {…}'`)
                g_classBodies.push_back(clsName);
                struct PopBody { ~PopBody() { g_classBodies.pop_back(); } } popBody;
                // A parametric role's GENERIC statements wait for a
                // parameterization (runGenericRoleBody); the rest run once, now.
                const GenericRoleBody* grb = cd->isRole ? genericRoleBody(cd) : nullptr;
                if (grb && !grb->any) grb = nullptr;
                // …unless every parameter has a default: the role type ITSELF is
                // then usable (`R.m`, a pun of the defaults), so its own body
                // runs whole, now, with the defaults bound where it runs
                if (grb) {
                    bool allDefaulted = true;
                    for (auto& p : cd->roleParams) {
                        if (p.slurpy) continue;
                        if (p.named ? (p.required && !p.defaultVal) : (!p.defaultVal && !p.optional)) {
                            allDefaulted = false;
                            break;
                        }
                    }
                    if (allDefaulted) {
                        for (auto& b : ci->roleParamBindings)
                            if (!b.first.empty() && !bodyEnv->local(b.first)) bodyEnv->define(b.first, b.second);
                        grb = nullptr;
                    }
                }
                // (what a generic statement declares still EXISTS here, unset: a
                // method compiled now — an `EVAL 'method …'` — can name it, and
                // a use of the role no parameterization runs for finds it; a
                // generic SUB is declared here too, as it always was)
                if (grb)
                    for (auto& nm : grb->lexicals)
                        if (!bodyEnv->local(nm))
                            bodyEnv->define(nm, nm[0] == '@' ? Value::array() : nm[0] == '%' ? Value::makeHash() : Value::any());
                try {
                    for (size_t bi = 0; bi < cd->body.size(); bi++)
                        if (!grb || bi >= grb->generic.size() || !grb->generic[bi] ||
                            (cd->body[bi] && cd->body[bi]->kind == NK::SubDecl)) exec(cd->body[bi].get());
                }
                catch (...) { tctx_.cur = saved; tctx_.pkgPrefix = savedPrefix; throw; }
                // `sub … is export` in a CLASS body still exports: in a `unit class`
                // the whole file IS the body, which is where several dists keep
                // their helper subs (XML::Entity's decode-xml-entities). Lift them
                // into the package env so the module-load republish carries them.
                if (loadingModuleDepth_ > 0 && curPkgEnv_ && curPkgEnv_ != bodyEnv)
                    for (auto& st : cd->body)
                        if (st->kind == NK::SubDecl) {
                            auto* sd = static_cast<SubDecl*>(st.get());
                            if (!sd->isExport || sd->name.empty()) continue;
                            auto it = bodyEnv->vars.find("&" + sd->name);
                            if (it != bodyEnv->vars.end()) curPkgEnv_->define(it->first, it->second);
                        }
                tctx_.cur = saved;
                tctx_.pkgPrefix = savedPrefix;
                // Inside a parameterization's run, `my class Box { class In … }`:
                // `Box::In` names THIS parameterization's In (as a `my package`'s
                // members are bound, above), whatever its registry name
                if (roleInstNames_ && !cd->name.empty() && !cd->isRole)
                    for (auto& kv : bodyEnv->vars)
                        if (kv.second.t == VT::Type && !kv.first.empty() &&
                            (ascii::isupper((unsigned char)kv.first[0]) || kv.first[0] == '_') &&
                            kv.second.s != clsName)
                            tctx_.cur->define(cd->name + "::" + kv.first, kv.second);
            }
            // A NESTED class shadows an outer one of the same name for the types
            // written in its parent's body — and it only exists NOW, because the
            // body that declares it has just run, where the attributes were
            // collected before it. `class Outer { class Inner {…}; has Inner
            // @.xs }` means Outer::Inner; taking the outer `Inner` made every
            // element the class built fail its own container's type check
            // (JSON::Unmarshal's 080-trait, which declares both in one file).
            for (auto& ca2 : ci->attrs)
                if (!ca2.type.empty() && ca2.type.find("::") == std::string::npos &&
                    classes_.count(clsName + "::" + ca2.type))
                    ca2.type = clsName + "::" + ca2.type;
            // …and a container class named for an `@`/`%` attribute
            // (`has @.a is G::A`, G declared in the body) is looked up from the
            // body too, where it now exists; an `is Array[Int]` one types it
            {
                bool anyIs = false;
                for (auto& ca2 : ci->attrs) if (!ca2.containerIs.empty()) { anyIs = true; break; }
                if (anyIs) {
                    auto savedCur = tctx_.cur;
                    std::string savedPfx = tctx_.pkgPrefix;
                    tctx_.cur = bodyEnv;
                    tctx_.pkgPrefix = clsName + "::";
                    for (auto& ca2 : ci->attrs) applyContainerElemType(ca2);
                    tctx_.cur = savedCur;
                    tctx_.pkgPrefix = savedPfx;
                }
            }
            // The metaobject a MODULE-SUPPLIED DECLARATOR names (`model Foo`),
            // made before the attribute traits run: a trait reaches it through
            // the type (`$attr.package.^add-relationship(…)` in Red's
            // `is relationship`), and must find Red's HOW there, not ours.
            auto makeDeclaredHow = [&]() {
            if (!cd->howName.empty() && ci->howObj.t != VT::Object) {
                    auto hcit = classes_.find(resolveClassAlias(cd->howName));
                    // a `my package EXPORTHOW` class may be registered by its
                    // short spelling (`SUPERSEDE::class`, `DECLARE::controller`)
                    if (hcit == classes_.end() && cd->howName.rfind("EXPORTHOW::", 0) == 0) {
                        hcit = classes_.find(cd->howName.substr(11));
                        if (hcit == classes_.end()) {
                            size_t lc = cd->howName.rfind("::");
                            hcit = classes_.find(cd->howName.substr(lc + 2));
                        }
                    }
                    if (hcit != classes_.end() && hcit->second) {
                        auto od = makePayload<ObjectData>();
                        od->cls = hcit->second;
                        od->attrs["__type"] = Value::typeObj(clsName);
                        // A metaclass is an ordinary object and keeps state in its
                        // own attributes across the hooks below (OO::Monitors holds
                        // its lock Attribute in one). Built by hand, it had no slots
                        // at all: `$!x = …` made one on assignment and looked fine,
                        // while `@!x.push(…)` and `%!x{…} = …` mutated a temporary
                        // and silently kept nothing. Give it the slots a constructed
                        // object would have.
                        ValueList noArgs;
                        runAttrDefaults(od, hcit->second, noArgs);
                        Value h; h.t = VT::Object; h.setObj(std::move(od));
                        ci->howObj = std::move(h);
                    }
                }
            };
            makeDeclaredHow();
            // `is Parent` is itself a trait, `trait_mod:<is>(Child, Parent)`, and
            // the built-in candidate behind it is plain inheritance. A user
            // candidate NARROWER than that one (its second parameter a class
            // the parent is) decides instead what the clause means: roast's
            // Advent::MetaBoundaryAspect makes `class Example is LoggingAspect`
            // hand LoggingAspect to its metaclass (`.HOW.add_aspect`) rather
            // than inherit from it. The inheritance is already in place here, so
            // it is taken back out while that candidate runs — which may add it
            // again, through `.HOW.add_parent` — and restored if none answers.
            if (!cd->isRole && (!cd->parent.empty() || !cd->extraParents.empty()))
                if (Value* tm = tctx_.cur ? tctx_.cur->find("&trait_mod:<is>") : nullptr;
                    tm && tm->t == VT::Code && tm->code()) {
                    // the classes a candidate's second positional names
                    std::vector<ClassInfo*> narrow;
                    auto scan = [&](const Value& c) {
                        if (c.t != VT::Code || !c.code() || !c.code()->params) return;
                        std::vector<const Param*> pos;
                        for (auto& p : *c.code()->params) if (!p.named && !p.slurpy) pos.push_back(&p);
                        if (pos.size() < 2 || pos[1]->type.empty()) return;
                        auto it = classes_.find(pos[1]->type);
                        if (it != classes_.end() && it->second && !it->second->isRole) narrow.push_back(it->second.get());
                    };
                    if (tm->code()->isMultiDispatcher) for (auto& c : tm->code()->candidates) scan(c);
                    else scan(*tm);
                    auto isA = [](ClassInfo* c, ClassInfo* want) {
                        std::function<bool(ClassInfo*)> walk = [&](ClassInfo* k) {
                            if (!k) return false;
                            if (k == want) return true;
                            if (walk(k->parent.get())) return true;
                            for (auto& e : k->extraParents) if (walk(e.get())) return true;
                            return false;
                        };
                        return walk(c);
                    };
                    std::vector<std::shared_ptr<ClassInfo>> written;
                    if (ci->parent && !cd->parentIsDoes) written.push_back(ci->parent);
                    for (auto& e : ci->extraParents) written.push_back(e);
                    for (auto& pc : written) {
                        if (!pc || pc->isRole) continue;
                        bool offered = false;
                        for (ClassInfo* n : narrow) if (isA(pc.get(), n)) { offered = true; break; }
                        if (!offered) continue;
                        auto savedParent = ci->parent;
                        auto savedExtra = ci->extraParents;
                        if (ci->parent == pc) {
                            ci->parent = nullptr;
                            if (!ci->extraParents.empty()) {
                                ci->parent = ci->extraParents.front();
                                ci->extraParents.erase(ci->extraParents.begin());
                            }
                        }
                        else
                            ci->extraParents.erase(std::remove(ci->extraParents.begin(), ci->extraParents.end(), pc),
                                                   ci->extraParents.end());
                        noteSymbolMutation("is-trait parent");
                        bool took = true;
                        try { callCallable(*tm, ValueList{Value::typeObj(clsName), Value::typeObj(pc->name)}); }
                        catch (RakuError& te) {
                            if (te.message.rfind("Cannot resolve caller", 0) != 0) throw;
                            took = false;
                        }
                        if (!took) {
                            ci->parent = savedParent;
                            ci->extraParents = savedExtra;
                            noteSymbolMutation("is-trait parent");
                        }
                    }
                }
            // USER attribute traits evaluate now, in the EXECUTED class-body
            // scope: `is unmarshalled-by(&unmarsh-version)` (META6) names a
            // `my multi sub` declared inside the body, which exists only after
            // the body ran. Only this declaration's own attrs — a ClassAttr
            // composed from a role arrived with its traits already evaluated
            // in the role's own scope.
            {
                std::set<const void*> ownDecls;
                for (auto& a : cd->attrs) ownDecls.insert(&a);
                auto saved = tctx_.cur;
                tctx_.cur = bodyEnv;
                for (auto& ca2 : ci->attrs) {
                    if (!ca2.declId || !ownDecls.count(ca2.declId) || !ca2.userTraits.empty()) continue;
                    auto* ad = static_cast<const AttrDecl*>(ca2.declId);
                    for (auto& ut : ad->userTraits) {
                        Value tv = Value::boolean(true); // bare trait: `is json-skip`
                        if (ut.second)
                            try { tv = eval(ut.second.get()); } catch (...) {}
                        ca2.userTraits.emplace_back(ut.first, tv);
                    }
                }
                // …and NOW dispatch them: `has $.x is customary` calls
                // trait_mod:<is>($attr, :customary). The trait body mixes a role
                // into the Attribute meta-object and sets its state (`$a does
                // MetaAttribute::Customary; $a.where = 'unknown'`) — which is why
                // that meta-object is built once and cached on the ClassAttr, and
                // why this runs HERE rather than at registration: the payloads it
                // passes are only evaluated by the loop above. The `trait:NAME`
                // keys stay on the meta-object either way, so modules whose traits
                // we answer directly (the JSON:: role surface) are unaffected, and
                // a name with NO trait_mod candidate is simply not a user trait.
                // A trait nothing answers is an error — but only where that is
                // a CERTAINTY: a unit that imports modules may be relying on a
                // trait this engine answers from the meta-object's own keys
                // (the JSON:: attribute traits), so it stays lenient.
                const Program* traitUnit = unitCurrent();
                const bool traitsCertain = traitUnit && !traitUnit->importsModules && !traitUnit->typeNamesOpaque;
                auto unknownTrait = [&](const std::string& name) {
                    throwTypedV("X::Comp::Trait::Unknown",
                        {{"type", Value::str("is")}, {"subtype", Value::str(name)},
                         {"declaring", Value::str(" attribute")}},
                        "Can't use unknown trait 'is " + name + "' in an attribute declaration.");
                };
                Value* tm = bodyEnv->find("&trait_mod:<is>");
                if (tm && tm->t == VT::Code) {
                    Value* prevPkg = bodyEnv->find("$*PACKAGE");
                    Value savedPkg = prevPkg ? *prevPkg : Value::nil();
                    bodyEnv->define("$*PACKAGE", rolePackageValue(clsName, cd->isRole));
                    for (auto& ca2 : ci->attrs) {
                        if (ca2.userTraits.empty()) continue;
                        Value am = attributeMetaObject(ca2, clsName);
                        const auto* traitDecl = static_cast<const AttrDecl*>(ca2.declId);
                        for (size_t ti = 0; ti < ca2.userTraits.size(); ti++) {
                            auto& ut = ca2.userTraits[ti];
                            // `is doc('barks')` where `doc` is a TYPE takes the
                            // positional form first, `trait_mod:<is>(Attribute,
                            // doc, 'barks')`, as a routine's trait does — and a
                            // type name is never an unknown trait
                            bool typeName = classes_.count(ut.first) > 0;
                            if (!typeName && bodyEnv)
                                if (Value* tv = bodyEnv->find(ut.first)) typeName = tv->t == VT::Type;
                            if (typeName) {
                                const bool hasArg = !(traitDecl && ti < traitDecl->userTraits.size() &&
                                                      traitDecl->userTraits[ti].first == ut.first &&
                                                      !traitDecl->userTraits[ti].second);
                                ValueList ta{am, Value::typeObj(ut.first)};
                                if (hasArg) ta.push_back(ut.second);
                                bool took = true;
                                try { callCallable(*tm, ta); }
                                catch (RakuError& te) {
                                    if (te.message.rfind("Cannot resolve caller", 0) != 0) throw;
                                    took = false;
                                }
                                if (took) continue;
                            }
                            Value p = Value::pair(ut.first, ut.second);
                            p.namedArg = true;
                            ValueList ta; ta.push_back(am); ta.push_back(p);
                            bool noCandidate = false;
                            try { callCallable(*tm, ta); }
                            catch (RakuError& te) {
                                // no candidate = not a user trait at all;
                                // anything else is the trait body's own error
                                if (te.message.rfind("Cannot resolve caller", 0) != 0) throw;
                                noCandidate = true;
                            }
                            if (noCandidate && !typeName && traitsCertain && ca2.declId && ownDecls.count(ca2.declId)) {
                                if (prevPkg) bodyEnv->define("$*PACKAGE", savedPkg);
                                tctx_.cur = saved;
                                unknownTrait(ut.first);
                            }
                        }
                        // what the traits mixed into `$a.container` is what every
                        // instance's slot starts from
                        if (am.t == VT::Hash && am.hash()) {
                            auto cit = am.hash()->find(ATTR_CONTAINER_KEY);
                            if (cit != am.hash()->end() && cit->second.t == VT::Object && cit->second.obj() &&
                                cit->second.obj()->cls && cit->second.obj()->cls->name.find("+{") != std::string::npos)
                                ca2.containerProto = cit->second;
                        }
                    }
                    if (prevPkg) bodyEnv->define("$*PACKAGE", savedPkg);
                }
                else if (traitsCertain)
                    for (auto& ca2 : ci->attrs)
                        if (!ca2.userTraits.empty() && ca2.declId && ownDecls.count(ca2.declId) &&
                            !classes_.count(ca2.userTraits.front().first)) {
                            tctx_.cur = saved;
                            unknownTrait(ca2.userTraits.front().first);
                        }
                // …and then COMPOSE each attribute whose meta-object has a
                // `compose` of its own. Rakudo calls `$attr.compose($package)`
                // for every attribute during class composition, and THAT is
                // where the accessor is made — so a role a trait mixed in can
                // override it and install a different one. PDF::COS::Tie does
                // exactly that for every `is entry` attribute: the accessor it
                // adds is the writable one (`$pdf.Root = {…}`), and with the
                // call missing the attribute kept only the read-only default.
                // Only when a mixed-in role actually supplies `compose` — the
                // default accessor is this engine's own and needs no hook.
                for (auto& ca2 : ci->attrs) {
                    if (ca2.userTraits.empty()) continue;
                    Value am = attributeMetaObject(ca2, clsName);
                    if (am.t != VT::Hash || !am.hash()) continue;
                    auto rit = am.hash()->find(ATTR_ROLES_KEY);
                    if (rit == am.hash()->end() || rit->second.t != VT::Array || !rit->second.arr())
                        continue;
                    bool hasCompose = false;
                    for (auto& rn : *rit->second.arr()) {
                        auto cit = classes_.find(rn.s);
                        if (cit != classes_.end() && cit->second && cit->second->findMethod("compose"))
                            { hasCompose = true; break; }
                    }
                    if (!hasCompose) continue;
                    try { methodCall(am, "compose", ValueList{Value::typeObj(clsName)}); }
                    catch (RakuError&) {}   // a composer that refuses (a name already
                                            // taken) leaves the attribute as it was
                }
                tctx_.cur = saved;
            }
            // CLASS-LEVEL user traits: `unit model Foo is table<sqlite_master>`
            // calls `trait_mod:<is>(Foo, :table('sqlite_master'))`, the same
            // dispatch the attribute traits above take. Before the composition
            // hook below, because that is where a metaclass reads what the
            // traits set (Red's compose builds the table from :table).
            if (!cd->userTraits.empty()) {
                if (Value* tm = tctx_.cur ? tctx_.cur->find("&trait_mod:<is>") : nullptr) {
                    if (tm->t == VT::Code) {
                        Value tobj = Value::typeObj(clsName);
                        for (auto& ut : cd->userTraits) {
                            Value tv = Value::boolean(true);   // a bare trait: `is temp`
                            if (ut.second) { try { tv = eval(ut.second.get()); } catch (...) { continue; } }
                            Value p = Value::pair(ut.first, tv);
                            p.namedArg = true;
                            ValueList ta; ta.push_back(tobj); ta.push_back(p);
                            try { callCallable(*tm, ta); }
                            catch (RakuError& te) {
                                // no candidate = not a user trait at all; anything
                                // else is the trait body's own error
                                if (te.message.rfind("Cannot resolve caller", 0) != 0) throw;
                            }
                        }
                    }
                }
            }
            // A MODULE-SUPPLIED DECLARATOR (`model Foo { … }`) says what the
            // type's metaobject is: an instance of the HOW its EXPORTHOW::DECLARE
            // named, instead of rakupp's own Metamodel::ClassHOW. Everything the
            // declaration built is already in place, so all this does is put the
            // right object in .HOW — and the composition hook right below then
            // runs that HOW's `compose`, which is where such a metaclass does its
            // work (Red's MetamodelX::Red::Model turns the attributes into
            // columns there).
            makeDeclaredHow();
            // …and that metaclass DRIVES the declaration, not just its end.
            // Rakudo hands a class to its HOW one piece at a time — new_type,
            // then add_attribute per attribute, then add_method per method,
            // then compose — and a metaclass does its work in whichever of
            // those it overrides. rakupp built the whole class with its own
            // metamodel and called `compose` alone, which was enough for the
            // one that prompted the hook (Red builds its columns in compose)
            // and silently nothing for a metaclass that works anywhere else:
            // OO::Monitors adds its lock attribute in `new_type` and wraps
            // every method in lock/unlock in `add_method`, so a `monitor`
            // declared through it was a PLAIN CLASS. Its own test suite lost
            // counts to the race it exists to prevent (issue #86).
            //
            // The class is already built when we get here, so these are not
            // the calls that construct it — they are the same hooks in the
            // same order, given the finished pieces. That is invisible to a
            // metaclass that only adds to what it is handed, which is what
            // these hooks are for; one that refuses a piece cannot unbuild it.
            if (!cd->howName.empty() && ci->howObj.t == VT::Object && ci->howObj.obj() &&
                ci->howObj.obj()->cls) {
                ClassInfo* hc = ci->howObj.obj()->cls.get();
                auto howHas = [&](const char* nm) {
                    for (ClassInfo* c = hc; c; c = c->parent.get())
                        if (c->methods.count(nm)) return true;
                    return false;
                };
                const Value tobj = Value::typeObj(clsName);
                // A metaclass hook reaches its base with `callsame`/`nextsame`
                // (or the qualified `self.Metamodel::ClassHOW::add_method(…)`,
                // which the HOW-object forward already answers). The base of
                // each of these IS what the engine has already done, so the
                // next candidate hands back what it produced and adds nothing.
                auto callHook = [&](const Value& self, const char* nm, ValueList hargs, Value sameAnswer) {
                    RedispatchCtx rc;
                    rc.sameArgs = hargs;
                    rc.next = [sameAnswer](ValueList) { return sameAnswer; };
                    redispatchStack_.push_back(std::move(rc));
                    try { methodCall(self, nm, std::move(hargs)); }
                    catch (...) { redispatchStack_.pop_back(); throw; }
                    redispatchStack_.pop_back();
                };
                // `new_type` is called on the HOW TYPE OBJECT — Rakudo has no
                // instance yet, which is why OO::Monitors reaches its own
                // instance the long way round, through `type.HOW`. Calling it
                // on the instance instead would quietly accept a metaclass
                // Rakudo rejects ("Cannot look up attributes in a … type
                // object"), so `self` is the type object here too.
                // An error inside a hook is the METACLASS's own and must be
                // heard — swallowing one is what left every Red model silently
                // half-built before the compose hook stopped doing it. The only
                // tolerated failure is the same one compose tolerates: a
                // `nextsame` that has run out of candidates to redispatch to.
                auto hookErrIsSpent = [](const RakuError& e) {
                    return e.message.find("to redispatch to") != std::string::npos ||
                           e.message.find("not in the dynamic scope of a dispatcher") != std::string::npos;
                };
                if (howHas("new_type")) {
                    struct DeclaringGuard {           // …and `callsame` inside it answers OUR type
                        std::string saved;
                        DeclaringGuard(const std::string& n) : saved(declaringType_) { declaringType_ = n; }
                        ~DeclaringGuard() { declaringType_ = saved; }
                    } dg(clsName);
                    try { callHook(Value::typeObj(hc->name), "new_type", ValueList{}, tobj); }
                    catch (RakuError& e) { if (!hookErrIsSpent(e)) throw; }
                }
                if (howHas("add_attribute"))
                    for (auto& ca : ci->attrs) {
                        Value am = attributeMetaObject(ca, clsName);
                        if (am.t != VT::Hash || !am.hash()) continue;
                        try { callHook(ci->howObj, "add_attribute", ValueList{tobj, am}, am); }
                        catch (RakuError& e) { if (!hookErrIsSpent(e)) throw; }
                    }
                // Only the methods THIS declaration wrote, under the names it
                // wrote them: what a role composed in belongs to the role's own
                // declaration, and a private method is `add_private_method`'s.
                if (howHas("add_method"))
                    for (auto& md : cd->methods) {
                        if (md->isPrivate) continue;
                        auto mit = ci->methods.find(md->name);
                        if (mit == ci->methods.end()) continue;
                        try { callHook(ci->howObj, "add_method",
                                       ValueList{tobj, Value::str(md->name), mit->second}, mit->second); }
                        catch (RakuError& e) { if (!hookErrIsSpent(e)) throw; }
                    }
            }
            // Composition hook: a role mixed into the class's persistent .HOW (via a
            // method trait — Method::Also's AliasableClassHOW) may define `compose`;
            // Rakudo's metamodel calls it when the class finishes composing, and the
            // handler installs the registered method aliases with ^add_method.
            if (ci->howObj.t == VT::Object && ci->howObj.obj() && ci->howObj.obj()->cls) {
                bool userCompose = false;
                for (ClassInfo* c = ci->howObj.obj()->cls.get(); c && !userCompose; c = c->parent.get())
                    if (c->methods.count("compose")) userCompose = true;
                if (userCompose) {
                    ValueList ca; ca.push_back(Value::typeObj(clsName));
                    // the metaclass's compose runs BEFORE the base one it defers
                    // to, so a multi it adds is queued until then (see ClassInfo::
                    // awaitingCompose) — and incorporated however the hook exits
                    ci->awaitingCompose = true;
                    struct Incorporate {
                        ClassInfo* c;
                        ~Incorporate() {
                            c->awaitingCompose = false;
                            auto pend = std::move(c->pendingMultis);
                            c->pendingMultis.clear();
                            for (auto& pm : pend) insertRuntimeMulti(c, pm.first, pm.second);
                        }
                    } incorporate{ci.get()};
                    try { methodCall(ci->howObj, "compose", ca); }
                    catch (RakuError& ce) {
                        // A `nextsame` past the mixin has no next candidate here,
                        // and that one is expected. ANYTHING else is the metaclass's
                        // own error and must be heard: swallowing it left Red's
                        // compose dying on its first statement and every model
                        // silently half-built (no columns, no Red::Model role).
                        const std::string& cm = ce.message;
                        if (cm.find("to redispatch to") == std::string::npos &&
                            cm.find("not in the dynamic scope of a dispatcher") == std::string::npos)
                            throw;
                    }
                }
            }
            // evaluate to the type object, so `my class Foo {…}` / anon `role {…}` work as expressions
            return Value::typeObj(clsName);
        }
        default: break;
    }
    return Value::any();   // unreachable: exec dispatches only the six above
}

// A bare block written as a statement — exec's NK::Block, out of exec's frame:
// the scope it makes is destroyed HERE, so an exception passing through a
// nested block does not stop in exec (one of the widest functions there is)
// to run a cleanup and then resume from its far end. A plain block — no
// phaser, not an END, not `{}` — is all this frame does; the others go to
// execBareBlockRare, whose temporaries are no charge on this one.
[[gnu::noinline]] Value Interpreter::execBareBlock(Block* b, bool sink, std::unique_ptr<HandedError>* handOff) {
    if (b->endSlot >= 0 || !b->phaser.empty() || b->stmts.empty() || tctx_.protoDepth > 0)
        return execBareBlockRare(b, sink);
    auto scope = std::make_shared<Env>();
    scope->parent = tctx_.cur;
    scope->btBlock = true; scope->btLine = b->line;   // a frame of its own in `.backtrace.list`
    return execBlock(b, scope, sink, handOff); // a sunk bare block sinks its final value too
}

[[gnu::noinline]] Value Interpreter::execBareBlockRare(Block* b, bool sink) {
    // A registered END belongs to program exit. The statement runners
    // skip it (isBlockPhaser), so this is the UNIT-level path — a
    // module's or an EVAL's mainline, which walks its statements
    // itself — and all that happens here is the scope capture.
    if (b->endSlot >= 0) { captureEndScope(b); return Value::nil(); }
    // a nested BEGIN/CHECK/INIT already ran before the code around it
    if (!staticPhaserVal_.empty() && !b->phaser.empty()) {
        auto sv = staticPhaserVal_.find(b);
        if (sv != staticPhaserVal_.end()) return sv->second;
    }
    // a BEGIN reached in place (an EVAL's unit): what dies in it is a
    // compile-time failure, as at the top level
    struct BeginDepth { int& d; bool on; BeginDepth(int& x, bool o) : d(x), on(o) { if (on) d++; } ~BeginDepth() { if (on) d--; } }
        beginDepthGuard{beginDepth_, b->phaser == "BEGIN"};
    if (b->phaser == "BEGIN" && !b->stmtForm) {   // (`BEGIN my $x = …` declares out here: left alone)
        b->phaser.clear();
        struct Restore { Block* b; ~Restore() { b->phaser = "BEGIN"; } } rs{b};
        try { return exec(b, sink); }
        catch (RakuError& e) {
            if (e.payload.t == VT::Type && e.payload.s == "X::Undeclared::Symbols") throw;
            Value inner = exceptionFor(e);
            throwTypedV("X::Comp::BeginTime", {{"exception", inner}, {"use-case", Value::str("evaluating a BEGIN")}},
                        "An exception occurred while evaluating a BEGIN: " + e.message);
        }
    }
    // `{*}` inside a `proto` body: THE dispatch point. It hands the proto's
    // own arguments to the best candidate (S06). Outside a proto it is just a
    // block evaluating to `*`, which is what it stays.
    if (tctx_.protoDepth > 0 && b->phaser.empty() && !b->isCatch &&
        b->stmts.size() == 1 && b->stmts[0]->kind == NK::ExprStmt &&
        static_cast<ExprStmt*>(b->stmts[0].get())->e &&
        static_cast<ExprStmt*>(b->stmts[0].get())->e->kind == NK::Whatever)
        return protoRedispatch();
    // a statement-level `{}` with no statements is Rakudo's empty-hash
    // composer, not a block (EVAL('{}') is {}, not Any)
    if (b->stmts.empty() && b->phaser.empty() && !b->isCatch)
        return Value::makeHash();
    // statement-form phaser (`INIT my $x = …`): the declaration belongs
    // to the ENCLOSING scope — run without a child env
    if (b->stmtForm && !b->phaser.empty())
        return execBlock(b, tctx_.cur, sink);
    auto scope = std::make_shared<Env>();
    scope->parent = tctx_.cur;
    // a bare block is a frame of its own in `.backtrace.list` (see btCaptureNow)
    if (b->phaser.empty() && !b->isCatch) { scope->btBlock = true; scope->btLine = b->line; }
    return execBlock(b, scope, sink); // a sunk bare block sinks its final value too
}

// The program's mainline: everything from adopting the unit's revision to the
// END phasers and the exit code. Runs once per program, so it lives here and
// not with the evaluator's hot paths in InterpreterCore.cpp.
int Interpreter::run(Program& prog) {
    int code = 0;
    bool crashed = false;
    // The unit's revision is known from its text, so adopt it before a single
    // statement runs. Waiting for the `use v6.e.PREVIEW` statement to execute
    // was too late for anything hoisted — every sub in the unit is created
    // before the mainline starts, and was being stamped 6.d.
    langRev_ = prog.langRev;
    // `$*RAKU` is ONE object for the process, and its version is the MAIN unit's
    // — Rakudo answers the same revision inside a module whatever that module's
    // own `use v6.…` says. langRev_ itself stays per-unit, because that is what
    // gates the features; only the reported LANGUAGE version is pinned here.
    // App::ModuleSnap defaults a parameter to `$*RAKU.version` inside a module
    // written `use v6.*`, and the value it stored came back 6.e where its own
    // suite expects the 6.d the test file is written in.
    if (!mainLangRevSet_) { mainLangRev_ = prog.langRev; mainLangRevSet_ = true; }
    // …and the pragma the parser recorded, BEFORE the subs below are hoisted:
    // each one is stamped with it, and a routine declared under the pragma
    // keeps the namespace open in its own body wherever it is called from.
    if (prog.usesRakuAst) { rakuAstPragma_ = true; anyRevSwitch_ = true; rakuAstMaterialize(); }
    if (prog.langRev != 1) anyRevSwitch_ = true;
    unitPush(&prog);
    struct UnitGuard { Interpreter& I; ~UnitGuard() { I.unitPop(); } } unitG{*this};
    { // mainline sink warnings, printed before execution (Rakudo compile-time style)
        bool noWorries = false;
        for (auto& s : prog.stmts)
            if (s->kind == NK::UseStmt) {
                auto* us = static_cast<UseStmt*>(s.get());
                if (us->isNo && (us->module == "worries" || us->module == "warnings")) noWorries = true;
            }
        if (!noWorries) {
            std::vector<std::string> ws;
            for (auto& s : prog.stmts) sinkWarnStmt(s.get(), false, ws);
            for (auto& w : ws) std::cerr << w << "\n";
        }
    }
    tctx_.curStateEnv = global_.get(); // mainline `state` vars persist here (e.g. across a top-level loop)
    {
        Value args = Value::array();
        for (auto& s : argv_) args.arr()->push_back(Value::str(s));
        tctx_.cur->define("@*ARGS", args);
    }
    // Partition top-level phasers (BEGIN/CHECK/INIT run before the mainline).
    // LEAVE/KEEP/UNDO of the compilation unit run when the mainline exits, so they
    // are deferred here too rather than executed at their textual position. END
    // is not partitioned at all: it is registered by the whole-unit walk below,
    // wherever in the program it sits.
    std::vector<Block*> beginP, checkP, initP, leaveP, enterP;
    std::vector<Stmt*> mainline;
    Block* topCatch = nullptr; // a CATCH in the mainline (the UNIT block) guards it
    Block* topControl = nullptr; // …and a mainline CONTROL is the outermost warn handler
    for (auto& s : prog.stmts) {
        if (s->kind == NK::Block) {
            auto* b = static_cast<Block*>(s.get());
            if (b->isCatch) {
                // CATCH and CONTROL share the flag; the phaser tells them
                // apart, and a CONTROL must not swallow real exceptions
                if (b->phaser == "CONTROL") topControl = b;
                else topCatch = b;
                continue;
            }
            if (b->phaser == "BEGIN") { beginP.push_back(b); continue; }
            if (b->phaser == "CHECK") { checkP.push_back(b); continue; }
            if (b->phaser == "INIT")  { continue; }  // collected by the whole-program walk below
            // END: left in the mainline. The walk below registers it like any
            // other, and reaching it only captures the mainline scope.
            if (b->phaser == "ENTER") { enterP.push_back(b); continue; } // file scope: before the mainline body
            if (b->phaser == "LEAVE" || b->phaser == "KEEP" || b->phaser == "UNDO")
                                      { leaveP.push_back(b); continue; }
        }
        mainline.push_back(s.get());
    }
    // Every INIT in the program, at any depth, in source order — including the
    // top-level ones just skipped. See collectPhasersStmt: they run before the
    // mainline, and their textual positions are then skipped.
    for (auto& s : prog.stmts) collectPhasersStmt(s.get(), "INIT", initP, /*topLevel=*/true);
    // …and every END, at any depth, in source order: they run at exit, in
    // reverse. A nested run() (an installed `bin/` script) registers on top of
    // its caller's, and takes only its own back off at the end.
    const size_t endMark = endPhasers_.size();
    EndUnitScope endUnit{*this};   // a nested run() (an installed `bin/` script) is a unit of its own
    registerEnds(prog);
    auto runPhaser = [&](Block* b) {
        if (b->stmtForm) { execBlock(b, tctx_.cur); return; } // `INIT my $x = …` declares in the mainline scope
        auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur; execBlock(b, sc);
    };
    // END phasers run in REVERSE SOURCE order, on any exit path — one order for
    // the whole compilation, so a nested END takes its place among the
    // mainline's (`END a; sub f { END b }; END c` runs c, b, a) and a module's
    // takes its place at the `use` that loaded it.
    auto runEnds = [&]() {
        // Dropped objects get their DESTROY before the ENDs, so an END block
        // observes destructor effects; objects an END itself releases wait for
        // a real process exit, like Rakudo's unguaranteed finalization.
        try { runPendingDestroys(); } catch (...) {}
        // Snapshot before running: ENDs run with the workers still alive (below),
        // and both an EVAL on a worker and an END that EVALs an END of its own
        // register into endPhasers_ while this runs — a reference into it would
        // not survive the reallocation. The lock is never held across a phaser
        // body, which would deadlock on the capture the body's own blocks make.
        auto snapshotFrom = [&](size_t from) {
            std::lock_guard<std::mutex> g(endPhaserMut_);
            if (from >= endPhasers_.size()) return std::vector<EndPhaser>{};
            return std::vector<EndPhaser>(endPhasers_.begin() + from, endPhasers_.end());
        };
        // What an END THREW is reported rather than lost. Rakudo runs every END
        // whatever the ones before it did — a dying phaser does not stop the
        // chain — and prints the exceptions together at the end, in the order
        // they were thrown. Measured against Rakudo 2026.07, and three things
        // about that report are deliberate: it goes to STDERR only, so nothing
        // reading stdout moves; it does NOT touch the exit status, which stays
        // whatever the mainline (or an `exit` in a phaser) made it, 0 included;
        // and the banner is plural even for one exception.
        std::vector<std::string> endErrors;
        auto runOne = [&](const EndPhaser& e) {
            try { runEndBody(e); }
            catch (ExitEx& ex) {
                code = ex.code;      // `exit` in an END block sets the exit status
                // …and DISCARDS what the ENDs before it threw. Measured, not
                // assumed: `END exit 3; END die "C"` runs C first and Rakudo
                // reports nothing, while `END die "A"; END exit 3` runs the exit
                // first and still reports A. This is the one place where copying
                // Rakudo LOSES a diagnostic rather than gaining one; it is copied
                // anyway, because a report that appears on one engine and not the
                // other is worse than one that is consistently absent.
                endErrors.clear();
            }
            catch (RakuError& err) { endErrors.push_back(renderEndError(err)); }
            // Control flow (a `return`/`last` with nothing to leave) is still
            // swallowed: it is not an exception a program can catch, and
            // reporting it as one would invent a diagnostic Rakudo never gives.
            catch (...) {}
        };
        // Deferred ENDs (modules, EVAL) first, newest registration first — then
        // this unit's own, reverse source order. A later-loaded module's
        // cleanup precedes the mainline END that inspects its results.
        auto bySource = [](std::vector<EndPhaser>& v) {
            std::stable_sort(v.begin(), v.end(),
                             [](const EndPhaser& a, const EndPhaser& b) { return a.key < b.key; });
        };
        auto batch = snapshotFrom(endMark);
        size_t seen = endMark + batch.size();
        bySource(batch);
        for (size_t i = batch.size(); i-- > 0; ) runOne(batch[i]);
        // `END { EVAL q[END …] }` registers while the loop above runs: those are
        // ends of the program too, newest first, until no more appear.
        for (auto more = snapshotFrom(seen); !more.empty(); more = snapshotFrom(seen)) {
            seen += more.size();
            bySource(more);
            for (size_t i = more.size(); i-- > 0; ) runOne(more[i]);
        }
        if (!endErrors.empty()) {
            std::cerr << "Some exceptions were thrown in END blocks:\n";
            for (auto& s2 : endErrors) std::cerr << s2;
        }
        // …and code marked `is DEPRECATED` that ran is reported as the program
        // ends, on STDERR, as Rakudo's own END does (`Deprecation.report` drains
        // the record earlier when a program asks for it; precompilation.t)
        {
            Value rep = deprecationReport();
            if (rep.t == VT::Str && !rep.s.empty()) std::cerr << rep.s.str() << "\n";
        }
        // A nested run() (an installed `bin/` script) hands the process back to
        // its caller: take this unit's registrations off so the outer run does
        // not fire them a second time, and let its blocks register again.
        std::lock_guard<std::mutex> g(endPhaserMut_);
        for (size_t i = endMark; i < endPhasers_.size(); i++) endPhasers_[i].blk->endSlot = -1;
        endPhasers_.resize(endMark);
    };
    // Extract a single top-level lexical declaration (name + whether it has an initializer).
    auto topDecl = [](Stmt* s, bool& hasInit) -> std::string {
        hasInit = true;
        if (s->kind == NK::VarDecl) { auto* vd = static_cast<VarDecl*>(s); hasInit = (bool)vd->init; return vd->names.size() == 1 ? vd->names[0] : ""; }
        if (s->kind == NK::ExprStmt) {
            Expr* e = static_cast<ExprStmt*>(s)->e.get();
            if (e && e->kind == NK::VarExpr && static_cast<VarExpr*>(e)->declare) { hasInit = false; return static_cast<VarExpr*>(e)->name; }
            if (e && e->kind == NK::Assign) { auto* a = static_cast<Assign*>(e);
                if (a->target && a->target->kind == NK::VarExpr && static_cast<VarExpr*>(a->target.get())->declare)
                    return static_cast<VarExpr*>(a->target.get())->name; }
        }
        return "";
    };
    uint64_t topCatchSerial = 0;
    try {
        hoistSubs(prog.stmts);
        preinstallNestedOurSubs(prog.stmts);
        // the main program's calls are checked before it runs, as an EVAL's
        // are — when it is plain statements the walker sees whole
        if (unitIsOutermost(&prog) && declCheckEnabled()) {
            bool plain = !prog.stmts.empty();
            for (auto& st : prog.stmts) if (!st || st->kind != NK::ExprStmt) { plain = false; break; }
            if (plain) checkUndeclaredCalls(prog.stmts);
        }
        // Remember what the program declared for itself, before any `use` runs:
        // a module's publish must not overwrite these (see mainlineSubNames_).
        if (mainlineSubNames_.empty())
            for (auto& kv : global_->vars)
                if (kv.first.size() > 1 && kv.first[0] == '&') mainlineSubNames_.insert(kv.first);
        // Pads (PADS-PLAN.md): the mainline is a pad owner. Installed only on
        // the FIRST program this interpreter runs (EVAL and module mainlines
        // re-enter here; their annotations would point at a layout no frame
        // carries, so they are skipped and stay on the map path). The layout
        // goes in BEFORE the pre-declare loop below, so the top-level `my`s it
        // defines land in the pad — pre-declared-and-live from the start,
        // which is exactly the visibility the map gave them.
        if (!global_->layout && unitIsOutermost(&prog)) {
            if (auto L = resolvePads(prog.stmts, nullptr)) {
                global_->layout = L;
                global_->pad.resize(L->names.size());
            }
        }
        // Pre-declare top-level lexicals so compile-time phasers (BEGIN/CHECK) can see them.
        bool hasInit;
        auto predeclare = [this](Expr* e) { // a declare-VarExpr, or a list declaration `my ($a, $b)`
            auto one = [this](Expr* x) {
                if (x && x->kind == NK::VarExpr && static_cast<VarExpr*>(x)->declare) {
                    auto* ve = static_cast<VarExpr*>(x);
                    if (!ve->name.empty() && !global_->find(ve->name)) {
                        // A CONTAINER TRAIT decides what the variable IS (`my %h is
                        // Set`, `my %h is MyHash`), and the type it names may not be
                        // registered yet at pre-declaration time. Leave those to the
                        // declaration itself rather than pre-defining a plain Hash
                        // that the trait can no longer replace — which is why a
                        // mainline `my %h is Set;` stayed a Hash while the same line
                        // inside a block did not.
                        if (!ve->containerIs.empty() &&
                            (ve->name[0] == '%' || ve->name[0] == '@')) { /* declaration handles it */ }
                        // Same reasoning for a PARAMETERIZED declared type
                        // (`my BinaryHeap::MinHeap[{ … }] $h`): the base type
                        // comes from a module, so at pre-declaration time — before
                        // any `use` has run — the parameterization cannot be
                        // evaluated, and pre-defining the textual fallback would
                        // leave the variable holding a type object that names no
                        // class. Leave it to the declaration, which runs after.
                        else if (ve->declTypeExpr) { /* declaration handles it */ }
                        else if (ve->declShape && ve->name[0] == '@')
                            global_->define(ve->name, makeShapedContainer(evalShapeDims(ve->declShape.get()), ve->declType));
                        else
                            global_->define(ve->name, declInitial(ve, ve->name[0]));
                    }
                }
            };
            if (!e) return;
            if (e->kind == NK::ListExpr) { for (auto& it : static_cast<ListExpr*>(e)->items) one(it.get()); }
            else one(e);
        };
        for (auto* s : mainline) {
            std::string nm = topDecl(s, hasInit);
            if (s->kind == NK::ExprStmt) { Expr* e = static_cast<ExprStmt*>(s)->e.get();
                if (e && e->kind == NK::Assign) predeclare(static_cast<Assign*>(e)->target.get());
                else predeclare(e); }
            if (nm.empty() || global_->find(nm)) continue;
            std::string dtype; // honor the declared type so `my num $n` pre-declares 0, not Any
            const VarExpr* dve = nullptr;
            if (s->kind == NK::ExprStmt) { Expr* e = static_cast<ExprStmt*>(s)->e.get();
                if (e && e->kind == NK::VarExpr) dve = static_cast<VarExpr*>(e);
                else if (e && e->kind == NK::Assign) { auto* a = static_cast<Assign*>(e); if (a->target && a->target->kind == NK::VarExpr) dve = static_cast<VarExpr*>(a->target.get()); } }
            if (dve) dtype = dve->declType;
            // …and the same deferral the predeclare walk makes: a container trait
            // decides what the variable IS, and pre-defining a plain Hash here
            // left the trait nothing to act on.
            if (dve && !dve->containerIs.empty() && (nm[0] == '%' || nm[0] == '@')) continue;
            // …and for a parameterized declared type, whose base comes from a
            // module that has not been `use`d yet at this point
            if (dve && dve->declTypeExpr) continue;
            global_->define(nm, typedDefault(dtype, nm[0])); }
        // the same modifier/ternary-buried declarations the block path hoists
        // (`my $x = E if COND` at file scope must declare $x even when COND is
        // false) — the loop above only sees plain top-level ExprStmts
        hoistExprDecls(prog.stmts, global_.get(), nullptr);
        // A `require Name` the unit writes installs a STUB package of that name at
        // compile time, before the load happens at run time — so a BEGIN already
        // sees the name (`BEGIN try EVAL '$staticname = Test'`, S11-modules/require.t).
        {
            std::function<void(const Expr*)> seeReq = [&](const Expr* e) {
                if (!e) return;
                if (e->kind == NK::Assign) { seeReq(static_cast<const Assign*>(e)->value.get()); return; }
                if (e->kind == NK::ListExpr) {
                    for (auto& it : static_cast<const ListExpr*>(e)->items) seeReq(it.get());
                    return;
                }
                if (e->kind != NK::Unary) return;
                auto* u = static_cast<const Unary*>(e);
                if (!opEq(u->op, "require")) { seeReq(u->operand.get()); return; }
                if (!u->operand || u->operand->kind != NK::StrLit) return;
                const std::string& nm = static_cast<const StrLit*>(u->operand.get())->v;
                if (!nm.empty() && ascii::isalpha((unsigned char)nm[0]) && !classes_.count(nm) &&
                    !pkgKind_.count(nm) && !isKnownTypeName(nm) && !global_->find(nm))
                    global_->define(nm, Value::typeObj(nm));
            };
            for (auto& st : prog.stmts) {
                if (!st) continue;
                if (st->kind == NK::ExprStmt) seeReq(static_cast<const ExprStmt*>(st.get())->e.get());
                else if (st->kind == NK::UseStmt) {
                    auto* us = static_cast<const UseStmt*>(st.get());
                    const std::string& nm = us->module;
                    if (us->isRequire && !us->fileExpr && !nm.empty() && ascii::isalpha((unsigned char)nm[0]) &&
                        !classes_.count(nm) && !pkgKind_.count(nm) && !isKnownTypeName(nm) && !global_->find(nm))
                        global_->define(nm, Value::typeObj(nm));
                }
            }
        }
        // nested BEGIN/CHECK/INIT (in blocks and closures) — see runStaticPhasers
        runStaticPhasers(prog.stmts, tctx_.cur, /*unitIsLive=*/false);
        // BEGIN: source order. What dies in one is a compile-time failure,
        // X::Comp::BeginTime wrapping the original as `.exception`
        for (auto* b : beginP) {
            try { runPhaser(b); }
            catch (RakuError& e) {
                // a routine not declared YET is a compile-time refusal of its own
                if (e.payload.t == VT::Type && e.payload.s == "X::Undeclared::Symbols") throw;
                Value inner = exceptionFor(e);
                std::string im = e.message;
                throwTypedV("X::Comp::BeginTime", {{"exception", inner}, {"use-case", Value::str("evaluating a BEGIN")}},
                            "An exception occurred while evaluating a BEGIN: " + im);
            }
        }
        for (auto it = checkP.rbegin(); it != checkP.rend(); ++it) runPhaser(*it); // CHECK: reverse
        for (auto* b : initP) runHoistedInit(b);                                  // INIT: source order, program-wide
        for (auto* b : enterP) runPhaser(b);                                      // ENTER: on UNIT-block entry, before the mainline
        // a mainline CONTROL {} is the outermost warn handler for the run
        if (topControl) tctx_.controlHandlers.push_back({topControl, tctx_.cur});
        // …and its CATCH the outermost handler (see dispatchBeforeUnwind)
        CatchReg topCatchReg{tctx_, topCatch, &prog.stmts, tctx_.cur};
        topCatchSerial = topCatchReg.serial;
        for (auto* s : mainline) {
            tctx_.endCurTopStmt = s;   // a `use` in it places the module's ENDs (see EndUnitScope)
            if (s->kind == NK::SubDecl && !static_cast<SubDecl*>(s)->name.empty() &&
                !static_cast<SubDecl*>(s)->isMethod) {
                auto* sd = static_cast<SubDecl*>(s);
                applySubTraits(sd);
                // A leading `unit module Foo;` has now set pkgPrefix, but this
                // `our sub` was already defined by hoistSubs (bare) — publish it
                // under its qualified name so `Foo::name()` resolves. The
                // module-LOADING loop has always done this; the mainline did
                // not, so a program headed `unit module Quux;` could reach its
                // own `$Quux::v` but not its own `Quux::deep()`.
                if (sd->isOur && !tctx_.pkgPrefix.empty())
                    if (Value* c = tctx_.cur->find("&" + sd->name))
                        global_->define("&" + tctx_.pkgPrefix + sd->name, *c);
                continue; // hoisted
            }
            // a bare `my $x;` (no init) must not clobber a value a phaser already set
            std::string nm = topDecl(s, hasInit);
            // …nor does `my $x ~= 'o'`: an OP-assigning declaration applies to
            // what an INIT left there (S04-phasers/init.t), so it runs as the
            // plain `$x ~= 'o'` it amounts to once the variable exists
            if (!nm.empty() && hasInit && !staticPhaserVal_.empty() && s->kind == NK::ExprStmt &&
                global_->local(nm)) {
                auto* a = static_cast<Assign*>(static_cast<ExprStmt*>(s)->e.get());
                if (a->kind == NK::Assign && !opEq(a->op, "=") && !opEq(a->op, ":=") && a->op.size() > 1 &&
                    a->op.back() == '=' && static_cast<VarExpr*>(a->target.get())->declScope == "my" &&
                    static_cast<VarExpr*>(a->target.get())->declType.empty()) {
                    auto* ve = static_cast<VarExpr*>(a->target.get());
                    ve->declare = false;
                    struct Re { VarExpr* v; ~Re() { v->declare = true; } } re{ve};
                    exec(s);
                    continue;
                }
            }
            if (!nm.empty() && !hasInit && global_->local(nm)) {
                // the skipped declaration still owns its container metadata:
                // `my $port is default(8080);` initializes AND registers the default
                if (s->kind == NK::ExprStmt) {
                    Expr* e0 = static_cast<ExprStmt*>(s)->e.get();
                    if (e0 && e0->kind == NK::VarExpr) {
                        auto* ve0 = static_cast<VarExpr*>(e0);
                        if (e0->line > 0) curLine_ = e0->line;
                        checkBareSubsetDecl(ve0, ve0->name[0]);
                        if (ve0->declSmiley) global_->x().varSmiley[ve0->name] = ve0->declSmiley; // `my Int:D @a …;`
                        if (ve0->declWhereExpr && ve0->name[0] == '$')
                            global_->x().varWhere[ve0->name] = ve0->declWhereExpr;   // `my $x where Int;`
                        // the SHAPE is evaluated when the declaration runs — the
                        // hoist saw `my int @m[$size; $size]` before $size was set
                        if (ve0->declShape && ve0->name[0] == '@' && !ve0->declDefault)
                            global_->define(ve0->name, makeShapedContainer(evalShapeDims(ve0->declShape.get()), ve0->declType));
                        if (ve0->declDefault) {
                            Value dv = eval(ve0->declDefault.get());
                            checkDeclDefault(ve0->declType, ve0->name[0], dv, false, ve0->declSmiley);
                            if (ve0->name[0] == '@' || ve0->name[0] == '%') {
                                // container stays empty; v is the ELEMENT default —
                                // but the DECLARED type still applies, so build the
                                // container declInitial would have built (`my Int @a
                                // is default(0)` is an Array[Int]; a bare one here
                                // left `.of` at Mu and `.raku` without its type)
                                Value c = declInitial(ve0, ve0->name[0]);
                                if (c.t != VT::Array && c.t != VT::Hash)
                                    c = ve0->name[0] == '@' ? Value::array() : Value::makeHash();
                                c.elemDefaultM() = std::make_shared<Value>(dv);
                                global_->vars[ve0->name] = c;
                            } else {
                                global_->x().varDefault[ve0->name] = dv;
                                global_->vars[ve0->name] = dv;
                            }
                        }
                        else if (ve0->name[0] == '$' && !ve0->declType.empty() &&
                                 ascii::isupper((unsigned char)ve0->declType[0]))
                            global_->x().varDefault[ve0->name] = Value::typeObj(ve0->declType);
                        // …its coercion type, for the same reason: a hoisted
                        // `my Int() $x;` never reaches the declaration path that
                        // would have recorded it, and the later `$x = "7"` would
                        // then be refused rather than converted.
                        if (!ve0->declCoerce.empty())
                            global_->x().varCoerce[ve0->name] = ve0->declCoerce;
                            if (!ve0->declCoerceFrom.empty()) global_->x().varCoerce[ve0->name + "\x01from"] = ve0->declCoerceFrom;
                        // …and its `is dynamic`, which the skipped declaration would
                        // otherwise never record (a mainline `my $x is dynamic;` with
                        // no initializer is hoisted here and never evaluated)
                        if (ve0->declDynamic) global_->x().varDynamic.insert(ve0->name);
                    }
                }
                continue;
            }
            exec(s, /*sink=*/true); // every top-level statement is in sink context (Rakudo)
        }
        // auto-invoke MAIN with command-line arguments, if defined
        // (a mainline CONTROL, registered below, is already live here)
        Value* mainSub = tctx_.cur->find("&MAIN");
        // never the run-script CALLER's MAIN — a wrapper's own dispatch would
        // recurse into run-script forever (see inheritedMainBarrier_)
        if (mainSub && mainSub != inheritedMainBarrier_) {
            ValueList margs;
            int rc = mainProtocol(*mainSub, margs);
            // From 6.d MAIN has taken the command line for itself, so inside it
            // `$*ARGFILES` is `$*IN`: a bare `lines` reads standard input, not
            // the files the arguments name (MISC/misc.t)
            if (rc < 0 && langRev_ >= 1) {
                VarExpr in("$*IN");
                tctx_.cur->define("$*ARGFILES", eval(&in));
            }
            // MAIN's own value is sunk (Rakudo): a Failure it returns detonates
            // and a Proc that exited unsuccessfully throws, which is how a
            // program whose last act is `run @cmd` still exits non-zero (#73).
            if (rc < 0) sinkReturnedValue(callCallable(*mainSub, margs));
            else code = rc;
        }
        if (docMode_) std::cout << docModeText(); // --doc: print the rendered POD after the program runs
    } catch (ExitEx& e) {
        code = e.code;
    } catch (RakuError& e) {
        const CatchSeen seen = topCatch ? catchSeen(topCatchSerial, e) : CatchSeen::Passed;
        if (seen == CatchSeen::Taken) {   // it ran ahead of the unwinding
            try { replayCatchOutcome(e); }
            catch (ExitEx& ex) { code = ex.code; } catch (...) {}
        } else if (seen == CatchSeen::Fresh) { // mainline CATCH: bind $_/$! to the exception and run its when/default
            tctx_.cur->define("$_", exceptionFor(e));
            tctx_.cur->define("$!", exceptionFor(e));
            try { for (auto& s : topCatch->stmts) exec(s.get()); }
            catch (BreakGivenEx&) {} catch (ExitEx& ex) { code = ex.code; } catch (...) {}
        } else {
            // RAKU_EXCEPTIONS_HANDLER=JSON serializes an uncaught exception as JSON
            // — whatever the payload was thrown as: an undeclared routine is
            // thrown as its TYPE and exceptionFor makes the X::Undeclared::Symbols
            Value jsonEx = envStr("RAKU_EXCEPTIONS_HANDLER") == "JSON" ? exceptionFor(e) : Value();
            if (jsonEx.t == VT::Object && jsonEx.obj())
                std::cerr << exceptionToJson(jsonEx); // exceptionFor attaches the frames
            else {
                // a compile-time (X::Comp-style) exception carries filename+line
                // attrs — print it with Rakudo's ===SORRY!=== banner and location
                std::string cf, cl;
                if (e.payload.t == VT::Object && e.payload.obj()) {
                    auto& at = e.payload.obj()->attrs;
                    auto fi = at.find("filename"), li = at.find("line");
                    if (fi != at.end() && li != at.end()) { cf = fi->second.toStr(); cl = li->second.toStr(); }
                }
                if (!cf.empty())
                    std::cerr << "===SORRY!=== Error while compiling " << cf << "\n"
                              << e.message << "\nat " << cf << ":" << cl << "\n";
                // A RUNTIME error: the message, then where it happened and how
                // the program got there (issue #67). The message stays line 1
                // byte for byte — every golden and grep that reads the first
                // line keeps working.
                else std::cerr << renderError(e, btStyleForStderr());
            }
            code = 1;
            crashed = true;
        }
    } catch (ReturnEx&) { // `return` outside any routine: the spec'd error (evalString already says so)
        if (topCatch) { // …which a mainline CATCH sees as X::ControlFlow::Return
            RakuError e{Value::typeObj("X::ControlFlow::Return"), "Attempt to return outside of any Routine"};
            tctx_.cur->define("$_", exceptionFor(e));
            tctx_.cur->define("$!", exceptionFor(e));
            try { for (auto& s : topCatch->stmts) exec(s.get()); }
            catch (BreakGivenEx&) {} catch (ExitEx& ex) { code = ex.code; } catch (...) {}
        }
        else { std::cerr << "Attempt to return outside of any Routine\n"; code = 1; crashed = true; }
    } catch (LastEx&) { // `last` outside any loop is a compile/run error, like Rakudo's
        std::cerr << "last without loop construct\n"; code = 1; crashed = true;
    } catch (NextEx&) {
        std::cerr << "next without loop construct\n"; code = 1; crashed = true;
    } catch (RedoEx&) {
        std::cerr << "redo without loop construct\n"; code = 1; crashed = true;
    } catch (BreakGivenEx&) { // `succeed` with no `given`/`when` around it (it used to abort the process)
        std::cerr << "succeed without when clause\n"; code = 1; crashed = true;
    } catch (ProceedEx&) {
        std::cerr << "proceed without when clause\n"; code = 1; crashed = true;
    } catch (ControlHandledEx&) {
        // the mainline's own CONTROL handled a warning without .resume: the
        // mainline is left, and the program ends normally
    }
    // the mainline CONTROL's registration ends with the mainline — an rk_run
    // session may run several programs in one process, and a stale handler
    // would point into a dead scope
    if (topControl && !tctx_.controlHandlers.empty()) tctx_.controlHandlers.pop_back();
    flushOpenWriteHandles(); // write out any file handle the program forgot to .close
    // Compilation-unit LEAVE/KEEP/UNDO phasers run (reverse source order) on the
    // way out — after the mainline, before END.
    for (auto it = leaveP.rbegin(); it != leaveP.rend(); ++it) {
        try { runPhaser(*it); } catch (ExitEx& e) { code = e.code; } catch (...) {}
    }
    // END phasers run with the WORKERS STILL ALIVE, as Rakudo's do (its thread
    // pool outlives the mainline and dies with the process). Log::Async's END
    // is `logger.done`, which starts a worker to close its Supply and then
    // waits on that Supply — drained first, the worker never ran and the wait
    // never returned, so every program that used the logger hung at exit.
    runEnds(); // END phasers (reverse source order), after the mainline
    drainWorkers(); // join any outstanding async workers before we tear down
    // Rakudo's Test module never fabricates a trailing plan (and does not warn):
    // a file that ran tests without `plan`/`done-testing` just ends its TAP.
    // A plan that was not met says so — on STDOUT when nothing ran at all, on
    // stderr otherwise, as Rakudo's Test does
    if (usedTest_ && planned_ >= 0 && testNum_ != planned_ && !crashed && !bailedOut_)
        (testNum_ == 0 ? std::cout : std::cerr)
            << "# You planned " << planned_ << " test" << (planned_ == 1 ? "" : "s")
            << ", but ran " << testNum_ << "\n" << std::flush;
    // Rakudo's end-of-run summary when some tests failed.
    if (usedTest_ && failCount_ > 0 && !crashed && !bailedOut_) {
        std::cerr << "# Looks like you failed " << failCount_ << " test" << (failCount_ == 1 ? "" : "s")
                  << " of " << testNum_ << "\n";
    }
    // Rakudo test exit status: 255 ("dubious") if the ran count != the plan,
    // else the number of failed tests (capped at 254).
    if (usedTest_ && code == 0 && !crashed && !bailedOut_) {
        if (planned_ >= 0 && testNum_ != planned_) code = 255;
        else if (failCount_ > 0) code = failCount_ > 254 ? 254 : (int)failCount_;
    }
    else if (failCount_ > 0 && code == 0) code = 1;
    // A daemon `start {…}` (e.g. a server accept loop) was left running. We've
    // emitted all output; flush and hard-exit so the detached thread can't wedge
    // teardown (and isn't waited on), matching Rakudo abandoning thread-pool work.
    if (abandonedWorkers_) { std::cout.flush(); std::cerr.flush(); std::_Exit(code); }
    return code;
}

} // namespace rakupp
