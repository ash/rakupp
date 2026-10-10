// InterpreterBinding.cpp — closures, signatures and parameter binding, multi dispatch, NativeCall
//
// One of the parts InterpreterParts.h lists; what they share is declared there.
#include "InterpreterParts.h"
#include "Sandbox.h"

namespace rakupp {

// macOS resolves a bare unversioned libssl/libcrypto name to the /usr/lib
// compat stubs, which warn ("loading libcrypto in an unsafe way") and can
// abort the process on some code paths. A VERSIONED library (Homebrew's
// libcrypto.3.dylib, or a full path from OpenSSL's resources) is safe — so for
// ssl/crypto try versioned names FIRST and leave the stub as the last resort
// (some dists' basic use of the stub does work; banning it outright cost
// OpenSSL 7/7 → 1/7).
// The Homebrew FORMULA directory a library file belongs to. Keg-only formulae —
// the ones whose library would shadow a system copy — are never symlinked into
// the prefix's own lib/, so `/opt/homebrew/lib/libarchive.13.dylib` does not
// exist while `/opt/homebrew/opt/libarchive/lib/libarchive.13.dylib` does. The
// stem is the file name without its `lib` prefix and without any version or
// extension: libarchive.13.dylib -> libarchive, libmagic.1.dylib -> libmagic.
static std::string brewKegStem(const std::string& base) {
    std::string stem = base;
    size_t dot = stem.find('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    return stem;
}

static std::vector<std::string> libCandidates(const std::string& l) {
    std::vector<std::string> cands;
    // A name that is already a FILE name — `libcairo.2.dylib`, what the
    // (name, Version) form of `is native` resolves to — is tried as given and
    // under the library dirs dyld does not search by default; wrapping it in
    // another `lib…dylib` produced nothing that exists.
    if (l.find(".dylib") != std::string::npos || l.find(".so") != std::string::npos ||
        l.find(".dll") != std::string::npos) {
        size_t slash = l.find_last_of('/');
        std::string dir  = slash == std::string::npos ? std::string() : l.substr(0, slash + 1);
        std::string base = slash == std::string::npos ? l : l.substr(slash + 1);
#if defined(__APPLE__)
        // The versioned-first rule below has to cover the DECORATED spelling as
        // well: `$*VM.platform-library-name('ssl'.IO)` answers `libssl.dylib`,
        // and that is how OpenSSL::NativeLib — so every TLS dist — names the
        // library. Unversioned, that name IS the /usr/lib compat stub, and the
        // stub does not merely warn: it aborts the process (SIGABRT, no
        // catchable error). The versioned file is what the unversioned name
        // symlinks to anyway.
        if (base == "libssl.dylib" || base == "libcrypto.dylib") {
            std::string stem = base.substr(0, base.size() - 6); // libssl / libcrypto
            for (const char* v : {"3", "1.1"}) {
                std::string f = stem + "." + v + ".dylib";
                // A DIRECTORY the caller named is a deliberate choice of OpenSSL
                // (openssl@1.1 installed beside openssl@3), so look only inside
                // it — no prefix below may answer with the other version.
                if (!dir.empty()) { cands.push_back(dir + f); continue; }
                cands.push_back("/opt/homebrew/lib/" + f);
                cands.push_back("/usr/local/lib/" + f);
                cands.push_back("/opt/local/lib/" + f);
            }
        }
#endif
        cands.push_back(l);
        // A baked-in absolute path that does not open — a stale prefix, or the
        // wrong architecture (`zef install OpenSSL` under an Intel toolchain
        // writes /usr/local/opt/openssl@3 into its resources/libraries.json,
        // which an arm64 build cannot load) — still names a FILE that may exist
        // under a prefix dyld does not search. Try the basename there before
        // giving up on the whole library.
#if defined(__APPLE__)
        cands.push_back("/opt/homebrew/lib/" + base);
        cands.push_back("/usr/local/lib/" + base);
        cands.push_back("/opt/local/lib/" + base);
        // …and the KEG-ONLY location, which is the only place a formula like
        // libarchive, libmagic or libidn puts its library at all.
        {
            const std::string keg = brewKegStem(base);
            if (!keg.empty()) {
                cands.push_back("/opt/homebrew/opt/" + keg + "/lib/" + base);
                cands.push_back("/usr/local/opt/" + keg + "/lib/" + base);
            }
        }
#else
        cands.push_back("/usr/local/lib/" + base);
#endif
        return cands;
    }
#if defined(__APPLE__)
    if (l == "ssl" || l == "crypto") {
        for (const char* v : {".3", ".1.1"}) {
            cands.push_back("lib" + l + v + ".dylib");
            cands.push_back("/opt/homebrew/lib/lib" + l + v + ".dylib");
            cands.push_back("/usr/local/lib/lib" + l + v + ".dylib");
        }
    }
#endif
    for (std::string c : {l, "lib" + l + ".dylib", "lib" + l + ".so", l + ".dylib", l + ".so"})
        cands.push_back(std::move(c));
#if defined(__APPLE__)
    // Homebrew's lib dir is NOT on dyld's default search path, so a bare
    // `is native('zstd')` found nothing although /opt/homebrew/lib has it
    // (Compress::Zstd). Last, so a system or rpath copy still wins.
    cands.push_back("/opt/homebrew/lib/lib" + l + ".dylib");
    cands.push_back("/usr/local/lib/lib" + l + ".dylib");
    // …and the KEG-ONLY directory for a BARE name: `is native('magic')` wants
    // /opt/homebrew/opt/libmagic/lib/libmagic.dylib, which is the only copy
    // Homebrew keeps of a formula whose library would shadow a system one.
    cands.push_back("/opt/homebrew/opt/lib" + l + "/lib/lib" + l + ".dylib");
    cands.push_back("/opt/homebrew/opt/" + l + "/lib/lib" + l + ".dylib");
    cands.push_back("/usr/local/opt/lib" + l + "/lib/lib" + l + ".dylib");
    cands.push_back("/usr/local/opt/" + l + "/lib/lib" + l + ".dylib");
    // A handful of libraries that stand on their own everywhere else are FOLDED
    // INTO libSystem on macOS: there is no libuuid.dylib to open, on disk or in
    // the dyld cache, yet uuid_generate is in every process. A dist written on
    // Linux says `is native('uuid')` (LibUUID does, and DB::Pg — so Red —
    // depends on it), and the only thing wrong with that here is the file name.
    // Named ONE BY ONE rather than by falling back to the global namespace for
    // any missing library: that would answer a wrong `is native('mylib')` with
    // libc's `open` or `read` instead of saying the library is not there.
    for (const char* sys : {"uuid", "c", "m", "pthread", "dl", "resolv", "util", "info"})
        if (l == sys) { cands.push_back("libSystem.B.dylib"); break; }
#endif
#if !defined(__APPLE__)
    // glibc ships libm/libc/libdl at the unversioned name only as linker
    // SCRIPTS (dlopen refuses those), and only when the -dev package is
    // installed; the loadable object is the soname, .so.6 for the whole
    // glibc family. `is native('m')` must work on a bare Linux.
    //
    // …and that is not a glibc peculiarity, it is how Linux packaging works
    // for every library: the unversioned `libfoo.so` is a symlink shipped by
    // `foo-dev`, while the loadable object in the runtime package is the
    // SONAME. libuuid is the one that reported it — `libuuid.so` comes from
    // uuid-dev, `libuuid.so.1` from libuuid1, which is installed everywhere —
    // and a dist that says `is native('uuid')` (LibUUID, so DB::Pg, so Red)
    // must load on a box that never installed a -dev package. Descending, as
    // the libffi probe already does: the -dev symlink points at the newest
    // version installed, so trying the newest first answers the same file.
    // Only reached once per sub (callNative caches the resolved symbol) and
    // only when every unversioned spelling above has already missed.
    for (const char* v : {".8", ".7", ".6", ".5", ".4", ".3", ".2", ".1", ".0"})
        cands.push_back("lib" + l + ".so" + v);
#endif
    return cands;
}

// dlopen a library by its candidate spellings, or throw naming the most
// useful failure (an arch mismatch beats a generic not-found). One implementation
// for callNative and cglobal — their copies had already been edited separately.
// The exception is X::AdHoc, as Rakudo's is: DBDish::Pg reads its client
// version under `CATCH { when X::AdHoc { } }`, and a libpq-less machine must
// answer "no version", not kill the file (our invented X::Libc sailed past it).
static void* dlopenLib(const std::string& lib) {
    std::string dlerr;
    for (const std::string& cand : libCandidates(lib)) {
        if (void* handle = dlopen(cand.c_str(), RTLD_LAZY | RTLD_GLOBAL)) return handle;
        if (const char* e = dlerror()) {
            std::string es = e;
            if (dlerr.empty() || es.find("architecture") != std::string::npos) dlerr = es;
        }
    }
    // Rakudo's wording — see the note at the symbol-lookup failure: modules read it.
    throw RakuError{Value::typeObj("X::AdHoc"),
        "Cannot locate native library '" + lib + "'" + (dlerr.empty() ? "" : ": " + dlerr)};
}

// ----------------- expressions -----------------
Value Interpreter::makeClosure(BlockExpr* be) {
    Value code; code.t = VT::Code;
    code.setCode(makePayload<Callable>());
    code.code()->params = &be->params;
    code.code()->body = &be->body;
    code.code()->langRev = langRev_;
    code.code()->rakuAst = rakuAstPragma_;
    code.code()->closure = tctx_.cur;
    code.code()->isBlock = !be->isSub; // a bare { } / pointy block is a Block; `sub {…}` stays a Sub
    // `my $m = method ($inv: $p) {…}` — an anonymous METHOD takes its invocant as the
    // first argument and binds `self`, exactly as a declared one does.
    code.code()->isMethod = be->isMethodTerm;
    code.code()->isSubmethod = be->isSubmethodTerm;
    // a NAMED sub term keeps its name: `my $s = sub bar {}` and
    // `anon sub hmac (…) {}` answer "bar" and "hmac" to .name
    if (!be->termName.empty()) code.code()->name = be->termName;
    // `-> | --> Nil {}` / `sub (--> 5) {}`: the return literal is the body's last
    // statement, and the signature still shows it (`:(| --> Nil)`), as a
    // declared sub's does
    if (be->retLiteralPresent && !be->body.empty() && be->body.back()->kind == NK::ExprStmt)
        code.code()->retLiteral = static_cast<const ExprStmt*>(be->body.back().get())->e.get();
    // a NAMED method term inside a class body is still that class's method
    // (`our &m1 = method m1($a) {…}` — Rakudo adds it, has-scoped)
    if (be->isMethodTerm && !be->termName.empty()) {
        code.code()->name = be->termName;
        std::string pk = tctx_.pkgPrefix;
        if (pk.size() > 2 && pk.compare(pk.size() - 2, 2, "::") == 0) pk.resize(pk.size() - 2);
        auto ci = classes_.find(pk);
        if (ci != classes_.end() && ci->second) {
            code.code()->pkg = ci->second->name;   // whose `is hidden` it answers to
            if (!ci->second->methods.count(be->termName)) ci->second->methods[be->termName] = code;
        }
    }
    code.code()->declFile = declFileNowI();
    code.code()->isStub = stmtIsStub(be->body);   // `(sub f() { ... }).yada`
    // a POINTY block wrote its signature, even when it is empty — so `-> {;}` is
    // `()` and only a bare `{;}` gets the implicit `$_`
    // …and so did an anonymous `sub ($x) {…}` that wrote parameters
    code.code()->hadSig = be->isPointy || (be->isSub && !be->params.empty());
    code.code()->retType = qualifyDeclType(be->retType); // `-> $x --> Int {…}` / `sub (--> Int) {…}`
    code.code()->retRw = be->retRw;     // `sub (…) is rw {…}` — the result is a container
    if (!be->pod.empty()) { code.code()->pod = be->pod; code.code()->podTrail = be->podTrail; code.code()->declLine = be->podLine; }
    // a block knows where it was written: `-> { }.line`
    if (!code.code()->declLine && be->line > 0) code.code()->declLine = be->line;
    if (be->params.empty()) {
        code.code()->placeholders = computePlaceholders(be->body);
        // `{ $^a + @_.sum }(1, 2, 3)` — `@_` / `%_` beside the placeholders
        // take what they leave over, so the bind has to know they are there
        if (!code.code()->placeholders.empty()) {
            std::set<std::string> ph2;
            for (auto& s2 : be->body) collectPHStmt(s2.get(), ph2);
            code.code()->implicitArgs = (ph2.count("@_") ? 1 : 0) | (ph2.count("%_") ? 2 : 0);
        }
    }
    // a WRITTEN signature — `-> {…}`, `sub () {…}` — forbids placeholders
    if (be->isPointy || be->sigParens)
        for (auto& ph : code.code()->placeholders)
            if (ph.size() > 1 && (ph[1] == '^' || ph[1] == ':'))
                throwTyped("X::Signature::Placeholder", {{"placeholder", ph}},
                           "Placeholder variable '" + ph + "' cannot override existing signature");
    // an anonymous `sub {…}` without parameters has the empty signature too,
    // unless its body reads @_/%_: `sub () { 42 }(23)` dies
    if (be->isSub && !be->isMethodTerm && be->params.empty() && code.code()->placeholders.empty()) {
        std::set<std::string> ph2;
        for (auto& s2 : be->body) collectPHStmt(s2.get(), ph2);
        code.code()->usesArgs = ph2.count("@_") || ph2.count("%_");
        code.code()->implicitArgs = (ph2.count("@_") ? 1 : 0) | (ph2.count("%_") ? 2 : 0);
        code.code()->hadSig = true;
    }
    // An anonymous `sub (…) {…}` or `-> … {…}` carries parameter traits just as a
    // declared sub does, and reaches none of the declaration paths: the trait on
    // `sub (Str :$foo is option(*.flip)) {}` — a converter handed to Getopt::Long
    // inline — has only this one place to run.
    applyParamTraits(be->params, code);
    // …and a named SUB term is declared where it stands, as Rakudo does:
    // `my $b = sub bar {…}; bar()` works, and so does `sub fact($n) { … fact(…) }`
    // recursing inside the term. `anon` installs nothing, and a method term
    // belongs to its class (above).
    if (!be->termName.empty() && !be->isMethodTerm && !be->anonTerm)
        tctx_.cur->define("&" + be->termName, code);
    // a USER trait on the anonymous routine (`sub (…) is memoized(%c) {…}`)
    if (!be->userTraits.empty())
        if (Value* tm = tctx_.cur->find("&trait_mod:<is>"))
            if (tm->t == VT::Code) for (auto& st : be->userTraits) callRoutineTrait(*tm, code, st);
    return code;
}

// `x ** { … }`: the block's value as (min, max) repetition bounds, max -1 =
// unbounded — Rakudo's rules: a Range rounds inward (`1^..^3` is 2..2,
// `1.7..2.2` is 2..2), negative bounds clamp to 0, an empty range, an infinite
// minimum and anything non-numeric are X::Syntax::Regex::QuantifierValue
std::pair<long, long> Interpreter::dynQuantLimits(const Value& v, bool unboundedHint) {
    auto fail = [&](const char* which, const std::string& msg) -> std::pair<long, long> {
        throwTypedV("X::Syntax::Regex::QuantifierValue",
                    {{"non-numeric-range", Value::boolean(std::string(which) == "non-numeric-range")},
                     {"empty-range", Value::boolean(std::string(which) == "empty-range")},
                     {"inf", Value::boolean(std::string(which) == "inf")},
                     {"non-numeric", Value::boolean(std::string(which) == "non-numeric")}},
                    msg);
        return {0, 0};
    };
    auto numOf = [&](const Value& e, bool& ok) -> double {
        ok = true;
        if (e.t == VT::Whatever) return INFINITY;
        if (e.t == VT::Str) {
            const std::string s = e.s.str();
            char* end = nullptr;
            double d = std::strtod(s.c_str(), &end);
            size_t b = s.find_first_not_of(" \t");
            if (b == std::string::npos || !end || *end) { ok = false; return 0; }
            return d;
        }
        if (e.t == VT::Int || e.t == VT::Num || e.t == VT::Rat || e.t == VT::Bool)
            return e.toNum();
        ok = false;
        return 0;
    };
    const double kInfL = 9.2e18;
    auto clampL = [&](double d) -> long { return d < 0 ? 0 : (long)d; };
    if (v.t == VT::Range) {
        double lo, hi;
        bool ok1 = true, ok2 = true;
        if (const RangeEnds* re = rangeEnds(v)) {
            lo = numOf(re->from, ok1); hi = numOf(re->to, ok2);
        }
        else if (v.rNum()) { lo = v.n; hi = v.im(); }
        else if (v.ofType() == "Str") { ok1 = false; lo = hi = 0; }
        else {
            lo = v.rFrom() <= -9223372036854775807LL ? -INFINITY : (double)v.rFrom();
            hi = v.rTo() >= (long long)1e15 ? INFINITY : (double)v.rTo();
        }
        if (unboundedHint && !rangeEnds(v)) hi = INFINITY;
        if (!ok1 || !ok2 || std::isnan(lo) || std::isnan(hi))
            return fail("non-numeric-range", "Range endpoints of quantifier must be numeric");
        if (lo == INFINITY) return fail("inf", "Minimum quantity to match for quantifier cannot be Inf");
        double l = std::floor(lo) + (v.rExFrom() ? 1 : 0);
        double h = hi == INFINITY ? INFINITY : (v.rExTo() ? std::ceil(hi) - 1 : std::floor(hi));
        if (lo == -INFINITY) l = 0;
        if (l < 0) l = 0;
        if (h < 0) h = 0;
        if (l > h) return fail("empty-range", "Range of quantifier cannot be empty");
        return {clampL(l), h == INFINITY || h > kInfL ? -1 : clampL(h)};
    }
    bool ok = true;
    double n = numOf(v, ok);
    if (!ok || std::isnan(n)) return fail("non-numeric", "Quantifier value must be numeric");
    if (n == INFINITY) return fail("inf", "Quantifier value cannot be Inf");
    long k = clampL(std::floor(n));
    return {k, k};
}

Value Interpreter::makeTypedEx(const std::string& type,
                               std::vector<std::pair<std::string, Value>> attrs,
                               const std::string& message) {
    // While workers are live, other threads read classes_ and every ClassInfo
    // without a lock (the symbol-table freeze, see symbolsFrozen_), so a throw
    // then neither registers a class nor extends one: it carries a class of
    // its own. Four workers failing one bind at once inserted the same name
    // into classes_ concurrently and died (t/race/evalcall.raku).
    const bool shared = parallelMode_ && liveWorkers_.load(std::memory_order_relaxed) > 0;
    std::shared_ptr<ClassInfo> cls;
    auto it = classes_.find(type);
    if (it == classes_.end()) {
        auto ci = std::make_shared<ClassInfo>();
        ci->name = type;
        for (auto& kv : attrs) { ClassAttr a; a.name = kv.first; a.sigil = '$'; a.pub = true; ci->attrs.push_back(a); }
        { ClassAttr a; a.name = "message"; a.sigil = '$'; a.pub = true; ci->attrs.push_back(a); }
        if (!shared) classes_[type] = ci;
        cls = ci;
    }
    else {
        // The class may already exist WITHOUT the attributes this throw carries —
        // registered by an earlier throw of the same type with a different set, or
        // by a bare `Value::typeObj(name)` throw that carries none. The object
        // below would then hold the attribute while the class declared no
        // accessor, so `$!.method` died with an X::Method::NotFound of its own.
        cls = it->second;
        bool copied = false;
        for (auto& kv : attrs) {
            bool have = false;
            for (auto& a : cls->attrs) if (a.name == kv.first) { have = true; break; }
            if (have) continue;
            if (shared && !copied) { cls = std::make_shared<ClassInfo>(*cls); copied = true; }
            ClassAttr a; a.name = kv.first; a.sigil = '$'; a.pub = true; cls->attrs.push_back(a);
        }
    }
    Value ex; ex.t = VT::Object; ex.setObj(makePayload<ObjectData>());
    ex.obj()->cls = cls;
    for (auto& kv : attrs) ex.obj()->attrs[kv.first] = kv.second;
    ex.obj()->attrs["message"] = Value::str(message);
    if (isAdHocKind(type) && !ex.obj()->attrs.count("payload"))
        ex.obj()->attrs["payload"] = Value::str(message);
    return ex;
}

void Interpreter::throwTypedV(const std::string& type,
                              std::vector<std::pair<std::string, Value>> attrs,
                              const std::string& message) {
    Value ex = makeTypedEx(type, std::move(attrs), message);
    throw RakuError{ex, message};
}

Value Interpreter::ioFailure(const std::string& type,
                             std::vector<std::pair<std::string, Value>> attrs,
                             const std::string& message) {
    Value f = rakuppNewFailure();
    (*f.hash())["exception"] = makeTypedEx(type, std::move(attrs), message);
    (*f.hash())["message"] = Value::str(message);
    return f;
}

// `$abd` with `$abc` and `$abe` in scope: X::Undeclared, and — as Rakudo —
// the names one or two edits away as `suggestions`
void Interpreter::throwUndeclaredVar(const std::string& name, const std::vector<std::string>* extra) {
    // In a class BODY, outside any method, a twigil-less attribute's alias
    // (`has int $a; say $a`) has no self to be read through (native.t)
    if (!g_classBodies.empty() && name.size() > 1 && (name[0] == '$' || name[0] == '@' || name[0] == '%') &&
        !(tctx_.cur && tctx_.cur->findSelf())) {
        auto it = classes_.find(g_classBodies.back());
        if (it != classes_.end() && it->second)
            for (auto& a : it->second->attrs)
                if (a.sigil == name[0] && a.name == name.substr(1)) {
                    const std::string v = std::string(1, name[0]) + "!" + a.name;
                    throwTypedV("X::Syntax::NoSelf", {{"variable", Value::str(v)}},
                                "Variable " + v + " used where no 'self' is available");
                }
    }
    std::vector<std::string> cands;
    if (extra) for (auto& n : *extra) if (n != name && n.size() > 1) cands.push_back(n);
    auto consider = [&](const std::string& n) {
        if (n.size() < 2 || n == name || n[1] == '*' || n[1] == '?' || n[1] == '!') return;
        if (n[0] != '$' && n[0] != '@' && n[0] != '%' && n[0] != '&') return;
        cands.push_back(n);
    };
    if (!name.empty() && (name[0] == '$' || name[0] == '@' || name[0] == '%' || name[0] == '&')) {
        for (Env* e = tctx_.cur.get(); e && cands.size() < 4096; e = e->parent.get()) {
            for (auto& kv : e->vars) consider(kv.first);
            if (e->layout)
                for (size_t i = 0; i < e->layout->names.size() && i < 64; i++)
                    if ((e->padLive.load(std::memory_order_acquire) >> i) & 1) consider(e->layout->names[i]);
        }
        // inside a method, the class's attributes: `$name` where `has $.name` meant `$!name`
        if (Value* sp = tctx_.cur ? tctx_.cur->findSelf() : nullptr) {
            ClassInfo* ci = nullptr;
            if (sp->t == VT::Object && sp->obj()) ci = sp->obj()->cls.get();
            else if (sp->t == VT::Type) { auto it = classes_.find(sp->s); if (it != classes_.end()) ci = it->second.get(); }
            for (ClassInfo* c = ci; c; c = c->parent.get())
                for (auto& a : c->attrs) cands.push_back(std::string(1, a.sigil) + "!" + a.name);
        }
    }
    std::vector<std::string> sug = suggestNames(name, cands);
    // …and a TYPE the bare name nearly spells: `$foo` next to `class Foo`
    // suggests Foo (Rakudo's X::Undeclared lists types among its suggestions)
    if (name.size() > 1 && (name[0] == '$' || name[0] == '@' || name[0] == '%')) {
        std::vector<std::string> typeCands;
        for (auto& kv : classes_)
            if (kv.first.find("::") == std::string::npos && !kv.first.empty() &&
                ascii::isupper((unsigned char)kv.first[0]) && kv.second && !kv.second->isRole)
                typeCands.push_back(kv.first);
        if (typeCands.size() < 2000)
            for (auto& t : suggestNames(name.substr(1), typeCands)) sug.push_back(t);
    }
    std::string msg = "Variable '" + name + "' is not declared";
    Value sl = Value::array(); sl.isList = true;
    for (auto& n : sug) sl.arr()->push_back(Value::str(n));
    if (sug.size() == 1) msg += ". Did you mean '" + sug[0] + "'?";
    else if (!sug.empty()) {
        msg += ". Did you mean any of these: ";
        for (size_t i = 0; i < sug.size(); i++) msg += (i ? ", '" : "'") + sug[i] + "'";
        msg += "?";
    }
    Value he = Value::array(); he.isList = true;
    throwTypedV("X::Undeclared", {{"symbol", Value::str(name)}, {"what", Value::str("Variable")},
                                  {"suggestions", sl}, {"pre", Value::str("")}, {"post", Value::str(name)},
                                  {"highexpect", he}}, msg);
}

// A declared type that names no type: a package (`my A $a`), or a type
// argument nothing declares (`my Array[Numerix] $x`).
// `array[Int]` — a native array takes only a native element type; Rakudo
// finds out while composing the type, at BEGIN time
void Interpreter::checkNativeArrayParam(const std::string& t) {
    if (t.size() < 8 || t.compare(0, 6, "array[") != 0 || t.back() != ']') return;
    std::string inner = t.substr(6, t.size() - 7);
    if (inner.empty() || isNativeTypeName(inner) || !ascii::isupper((unsigned char)inner[0])) return;
    Value e = makeTypedEx("X::AdHoc", {{"payload", Value::str("Can only parameterize array with a native type, not " + inner)}},
                          "Can only parameterize array with a native type, not " + inner);
    throwTypedV("X::Comp::BeginTime", {{"exception", e}, {"use-case", Value::str("parameterizing array")}},
                "An exception occurred while parameterizing array: Can only parameterize array with a native type, not " + inner);
}

void Interpreter::checkDeclTypeSane(const VarExpr* ve) {
    checkNativeArrayParam(ve->declType);
    // `my package A {}; my A $a` — a package is no type
    if (!ve->declType.empty() && nameIsPackageNow(ve->declType))
        throwTypedV("X::Syntax::Variable::BadType", {{"type", Value::typeObj(ve->declType)}},
                    "'" + ve->declType + "' cannot be used as a type");
    // `my Array[Numerix] $x` — a type argument nothing declares
    if (ve->declType.find('[') != std::string::npos) {
        const std::string& dt = ve->declType;
        size_t i = 0;
        while (i < dt.size()) {
            if (!ascii::isupper((unsigned char)dt[i]) || (i > 0 && (ascii::isalnum((unsigned char)dt[i - 1]) || dt[i - 1] == ':' || dt[i - 1] == '-' || dt[i - 1] == '_'))) { i++; continue; }
            size_t j = i;
            while (j < dt.size() && (ascii::isalnum((unsigned char)dt[j]) || dt[j] == '_' || dt[j] == '-' ||
                                     (dt[j] == ':' && j + 1 < dt.size() && dt[j + 1] == ':') ||
                                     (dt[j] == ':' && j > 0 && dt[j - 1] == ':'))) j++;
            std::string tn = dt.substr(i, j - i);
            i = j;
            if (tn.find("::") != std::string::npos) continue;
            if (classes_.count(tn) || isKnownTypeName(tn) || subsets_.count(tn) || isNativeTypeName(tn) ||
                classes_.count(resolveClassAlias(tn)) || tctx_.cur->find(tn) ||
                (!tctx_.pkgPrefix.empty() && classes_.count(tctx_.pkgPrefix + tn))) continue;
            const Program* unit = unitCurrent();
            if (!unit || unit->typeNamesOpaque || unit->declaredTypeNames.count(tn)) continue;
            throw RakuError{Value::typeObj("X::Undeclared::Symbols"), "Undeclared name '" + tn + "'"};
        }
    }
}

void Interpreter::throwTyped(const std::string& type,
                             std::vector<std::pair<std::string, std::string>> attrs,
                             const std::string& message) {
    std::vector<std::pair<std::string, Value>> va;
    va.reserve(attrs.size());
    for (auto& kv : attrs)
        // a `type` attribute naming a core type is the type OBJECT (so
        // throws-like `type => Array` smartmatches), not the string
        if ((kv.first == "type" && isKnownTypeName(kv.second)) ||
            (type == "X::Syntax::Variable::ConflictingTypes" && (kv.first == "outer" || kv.first == "inner")))
            va.emplace_back(kv.first, Value::typeObj(kv.second));
        // …and a FLAG is a Bool (X::Method::NotFound's `private`)
        else if (kv.first == "private" && (kv.second == "True" || kv.second == "False"))
            va.emplace_back(kv.first, Value::boolean(kv.second == "True"));
        // …and a compile error's `line` is an Int, as Rakudo's X::Comp has it
        // (`$!.line` of a failed EVAL was the Str "2")
        else if (kv.first == "line" && !kv.second.empty() && kv.second.size() < 10 &&
                 kv.second.find_first_not_of("0123456789") == std::string::npos)
            va.emplace_back(kv.first, Value::integer(std::stoll(kv.second)));
        // …and X::Comp::Trait::Scope's `supported` is a LIST of scopes
        else if (kv.first == "supported" && type == "X::Comp::Trait::Scope") {
            Value l = Value::array(); l.isList = true;
            l.arr()->push_back(Value::str(kv.second));
            va.emplace_back(kv.first, l);
        }
        else
            va.emplace_back(kv.first, Value::str(kv.second));
    throwTypedV(type, std::move(va), message);
}

// Build the effective symbol name of a (possibly multi-segment) symbolic ref:
// join `pkg` + the head + `::seg` parts, hoist a sigil on the LAST segment to
// the front (`OUR::('$x')` looks up `$OUR::x`), then rewrite pseudo-package
// heads (GLOBAL:: strips, OUR:: is the current package, MY::/UNIT::/OUTER::/
// CALLER::/SETTING::/CORE:: fall back to the lexical chain — approximations).
std::string Interpreter::symRefName(SymbolicRef* sr, bool* callerHead, std::string* rawOut, bool* settingTail) {
    if (callerHead) *callerHead = false;
    if (settingTail) *settingTail = false;
    std::string nm;
    if (sr->nameExpr) nm = eval(sr->nameExpr.get()).toStr();
    for (auto& sg : sr->segs) {
        if (!nm.empty()) nm += "::";
        nm += eval(sg.get()).toStr();
    }
    if (!sr->pkg.empty()) nm = sr->pkg + "::" + nm;
    // a sigil written on the last path part names the variable: A::B::$x == $A::B::x
    size_t lastSep = nm.rfind("::");
    if (lastSep != std::string::npos && lastSep + 2 < nm.size() &&
        std::strchr("$@%&", nm[lastSep + 2]))
        nm = nm[lastSep + 2] + nm.substr(0, lastSep + 2) + nm.substr(lastSep + 3);
    if (!sr->sigil.empty() && (nm.empty() || !std::strchr("$@%&", nm[0])))
        nm = sr->sigil + nm;
    if (rawOut) *rawOut = nm;   // before the pseudo-package heads come off
    // pseudo-package heads
    std::string sig;
    if (!nm.empty() && std::strchr("$@%&", nm[0])) { sig = nm.substr(0, 1); nm = nm.substr(1); }
    for (;;) {
        // `CALLER::` joined in front of a sigiled name (`CALLER::&SETTING::not`):
        // once the head is off, the sigil is exposed — set it aside and go on
        if (sig.empty() && !nm.empty() && std::strchr("$@%&", nm[0])) { sig = nm.substr(0, 1); nm = nm.substr(1); }
        if      (nm.rfind("GLOBAL::",  0) == 0) nm = nm.substr(8);
        else if (nm.rfind("OUR::",     0) == 0) nm = tctx_.pkgPrefix + nm.substr(5);
        else if (nm.rfind("MY::",      0) == 0) nm = nm.substr(4);
        else if (nm.rfind("UNIT::",    0) == 0) nm = nm.substr(6);
        else if (nm.rfind("OUTER::",   0) == 0) nm = nm.substr(7);
        else if (nm.rfind("CALLER::",  0) == 0) { nm = nm.substr(8); if (callerHead) *callerHead = true; } // the caller's frame — see the read site
        else if (nm.rfind("CALLERS::", 0) == 0) { nm = nm.substr(9); if (callerHead) *callerHead = true; }
        // `CALLER::SETTING::not` / `CALLER::CORE::not` — in 6.e the SETTING
        // of the caller is CORE whatever the caller has shadowed, so once a
        // setting head is met the caller's frame is no longer where to look:
        // the name is the builtin's (roast S02-names/SETTING-6e.t reaches
        // CORE's negating `&not` through exactly this chain, past a `sub not`
        // identity shadow in the frame the CALLER:: head would have searched).
        // Under 6.c/6.d an EVAL's SETTING is the code that called it, shadows
        // and all, so there the lexical lookup stands (pseudo-6d.t).
        else if (nm.rfind("SETTING::", 0) == 0) { nm = nm.substr(9); if (sixE() && callerHead && *callerHead) { *callerHead = false; if (settingTail) *settingTail = true; } }
        else if (nm.rfind("CORE::",    0) == 0) { nm = nm.substr(6); if (sixE() && callerHead && *callerHead) { *callerHead = false; if (settingTail) *settingTail = true; } }
        else break;
    }
    return sig + nm;
}

// A coercion-type parameter `T(F) $x`: the argument must be an F (with its
// smiley), is coerced unless it already is a T, and the result must BE a T
// (with the target's smiley) — else X::Coerce::Impossible.
// `method foo(T $var)` in a `role R[::T]` composed as `R[Str:D(Numeric)]`: the
// parameter's type is what T was given, and a COERCION type converts the value
// exactly as the same type written out would (S12-coercion/parameterized.t).
// True when it did; the value is then bound as converted.
bool Interpreter::coerceViaTypeVar(const Param& p, Value& v, Env* env) {
    if (p.coerce || p.typeCapture || p.sigil != '$' || p.type.empty() || !env ||
        !ascii::isupper((unsigned char)p.type[0]) || p.type.find('(') != std::string::npos ||
        classes_.count(p.type) || isKnownTypeName(p.type))
        return false;
    Value* tv = env->find(p.type);
    if (!tv || tv->t != VT::Type || tv->s == p.type) return false;
    const std::string ts = tv->s.str();
    const size_t o = ts.find('(');
    if (o == std::string::npos || o == 0 || ts.back() != ')') return false;
    std::string target = ts.substr(0, o), from = ts.substr(o + 1, ts.size() - o - 2);
    int dc = 0;
    if (target.size() > 2 && target[target.size() - 2] == ':') {
        dc = target.back() == 'D' ? 1 : target.back() == 'U' ? 2 : 0;
        target.resize(target.size() - 2);
    }
    coerceParam(p, v, &target, &from, dc);
    return true;
}

// (the overrides describe a coercion the parameter's TYPE VARIABLE resolved to:
// `method foo(T $var)` in a role composed as `R[Str:D(Numeric)]`)
void Interpreter::coerceParam(const Param& p, Value& v, const std::string* typeOverride,
                              const std::string* fromOverride, int defOverride) {
    const std::string& ptype = typeOverride ? *typeOverride : p.type;
    const std::string& coerceFrom = fromOverride ? *fromOverride : p.coerceFrom;
    const int defConstraint = defOverride >= 0 ? defOverride : p.defConstraint;
    std::string from = coerceFrom; int fsm = 0;
    if (from.size() > 2 && from[from.size() - 2] == ':') {
        char c = from.back(); fsm = c == 'D' ? 1 : c == 'U' ? 2 : 0; from.resize(from.size() - 2);
    }
    auto render = [&]() {
        std::string t = ptype + (defConstraint == 1 ? ":D" : defConstraint == 2 ? ":U" : "");
        return t + "(" + coerceFrom + ")";
    };
    // A coercion type accepts its TARGET as well as its source: `Int(Str)`
    // binds an Int as it is, and XML's `IO::Path(Str) $src` an IO::Path. (A
    // List is not the Array it would coerce to — that case goes on below.)
    {
        const bool definedArg = isDefined(v);
        if (!(ptype == "Array" && v.t == VT::Array && v.isList && v.hashKind.empty()) &&
            typeOrSubsetMatches(v, ptype) &&
            !(defConstraint == 1 && !definedArg) && !(defConstraint == 2 && definedArg))
            return;
    }
    // a NESTED from-type, `Str(Rat(Source))`: the argument is a Rat, or a
    // Source coerced to one, before the outer coercion sees it
    if (size_t lp = from.find('('); lp != std::string::npos && from.back() == ')') {
        std::function<bool(Value&, const std::string&)> satisfy = [&](Value& x, const std::string& spec) -> bool {
            size_t l = spec.find('(');
            if (l == std::string::npos || spec.back() != ')') {
                std::string t = spec; int sm = 0;
                if (t.size() > 2 && t[t.size() - 2] == ':') {
                    char c = t.back(); sm = c == 'D' ? 1 : c == 'U' ? 2 : 0; t.resize(t.size() - 2);
                }
                if (sm == 1 && !isDefined(x)) return false;
                if (sm == 2 && isDefined(x)) return false;
                return t.empty() || t == "Any" || t == "Mu" || typeOrSubsetMatches(x, t);
            }
            std::string t = spec.substr(0, l), f = spec.substr(l + 1, spec.size() - l - 2);
            if (typeOrSubsetMatches(x, t)) return true;
            if (!satisfy(x, f)) return false;
            x = coerceToType(x, t);
            return true;
        };
        if (!satisfy(v, from))
            throwTypedV("X::TypeCheck::Binding::Parameter",
                {{"got", v}, {"expected", Value::typeObj(ptype)}, {"symbol", Value::str(p.name)}},
                "Type check failed in binding to parameter '" + paramShownName(p) + "'; expected " + render() +
                " but got " + v.typeName() + " (" + typeCheckRepr(v) + ")");
        from = from.substr(0, lp);
    }
    const bool defined = isDefined(v);
    if ((!from.empty() && from != "Any" && from != "Mu" && !typeOrSubsetMatches(v, from)) ||
        (from.empty() && v.t == VT::Type && v.s == "Mu") ||
        (fsm == 1 && !defined) || (fsm == 2 && defined))
        throwTypedV("X::TypeCheck::Binding::Parameter",
            {{"got", v}, {"expected", Value::typeObj(ptype)}, {"symbol", Value::str(p.name)}},
            "Type check failed in binding to parameter '" + paramShownName(p) + "'; expected " + render() +
            " but got " + v.typeName() + " (" + typeCheckRepr(v) + ")");
    // A DEFINITE target is never reached from a type object: `Str:D()` given
    // `Int` dies rather than binding `Int.Str`'s empty string (Rakudo dies here
    // too, each target in its own words). The target's own type object is the
    // Impossible coercion below.
    if (defConstraint == 1 && v.t == VT::Type && !typeOrSubsetMatches(v, ptype))
        throw RakuError{Value::typeObj("X::AdHoc"),
            "Cannot coerce the type object " + v.typeName() + " to " + ptype +
            ":D in binding to parameter '" + paramShownName(p) + "'"};
    // a List is not an Array, whatever they share underneath: `Array(Any)`
    // turns `(1, 2)` into one
    const bool listToArray = ptype == "Array" && v.t == VT::Array && v.isList && v.hashKind.empty();
    if (listToArray || !typeOrSubsetMatches(v, ptype) || (defConstraint == 1 && !defined))
        v = listToArray ? methodCall(v, "Array", {}) : coerceToType(v, ptype);
    else return;
    if ((v.t == VT::Hash && v.hashKind == "Failure")) return;   // the coercer's own failure stands
    if (!typeOrSubsetMatches(v, ptype) || (defConstraint == 1 && !isDefined(v)) ||
        (defConstraint == 2 && isDefined(v)))
        throwTypedV("X::Coerce::Impossible",
            {{"target-type", Value::typeObj(ptype)}, {"from-type", Value::typeObj(v.typeName())}},
            "Impossible coercion from '" + (from.empty() ? std::string("Any") : from) + "' into '" + ptype +
            "': method " + ptype + " returned " + (isDefined(v) ? "an instance" : "a type object") +
            " of " + v.typeName());
}

// A lone typed candidate rejects a mismatched argument (multi dispatch already
// type-selects; without this a single `sub f(Int $x)` bound anything). Type
// objects/undefined bind (no :D enforcement here) and junction kinds pass —
// they were autothreaded upstream; one reaching here is a matcher-style arg.
// How a type-check failure renders the offending value. Rakudo uses `.raku`, not
// `.gist` — so a Str keeps its quotes, a List separates with commas and a Hash
// uses the colon-pair form — and elides once the repr exceeds 23 characters,
// keeping 20 plus "...". (Measured against the oracle at every length either
// side of the boundary.) Five message builders each rendered this their own way;
// they share this one now.
std::string typeCheckRepr(const Value& v) {
    std::string r = g_rakuRepr ? g_rakuRepr(v) : v.gist();
    // an itemised value reprs as `$(1, 2)`; the message shows the bare `(1, 2)`
    if (r.size() > 1 && r[0] == '$' && (r[1] == '(' || r[1] == '[' || r[1] == '{')) r.erase(0, 1);
    return r.size() > 23 ? r.substr(0, 20) + "..." : r;
}
// Value::natWidthOfType, memoised per parameter: it substr()s the type name, so
// calling it per bind allocated a std::string for every typed parameter of every
// call. Answer encoded as bits<<1 | signed (0 = not a native-width type).
int paramNatSpec(const Param& p) {
    int spec = p.natSpec;
    if (spec < 0) {
        bool sg; int bits = Value::natWidthOfType(p.type, sg);
        spec = (bits << 1) | (sg ? 1 : 0);
        p.natSpec = spec;
    }
    return spec;
}

// `&code:(Int --> Bool)`: does the Callable's own signature satisfy the one
// the parameter spells? Rakudo's Signature.ACCEPTS(Signature) — positionals
// pairwise (each of the code's parameter types must conform to the
// constraint's), no required parameter left over on either side, and the
// SAME return type.
bool Interpreter::codeSigAccepts(const Param& p, const Value& code, Env* sigEnv) {
    if (!p.codeSig) return true;
    if (code.t != VT::Code || !code.code()) return code.t == VT::Regex;
    const Callable& c = *code.code();
    // the code's positionals, as Parameters would list them
    struct TP { std::string type; bool optional, slurpy; };
    std::vector<TP> tpos;
    if (c.params && (c.hadSig || !c.params->empty())) {
        for (auto& q : *c.params) {
            if (q.named || q.invocant) continue;
            bool sl = q.slurpy || q.sigil == '|';
            tpos.push_back({q.type.empty() ? std::string(c.isBlock ? "Mu" : "Any") : q.type,
                            q.optional || q.defaultVal != nullptr, sl});
        }
    }
    else if (!c.placeholders.empty()) {
        for (auto& ph : c.placeholders)
            if (!ph.empty() && ph.find(':') == std::string::npos) tpos.push_back({"Mu", false, false});
    }
    else if (c.isBlock) tpos.push_back({"Mu", true, false});   // `{ … }`: an optional $_
    std::vector<const Param*> spos;
    for (auto& q : *p.codeSig) if (!q.named && !q.invocant) spos.push_back(&q);
    size_t si = 0, ti = 0;
    while (si < spos.size()) {
        if (ti >= tpos.size()) break;
        const Param* sp = spos[si++];
        const TP& tp = tpos[ti++];
        if (sp->slurpy || sp->sigil == '|') { si = spos.size(); ti = tpos.size(); break; }
        if (tp.slurpy) {
            bool any = false;
            for (size_t k = si - 1; k < spos.size(); k++)
                if (spos[k]->slurpy || spos[k]->sigil == '|') any = true;
            if (!any) return false;
            si = spos.size(); ti = tpos.size(); break;
        }
        const bool sOpt = sp->optional || sp->defaultVal != nullptr;
        if (!sOpt && tp.optional) return false;
        std::string st = sp->type.empty() ? std::string("Mu") : sp->type;
        // `&fn:(T)` after `::T $v`: the capture as bound for this call
        if (sigEnv && st != "Mu" && !classes_.count(st) && !isKnownTypeName(st))
            if (Value* tv = sigEnv->find(st))
                if (tv->t == VT::Type && !tv->s.empty()) st = tv->s.str();
        if (st != "Mu" && tp.type != st && !typeNameConforms(tp.type, st, std::string(), std::string()))
            return false;
    }
    if (ti < tpos.size()) {
        // a leftover OPTIONAL parameter of the code takes nothing and is fine
        for (; ti < tpos.size(); ti++) if (!tpos[ti].optional && !tpos[ti].slurpy) return false;
    }
    if (si < spos.size()) {
        const Param* sp = spos[si];
        if (!(sp->optional || sp->defaultVal || sp->slurpy || sp->sigil == '|')) return false;
    }
    auto norm = [](std::string t) {
        if (size_t x = t.find('\x01'); x != std::string::npos) t = t.substr(0, x);
        return t.empty() ? std::string("Mu") : t;
    };
    return norm(p.codeSigRet) == norm(c.retType);
}

// Does an argument satisfy a parameter type-constraint name?
// Package-relative short-name view for the type matchers (free/static functions):
// set by the Interpreter so `has Path $.path` accepts a URI::Path object when
// `Path` is an alias. Null in the compiled-runtime path (no aliasing there).
const std::unordered_map<std::string, std::string>* s_classAliases = nullptr;
const std::unordered_map<std::string, std::shared_ptr<ClassInfo>>* s_classesForAlias = nullptr;
void rtSetAliasView(const std::unordered_map<std::string, std::string>* a,
                    const std::unordered_map<std::string, std::shared_ptr<ClassInfo>>* c) {
    s_classAliases = a; s_classesForAlias = c;
}

// The parameters of a type as the engine spells them, split into components
// with their definedness smileys reattached. A type object keeps `Array[Int:D]`
// as "Int,D" - the smiley rides as a trailing pseudo-parameter - and a hash's
// value and key types as "Int,Str", so "Int,D,Str" reads back as {"Int:D", "Str"}.
std::vector<std::string> typeParamParts(const std::string& ofType) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= ofType.size()) {
        size_t c = ofType.find(',', pos);
        std::string part = ofType.substr(pos, c == std::string::npos ? std::string::npos : c - pos);
        if ((part == "D" || part == "U" || part == "_") && !out.empty()) out.back() += ":" + part;
        else if (!part.empty()) out.push_back(part);
        if (c == std::string::npos) break;
        pos = c + 1;
    }
    return out;
}
// Is the parameterized role `have` (as written: `R2[Int]`) a `want` (`R2[Cool]`)?
// The same role, and each argument of `have` conforms to the matching one of
// `want` — role parameters are covariant (`R1[Cool] ~~ R1[Any]`).
bool roleArgsCovariant(const std::string& have, const std::string& want) {
    size_t hb = have.find('['), wb = want.find('[');
    if (hb == std::string::npos || wb == std::string::npos || have.back() != ']' || want.back() != ']') return false;
    if (have.compare(0, hb, want, 0, wb) != 0 || hb != wb) return false;
    auto split = [](const std::string& s) {
        std::vector<std::string> out; int d = 0; std::string cur;
        for (char c : s) {
            if (c == '[') d++; else if (c == ']') d--;
            if (c == ',' && d == 0) { out.push_back(cur); cur.clear(); continue; }
            cur += c;
        }
        out.push_back(cur);
        return out;
    };
    auto ha = split(have.substr(hb + 1, have.size() - hb - 2));
    auto wa = split(want.substr(wb + 1, want.size() - wb - 2));
    if (ha.size() != wa.size()) return false;
    for (size_t i = 0; i < ha.size(); i++) {
        if (ha[i] == wa[i] || wa[i] == "Mu" || (wa[i] == "Any" && ha[i] != "Mu")) continue;
        if (!ascii::isupper((unsigned char)ha[i][0]) || !ascii::isupper((unsigned char)wa[i][0])) return false;
        if (ha[i].find('[') != std::string::npos && roleArgsCovariant(ha[i], wa[i])) continue;
        if (!typeNameConforms(ha[i], wa[i], std::string(), std::string())) return false;
    }
    return true;
}
// Does the TYPE NAME `ln` conform to `rn`? The built-in "does" table, the
// numeric/string tower, and the user class/role ancestry — one answer, shared by
// the `~~` operator and by parameter dispatch. They used to disagree: `~~` knew
// all of this while dispatch answered a blanket true for any type object, so
// `uri-escape(Str)` bound a `Match $s` parameter.
bool typeNameConforms(const std::string& lnIn, const std::string& rn,
                             const std::string& lOfType, const std::string& rOfType) {
    // a composed spelling ("CArray[int32]", the way a CArray instance names
    // itself) carries the parameter inside the name
    std::string ln = lnIn, lp = lOfType;
    size_t br = ln.find('[');
    if (br != std::string::npos) {
        if (lp.empty() && ln.back() == ']') lp = ln.substr(br + 1, ln.size() - br - 2);
        ln = ln.substr(0, br);
    }
    if (ln == rn) return true;
    static const std::map<std::string, std::set<std::string>> typeDoes = {
        // …and NOT Array or List: Rakudo's native array is its own type
        {"array", {"array", "Positional", "Iterable", "Cool"}},
        {"Array", {"Array", "List", "Positional", "Iterable", "Cool"}},
        {"List",  {"List", "Positional", "Iterable", "Cool"}},
        // (a Seq is no List and, to `~~`, no Positional: it does Sequence and
        // PositionalBindFailover, which is how it reaches an `@` parameter)
        {"Seq",   {"Seq", "Sequence", "PositionalBindFailover", "Iterable", "Cool"}},
        {"Slip",  {"Slip", "List", "Positional", "Iterable", "Cool"}},
        // `.backtrace` answers a Backtrace (a List of frames here)
        {"Backtrace", {"Backtrace", "List", "Positional", "Iterable", "Cool"}},
        {"WalkList", {"WalkList", "List", "Positional", "Iterable", "Cool"}},
        // a Range is Positional as well as Iterable: `("0".."9") ~~ Positional`
        // is how a module asks "is this a set of things I can draw from"
        {"Range", {"Range", "Positional", "Iterable", "Cool"}},
        {"Hash",  {"Hash", "Map", "Associative", "Cool"}},
        {"Map",   {"Map", "Associative", "Cool"}},
        {"Set",   {"Set", "Setty", "QuantHash", "Associative"}},
        {"SetHash", {"SetHash", "Setty", "QuantHash", "Associative"}},
        {"Bag",   {"Bag", "Baggy", "QuantHash", "Associative"}},
        {"BagHash", {"BagHash", "Baggy", "QuantHash", "Associative"}},
        {"Mix",   {"Mix", "Mixy", "Baggy", "QuantHash", "Associative"}},
        {"MixHash", {"MixHash", "Mixy", "Baggy", "QuantHash", "Associative"}},
        // the ROLES of that family are Associative too
        {"QuantHash", {"QuantHash", "Associative"}},
        {"Setty", {"Setty", "QuantHash", "Associative"}},
        {"Baggy", {"Baggy", "QuantHash", "Associative"}},
        {"Mixy",  {"Mixy", "Baggy", "QuantHash", "Associative"}},
        {"Pair",  {"Pair", "Associative"}},
        {"Date",     {"Date", "Dateish"}},
        {"DateTime", {"DateTime", "Dateish"}},
        {"IO::Path",   {"IO::Path", "IO", "Cool"}},
        {"IO::Path::Win32",  {"IO::Path::Win32", "IO::Path", "IO", "Cool"}},
        {"IO::Path::Unix",   {"IO::Path::Unix", "IO::Path", "IO", "Cool"}},
        {"IO::Path::Cygwin", {"IO::Path::Cygwin", "IO::Path", "IO", "Cool"}},
        {"IO::Path::QNX",    {"IO::Path::QNX", "IO::Path", "IO", "Cool"}},
        {"IO::Handle", {"IO::Handle", "IO"}},
        {"IO::Special", {"IO::Special", "IO"}},
        {"Grammar", {"Grammar", "Match", "Capture", "Cool"}},
        {"Match",   {"Match", "Capture", "Cool"}},
        // a synchronous socket does the IO::Socket role — but is not an IO
        {"IO::Socket::INET", {"IO::Socket::INET", "IO::Socket"}},
        // The Uni family: each normalisation form is a Uni SUBCLASS, and Uni
        // does Positional/Iterable — `"x".NFD ~~ Uni` is True on Rakudo, and
        // JSON::Fast binds `Uni:D \codes` to exactly such a value.
        // The byte-buffer family: a Buf DOES Blob, both are Positional + Stringy,
        // and neither is Cool (an instance answers False on Rakudo). Without the
        // Buf→Blob step `Buf ~~ Blob` was False, and DBDish::mysql then decided a
        // BLOB column needed .decode.
        {"Blob",  {"Blob", "Positional", "Stringy"}},
        // a CArray does Positional/Iterable (NativeCall's class declares both) —
        // absent here, `$carray ~~ Positional:D` was False and every module
        // guarding its native path on it fell through (Math::DistanceFunctions)
        {"CArray", {"CArray", "Positional", "Iterable"}},
        {"Buf",   {"Buf", "Blob", "Positional", "Stringy"}},
        {"blob8", {"blob8", "Blob", "Positional", "Stringy"}},
        {"blob16",{"blob16", "Blob", "Positional", "Stringy"}},
        {"blob32",{"blob32", "Blob", "Positional", "Stringy"}},
        {"blob64",{"blob64", "Blob", "Positional", "Stringy"}},
        {"buf8",  {"buf8", "Buf", "Blob", "Positional", "Stringy"}},
        {"buf16", {"buf16", "Buf", "Blob", "Positional", "Stringy"}},
        {"buf32", {"buf32", "Buf", "Blob", "Positional", "Stringy"}},
        {"buf64", {"buf64", "Buf", "Blob", "Positional", "Stringy"}},
        {"utf8",  {"utf8", "Blob", "Positional", "Stringy"}},
        {"utf16", {"utf16", "Blob", "Positional", "Stringy"}},
        {"utf32", {"utf32", "Blob", "Positional", "Stringy"}},
        {"ValueObjAt", {"ValueObjAt", "ObjAt"}},
        {"Uni",  {"Uni", "Positional", "Iterable"}},
        {"NFC",  {"NFC", "Uni", "Positional", "Iterable"}},
        {"NFD",  {"NFD", "Uni", "Positional", "Iterable"}},
        {"NFKC", {"NFKC", "Uni", "Positional", "Iterable"}},
        {"NFKD", {"NFKD", "Uni", "Positional", "Iterable"}},
    };
    auto td = typeDoes.find(ln);
    bool baseOk = (td != typeDoes.end() && td->second.count(rn));
    if (!baseOk) { // numeric/string tower (Int -> Real -> Numeric -> Cool -> Any -> Mu)
        static const std::map<std::string, std::vector<std::string>> tower = {
            {"Int", {"Real", "Numeric", "Cool"}}, {"Num", {"Real", "Numeric", "Cool"}},
            {"Rat", {"Rational", "Real", "Numeric", "Cool"}},
            {"FatRat", {"Rational", "Real", "Numeric", "Cool"}}, // NOT a Rat: both do Rational
            {"Complex", {"Numeric", "Cool"}},
            {"Str", {"Stringy", "Cool"}},
            // Bool is an Int-backed enum: its MRO is Bool, Int, Cool, Any, Mu, so
            // the TYPE object conforms to Int/Real/Numeric as its values do
            // (`Bool ~~ Int` is True on Rakudo; it answered False here)
            {"Bool", {"Int", "Real", "Numeric", "Cool"}},
        };
        auto tw = tower.find(ln);
        if (tw != tower.end()) for (auto& anc : tw->second) if (anc == rn) { baseOk = true; break; }
        // …and the full built-in ancestry (typeAncestry is what .isa/.does/.^mro
        // read; the two tables had drifted — `IntStr ~~ Str` was False here)
        if (!baseOk) for (auto& anc : typeAncestry(ln)) if (anc == rn && anc != "Any" && anc != "Mu") { baseOk = true; break; }
    }
    // ...and the role's parameter, when it has one: `Array[Int]` is a
    // `Positional[Cool]` and `Array[Str:D]` a `Positional[Str]`
    // (roleParamConforms has the rules)
    if (baseOk) return rOfType.empty() || roleParamConforms(typeParamParts(lp), typeParamParts(rOfType));
    // a user type object matches its own ancestry: parent classes, composed
    // roles, and roles/parents anywhere up the chain — including a BUILT-IN
    // parent (`is Str`, or the implicit Grammar) and everything above it
    if (g_matchClasses) {
        // a role PUN (`R[Int]`) is asked by the parameterization it spells:
        // a class that does R[Int] conforms, one that does R[Str] does not
        std::string rdisp;
        if (rn.find('\x01') != std::string::npos) {
            auto ri = g_matchClasses->find(rn);
            if (ri != g_matchClasses->end() && ri->second) rdisp = ri->second->dispName;
        }
        auto conformsFrom = [&](ClassInfo* start) -> bool {
            for (ClassInfo* c = start; c; c = c->parent.get()) {
                if (c->name == rn || c->doneRoles.count(rn)) return true;
                if (!rdisp.empty() && c->doneRoles.count(rdisp)) return true;
                if (!rdisp.empty())
                    for (auto& dr : c->doneRoles)
                        if (dr.find('[') != std::string::npos && roleArgsCovariant(dr, rdisp)) return true;
                if (!c->nativeParent.empty()) {
                    if (c->nativeParent == rn) return true; // ancestry may lack table entries
                    for (auto& anc : typeAncestry(c->nativeParent)) if (anc == rn) return true;
                }
                for (auto& p : c->extraParents)
                    if (p && (p->name == rn || p->doneRoles.count(rn))) return true;
            }
            return false;
        };
        // A parametric role's bare name is its GROUP, and a group conforms to
        // what its NON-parameterized member inherits and does — never to a
        // parameterized member's parent: with `role R is A {}` and `role R[::T]
        // is B {}`, R ~~ A and R !~~ B, and a group of parameterized members
        // alone conforms to nothing it inherits. A curried `R[Int]` (a pun)
        // conforms through its own variant AND through its group
        // (S14-roles/typecheck.t, checked against Rakudo).
        auto groupConforms = [&](const std::string& group) -> bool {
            auto git = g_matchClasses->find(group);
            if (git == g_matchClasses->end() || !git->second) return false;
            ClassInfo* g = git->second.get();
            auto plain = [](ClassInfo* v) {
                return v && v->decl && v->decl->roleParams.empty() && !v->decl->parameterized;
            };
            if (plain(g)) return conformsFrom(g);
            for (auto& v : g->roleVariants) if (plain(v.get())) return conformsFrom(v.get());
            return false;
        };
        auto itc = g_matchClasses->find(ln);
        if (itc != g_matchClasses->end() && itc->second) {
            ClassInfo* ci = itc->second.get();
            if (ci->isRole && ci->decl) {
                size_t px = ci->name.find('\x01');
                if (px != std::string::npos)   // a curried pun
                    return conformsFrom(ci) || groupConforms(ci->name.substr(0, px));
                return groupConforms(ln);
            }
            return conformsFrom(ci);
        }
    }
    return false;
}

// Does value v satisfy subset `name` (base type chain + where constraint)?
// From 6.e a subset's implicit default is its nominalization, so a bare
// `my S $s;` whose subset refuses that type object (`of Int:D`, `where
// .defined`) has nothing to hold.
void Interpreter::checkBareSubsetDecl(const VarExpr* ve, char sigil) {
    if (!ve->declBare || sigil != '$' || ve->declDefault || ve->declType.empty()) return;
    auto si = subsets_.find(ve->declType);
    // a DEFINITE coercion base (`subset S of Str:D(Rat)`) has no type object
    // to start from under any revision (S02-types/nominalizables.t)
    if (si != subsets_.end() && si->second.coerce && si->second.defConstraint == 1)
        throwTypedV("X::Syntax::Variable::MissingInitializer",
            {{"type", Value::str(ve->declType)}, {"implicit", Value::str(": by pragma")}},
            "Variable definition of type " + ve->declType +
            " (implicit : by pragma) needs to be given an initializer");
    if (si == subsets_.end() || si->second.langRev < 2) return;
    std::string base = si->second.base;
    for (int guard = 0; guard < 16; guard++) {
        auto sj = subsets_.find(base);
        if (sj == subsets_.end()) break;
        base = sj->second.base;
    }
    auto colon = base.find(':');
    if (colon != std::string::npos) base.resize(colon);
    if (!base.empty() && !subsetMatches(ve->declType, Value::typeObj(base), 0))
        throwTypedV("X::Syntax::Variable::MissingInitializer",
            {{"type", Value::str(ve->declType)}, {"implicit", Value::str(": by pragma")}},
            "Variable definition of type " + ve->declType +
            " (implicit : by pragma) needs to be given an initializer");
}

bool Interpreter::subsetMatches(const std::string& name, const Value& v, int depth) {
    if (depth > 16) return false; // subset cycle backstop
    // TYPE-OBJECT arguments memoize per (subset, type): a where-clause over a
    // type object is deterministic per type (it introspects the type, not
    // runtime state — the same bet Rakudo's type-check cache makes), and
    // multi dispatch re-runs these constantly. JSON::Unmarshal's ClassLike /
    // PosNoAccessor subsets each run `.^attributes.first({...})` per check;
    // uncached, License::SPDX's 700-license unmarshal (~6,000 dispatches)
    // ran for minutes. Instance arguments stay uncached — their where
    // clauses see values.
    thread_local std::map<std::string, bool> typeSubsetCache;
    std::string cacheKey;
    if (v.t == VT::Type) {
        cacheKey = name + "|" + v.s + "[" + v.ofType() + "]";
        auto ci = typeSubsetCache.find(cacheKey);
        if (ci != typeSubsetCache.end()) return ci->second;
    }
    auto it = subsets_.find(name);
    if (it == subsets_.end()) {
        bool r = typeMatchesResolved(v, name);
        if (!cacheKey.empty()) typeSubsetCache[cacheKey] = r;
        return r;
    }
    const SubsetInfo& si = it->second;
    // `subset CreditCard of Str() where …` — the base is a COERCION, so the
    // question is not "is this a Str" but "can this become one": Business::
    // CreditCard's suite asks about card numbers written as Int literals.
    // Everything below (the smiley, the where clause) sees the COERCED value.
    Value coerced;
    const Value* vp = &v;
    // A coercion type ACCEPTS its target and its source: `of Num(Str)` takes a
    // Num or a Str (the Str type object too), and not an Int (S02-types/
    // nominalizables.t). Only a defined source is coerced for the checks below.
    if (si.coerce && !si.base.empty() && !si.coerceFrom.empty() && si.coerceFrom != "Any" &&
        si.coerceFrom != "Mu" && !subsetMatches(si.base, v, depth + 1)) {
        std::string f = si.coerceFrom;
        if (f.size() > 2 && f[f.size() - 2] == ':') f.resize(f.size() - 2);
        if (!typeOrSubsetMatches(v, f)) {
            if (!cacheKey.empty()) typeSubsetCache[cacheKey] = false;
            return false;
        }
        // a type object is no DEFINITE target (`of Str:D(Numeric)` refuses Int)
        if (!isDefined(v) && si.defConstraint == 1) {
            if (!cacheKey.empty()) typeSubsetCache[cacheKey] = false;
            return false;
        }
        // nothing further to ask of it: no coercion is needed to say yes
        if (!isDefined(v) || (!si.where && si.defConstraint == 0)) {
            if (!cacheKey.empty()) typeSubsetCache[cacheKey] = true;
            return true;
        }
    }
    if (si.coerce && !si.base.empty() && isDefined(v) && v.typeName() != si.base) {
        try { coerced = coerceToType(v, si.base); }
        catch (...) { if (!cacheKey.empty()) typeSubsetCache[cacheKey] = false; return false; }
        vp = &coerced;
    }
    const Value& sv = *vp;
    if (!si.base.empty() && !subsetMatches(si.base, sv, depth + 1)) {
        if (!cacheKey.empty()) typeSubsetCache[cacheKey] = false;
        return false;
    }
    // the base type's smiley: `subset S of Str:D` accepts no type object,
    // `of Str:U` accepts nothing else
    if ((si.defConstraint == 1 && !isDefined(sv)) ||
        (si.defConstraint == 2 && isDefined(sv))) {
        if (!cacheKey.empty()) typeSubsetCache[cacheKey] = false;
        return false;
    }
    if (si.where) {
        auto env = std::make_shared<Env>(); env->parent = si.declEnv ? si.declEnv : tctx_.cur;
        env->define("$_", sv);
        auto saved = tctx_.cur; tctx_.cur = env;
        bool ok = false;
        try {
            Value cv;
            if (si.whereBlock) cv = *si.whereBlock;
            else {
                cv = eval(const_cast<Expr*>(si.where));
                if (si.where->kind == NK::BlockExpr && cv.t == VT::Code)
                    const_cast<SubsetInfo&>(si).whereBlock = std::make_shared<Value>(cv);
            }
            // `where EXPR` is a smartmatch: a Code/WhateverCode is called with
            // the value; anything else (a type, a junction like
            // `Cro::Message | Cro::Connection`) is smartmatched — NOT boolified
            if (cv.t == VT::Code && cv.code()) {
                Value wr = callCallable(cv, ValueList{sv});
                // `where { … or fail "$num is not even" }` — the constraint's own
                // Failure IS the error to report
                if (wr.t == VT::Hash && wr.hashKind == "Failure" && wr.hash() &&
                    !(wr.hash()->count("handled") && (*wr.hash())["handled"].truthy()))
                    failureDetonate(wr);
                ok = boolify(wr);
            }
            else if (cv.t == VT::Regex) ok = boolify(regexMatch(rxSubject(sv), cv.s, &cv));
            else ok = boolify(smartmatchValue("~~", sv, cv));
        } catch (RakuError& e) {
            tctx_.cur = saved;
            // a `fail`/`die` in the constraint is ITS verdict, message and all
            // (`where { $^n %% 2 or fail "$n is not even" }`); a TYPED error from
            // asking the value something it cannot answer is just "no"
            if (!e.altLabel.empty() || e.payload.typeName() == "X::AdHoc") throw;
            if (!cacheKey.empty()) typeSubsetCache[cacheKey] = false;
            return false;
        } catch (ReturnEx& r) {
            // …which, from a bare block, arrives as a return carrying the Failure
            tctx_.cur = saved;
            tctx_.returning = false;
            if (r.v.t == VT::Hash && r.v.hashKind == "Failure") failureDetonate(r.v);
            if (!cacheKey.empty()) typeSubsetCache[cacheKey] = false;
            return false;
        } catch (...) { tctx_.cur = saved; if (!cacheKey.empty()) typeSubsetCache[cacheKey] = false; return false; }
        tctx_.cur = saved;
        if (!cacheKey.empty()) typeSubsetCache[cacheKey] = ok;
        return ok;
    }
    if (!cacheKey.empty()) typeSubsetCache[cacheKey] = true;
    return true;
}

// A parameter typed by a COERCION SUBSET (`subset CC of Str() where …`) binds
// the COERCED value, exactly as a plain `Str() $x` parameter does — the routine
// then works on the Str, whatever the caller wrote.
bool Interpreter::isCoercionSubset(const std::string& type) const {
    auto it = subsets_.find(type);
    return it != subsets_.end() && it->second.coerce && !it->second.base.empty();
}

Value Interpreter::coerceViaSubset(const Value& v, const std::string& type) {
    auto it = subsets_.find(type);
    if (it == subsets_.end() || !it->second.coerce || it->second.base.empty()) return v;
    if (!isDefined(v) || v.typeName() == it->second.base) return v;
    try { return coerceToType(v, it->second.base); } catch (...) { return v; }
}

// An enum's value type — what its members are (`enum Color <R G B>` is Int,
// `enum S (a => "x")` Str) — read off its first member.
std::string Interpreter::enumBaseType(const std::string& enumType) {
    auto ep = enumPairs_.find(enumType);
    if (ep == enumPairs_.end() || !ep->second.arr() || ep->second.arr()->empty()) return "Int";
    const Value& p0 = (*ep->second.arr())[0];
    const Value* v0 = p0.t == VT::Pair ? p0.pairVal() : nullptr;
    if (!v0) return "Int";
    return v0->t == VT::Int ? std::string("Int") : v0->typeName();
}

bool Interpreter::typeOrSubsetMatches(const Value& v, const std::string& type) {
    if (subsets_.count(type)) return subsetMatches(type, v);
    return typeMatchesResolved(v, type);
}

// The ELEMENT type a container constrains its slots to, "" for an unconstrained
// one. A container's `ofType` is the declarator's encoding: an Array's is the
// element type outright, a Hash's is "valueType,keyType" — the VALUE half is
// what an element assignment has to satisfy. Any/Mu constrain nothing, and the
// NATIVE lowercase types have their own (narrower, pre-existing) checks.
// The first comma at BRACKET DEPTH ZERO. A Hash's ofType is
// "valueType,keyType", so the value type is everything before that comma — but
// an element type may itself be PARAMETERISED and carry commas inside its own
// brackets (`my Hash[Int,Str] @a`), which a plain find(',') cut in half.
size_t topLevelComma(const std::string& s) {   // (shared: MethodCallPart3.cpp)
    int depth = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '[') depth++;
        else if (s[i] == ']') { if (depth) depth--; }
        else if (s[i] == ',' && depth == 0) return i;
    }
    return std::string::npos;
}
std::string elemTypeOfSpec(const std::string& ofType) {
    if (ofType.empty()) return "";
    std::string first = ofType.substr(0, topLevelComma(ofType));
    if (first.empty() || first == "Any" || first == "Mu" || first == "Cool") return "";
    if (!ascii::isupper((unsigned char)first[0])) return ""; // native: see natCheck
    return first;
}
// A COERCION type's target, `Int(Any)` → "Int", with its source (`Any`, or a
// nested `Int(Cool)`) in *from; "" for anything else. A parameterisation is
// not one: `Array[Int(Any)]` is an Array.
std::string coercionTarget(const std::string& t, std::string* from = nullptr) {
    size_t lp = t.find('(');
    if (lp == std::string::npos || lp == 0 || t.back() != ')') return "";
    size_t br = t.find('[');
    if (br != std::string::npos && br < lp) return "";
    if (from) *from = t.substr(lp + 1, t.size() - lp - 2);
    return t.substr(0, lp);
}
// What a value becomes on its way into a coercion-typed element (`my Hash() %h`
// is Rakudo's Hash[Hash(Any)]): coerced to the target when its source type
// takes it, and left alone otherwise, for checkElemType to refuse by the
// coercion type's full name. A nested source coerces inside out:
// `Str(Int(Cool))` makes 4.7 the Int 4 and then "4". An undefined value is not
// coerced (Rakudo's attempt fails, with another error).
void coerceElemValue(Interpreter& I, const std::string& want, Value& v) {
    std::string from;
    std::string target = coercionTarget(want, &from);
    if (target.empty() || v.t == VT::Nil || !rtIsDefined(v) || I.typeOrSubsetMatches(v, target)) return;
    std::string inner = coercionTarget(from);
    if (!inner.empty()) {
        coerceElemValue(I, from, v);
        if (!I.typeOrSubsetMatches(v, inner)) return;
    }
    else if (!from.empty() && from != "Any" && from != "Mu" && !I.typeOrSubsetMatches(v, from)) return;
    v = I.coerceToType(v, target);
    // a coercion that fails throws its own error (`"abc"` into an Int() is X::Str::Numeric)
    if (v.t == VT::Hash && v.hashKind == "Failure") failureDetonate(v);
}
std::string Interpreter::elemTypeOf(const Value& container) {
    if (container.ofType().empty()) return "";
    if (container.t != VT::Array && container.t != VT::Hash) return ""; // Range/IO::Path ride on ofType too
    if (container.t == VT::Hash && !container.hashKind.empty()) return ""; // a Set/Bag keys on ofType
    return elemTypeOfSpec(container.ofType());
}

// How an element-check message names the container written to: its own spelling
// for a variable (`@a`), the private-attribute spelling for an accessor
// (`self.data` → `@!data`), nothing for an expression with no name of its own.
std::string containerNameOf(const Expr* e, char sigil) {
    if (!e) return "";
    if (e->kind == NK::VarExpr) return static_cast<const VarExpr*>(e)->name;
    if (e->kind == NK::MethodCall)
        return std::string(1, sigil) + "!" + static_cast<const MethodCall*>(e)->method;
    return "";
}

// A value entering a typed container's element must conform, exactly as a typed
// scalar's assignment must. Only Nil is exempt, and only because it is not a
// store at all: it RESETS the slot to the element default, which
// nilElemDefault/elemDef have already turned it into. Everything else is asked,
// including the two values that look like they should slip through:
//   * an UNDEFINED value carries its own type, and conforms iff that type does
//     (`my Str @a; @a[0] = Str` is fine, `= Any` is not);
//   * a Failure does NOT soak into a typed container the way it does into an
//     untyped one — the check has to look at it, and Rakudo fails the
//     assignment (oracle-checked: `my Int @a; @a[0] = Failure.new("boom")`
//     throws X::TypeCheck::Assignment).
// The predicate is nominal conformance, not `~~`: a Junction smart-matches Int
// and is still an illegal Int element, which is Rakudo's rule too.
// `my Int $a is default("foo")` — the default has to fit the declared type,
// or the declaration dies: X::Parameter::Default::TypeCheck for a variable,
// X::TypeCheck::Attribute::Default for an attribute. A NATIVE type has no
// default at all (X::Comp::Trait::NotOnNative). Nil fits no type.
void Interpreter::checkDeclDefault(const std::string& declType, char sigil, const Value& dv, bool attr, char smiley) {
    std::string t = declType.substr(0, declType.find(','));   // `%h{Str}` → value type
    if (t.empty() || t == "Mu" || t == "Any") return;   // (an object hash's implicit value type is Any)
    if (ascii::islower((unsigned char)t[0])) {
        if (attr) return;
        throwTyped("X::Comp::Trait::NotOnNative", {{"type", "is"}, {"subtype", "default"}},
                   "Can't use trait 'is default' on a native.");
    }
    // …and its smiley: `my Int:U $y is default(0)` can never hold its default
    // either (the message names the bare type for a scalar, as Rakudo's does)
    const bool smileyFails = (smiley == 'U' && isDefined(dv)) || (smiley == 'D' && !isDefined(dv));
    if (!smileyFails && typeOrSubsetMatches(dv, t)) return;
    if (!classes_.count(t) && !isKnownTypeName(t)) return;   // a type we cannot judge
    const std::string elemT = smiley == 'U' || smiley == 'D' ? t + ":" + smiley : t;
    std::string shown = sigil == '@' ? "Array[" + elemT + "]" : sigil == '%' ? "Hash[" + elemT + "]" : t;
    Value expected = Value::typeObj(t);
    if (sigil == '@' || sigil == '%') {
        expected = Value::typeObj(sigil == '@' ? "Array" : "Hash");
        expected.ofTypeM() = t;
        expected.s = shown;
    }
    throwTypedV(attr ? "X::TypeCheck::Attribute::Default" : "X::Parameter::Default::TypeCheck",
                {{"got", dv}, {"expected", expected}},
                "Default value '" + (dv.t == VT::Nil ? std::string("Nil") : dv.gist()) +
                    "' will never bind to " + (attr ? "an attribute" : "a variable") + " of type " + shown);
}

// The `:D`/`:U` smiley a declared `@`/`%` variable puts on its ELEMENTS
// (`my Int:D @a`), or 0. `symbol` is the variable's own name.
char Interpreter::elemSmileyOf(const std::string& symbol) {
    if (!elemSmileyDeclared_.load(std::memory_order_relaxed)) return 0;
    if (symbol.size() < 2 || (symbol[0] != '@' && symbol[0] != '%')) return 0;
    for (Env* en = tctx_.cur.get(); en; en = en->parent.get()) {
        auto si = en->xr().varSmiley.find(symbol);
        if (si != en->xr().varSmiley.end()) return si->second;
        if (en->local(symbol)) return 0;
    }
    return 0;
}
// …and the check: an element of a `:D` container must be defined, of a `:U`
// one undefined (X::TypeCheck::Assignment naming the container).
void Interpreter::checkElemSmiley(const std::string& symbol, const std::string& type, const Value& v) {
    char sm = elemSmileyOf(symbol);
    if (!sm) return;
    const bool def = v.t != VT::Nil && isDefined(v);
    if ((sm == 'D' && def) || (sm == 'U' && !def)) return;
    std::string want = (type.empty() ? std::string("Any") : type) + (sm == 'D' ? ":D" : ":U");
    throwTypedV("X::TypeCheck::Assignment",
        {{"got", v}, {"expected", Value::typeObj(want)}, {"symbol", Value::str(symbol)}},
        "Type check failed for an element of " + symbol + "; expected " + want +
        " but got " + (v.t == VT::Nil ? std::string("Nil") : v.typeName()));
}

void Interpreter::checkElemType(const std::string& want, const Value& v, const std::string& symbol) {
    // Nil RESETS the element to its default — the bare type, which a `:D`
    // element type refuses (`my Int:D @a; @a[2] = Nil` dies)
    if (v.t == VT::Nil && want.size() > 2 && want.compare(want.size() - 2, 2, ":D") == 0) {
        const std::string base = want.substr(0, want.size() - 2);
        throwTypedV("X::TypeCheck::Assignment",
                    {{"got", Value::typeObj(base)}, {"expected", Value::typeObj(want)}},
                    "Type check failed for an element of " +
                        (symbol.empty() ? std::string("the container") : symbol) +
                        "; expected " + want + " but got " + base + " (" + base + ")");
    }
    if (v.t == VT::Nil) return;
    // a COERCION type's element is its target once coerceElemValue ran
    // (the message still names the whole `Int(Str)`)
    std::string target = coercionTarget(want);
    if (!target.empty()) {
        if (typeOrSubsetMatches(v, target)) return;
    }
    // A PARAMETERISED element type constrains TWICE: the value must be that
    // base type AND carry the same parameterisation. `my Array[Int] @a` takes
    // an Array[Int] and neither an Array[Str] nor a plain Array — Rakudo
    // rejects both, and roast S06-currying/positional.t declares exactly
    // `my Array[Int] @AoAoI = $@AoI, $@AoI`.
    else if (size_t br = want.find('['); br != std::string::npos && want.back() == ']') {
        if (typeOrSubsetMatches(v, want.substr(0, br)) &&
            v.ofType() == want.substr(br + 1, want.size() - br - 2)) return;
    }
    else if (typeOrSubsetMatches(v, want)) return;
    throwTypedV("X::TypeCheck::Assignment",
                {{"got", v}, {"expected", Value::typeObj(want)}},
                "Type check failed for an element of " +
                    (symbol.empty() ? std::string("the container") : symbol) +
                    "; expected " + want + " but got " + v.typeName() +
                    (isDefined(v) ? " (" + typeCheckRepr(v) + ")" : " " + v.gist()));
}

// Nominal conformance with SUBSET names resolved — for TYPE OBJECTS only; a
// defined value takes the ordinary path untouched (a negative Int still fails
// UInt on its sign). An undefined `subset Port of UInt` attribute reads back
// as the Port TYPE OBJECT, and a `--> Port` / `--> UInt` boundary must see
// the Int underneath: Rakudo's core UInt is
//     subset UInt of Int where { not .defined or $_ >= 0 }
// so every Int-derived type object passes it SILENTLY, and a subset-named
// type object conforms wherever its base chain does. URI's
// `method port(--> Port)` returns exactly this when neither the URI nor its
// scheme carries a port, and its whole suite died at that boundary. A
// hash-miss Any still fails an Int-based constraint — nominal first.
const std::string& Interpreter::typeAliasTarget(const std::string& name) {
    auto hit = typeAliasCache_.find(name);
    if (hit != typeAliasCache_.end()) return hit->second;
    std::string target = name;
    // Only a name nothing else claims: a real class, a subset or a built-in
    // resolves itself, and asking the scope about those would cost a lookup on
    // every dispatch for no answer.
    if (!name.empty() && !classes_.count(name) && !subsets_.count(name) &&
        name.find("+{") == std::string::npos)
        if (Value* v = tctx_.cur ? tctx_.cur->find(name) : nullptr)
            if (v->t == VT::Type && !v->s.empty()) target = v->s;
    return typeAliasCache_.emplace(name, std::move(target)).first->second;
}

bool Interpreter::typeMatchesResolved(const Value& v, const std::string& type) {
    // a USER role named like a core one (`role Numeric { }`) SHADOWS it
    if (v.t != VT::Object && v.t != VT::Type && userShadowsCoreRole(type)) return false;
    // Distribution::Path, ::Hash and a repository's own dists report their own
    // names, and every one of them does Distribution
    if (v.t == VT::Hash && v.hashKind == "Distribution" && type == "Distribution") return true;
    // an enum TYPE object (the tagged pair-list, enumName empty) is a type
    // object of the enum, which does Enumeration and inherits from the type of
    // its values: `enum Color <Red Green>` has the MRO Color, Int, Cool, Any,
    // Mu, so it binds an Int parameter and not a Str one
    if (v.t == VT::Array && !v.enumType.empty() && v.enumName.empty()) {
        if (type == v.enumType || type == "Enumeration" || type == "Any" || type == "Mu") return true;
        std::string base;
        if (v.arr())
            for (const Value& pr : *v.arr()) {
                const Value* pv = pr.pairVal();
                std::string t = pv ? pv->typeName() : std::string();
                if (base.empty()) base = t;
                else if (t != base) { base.clear(); break; }
            }
        return !base.empty() && typeMatchesArg(Value::typeObj(base), type);
    }
    // …and no other type object is one of the enum (`Int` does not bind
    // `Color $c`): the lenient tail of typeMatchesArg cannot tell, because a
    // user enum's name is no type it knows
    if (v.t == VT::Type && enumPairs_.count(type) && !classes_.count(type)) return v.s == type;
    // a PARAMETERIZED container type: `Array[Bool]` takes an Array whose element
    // type is Bool (what `Array[Array[Bool]].new($(Array[Bool].new(…)))` checks)
    if (v.t == VT::Array && type.size() > 2 && type.back() == ']' && v.enumName.empty()) {
        size_t br = type.find('[');
        if (br != std::string::npos) {
            std::string base = type.substr(0, br), param = type.substr(br + 1, type.size() - br - 2);
            if (base == "Array" || base == "Positional" || (base == "List" && v.isList)) {
                const std::string& et = v.ofType();
                if (et.empty() || et.find(',') != std::string::npos) return param == "Mu";
                if (et == param) return true;
                return typeMatchesArg(Value::typeObj(et), param);
            }
        }
    }
    // …and the enum TYPE by its package-qualified name: `has Cro::WebSocket::
    // Message::Opcode $.opcode` names the enum `package Cro::WebSocket::Message
    // { enum Opcode … }` declared, whose values carry only the short name. The
    // qualified name is bound to the enum's type object (its tagged pair-list).
    if (!v.enumName.empty() && !v.enumType.empty() && type.size() > v.enumType.size() + 2 &&
        type.compare(type.size() - v.enumType.size() - 2, std::string::npos, "::" + v.enumType) == 0 &&
        !typeMatchesArg(v, type)) {
        const Value* tv = tctx_.cur ? tctx_.cur->find(type) : nullptr;
        if (!tv && global_) tv = global_->find(type);
        if (tv && tv->t == VT::Array && tv->enumName.empty() && tv->enumType == v.enumType) return true;
    }
    if (v.t != VT::Type) return typeMatchesArg(v, type);
    auto resolve = [&](std::string n) {
        size_t br = n.find('[');
        if (br != std::string::npos) n = n.substr(0, br);
        for (int guard = 0; guard < 16; guard++) {
            if (n == "UInt") { n = "Int"; break; }
            auto it = subsets_.find(n);
            if (it == subsets_.end() || it->second.base.empty()) break;
            n = it->second.base;
        }
        return n;
    };
    std::string have = resolve(v.s);
    std::string want = type == "UInt" ? "Int" : type;
    if (have == v.s && want == type) return typeMatchesArg(v, type);
    return typeMatchesArg(Value::typeObj(have), want);
}

bool Interpreter::multiTie(const Value& a, const Value& b) {
    const Callable* ca = a.code();
    const Callable* cb = b.code();
    if (!ca || !cb || !ca->params || !cb->params) return false;
    if (ca->isDefaultCand || cb->isDefaultCand || ca->pkg != cb->pkg) return false;
    auto positional = [](const Callable* c, std::vector<const Param*>& out) -> bool {
        for (auto& p : *c->params) {
            if (p.invocant) continue;
            if (p.named) return false;
            if (p.pastDoubleSemi) break;
            if (p.slurpy || p.optional || p.defaultVal || p.whereExpr || p.hadWhere || p.litVal ||
                p.subSig || p.coerce || p.typeCapture || p.sigil != '$') return false;
            out.push_back(&p);
        }
        return true;
    };
    std::vector<const Param*> pa, pb;
    if (!positional(ca, pa) || !positional(cb, pb) || pa.size() != pb.size()) return false;
    // nothing BEFORE a `;;` to tell them apart: `multi a(;; Any $b)` beside
    // `multi a(;; Int $a)` is as ambiguous as two identical signatures
    if (pa.empty()) {
        auto afterSemi = [](const Callable* c) {
            for (auto& p : *c->params) if (!p.invocant && !p.named && p.pastDoubleSemi) return true;
            return false;
        };
        return afterSemi(ca) && afterSemi(cb);
    }
    bool narrower = false, wider = false;
    auto isUserClass = [&](const std::string& t) {
        auto it = classes_.find(t);
        return it != classes_.end() && it->second && !it->second->isRole && !subsets_.count(t);
    };
    auto inherits = [&](const std::string& sub, const std::string& sup) {
        auto it = classes_.find(sub);
        for (ClassInfo* c = it == classes_.end() ? nullptr : it->second.get(); c; c = c->parent.get()) {
            if (c->name == sup) return true;
            if (!c->extraParents.empty()) return false;   // multiple inheritance: not judged
        }
        return false;
    };
    for (size_t i = 0; i < pa.size(); i++) {
        const Param& x = *pa[i];
        const Param& y = *pb[i];
        if (x.defConstraint != y.defConstraint) return false;
        // `is rw` / `is copy` / `is raw` decide by what the argument is
        if (x.isRw != y.isRw || x.isCopy != y.isCopy || x.isRaw != y.isRaw) return false;
        // (an untyped parameter is Any)
        const std::string xt = x.type.empty() ? "Any" : x.type, yt = y.type.empty() ? "Any" : y.type;
        if (xt == yt) continue;
        // two CORE types are ordered by the core ancestry: `(Int, Any)` beside
        // `(Any, Int)` each wins a position, which is ambiguous for (1, 1)
        auto coreKnown = [&](const std::string& t) {
            return !classes_.count(t) && !subsets_.count(t) &&
                   (t == "Any" || t == "Mu" || typeAncestry(t).size() > 2 || typeAncestry(t)[0] == t);
        };
        if (coreKnown(xt) && coreKnown(yt)) {
            auto under = [](const std::string& sub, const std::string& sup) {
                for (auto& a : typeAncestry(sub)) if (a == sup) return true;
                return sup == "Mu" || (sup == "Any" && sub != "Mu");
            };
            if (under(xt, yt)) narrower = true;
            else if (under(yt, xt)) wider = true;
            else return false;
            continue;
        }
        if (!isUserClass(xt) || !isUserClass(yt)) return false;
        if (inherits(xt, yt)) narrower = true;
        else if (inherits(yt, xt)) wider = true;
        else return false;
    }
    return narrower == wider;   // all equal, or each wins a position
}

void Interpreter::throwIfAmbiguous(const Callable& c, const Value* best, const Value* const* matched,
                                   int n, const ValueList& as) {
    std::vector<const Value*> tied;
    for (int i = 0; i < n; i++)
        if (matched[i] != best && multiTie(*best, *matched[i])) tied.push_back(matched[i]);
    if (tied.empty()) return;
    tied.insert(tied.begin(), best);
    std::string prof;
    for (auto& a : as) {
        if (isNamedArg(a)) continue;
        if (!prof.empty()) prof += ", ";
        prof += a.typeName();
    }
    std::string sigs;
    for (auto* t : tied) {
        try { sigs += "\n    " + methodCall(methodCall(*t, "signature", {}), "gist", {}).toStr(); }
        catch (...) {}
    }
    throw RakuError{Value::typeObj("X::Multi::Ambiguous"),
                    "Ambiguous call to '" + c.name + "(" + prof + ")'; these signatures all match:" + sigs};
}

// A user `multi infix:<op>` whose operands are CORE values joins the same
// dispatch as the built-in operator in Rakudo: `multi infix:<+>(Int:D $a,
// Int:D $b where * > 0)` is narrower than the core `(Int:D, Int:D)` and wins,
// `(Int $a, Int $b)` ties with it and the call is ambiguous, and `(Numeric,
// Numeric)` is broader and the core candidate wins. The built-in operators are
// not candidates here, so the one they would field is modelled from the
// operand types: same-typed numbers meet their own type (Rat meets Rational),
// mixed reals meet `Real`, string comparisons over two Strs meet `Str`, and
// anything else meets the generic `(\a, \b)`.
//
// Only the operators below are modelled, and a group is looked at only once
// one of its candidates names a concrete core type (noteUserInfixCandidate
// arms the operator's bit in g_lexShadowMask): a group over a user class
// alone keeps the fast paths, and its candidates are dispatched where object
// operands are (evalBinary).
static bool userInfixModelledOp(const std::string& op, bool& stringOp) {
    static const std::unordered_map<std::string, bool> kOps = {
        {"+", false}, {"-", false}, {"*", false}, {"/", false}, {"%", false}, {"**", false},
        {"==", false}, {"!=", false}, {"<", false}, {">", false}, {"<=", false}, {">=", false},
        {"<=>", false}, {"div", false}, {"mod", false},
        {"~", true}, {"eq", true}, {"ne", true}, {"lt", true}, {"gt", true}, {"le", true}, {"ge", true}, {"leg", true}};
    auto it = kOps.find(op);
    if (it == kOps.end()) return false;
    stringOp = it->second;
    return true;
}

static bool userInfixCoreType(const std::string& t) {
    return t == "Int" || t == "UInt" || t == "Num" || t == "Rat" || t == "FatRat" || t == "Str" ||
           t == "Bool" || t == "Complex";
}

// The two positional parameters a binary candidate dispatches on, or false
// when it has some other shape (a slurpy, an optional, a named it requires…).
static bool userInfixParams(const Callable* c, const Param*& a, const Param*& b) {
    if (!c || !c->params) return false;
    const Param* ps[2] = {nullptr, nullptr};
    int n = 0;
    for (auto& p : *c->params) {
        if (p.invocant) continue;
        if (p.named) { if (p.required) return false; continue; }
        if (p.pastDoubleSemi || p.slurpy || p.optional || p.defaultVal || p.subSig || p.coerce ||
            p.typeCapture || p.sigil != '$' || n == 2)
            return false;
        ps[n++] = &p;
    }
    if (n != 2) return false;
    a = ps[0]; b = ps[1];
    return true;
}

void Interpreter::noteUserInfixCandidate(const std::string& name, const Callable* cand) {
    if (name.size() < 9 || name.compare(0, 7, "infix:<") != 0 || name.back() != '>') return;
    const std::string op = name.substr(7, name.size() - 8);
    bool stringOp;
    if (!userInfixModelledOp(op, stringOp)) return;
    const Param *a, *b;
    if (!userInfixParams(cand, a, b)) return;
    auto concrete = [&](const Param* p) {
        if (p->litVal) return true;
        std::string t = p->type;
        for (int guard = 0; guard < 16; guard++) {
            auto it = subsets_.find(t);
            if (it == subsets_.end() || it->second.base.empty()) break;
            t = it->second.base;
        }
        if (userInfixCoreType(t)) return true;
        // a name nothing has declared YET: the sub is hoisted above the
        // `subset P of Int` its parameter names, so it may be one
        static const std::set<std::string> kBroad = {
            "Any", "Mu", "Cool", "Numeric", "Real", "Rational", "Stringy", "Positional", "Associative",
            "Callable", "Code", "Iterable", "Junction", "List", "Array", "Hash", "Seq", "Range"};
        return !t.empty() && !kBroad.count(t) && !classes_.count(t);
    };
    if (concrete(a) && concrete(b))
        g_lexShadowMask.fetch_or(1ull << lexShadowSlot(op.data(), op.size()), std::memory_order_relaxed);
}

bool Interpreter::userInfixOverCore(const std::string& op, const Value& l, const Value& r, Value& out) {
    bool stringOp;
    if (!tctx_.cur || !userInfixModelledOp(op, stringOp)) return false;
    // (…and the built-in Dateish values, whose operators are core multis a
    // user candidate can be narrower than: `multi infix:«-»(Date:D, Str:D)`)
    auto plain = [](const Value& v) {
        if (v.t == VT::Hash && (v.hashKind == "Date" || v.hashKind == "DateTime")) return true;
        return isDefined(v) && v.hashKind.empty() && !v.isAllomorph() && v.enumType.empty() &&
               (v.t == VT::Int || v.t == VT::Num || v.t == VT::Rat || v.t == VT::Str ||
                v.t == VT::Bool || v.t == VT::Complex);
    };
    if (!plain(l) || !plain(r)) return false;
    static thread_local std::string key;
    key.assign("&infix:<").append(op).append(">");
    Value* f = tctx_.cur->find(key);
    if (!f || f->t != VT::Code || !f->code() || !f->code()->isMultiDispatcher) return false;
    // the built-in candidate these operands meet
    const std::string tl = l.typeName(), tr = r.typeName();
    auto intish = [](const std::string& t) { return t == "Int" || t == "Bool"; };
    auto real = [&](const std::string& t) { return intish(t) || t == "Num" || t == "Rat" || t == "FatRat"; };
    std::string core;
    if (stringOp) core = tl == "Str" && tr == "Str" ? "Str" : "Any";
    else if (intish(tl) && intish(tr)) core = "Int";
    else if (tl == tr && (tl == "Num" || tl == "Complex")) core = tl;
    else if ((tl == "Rat" || tl == "FatRat") && (tr == "Rat" || tr == "FatRat")) core = "Rational";
    else if (real(tl) && real(tr)) core = "Real";
    else core = "Any";
    const ValueList args{l, r};
    enum { Lose, Tie, Win };
    int verdict = Lose;
    const Value* tied = nullptr;
    for (auto& cand : f->code()->candidates) {
        const Callable* c = cand.code();
        if (!c || c->isProto || c->isProtoBody) continue;
        const Param *pa, *pb;
        if (!userInfixParams(c, pa, pb) || scoreCandidate(cand, args) < 0) continue;
        bool constrained = c->isDefaultCand, narrower = false, broader = false;
        const Param* ps[2] = {pa, pb};
        for (int i = 0; i < 2; i++) {
            const Param& p = *ps[i];
            // a literal parameter is its value's type with a constraint
            std::string t = p.litVal ? args[i].typeName() : p.type.empty() ? std::string("Any") : p.type;
            if (p.litVal || p.whereExpr || p.hadWhere) constrained = true;
            for (int guard = 0; guard < 16; guard++) {
                auto it = subsets_.find(t);
                if (it == subsets_.end() || it->second.base.empty()) break;
                t = it->second.base;
                constrained = true;
            }
            if (t == "UInt") { t = "Int"; constrained = true; }
            if (t == core) continue;
            if (typeMatchesArg(Value::typeObj(t), core)) narrower = true;
            else broader = true;
        }
        if (broader) continue;
        if (narrower || constrained) { verdict = Win; break; }
        verdict = Tie;
        if (!tied) tied = &cand;
    }
    if (verdict == Win) { out = callCallable(*f, ValueList{l, r}); return true; }
    if (verdict == Lose) return false;
    const bool angled = op.find_first_of("<>") != std::string::npos;
    const std::string name = angled ? "infix:\u00AB" + op + "\u00BB" : "infix:<" + op + ">";
    std::string sigs = "\n    (" + core + ":D, " + core + ":D)";
    if (core == "Any" || core == "Real") sigs = "\n    (" + core + " \\a, " + core + " \\b)";
    try { sigs += "\n    " + methodCall(methodCall(*tied, "signature", {}), "gist", {}).toStr(); }
    catch (...) {}
    throw RakuError{Value::typeObj("X::Multi::Ambiguous"),
                    "Ambiguous call to '" + name + "(" + tl + ", " + tr + ")'; these signatures all match:" + sigs};
}

// `try { … }` runs its block under `use fatal` (Rakudo): the call marks the
// block's scope fatal (a `fail` in it throws, a call in it that answers a
// Failure throws), and with a CATCH, a Failure the block ENDS with is thrown
// inside it, where that CATCH sees it. The try sets this for the one call it
// makes; that call consumes it on entry, so the block's own inner calls run as
// usual.
thread_local bool t_fatalTry = false;

// Per-thread stack accounting for the recursion guard. `t_stack.top` is a byte
// address near the top of this thread's stack (set once, lazily, from the first
// guarded frame); `t_stack.limit` is that thread's usable stack size. The main
// interpreter runs on a 1 GiB stack (Runtime.cpp) and workers on 256 MiB
// (BigStackThread) — a headroom check fits both, where a fixed frame count
// cannot. We stop with X::Recursion while ~2 MiB of stack remains, so the throw
// unwinds cleanly instead of the process taking SIGSEGV/SIGBUS (the latter
// wedging kill-proof under Rosetta).
thread_local StackBounds t_stack;
// Where this frame sits on the stack. AddressSanitizer's use-after-return mode
// moves locals onto heap-allocated "fake" frames, so a local's address says
// nothing about stack depth there; the frame address still does.
#if defined(__SANITIZE_ADDRESS__)
#  define RAKUPP_STACK_HERE() static_cast<char*>(__builtin_frame_address(0))
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define RAKUPP_STACK_HERE() static_cast<char*>(__builtin_frame_address(0))
#  endif
#endif

// Does this expression contain a literal `*` (Whatever) term — walking only
// through composable expression forms, never into variables or calls? This is
// the syntactic test Whatever-currying keys on: `(* * 2).floor` composes,
// `my $d = * * 2; $d.floor` does not.
// `»`.method over a container: apply to each TOP-LEVEL element, preserving
// structure — a Hash keeps its keys and maps its values, and nothing deep-flattens.
// Shared by the direct `@a».m` path and the closure a `*.attr».m` WhateverCode
// curries into; the latter used to fall back to a plain method call, which
// dispatched to the CONTAINER ("No such method 'lang' for invocant of type 'Array'").
// The methods Rakudo marks `is nodal`: the ones that ask about a CONTAINER
// rather than about a value. A hyper applies those to each node and stops
// there; every other method reaches the LEAVES, descending through nested
// collections. Derived by running each name of Any/List/Array's method table
// through `[[1,2],]».NAME` under Rakudo and comparing the answer with the
// node's own and with the per-leaf one (scratch probe nodal2/nodal3).
bool isNodalMethod(const std::string& m) {
    static const std::set<std::string> kNodal = {
        "Array", "Bag", "BagHash", "Hash", "List", "Map", "Mix", "MixHash",
        "Seq", "Set", "SetHash", "Slip", "Supply",
        "all", "antipairs", "any", "append", "batch", "categorize", "classify",
        "combinations", "elems", "end", "first", "flat", "grep", "hash", "head",
        "item", "join", "keys", "kv", "list", "map", "max", "min", "minmax",
        "none", "one", "pairs", "pairup", "permutations", "pop", "prepend",
        "push", "reduce", "repeated", "reverse", "roll", "rotate", "rotor",
        "serial", "shift", "slice", "sort", "squish", "sum", "tail", "tree",
        "unique", "unshift", "values",
        // …and the ones the probe could not classify because they take an
        // argument (it would have had to invent one). S03-metaops/hyper.t names
        // them: without `flatmap` here the hyper descended and asked an Int for
        // it, which took the whole file down.
        "deepmap", "duckmap", "flatmap", "invert", "nodemap", "pick",
        "produce", "splice"};
    return kNodal.count(m) > 0;
}

// A List, an Array or a Slip: a built-in value whose class chain runs through List.
static bool listBuiltIn(const Value& v) {
    return v.t == VT::Array && v.arr() && v.enumName.empty() && (v.s.empty() || v.s == "Slip");
}

// `.+m` / `.*m` — the results of every candidate, as a List. On a user object
// each class of the MRO that declares the method runs it, most-derived first
// (`$c.*foo` climbs C, B, A); otherwise the one method dispatch finds. `.*` of a
// method nobody has is the empty List, `.+` of one dies.
// Of the BUILT-IN classes, List and Any each declare an `elems` of their own,
// Any's being `self.list.elems`, a fresh dispatch: so a List answers `.+elems`
// twice (`<a b>.+elems` is (2 2), S03-metaops/hyper.t), a class built on one
// gets both after its own (`class M is Array { method elems { 42 } }` gives
// (42 2 42)), and any other class that declares `elems` gets Any's after its
// own (7 1). Only those two are modelled; Rakudo's Seq, Range, Map and the rest
// declare one too, and answer a single candidate here.
Value Interpreter::callAllCandidates(const Value& inv, const std::string& mname, ValueList args,
                                     char mode, const std::vector<ExprPtr>* rwArgs) {
    Value l = Value::array(); l.isList = true;
    const bool allElems = mname == "elems" && args.empty();
    // Any's: `self.list.elems` — a List's .list is itself, so its own dispatch
    auto anyElems = [&](const Value& self, bool listSelf) -> Value {
        if (listSelf) return methodCall(self, "elems", ValueList{});
        return methodCall(methodCall(self, "list", ValueList{}), "elems", ValueList{});
    };
    if (allElems && listBuiltIn(inv)) {
        l.arr()->push_back(methodCall(inv, "elems", ValueList{}));   // List's
        l.arr()->push_back(anyElems(inv, true));                     // Any's
        return l;
    }
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) {
        std::vector<ClassInfo*> mro;
        std::set<ClassInfo*> seen;
        std::function<void(ClassInfo*)> walk = [&](ClassInfo* c) {
            if (!c || !seen.insert(c).second) return;
            mro.push_back(c);
            walk(c->parent.get());
            for (auto& p : c->extraParents) walk(p.get());
        };
        walk(inv.obj()->cls.get());
        bool any = false;
        for (ClassInfo* c : mro) {
            auto it = c->methods.find(mname);
            if (it == c->methods.end()) continue;
            any = true;
            Value um = it->second;
            l.arr()->push_back(invokeMethodChain(mname, c, inv, args, rwArgs, &um, c));
        }
        if (allElems) {
            // …then the built-ins': List's for a class built on one, and Any's
            const bool onList = inv.obj()->hasBoxed && listBuiltIn(inv.obj()->boxed());
            if (onList) l.arr()->push_back(methodCall(inv.obj()->boxed(), "elems", ValueList{}));
            if (any || onList) {
                l.arr()->push_back(anyElems(inv, onList));
                return l;
            }
        }
        if (any) return l;
    }
    try { l.arr()->push_back(methodCall(inv, mname, std::move(args), rwArgs)); }
    catch (RakuError& err) {
        const Value& p = err.payload;
        bool notFound = (p.t == VT::Type && p.s == "X::Method::NotFound") ||
                        (p.t == VT::Object && p.obj() && p.obj()->cls &&
                         p.obj()->cls->name == "X::Method::NotFound");
        if (mode == '*' && notFound) return l;
        throw;
    }
    return l;
}

Value Interpreter::hyperMethodEach(const Value& inv, const std::string& m, ValueList& args, bool maybe) {
    forceLazy(inv);   // a finite lazy source (`$fh.lines».words`) is read in full first
    // A non-nodal method reaches the LEAVES: `[[1,2],[3,4]]».Str` is
    // `[["1","2"],["3","4"]]`, not two stringified rows, and `@data».are` over
    // a list of hashes answers a hash of types per element. rakupp stopped at
    // the top level for every method, which is only right for the nodal ones.
    const bool nodal = isNodalMethod(m);
    auto descends = [&](const Value& v) {
        return !nodal && ((v.t == VT::Array && v.arr()) ||
                          (v.t == VT::Hash && v.hash() && v.hashKind.empty()));
    };
    const bool invoke = opEq(m, kHyperInvoke);   // `».(args)`: call each element
    // a list of plain machine numbers and a numeric method: the native kernel
    // (InterpreterRegex.cpp)
    if (Value nk; !invoke && hyperNumericMethod(inv, m, args, nk)) return nk;
    auto each = [&](const Value& el) -> Value {
        if (descends(el)) return hyperMethodEach(el, m, args, maybe);
        if (invoke) {
            if (el.t == VT::Code && el.code()) return callCallable(el, args);
            // Rakudo binds each element to a `&code` parameter first
            throwTypedV("X::TypeCheck::Binding::Parameter", {{"got", el}},
                        "Type check failed in binding to parameter '&code'; expected Callable but got " +
                        el.typeName() + (isDefined(el) ? " (" + typeCheckRepr(el) + ")" : ""));
        }
        if (!maybe) return methodCall(el, m, args);
        // `».?name`: an element without the method answers Nil, not a death
        try { return methodCall(el, m, args); }
        catch (RakuError& err) {
            const Value& p = err.payload;
            bool notFound = (p.t == VT::Type && p.s == "X::Method::NotFound") ||
                            (p.t == VT::Object && p.obj() && p.obj()->cls &&
                             p.obj()->cls->name == "X::Method::NotFound");
            if (!notFound) throw;
            return Value::any();
        }
    };
    // (…and a Map hypers to a Map: `(:42a, :666b).Map».Str` — S03-metaops/hyper.t)
    if (inv.t == VT::Hash && inv.hash() && (inv.hashKind.empty() || inv.hashKind == "Map")) {
        Value hout = Value::makeHash();
        hout.hashKind = inv.hashKind;
        hout.ofTypeM() = inv.ofType(); hout.objKeyed = inv.objKeyed;
        for (auto& kv : *inv.hash()) (*hout.hash())[kv.first] = each(kv.second);
        return hout;
    }
    Value out = Value::array();
    // a Blob/Buf is Positional over its ELEMENTS, so `$blob».fmt('%08b')` runs
    // once per byte — flatten() would hand back the whole buffer as one item
    if (inv.t == VT::Str && (inv.hashKind == "Blob" || inv.hashKind == "Buf")) {
        for (auto& el : inv.blobList()) out.arr()->push_back(methodCall(el, m, args));
        out.isList = true;
        return out;
    }
    // A SLIP element splices its mapped result into the surrounding list, where a
    // plain nested list keeps its shape (`[[1,2],]>>.Str` stays nested). Only the
    // DESCENDING case slips — a nodal method answers about the node itself, so
    // `@m>>.Slip` is still a list OF slips. This is what flattens
    // `$m>>.Slip>>.Str` down to the captures themselves.
    auto emit = [&](const Value& el) {
        Value r = each(el);
        if (descends(el) && el.s == "Slip" && r.t == VT::Array && r.arr()) {
            for (auto& x : *r.arr()) out.arr()->push_back(x);
            return;
        }
        // …and a Slip the METHOD returned splices too, for the same reason: a
        // non-nodal hyper is Rakudo's deepmap, and a map slips a Slip. A NODAL
        // one is nodemap, which does not — `@m>>.Slip` stays a list of slips
        // while `@matches».ast` over actions that `make` a Slip flattens.
        // PDF::Grammar::Content builds a content stream that way, and every
        // block came back nested one level too deep.
        if (!nodal && r.t == VT::Array && r.arr() && r.s == "Slip") {
            for (auto& x : *r.arr()) out.arr()->push_back(x);
            return;
        }
        out.arr()->push_back(std::move(r));
    };
    if (inv.t == VT::Array && inv.arr())
        for (auto& el : *inv.arr()) emit(el);
    else
        for (auto& el : inv.flatten()) emit(el);
    // …and the answer keeps the invocant's shape: an Array in, an Array out —
    // for the DEEP methods. A nodal one answers a List either way (Rakudo maps
    // the nodes with nodemap, whose result is a Seq): `@a».elems` is `(1, 1)`.
    out.isList = nodal || !(inv.t == VT::Array && !inv.isList);
    return out;
}

Value Interpreter::seqOpGroups(Value seed, const std::vector<ValueList>& groups,
                               const std::vector<char>& exclEnd, bool exclSeed) {
    Value out = Value::array(); out.isList = true; out.s = "Seq";
    Value cur = std::move(seed);
    size_t skip = 0;                    // head of the segment already emitted
    // drop `n` leading elements from the lazily-continued tail, then hand back a
    // Seq whose realised prefix is everything emitted so far
    auto withLazyTail = [&](Value& seg, size_t drop) {
        auto inner = std::make_shared<Value>(seg);
        auto innerSt = std::static_pointer_cast<LazySeqState>(seg.ext());
        auto pos = std::make_shared<size_t>(drop);
        auto st = std::make_shared<LazySeqState>();
        st->infinite = innerSt->infinite;
        st->streaming = innerSt->streaming;
        st->appendNext = [inner, innerSt, pos](ValueList& cache) -> bool {
            while (*pos >= inner->arr()->size())
                if (!innerSt->appendNext(*inner->arr())) return false;
            cache.push_back((*inner->arr())[(*pos)++]);
            return true;
        };
        out.extM() = st;
        if (exclSeed) {
            if (out.arr()->empty()) st->appendNext(*out.arr());
            if (!out.arr()->empty()) out.arr()->erase(out.arr()->begin());
        }
        return out;
    };
    for (size_t i = 0; i < groups.size(); i++) {
        if (groups[i].empty()) continue;
        const bool last = (i + 1 == groups.size());
        const bool chained = groups.size() > 1;
        // In a CHAIN every segment stops SHORT of its endpoint, because the whole
        // group — endpoint included — is emitted below as the next seed. The
        // endpoint therefore appears exactly once whether or not the walk landed
        // on it, which is why `1 ... 5, 10, 20 ... 100` ends in 100 although
        // doubling from 20 jumps 80 → 160 straight past it. The UN-chained form
        // is the plain binary operator and keeps its own `...^`: there an
        // endpoint the walk never reaches is simply not part of the sequence
        // (`1, 2, 4 ... 100` stops at 64). Rakudo splits these the same way —
        // one candidate per arity.
        // A MATCHER endpoint (a closure, regex or type) is not a value to land
        // on: its segment runs up to and including the first element it accepts,
        // and the matcher itself is never emitted or seeded — `1, *+1 ... { $_ >= 4 },
        // 5, *+10 ...` walks 1..4 and then seeds (5, *+10)
        const Value& endpt = groups[i][0];
        const bool endMatcher = endpt.t == VT::Code || endpt.t == VT::Regex ||
                                (endpt.t == VT::Type && endpt.s != "Whatever");
        const bool excl = chained ? !endMatcher : (i < exclEnd.size() && exclEnd[i]);
        Value seg = seqOp(cur, groups[i][0], excl);
        // An ENDLESS segment ends the chain: nothing after it can ever be reached,
        // so the rest of the sequence IS its tail, continued lazily. (A lazy one
        // with an endpoint — a generator walking to 31 — is finite: realise it.)
        if (seg.t == VT::Array && seg.ext() && !last) {
            auto lst = std::static_pointer_cast<LazySeqState>(seg.ext());
            if (lst && !lst->infinite) {
                materializeLazy(seg, 1000000);
                if (lst->exhausted || seg.arr()->size() < 1000000) seg.extM().reset();
            }
        }
        if (seg.t == VT::Array && seg.ext()) return withLazyTail(seg, skip);
        if (seg.t == VT::Array && seg.arr())
            for (size_t k = skip; k < seg.arr()->size(); k++) out.arr()->push_back((*seg.arr())[k]);
        else if (skip == 0) out.arr()->push_back(seg);
        // A chained group is emitted whole; an unchained one contributes only its
        // trailing elements, the endpoint having been handled by seqOp itself
        // (`1 ... 5, 9` is (1, 2, 3, 4, 5, 9)). A trailing `...^` on the last
        // chained group drops that group's endpoint, the one thing `^` can still
        // mean once the segment itself has stopped short.
        size_t from = chained ? (endMatcher ? 1 : (last && i < exclEnd.size() && exclEnd[i] ? 1 : 0)) : 1;
        // (a GENERATOR in the group seeds the next segment; it is not a value)
        for (size_t k = from; k < groups[i].size(); k++)
            if (groups[i][k].t != VT::Code) out.arr()->push_back(groups[i][k]);
        if (!last) {
            // The WHOLE group seeds the next segment, which is what makes
            // `1 ... 5, 10 ... 15` continue by FIVES and answer (1..5, 10, 15).
            // Seeding from the tail alone (10) deduced a step of 1 and walked 10..15.
            Value nxt = Value::array(); nxt.isList = true;
            size_t vals = 0;
            for (size_t k = endMatcher ? 1 : 0; k < groups[i].size(); k++) {
                nxt.arr()->push_back(groups[i][k]);
                if (groups[i][k].t != VT::Code) vals++;
            }
            cur = nxt; skip = vals;
        }
    }
    if (exclSeed && !out.arr()->empty()) out.arr()->erase(out.arr()->begin());
    return out;
}

bool Interpreter::exprHasWhateverLit(const Expr* e) {
    if (!e) return false;
    switch (e->kind) {
        case NK::Whatever: return !static_cast<const WhateverExpr*>(e)->curryClosed;
        case NK::Binary: {
            auto* b = static_cast<const Binary*>(e);
            if (b->curryClosed) return false;
            return exprHasWhateverLit(b->lhs.get()) || exprHasWhateverLit(b->rhs.get());
        }
        case NK::Unary: return exprHasWhateverLit(static_cast<const Unary*>(e)->operand.get());
        case NK::Call: {
            auto* c = static_cast<const Call*>(e);
            if (c->dotAmp && !c->args.empty() && exprHasWhateverLit(c->args[0].get())) return true;
            // a USER infix is a Call: `(* quack *) quack 'a'` extends the curry
            // its left side started (S02-types/whatever.t)
            if (c->callee) return false;
            // …and a USER postfix, `*!`: `(*! + 1)` curries on through it
            if (c->args.size() == 1 && c->name.rfind("postfix:<", 0) == 0)
                return exprHasWhateverLit(c->args[0].get());
            return c->args.size() == 2 && c->name.rfind("infix:<", 0) == 0 &&
                   (exprHasWhateverLit(c->args[0].get()) || exprHasWhateverLit(c->args[1].get()));
        }
        case NK::MethodCall: {
            auto* m = static_cast<const MethodCall*>(e);
            if (m->curryClosed) return false;
            // a MACRO (`.WHAT` `.WHO` `.HOW` `.VAR` `.WHERE`) answers directly,
            // so the curry stops there: `(* + 1).VAR.^name` is WhateverCode
            if (!m->meta && !m->methodExpr &&
                (m->method == "WHAT" || m->method == "WHO" || m->method == "HOW" ||
                 m->method == "VAR" || m->method == "WHERE"))
                return false;
            return exprHasWhateverLit(m->inv.get());
        }
        case NK::Index: return exprHasWhateverLit(static_cast<const Index*>(e)->base.get());
        case NK::ChainExpr: {
            auto* c = static_cast<const ChainExpr*>(e);
            for (auto& o : c->operands) if (exprHasWhateverLit(o.get())) return true;
            return false;
        }
        case NK::ListExpr: {
            auto* l = static_cast<const ListExpr*>(e);
            if (l->items.size() != 1) return false; // only parenthesized grouping composes
            // …and only ONE level of it: `((*.flip)).assuming(42)` calls the
            // WhateverCode's own .assuming, as in Rakudo
            if (l->items[0] && l->items[0]->kind == NK::ListExpr) return false;
            return exprHasWhateverLit(l->items[0].get());
        }
        default: return false;
    }
}

// A name that is a TERM, not a routine — `pi()`, `e()`, `τ()` — is not an
// UNDEFINED routine: Raku reports the routine form of the name as undeclared
// ("Variable '&pi' is not declared"), a different exception from the
// X::Undeclared::Symbols a genuinely unknown name raises.
static bool termConstantName(const std::string& n) {
    static const std::set<std::string> terms = {
        "pi", "tau", "e", "i", "Inf", "NaN", "\xCF\x80", "\xCF\x84"
    };
    return terms.count(n) != 0;
}
[[noreturn]] void undeclaredRoutine(const std::string& name) {
    if (termConstantName(name))
        throw RakuError{Value::typeObj("X::Undeclared"),
            "Variable '&" + name + "' is not declared. Perhaps you forgot a 'sub' if this "
            "was intended to be part of a signature?"};
    // `begin 42` — a phaser spelled in lower case: Rakudo suggests the phaser
    {
        static const std::set<std::string> kPhasers = {
            "BEGIN", "CHECK", "INIT", "END", "ENTER", "LEAVE", "KEEP", "UNDO", "FIRST",
            "NEXT", "LAST", "PRE", "POST", "CATCH", "CONTROL", "QUIT", "CLOSE", "DOC"};
        std::string up = name;
        for (auto& ch : up) if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        if (up != name && kPhasers.count(up))
            throw RakuError{Value::typeObj("X::Undeclared::Symbols"),
                "Undefined routine '" + name + "'. Did you mean '" + up + "'?"};
    }
    throw RakuError{Value::typeObj("X::Undeclared::Symbols"), "Undefined routine '" + name + "'"};
}

Value Interpreter::callBuiltin(const std::string& name, ValueList args) {
    // A WRAPPED builtin (`&dir.wrap({…})`) routes through callCallable so the
    // wrapper stack runs; unwrapped builtins never pay for the lookup because
    // the map is empty until someone takes a `&builtin` reference.
    if (!builtinRefs_.empty()) {
        auto rit = builtinRefs_.find(name);
        if (rit != builtinRefs_.end() && rit->second.code() && !rit->second.code()->wrappers.empty())
            return callCallable(rit->second, std::move(args));
    }
    auto it = builtins_.find(name);
    if (it != builtins_.end() && !builtinVisible(name)) it = builtins_.end(); // 6.e-only, and this is not 6.e
    if (it == builtins_.end()) {
        // not a builtin: a module-loaded (or otherwise env-bound) routine?
        Value* p = tctx_.cur ? tctx_.cur->find("&" + name) : nullptr;
        if (!p && global_) { auto g = global_->vars.find("&" + name); if (g != global_->vars.end()) p = &g->second; }
        if (p && p->t == VT::Code) return callCallable(*p, std::move(args));
        if (Value* f = pkgRelativeRoutine(name)) return callCallable(*f, std::move(args));
        // `Foo1::bar(); module Foo1 { our sub bar {…} }` — the package is declared
        // further down, and Rakudo installs it (and its `our` subs) at compile
        // time: build the pending declaration now, then look again
        // (S10-packages/basic.t)
        const size_t dc = name.rfind("::");
        // (a MODULE never enters classes_, so the answer is not the test: look again)
        if (dc != std::string::npos && dc > 0 && pendingTypes_.count(name.substr(0, dc))) {
            materializePendingType(name.substr(0, dc));
            p = tctx_.cur ? tctx_.cur->find("&" + name) : nullptr;
            if (!p && global_) { auto g = global_->vars.find("&" + name); if (g != global_->vars.end()) p = &g->second; }
            if (p && p->t == VT::Code) return callCallable(*p, std::move(args));
        }
        undeclaredRoutine(name);
    }
    return it->second(*this, args);
}

Value Interpreter::callEnvFirst(const std::string& name, ValueList args) {
    return callEnvFirstAmp("&" + name, std::move(args));
}
Value Interpreter::callEnvFirstAmp(const std::string& ampName, ValueList args) {
    Value* p = tctx_.cur ? tctx_.cur->find(ampName) : nullptr;
    if (!p && global_) { auto g = global_->vars.find(ampName); if (g != global_->vars.end()) p = &g->second; }
    if (p) return callCallable(*p, std::move(args));
    return callBuiltin(ampName.substr(1), std::move(args));
}

Value Interpreter::getArgs() {
    Value a = Value::array(); a.isList = true;
    for (auto& s : argv_) a.arr()->push_back(Value::str(s));
    return a;
}

// @*ARGS as the program has it NOW — `@*ARGS = 'f' xx 2` before the first
// read of $*ARGFILES decides which files it reads
Value Interpreter::liveArgs() {
    Value* av = tctx_.cur ? tctx_.cur->find("@*ARGS") : nullptr;
    if (!av && global_) av = global_->find("@*ARGS");
    if (!av || av->t != VT::Array || !av->arr()) return getArgs();
    Value a = Value::array(); a.isList = true;
    for (auto& x : *av->arr()) a.arr()->push_back(Value::str(x.toStr()));
    return a;
}

// Push the current %*ENV hash into the real process environment so a child
// launched by run()/shell() inherits any variables the program set/changed.
const Value* Interpreter::envLookup(const std::string& name) {
    auto it = global_->vars.find("%*ENV");
    if (it == global_->vars.end() || it->second.t != VT::Hash || !it->second.hash()) return nullptr;
    auto vit = it->second.hash()->find(name);
    return vit != it->second.hash()->end() ? &vit->second : nullptr;
}

bool Interpreter::envFlag(const std::string& name) {
    const Value* v = envLookup(name);
    return v && v->truthy();
}

std::string Interpreter::envStr(const std::string& name) {
    const Value* v = envLookup(name);
    return v ? v->toStr() : "";
}

// Serialize an uncaught exception as JSON for RAKU_EXCEPTIONS_HANDLER=JSON:
// { "Type::Name" : { "attr" : value, …, "message" : <msg-or-null> } }
std::string Interpreter::exceptionToJson(const Value& ex) {
    // one JSON string escaper for the whole tree (JsonLite) — this had its own
    // near-copy, byte-different on \b/\f and \u case
    auto jstr = [](const std::string& s) { std::string o; rakupp::json::dumpStr(s, o); return o; };
    std::string tn = ex.obj() && ex.obj()->cls ? ex.obj()->cls->name : "Exception";
    std::string o = "{\n  " + jstr(tn) + " : {\n";
    bool first = true;
    if (ex.obj()) for (auto& kv : ex.obj()->attrs) {
        if (kv.first == "message") continue; // message emitted last (may be null)
        if (kv.first == "__bt") continue;    // the frame handle is internal; `backtrace` below is its JSON
        o += (first ? "" : ",\n"); first = false;
        o += "    " + jstr(kv.first) + " : " + (kv.second.isNumeric() ? kv.second.toStr() : jstr(kv.second.toStr()));
    }
    // message: the value if present, else null — and an exception class that
    // gives itself no message at all (`class X::Foo is Exception {}`) has none:
    // its empty default is null, as Rakudo writes it
    std::string msg;
    bool hasMsg = ex.obj() && ex.obj()->attrs.count("message");
    if (hasMsg) msg = ex.obj()->attrs.at("message").toStr();
    if (hasMsg && msg.empty() && ex.obj()->cls && !ex.obj()->cls->findMethod("message")) hasMsg = false;
    // …and the frames, for a consumer that wants the position too (issue #67)
    if (ex.obj() && ex.obj()->attrs.count("__bt")) {
        Value bt = backtraceOf(ex);
        if (bt.t == VT::Array && bt.arr() && !bt.arr()->empty()) {
            o += (first ? "" : ",\n"); first = false;
            o += "    \"backtrace\" : [\n";
            bool f2 = true;
            for (auto& f : *bt.arr()) {
                if (f.t != VT::Hash || !f.hash()) continue;
                auto& h = *f.hash();
                auto fi = h.find("file"), li = h.find("line"), ci = h.find("code");
                std::string nm = ci != h.end() && ci->second.code() ? ci->second.code()->name : "<unit>";
                o += (f2 ? "" : ",\n"); f2 = false;
                o += "      { \"file\" : " + jstr(fi != h.end() ? fi->second.toStr() : "") +
                     ", \"line\" : " + (li != h.end() ? li->second.toStr() : "0") +
                     ", \"subname\" : " + jstr(nm) + " }";
            }
            o += "\n    ]";
        }
    }
    o += (first ? "" : ",\n");
    o += "    \"message\" : " + (hasMsg ? jstr(msg) : std::string("null")) + "\n";
    o += "  }\n}\n";
    return o;
}

void Interpreter::syncEnvToProcess() {
    // --sandbox: %*ENV is the program's own; the process keeps the host's
    // environment, which is also where the engine reads its own switches
    if (sandboxed()) return;
    auto it = global_->vars.find("%*ENV");
    if (it == global_->vars.end() || it->second.t != VT::Hash || !it->second.hash()) return;
    for (auto& kv : *it->second.hash()) {
        std::string val = kv.second.toStr();
        setenv(kv.first.c_str(), val.c_str(), 1);
    }
}

// Index with a Whatever/WhateverCode key, for native codegen: `@a[*-1]` (call
// the WhateverCode with the length), `@a[*]` (all elements as a list).
// Grow a lazy list's materialised prefix to at least `n` elements (bounded, so a
// runaway never truly loops). No-op for a normal (non-lazy) array.
// The g_forceLazy hook (Value.h): fill a finite lazy sequence's buffer so a
// renderer or a comparator sees its elements. An unbounded source is left
// alone — there is no "all of it" to fill.
// steady_clock microseconds — the gather probe's budget clock (see gatherDeadlines).
long long nowMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The gather probe's per-loop-iteration check, off the inline fast path (see
// gatherProbePoint). A prefix of NONE is not a probe — gatherTake says why the
// first take has to be allowed to land however long it takes — so a gather that
// has taken nothing yet keeps running.
void Interpreter::gatherProbeCheck() {
    if (nowMicros() <= t_poll.gatherDeadline) return;
    if (tctx_.gatherStack.empty() || tctx_.gatherStack.back()->empty()) return;
    throw StopGatherEx{};
}
// Is this sequence LAZY in Rakudo's sense — endless, or lazy by declaration —
// rather than merely not reified yet? A gather, and a map/grep/skip view of
// one, is the second kind: `.is-lazy` says False, and whatever binds or
// assigns it reads it whole.
bool seqIsLazy(const Value& v) {
    if (!(v.t == VT::Array && v.ext())) return false;
    auto* st = static_cast<LazySeqState*>(v.ext().get());
    return !st->gatherSeq || st->declaredLazy;
}

static void forceLazyImpl(const Value& v) {
    if (!v.ext() || !v.arr()) return;
    auto st = std::static_pointer_cast<LazySeqState>(v.ext());
    if (st->infinite || !g_cbInterp) return;
#if !RAKUPP_HAVE_CORO
    // A gather that outgrew its probe may be finite or unbounded, and the only
    // way to find out is to ask for more. Ask ONCE per sequence: an unbounded
    // one would otherwise pay a whole re-run of its block every time a value
    // that holds it is printed. (A gather running as a coroutine resumes where
    // it stopped instead, so there is nothing to re-run: it is drained below.)
    if (st->gatherSeq && !st->exhausted) {
        if (st->forceProbed) return;
        st->forceProbed = true;
        g_cbInterp->materializeLazy(v, v.arr()->size() + 1);  // one growth step
        if (!st->exhausted) return;                         // unbounded: no "all of it"
    }
#endif
    g_cbInterp->materializeLazy(v, 1000000);
}
// The display name of a registry key — see ClassInfo::dispName. Reads the live
// class registry through g_matchClasses, so a free function suffices and Value
// can call it without knowing about the Interpreter.
static std::string typeDispNameImpl(const std::string& key) {
    if (!g_matchClasses) return std::string();
    auto it = g_matchClasses->find(key);
    if (it == g_matchClasses->end() || !it->second) return std::string();
    return !it->second->shownName.empty() ? it->second->shownName : it->second->dispName;
}
static const bool g_typeDispNameInstalled = ((g_typeDispName = &typeDispNameImpl), true);
static const bool g_forceLazyInstalled = ((g_forceLazy = &forceLazyImpl), true);
// makeSignature (Builtins.cpp) asks whether a method's class is `is hidden`
extern bool (*g_pkgIsHidden)(const std::string&);
static bool pkgIsHiddenImpl(const std::string& p) {
    if (!g_revInterp) return false;
    auto it = g_revInterp->classes_.find(p);
    return it != g_revInterp->classes_.end() && it->second && it->second->hidden;
}
static const bool g_pkgIsHiddenInstalled = ((g_pkgIsHidden = &pkgIsHiddenImpl), true);
// …and whether it is a ROLE: a role method's invocant is whatever class composes
// it, which its Callable does not know
extern bool (*g_pkgIsRole)(const std::string&);
static bool pkgIsRoleImpl(const std::string& p) {
    if (!g_revInterp) return false;
    auto it = g_revInterp->classes_.find(p);
    return it != g_revInterp->classes_.end() && it->second && it->second->isRole;
}
static const bool g_pkgIsRoleInstalled = ((g_pkgIsRole = &pkgIsRoleImpl), true);
// …and what a SUBSET parameter's type narrows: the first name up its chain
// that is no subset ("" when the name is not one)
extern std::string (*g_subsetNominal)(const std::string&);
static std::string subsetNominalImpl(const std::string& t) {
    if (t == "UInt") return "Int";
    if (!g_revInterp) return "";
    const auto& subs = g_revInterp->subsets_;
    if (!subs.count(t) || g_revInterp->classes_.count(t)) return "";
    std::string n = t;
    for (int hop = 0; hop < 32; hop++) {
        auto si = subs.find(n);
        if (si == subs.end() || g_revInterp->classes_.count(n)) break;
        n = si->second.base;
        if (n == "UInt") { n = "Int"; break; }
    }
    return n.empty() ? std::string("Any") : n;
}
static const bool g_subsetNominalInstalled = ((g_subsetNominal = &subsetNominalImpl), true);
extern void (*g_pullLazy)(const Value&, size_t);   // Value.cpp
static void pullLazyImpl(const Value& v, size_t n) { if (g_cbInterp) g_cbInterp->materializeLazy(v, n); }
static const bool g_pullLazyInstalled = ((g_pullLazy = &pullLazyImpl), true);

// The g_makeTypedEx hook (Value.h): the free runtime helpers that raise a typed
// exception build it through the running interpreter's class registry, so the
// object carries its attributes ($!.range and friends).
static Value makeTypedExImpl(const std::string& type,
                             std::vector<std::pair<std::string, Value>> attrs,
                             const std::string& message) {
    if (!g_cbInterp) return Value::typeObj(type);
    return g_cbInterp->makeTypedEx(type, std::move(attrs), message);
}
static const bool g_makeTypedExInstalled = ((g_makeTypedEx = &makeTypedExImpl), true);

// The g_dateFormat hook (Value.h): render a Date/DateTime through its stored
// `:formatter`. A formatter that stringifies its OWN argument is unbounded —
// Rakudo loops forever on one — and unbounded recursion HERE is a stack
// overflow, so a Date already being formatted falls back to ISO 8601. The
// guard is per-VALUE, not a flat depth counter: a formatter that renders some
// OTHER Date (`{ .year ~ Date.new(2021,2,3) }`) is legitimate and must work.
static thread_local std::vector<const void*> t_dateFormatting;
static bool dateFormatImpl(const Value& d, std::string& out) {
    if (!g_cbInterp || !d.hash()) return false;
    auto it = d.hash()->find("formatter");
    if (it == d.hash()->end() ||
        (it->second.t != VT::Code && it->second.t != VT::Object)) return false;
    const void* key = d.hash();
    for (const void* p : t_dateFormatting) if (p == key) return false;
    t_dateFormatting.push_back(key);
    struct Pop { ~Pop() { t_dateFormatting.pop_back(); } } pop;
    ValueList fa{d};
    out = g_cbInterp->callCallable(it->second, fa).toStr();
    return true;
}
static const bool g_dateFormatInstalled = ((g_dateFormat = &dateFormatImpl), true);
static bool endlessLazyImpl(const Value& v) { // caller checked t == Array && ext
    return std::static_pointer_cast<LazySeqState>(v.ext())->infinite;
}
static const bool g_endlessLazyInstalled = ((g_endlessLazy = &endlessLazyImpl), true);
static bool declLazyLiveImpl(const Value& v) { // caller checked t == Array && ext
    auto* st = static_cast<LazySeqState*>(v.ext().get());
    return st->declaredLazy && !st->exhausted && !st->infinite;
}
static const bool g_declLazyLiveInstalled = ((g_declLazyLive = &declLazyLiveImpl), true);
void Interpreter::materializeLazy(const Value& v, size_t n) {
    if (!v.ext() || !v.arr()) return;
    auto st = std::static_pointer_cast<LazySeqState>(v.ext());
    if (!st->appendNext) return;
    const size_t CAP = 1000000;
    while (v.arr()->size() < n && v.arr()->size() < CAP) {
        st->pullHint = std::min(n, CAP) - v.arr()->size();   // one switch fills a gather's share
        if (!st->appendNext(*v.arr())) { st->exhausted = true; break; }
    }
}

// A FINITE lazy sequence has to be drained before anything walks it, or the
// walker sees only whatever prefix happened to be materialised already. `for 1,
// { $_ + 1 } ... 5` ran its body ONCE, on the seed: a closure generator produces
// its elements on demand and the loop never asked. (`1, 2, 4 ... 16` looked fine
// only because a non-closure generator is built eagerly, and `.elems`/`.List`
// looked fine because they force — so one Seq answered 5 to one question and 1
// to another.)
//
// An INFINITE one is left alone: draining it would not return. Walking that
// incrementally means teaching each loop to pull as it goes, which is its own
// change — `for 1, { $_ + 1 } ... * { last … }` is still short.
Value Interpreter::assignChecked(Expr* target, Value v, const Value* invVal) {
    // `(Foo.new) .= meth`: an invocant with no container behind it is an
    // OBJECT, and assigning to an object is its STORE (Rakudo's assign to a
    // non-container) — the invocant is already evaluated, so it is not re-run
    // (…parenthesized too: `(my class Foo {…}.new).=foo`)
    std::function<bool(Expr*)> freshObj = [&](Expr* t) -> bool {
        if (t->kind == NK::ListExpr && static_cast<ListExpr*>(t)->items.size() == 1)
            return freshObj(static_cast<ListExpr*>(t)->items[0].get());
        if (t->kind == NK::Call || t->kind == NK::Unary) return true;   // (a `do`/declaration term too)
        if (t->kind != NK::MethodCall) return false;
        const std::string& mn = static_cast<MethodCall*>(t)->method;
        return mn == "new" || mn == "bless" || mn == "clone" || mn == "CREATE";
    };
    // …and the answer is the OBJECT the value was stored into, as an
    // assignment answers its target
    if (invVal && target && freshObj(target) &&
        invVal->t == VT::Object && invVal->obj() && invVal->obj()->cls &&
        invVal->obj()->cls->findMethod("STORE")) {
        methodCall(*invVal, "STORE", ValueList{v});
        return *invVal;
    }
    // `(@a.self) .= lc` — `.self` answers its invocant, container and all, so
    // the store is `@a .= lc`'s (inplace.t)
    {
        Expr* it = target;
        if (it && it->kind == NK::ListExpr && static_cast<ListExpr*>(it)->items.size() == 1)
            it = static_cast<ListExpr*>(it)->items[0].get();
        if (it && it->kind == NK::MethodCall) {
            auto* sm = static_cast<MethodCall*>(it);
            if (sm->method == "self" && sm->args.empty() && !sm->meta && !sm->hyper && !sm->methodExpr &&
                sm->inv && sm->inv->kind == NK::VarExpr)
                target = sm->inv.get();
        }
    }
    // A parenthesised list target distributes: `($a, $b) .= reverse` is the
    // mutating form of `($a, $b) = ($a, $b).reverse`, and lvalue() has nothing
    // to hand back for the list itself.
    if (target && target->kind == NK::ListExpr) {
        assignListTarget(static_cast<ListExpr*>(target), v);
        return v;
    }
    // The SIGIL is a container type on the way back in, exactly as it is for a
    // plain `=`: `@a .= repeated` stores an Array, not the Seq the method
    // answered, and `%h .= Hash` a Hash. (This is `.=`'s only caller, so the
    // coercion cannot leak into an ordinary assignment.)
    if (target && target->kind == NK::VarExpr) {
        const std::string& nm = static_cast<VarExpr*>(target)->name;
        // (a SHAPED array is already the Array it will be — coercing it would
        // flatten its rows: `my @c[2;2] .= new(:shape(2,2), …)`)
        if (!nm.empty() && nm[0] == '@' && !(v.t == VT::Array && v.shape())) v = coerceArray(v);
        else if (!nm.empty() && nm[0] == '%') v = coerceHash(v);
    }
    if (Value* lv = lvalue(target)) {
        if (lv->readonly)
            throwNotWritable(*lv);
        v.readonly = v.immutableBind = false;                 // the flag marks the container, not the value
        // a Proxy (a `take-rw` element as the topic) STOREs, as `=` does
        if (lv->t == VT::Hash && lv->hashKind == "Proxy" && lv->hash() && lv->hash()->count("STORE")) {
            Value stored = *lv;
            proxyStore(stored, v);
            return deproxy(stored);
        }
        // `with @a { .=uc }` — the topic IS @a: list-assign into that Array
        if (target && target->kind == NK::VarExpr && static_cast<VarExpr*>(target)->name == "$_" &&
            lv->t == VT::Array && lv->arr() && !lv->isList && !lv->itemized && tctx_.cur) {
            Env* own = nullptr;
            tctx_.cur->findRaw("$_", &own);
            if (own && own->ex && own->ex->topicBindsArray) {
                Value arr = coerceArray(v);
                ValueList elems = arr.t == VT::Array && arr.arr() ? *arr.arr() : ValueList{v};
                lv->arr()->swap(elems);
                return *lv;
            }
        }
        *lv = std::move(v);
        return *lv;
    }
    return v;
}

void Interpreter::drainIfFiniteLazy(const Value& v) {
    if (!(v.t == VT::Array && v.ext() && v.arr())) return;
    auto st = std::static_pointer_cast<LazySeqState>(v.ext());
    if (st->infinite) return;
    // …nor is a STREAMING source: it ends, but only when its producer says so,
    // and the producer may be waiting on what this loop is about to print.
    if (st->streaming) return;
    // A gather that outgrew its probe is not KNOWN to be finite. It is a block
    // that has not yet said it is done, and the only way to ask is to run it —
    // so draining one here ran its generator up to the million-element cap to
    // find out. For a generator that costs anything that is unbounded work for
    // a loop that wanted two elements: Digest::SHA3's squeeze phase is
    // `gather loop { take …; $state .= &KeccakF1600 }`, one Keccak permutation
    // per take, and `sha3_256('hello world')` — which needs ONE — spent them a
    // million at a time. Those iterate LIVE instead, exactly as an endless
    // source does; a gather that reaches its end says so, and from then on
    // this drains it like any other finite lazy list.
    if (st->gatherSeq && !st->exhausted) return;
    materializeLazy(v, 1000000);
}

Value Interpreter::idxW(const Value& base, Value key, bool isHash) {
    // a `but`/`does` mixin over a Hash/Array delegates subscripting to the box
    if (base.t == VT::Object && base.obj() && base.obj()->hasBoxed)
        return idxW(base.obj()->boxed(), std::move(key), isHash);
    // @a[*-1] / @a[*] against an infinite lazy array can't know the end
    if (base.t == VT::Array && base.ext() && std::static_pointer_cast<LazySeqState>(base.ext())->infinite
        && (key.t == VT::Whatever || (key.t == VT::Code && key.code() && key.code()->isWhateverCode)))
        throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot use a Whatever index on an infinite list"};
    long long n = (base.t == VT::Array && base.arr()) ? (long long)base.arr()->size()
                : base.t == VT::Range ? (long long)base.flatten().size() : 0;
    if (key.t == VT::Code && key.code() && key.code()->isWhateverCode)
        key = callCallable(key, ValueList{Value::integer(n)});
    if (key.t == VT::Whatever) { // @a[*] — the whole list / %h{*} — all values
        Value o = Value::array(); o.isList = true;
        if (isHash && base.t == VT::Hash && base.hash()) { for (auto& kv : *base.hash()) o.arr()->push_back(kv.second); }
        else if (base.t == VT::Array && base.arr()) *o.arr() = *base.arr();
        else *o.arr() = base.flatten();
        return o;
    }
    return rtIndexGet(base, key, isHash);
}

// $* / $? magical variables, for native codegen (mirrors the VarExpr evaluator).
// $*RAKU and its .compiler. The object answers everything through
// methodCallInner, but `say $*RAKU` renders from the Value alone, so leave the
// name and the version it displays where Value::gist can find them.
Value Interpreter::rakuIntrospection(bool compiler) {
    Value r = Value::makeHash();
    r.hashKind = compiler ? "Compiler" : "Raku";
    (*r.hash())["name"] = Value::str(compiler ? "Raku++" : "Raku");
    // The language object shows the language revision; the COMPILER shows its
    // own version, which is the Rakudo era we verify against (kOracleEra,
    // Interpreter.h) — `rakudo (2026.08)` is the shape Rakudo uses.
    const int lr = mainLangRevSet_ ? mainLangRev_ : langRev_;   // the process's language, not this unit's
    (*r.hash())["ver"] = Value::str(compiler ? kOracleEra
                       : (lr == 0 ? "6.c" : lr == 1 ? "6.d" : "6.e"));
    return r;
}

std::string Interpreter::cwdName() {
    if (Value* p = findDynamicLenient("$*CWD")) return p->toStr();
    if (!logicalCwd_.empty()) return logicalCwd_;
    char buf[4096]; return getcwd(buf, sizeof buf) ? buf : ".";
}

Value Interpreter::dynVar(const std::string& name) {
    if (name == "$*CWD") { Value p = Value::str(cwdName()); p.hashKind = "IO"; return p; }
    // IntStr allomorphs like Rakudo's: numeric face = uid/gid, string face =
    // the account/group name (falls back to the bare number when the id has
    // no passwd/group entry — containers do that).
    if (name == "$*USER" || name == "$*GROUP") {
#ifndef _WIN32
        bool isUser = name == "$*USER";
        long long id = isUser ? (long long)::getuid() : (long long)::getgid();
        std::string nm;
        if (isUser) { if (struct passwd* pw = ::getpwuid((uid_t)id)) nm = pw->pw_name; }
        else        { if (struct group*  gr = ::getgrgid((gid_t)id)) nm = gr->gr_name; }
        Value v = Value::integer(id);
        v.s = nm.empty() ? std::to_string(id) : nm;
        v.hashKind = "IntStr";
        return v;
#else
        return Value::any();
#endif
    }
    if (name == "$*RAKU" || name == "$*PERL" || name == "$?RAKU" || name == "$?PERL") return rakuIntrospection(false);
    if (name == "$?FILE") return Value::str(fileConstNow());
    if (name == "$*PROGRAM") { Value p = Value::str(progName()); p.hashKind = "IO"; return p; }
    if (name == "$*PROGRAM-NAME") return Value::str(progName());
    if (name == "$*USAGE") { std::string u = mainUsage(); if (!u.empty() && u.back() == '\n') u.pop_back(); return Value::str(u); }
    if (name == "$*EXECUTABLE" || name == "$*EXECUTABLE-NAME") { Value p = Value::str(execPath_); p.hashKind = "IO"; return p; }
    if (name == "$*OUT" || name == "$*ERR" || name == "$*IN") { Value h = Value::makeHash(); h.hashKind = "FileHandle"; (*h.hash())["std"] = Value::str(name == "$*ERR" ? "err" : name == "$*IN" ? "in" : "out"); return h; }
    // $*ARGFILES — the files named in @*ARGS as ONE handle (awk/perl style),
    // or standard input when there are none. Built on ACCESS, not at startup:
    // a program that never mentions it must not read files (or block on stdin).
    // The content is captured in memory, which is what makes `.lines` span
    // every file; `path` carries the first name for the gist.
    if (name == "$*ARGFILES") {
        // ONE handle per @*ARGS: what one read consumes, the next does not see
        // again (`lines(); lines()` reads the files once). Rebuilt only when
        // @*ARGS itself changed before the handle was made from it.
        Value argv = liveArgs();
        std::string key = "\x01";
        if (argv.arr()) for (auto& fn : *argv.arr()) { key += fn.toStr(); key += '\0'; }
        {
            std::lock_guard<std::mutex> lk(argFilesMu_);
            if (argFilesKey_ == key && argFilesCache_.t == VT::Hash) return argFilesCache_;
        }
        Value h = Value::makeHash(); h.hashKind = "FileHandle";
        auto remember = [&](const Value& v) {
            std::lock_guard<std::mutex> lk(argFilesMu_);
            argFilesKey_ = key; argFilesCache_ = v;
            return v;
        };
        if (argv.arr() && !argv.arr()->empty()) {
            std::string all, raw;   // raw: the bytes as they are, for a slurp
            for (auto& fn : *argv.arr()) {
                // each file's last line ends with its file, newline or not:
                // `lines` over THE / FILES / CONTENT is three lines, not one
                if (!all.empty() && all.back() != '\n') all += '\n';
                // `-` is standard input, in its place among the files
                if (fn.toStr() == "-") { std::ostringstream ss; ss << std::cin.rdbuf(); all += ss.str(); raw += ss.str(); noteStdinAtEnd(); continue; }
                // --sandbox: standard input, yes (`-` above, or no @*ARGS); a
                // file the program names in @*ARGS, no
                if (g_sandboxChecks) sandboxRefuse(*this, "$*ARGFILES", SandboxCap::Read);
                std::ifstream in(fn.toStr(), std::ios::binary);
                // An unopenable file is FATAL, as in Rakudo — skipping it
                // silently turned a mistyped path into an empty result, which
                // is exactly how issue #14's reporter and I both lost time.
                if (!in) throw RakuError{Value::typeObj("X::IO::DoesNotExist"),
                    "Failed to open file " + fn.toStr() + ": No such file or directory"};
                std::ostringstream ss; ss << in.rdbuf();
                all += ss.str();
                raw += ss.str();
            }
            (*h.hash())["captured"] = Value::boolean(true);
            (*h.hash())["buffer"] = Value::str(all);
            (*h.hash())["rawbuffer"] = Value::str(raw);
            (*h.hash())["path"] = Value::str((*argv.arr())[0].toStr());
            (*h.hash())["argfiles"] = Value::boolean(true);
            (*h.hash())["mode"] = Value::str("r");
            return remember(h);
        }
        (*h.hash())["std"] = Value::str("in");             // no arguments: read $*IN
        (*h.hash())["argfiles"] = Value::boolean(true);
        return remember(h);
    }
    if (name == "$*DISTRO") { Value h = Value::makeHash(); h.hashKind = "Distro"; (*h.hash())["name"] = Value::str(platDistroName()); return h; }
    if (name == "$*KERNEL") { Value h = Value::makeHash(); h.hashKind = "Kernel"; (*h.hash())["name"] = Value::str(platKernelName()); return h; }
    if (name == "$*VM")     { Value h = Value::makeHash(); h.hashKind = "VM";     (*h.hash())["name"] = Value::str(vmName());   return h; }
    if (name == "$*SPEC") return Value::typeObj("IO::Spec::Unix");
    if (name == "$*PID") return Value::integer((long long)::getpid());
    if (name == "$*TZ") return Value::integer(tzOffsetDyn());
    // the default Collation: every level on, in the usual direction
    if (name == "$*COLLATION") return makeCollation();
    // the default `=~=` tolerance. A program that SETS it gets its own dynamic
    // and never reaches here; reading it used to answer Any, which is a value
    // no arithmetic can use.
    if (name == "$*TOLERANCE") return Value::number(1e-15);
    if (name == "$*INIT-INSTANT") return initInstantVal();
    if (name == "$*THREAD") return currentThread();
    if (name == "$*SCHEDULER") {
        if (tctx_.cur) if (Value* p = tctx_.cur->find("$*SCHEDULER")) return *p; // user-assigned wins
        return defaultScheduler_; // shared .hash(): attr writes (uncaught_handler) persist
    }
    if (name == "$*TMPDIR") { Value p = Value::str(tmpDirPath()); p.hashKind = "IO"; return p; }
    // $*HOME — the user's home directory as an IO::Path (Any when the environment
    // does not say, which is Rakudo's rule). zef reaches its config through it.
    if (name == "$*HOME") {
        // …read from %*ENV, which the program may have changed (a BEGIN that
        // deletes HOME leaves $*HOME Nil, as Rakudo has it on non-Windows)
        std::string hs;
        Value* envp = tctx_.cur ? tctx_.cur->find("%*ENV") : nullptr;
        if (!envp && global_) { auto it = global_->vars.find("%*ENV"); if (it != global_->vars.end()) envp = &it->second; }
        if (envp && envp->t == VT::Hash && envp->hash()) {
            auto envStr = [&](const char* k, std::string& out) {
                auto it = envp->hash()->find(k);
                if (it == envp->hash()->end() || it->second.t == VT::Any || it->second.t == VT::Nil ||
                    it->second.t == VT::Type) return false;
                out = it->second.toStr();
                return true;
            };
            bool found = envStr("HOME", hs);
#ifdef _WIN32
            // Windows sets no HOME: USERPROFILE, then HOMEDRIVE+HOMEPATH, as
            // platHomeDir does. Without them `rakupp install` found no home.
            if (!found || hs.empty()) {
                std::string u, d, p;
                if (envStr("USERPROFILE", u) && !u.empty()) { hs = u; found = true; }
                else if (envStr("HOMEDRIVE", d) && envStr("HOMEPATH", p) && !d.empty() && !p.empty()) {
                    hs = d + p; found = true;
                }
            }
#endif
            if (!found) return Value::nil();
        }
        else hs = platHomeDir();
        const char* h = hs.empty() ? nullptr : hs.c_str();
        if (!h || !*h) return Value::any();
        std::string d = h;
        while (d.size() > 1 && d.back() == '/') d.pop_back();
        Value p = Value::str(d); p.hashKind = "IO"; return p;
    }
    // anything else: resolve from the live env chain (covers %*ENV, $*REPO,
    // program-declared dynamics, $!, $/ — used by native codegen). A `*` name
    // is found through the CALLERS first, as the interpreter's own read finds
    // it: a routine's `my $*X` is what its callees see, not whatever `$*X`
    // their lexical scope holds.
    if (name.size() > 1 && name[1] == '*')
        if (Value* p = findDynamicLenient(name)) return *p;
    if (tctx_.cur) if (Value* p = tctx_.cur->find(name)) return *p;
    if (global_) { auto it = global_->vars.find(name); if (it != global_->vars.end()) return it->second; }
    return Value::any();
}

// ---------------------------------------------------------------------------
// X::Dynamic::NotFound. A `*`-twigil name that nothing declares is not `Any` in
// Raku: a READ answers an armed Failure — so `$*foo.defined`, `if $*foo` and
// `$*foo // …` stay quiet while `$*foo + 1` detonates — and a WRITE throws
// outright, `temp $*foo = 42` with it, since temp assigns through lvalue. rakupp
// used to hand back `Any` on read and MINT A SLOT on write, so a misspelt
// dynamic was silently a brand-new variable and `$*OS` (removed from the
// language) read as undefined instead of saying so.
//
// The exemption list is needed on the WRITE path only. A read of a built-in
// ($*CWD, $*OUT, $*KERNEL …) is answered by eval's own built-in table well
// before the generic dynamic lookup, but `lvalue` has no such table — it
// special-cases the three standard handles and nothing else — so without this
// `$*CWD = "/tmp"` would start throwing. The names are the ones dynVar() above
// synthesises, plus %*ENV / @*ARGS / $*REPO / %*SUB-MAIN-OPTS (engine-provided,
// normally already in global_) and two Rakudo declares that rakupp does not yet
// implement: $*COLLATION and $*EXIT. Answering those `Any` is a gap we already
// had, and turning a quiet gap into a detonating Failure is not a fix for it.
// @*INC and %*INC are deliberately ABSENT — Rakudo raises NotFound for both.
// A `Collation`: the four UCA levels, each 1 (compare), 0 (skip) or -1
// (compare reversed) — what `coll`, `.collate` and `$*COLLATION` work with.
Value makeCollation() {
    Value c = Value::makeHash();
    c.hashKind = "Collation";
    for (const char* k : {"primary", "secondary", "tertiary", "quaternary"})
        (*c.hash())[k] = Value::integer(1);
    return c;
}

// `is DEPRECATED(…)`: what to use instead, as the report words it
std::shared_ptr<std::string> Interpreter::deprecationFor(SubDecl* sd) {
    std::string with = "something else";
    if (sd->deprecatedWith) with = eval(sd->deprecatedWith.get()).toStr();
    return std::make_shared<std::string>(with);
}

// A call of a deprecated routine: remembered by (kind, name, package, advice),
// with the line it was made from, until `Deprecation.report` collects them.
void Interpreter::noteDeprecatedCall(const Callable& c, int line) {
    noteDeprecation(c.isMethod ? "Method" : "Sub", c.name, c.pkg.empty() ? "GLOBAL" : c.pkg, *c.deprecated, line);
}
void Interpreter::noteDeprecation(const std::string& kind, const std::string& name, const std::string& from,
                                  const std::string& with, int line) {
    std::lock_guard<std::mutex> lk(deprecM_);
    for (auto& r : deprecations_)
        if (r.kind == kind && r.name == name && r.from == from && r.with == with) {
            if (std::find(r.lines.begin(), r.lines.end(), line) == r.lines.end()) r.lines.push_back(line);
            return;
        }
    deprecations_.push_back({kind, name, from, with, {line}});
}

Value Interpreter::deprecationReport() {
    std::lock_guard<std::mutex> lk(deprecM_);
    if (deprecations_.empty()) return Value::nil();
    std::string out = "Saw " + std::to_string(deprecations_.size()) + " occurrence" +
                      (deprecations_.size() == 1 ? "" : "s") + " of deprecated code.\n";
    for (auto& r : deprecations_) {
        out += std::string(80, '=') + "\n";
        // (a spelling, not a routine — `"-".IO` — is named as it is written)
        out += r.kind.empty() ? r.name + " seen at:\n"
                              : r.kind + " " + r.name + " (from " + r.from + ") seen at:\n";
        out += "  " + progName() + ", line" + (r.lines.size() == 1 ? "" : "s") + " ";
        for (size_t i = 0; i < r.lines.size(); i++) out += (i ? "," : "") + std::to_string(r.lines[i]);
        out += "\nPlease use " + r.with + " instead.\n";
    }
    out += std::string(80, '-');
    deprecations_.clear();
    return Value::str(out);
}

bool Interpreter::isBuiltinDynamic(const std::string& name) {
    static const std::set<std::string> kBuiltinDyn = {
        "$*ARGFILES", "$*COLLATION", "$*CWD", "$*DEFAULT-READ-ELEMS", "$*DISTRO",
        "$*ERR", "$*EXECUTABLE", "$*EXECUTABLE-NAME", "$*EXIT", "$*GROUP",
        "$*HOME", "$*IN", "$*INIT-INSTANT", "$*KERNEL", "$*OUT", "$*PERL",
        "$*PID", "$*PROGRAM", "$*PROGRAM-NAME", "$*RAKU", "$*REPO", "$*SCHEDULER",
        "$*SPEC", "$*THREAD", "$*TMPDIR", "$*TOLERANCE", "$*TZ", "$*USAGE",
        "$*USER", "$*VM", "%*ENV", "%*SUB-MAIN-OPTS", "@*ARGS",
    };
    return kBuiltinDyn.count(name) != 0;
}
// Rakudo's exception carries the full spelling, sigil and twigil included, and
// composes the message from it; both faces build the same object.
Value Interpreter::dynNotFound(const std::string& name) {
    const std::string msg = "Dynamic variable " + name + " not found";
    Value f = rakuppNewFailure();
    (*f.hash())["exception"] =
        makeTypedEx("X::Dynamic::NotFound", {{"name", Value::str(name)}}, msg);
    (*f.hash())["message"] = Value::str(msg);
    return f;
}
void Interpreter::dynNotFoundThrow(const std::string& name) {
    const std::string msg = "Dynamic variable " + name + " not found";
    throw RakuError{makeTypedEx("X::Dynamic::NotFound", {{"name", Value::str(name)}}, msg), msg};
}

// @a[i]:exists / %h<k>:delete / :k / :v / :kv / :p for native codegen — the
// static subset of the interpreter's adverbed indexing (no $var conditionals).
// A negative subscript is OUT OF RANGE in Raku — `@a[*-1]` indexes from the
// end, `@a[-1]` never does (Rakudo: X::OutOfRange). A read answers the ARMED
// Failure — quiet in a boolean test, fatal on use — because Roast's
// nested_arrays.t stores such reads and asserts their type, and Cro's
// `@empty[*-1]` needs the soft form; a write throws.
// The X::OutOfRange a negative subscript raises, with the attributes roast
// reads off it — $!.what, $!.got and $!.range (`0..^Inf`) — built through the
// hook so these free helpers reach the class registry (sheet LA-15).
static Value outOfRangeEx(long long i, const std::string& msg) {
    if (!g_makeTypedEx) return Value::typeObj("X::OutOfRange");
    return g_makeTypedEx("X::OutOfRange",
        {{"what", Value::str("Index")}, {"got", Value::integer(i)},
         {"range", Value::str("0..^Inf")}}, msg);   // Rakudo's is a Str
}
Value negIndexFailure(long long i) {
    const std::string msg = "Index out of range. Is: " + std::to_string(i) + ", should be in 0..^Inf";
    Value f = Value::makeHash(); f.hashKind = "Failure";
    (*f.hash())["exception"] = outOfRangeEx(i, msg);
    (*f.hash())["message"] = Value::str(msg);
    return f;
}
// A negative index carries its RANGE on the exception — $!.range is `0..^Inf`,
// which roast reads (sheet LA-15). A bare payload had no attributes at all.
// An index used to WRITE (and so to grow) an array: Inf or NaN has no position,
// and taking it as LLONG_MAX grew the array until the process was killed.
long long writeIndexInt(const Value& k) {
    if (k.t == VT::Num && !std::isfinite(k.n))
        throw RakuError{Value::typeObj("X::Item"),
                        std::string("Cannot convert ") + (std::isnan(k.n) ? "NaN" : k.n > 0 ? "Inf" : "-Inf") + " to Int"};
    return k.toInt();
}
[[noreturn]] void negIndexThrow(long long i) {
    const std::string msg = "Index out of range. Is: " + std::to_string(i) + ", should be in 0..^Inf";
    throw RakuError{outOfRangeEx(i, msg), msg};
}

Value rtIndexAdverb(Value& base, const Value& keyIn, bool isHash, const std::string& adverb) {
    bool wantExists = false, negExists = false, wantDelete = false;
    bool kvF = false, pF = false, kF = false, vF = false;
    {
        std::string rest = adverb;
        while (!rest.empty()) {
            size_t c = rest.find(':');
            std::string part = c == std::string::npos ? rest : rest.substr(0, c);
            rest = c == std::string::npos ? "" : rest.substr(c + 1);
            bool neg = !part.empty() && part[0] == '!';
            if (neg) part = part.substr(1);
            if (part == "exists") { wantExists = true; negExists = neg; }
            else if (part == "delete") wantDelete = true;
            else if (part == "kv") kvF = true;
            else if (part == "p") pF = true;
            else if (part == "k") kF = true;
            else if (part == "v") vF = true;
        }
    }
    bool exists = false;
    Value val;
    std::string key;
    long long ai = 0;
    if (isHash) {
        key = keyIn.toStr();
        if (base.t == VT::Hash && base.hash()) {
            auto it = base.hash()->find(key);
            if (it != base.hash()->end()) { exists = true; val = it->second; }
        }
    } else {
        ai = keyIn.toInt();
        // a LAZY array reifies up to the index first (see the evaluator's
        // adverb arm): `@a[2]:delete` deletes what is there
        if (ai >= 0 && base.t == VT::Array && base.ext() && base.arr() && g_cbInterp)
            g_cbInterp->materializeLazy(base, (size_t)ai + (wantDelete ? 2 : 1));
        if (base.t == VT::Array && base.arr()) {
            if (ai < 0) ai += (long long)base.arr()->size();
            if (ai >= 0 && ai < (long long)base.arr()->size()) {
                const Value& v = (*base.arr())[ai];
                exists = !(v.t == VT::Nil || v.t == VT::Any || v.t == VT::Type);
                val = v;
            }
        }
    }
    Value keyV = isHash ? Value::str(key) : Value::integer(ai);
    // A Pair and a List are immutable: `:delete` on either is an error, not a
    // no-op. `base.hash()` is null for a Pair, so the erase below dereferenced
    // nothing and took the process with it — Crane's remove tests segfaulted
    // once the Pair stopped being silently promoted to a Hash (issue #69).
    // The WORDING is asserted, not decoration: Crane matches the payload with
    // `Can not remove [values|elements] from a (\w+)` and rethrows as its own
    // X::Crane::Remove::RO.
    if (wantDelete && base.t == VT::Pair)
        throw RakuError{Value::typeObj("X::AdHoc"), "Can not remove values from a Pair"};
    if (wantDelete && base.t == VT::Array && base.isList && base.s != "Seq" &&
        base.enumName.empty())
        throw RakuError{Value::typeObj("X::AdHoc"), "Can not remove elements from a List"};
    if (wantDelete && exists) {
        if (isHash) base.hash()->erase(key);
        else {
            (*base.arr())[ai] = Value::any();
            // a trailing delete SHORTENS the array — unless it is LAZY, whose
            // reified prefix is not its end (see evalIndex's adverb arm)
            if (ai == (long long)base.arr()->size() - 1 && !base.ext()) {
                base.arr()->pop_back();
                while (!base.arr()->empty() &&
                       (base.arr()->back().t == VT::Nil || base.arr()->back().t == VT::Any))
                    base.arr()->pop_back();
            }
        }
    }
    if (wantExists) {
        Value ex = Value::boolean(negExists ? !exists : exists);
        if (kvF) { Value o = Value::array({keyV, ex}); o.isList = true; return o; }
        if (pF) return Value::pair(keyV.toStr(), ex);
        return ex;
    }
    if (kF) return exists ? keyV : Value::array();
    if (vF) return exists ? val : Value::array();
    if (kvF) { Value o = exists ? Value::array({keyV, val}) : Value::array(); o.isList = true; return o; }
    if (pF) return exists ? Value::pair(keyV.toStr(), val) : Value::array();
    if (exists) return val;
    // missing (or just-deleted) element: the container's default, when it has one
    Value dv = arrayMissingDefault(base);
    return dv.t == VT::Nil ? Value::any() : dv;
}

// $obj.accessor as an assignable slot (native codegen) — same semantics as the
// interpreter's MethodCall lvalue: FileHandle slots are free-form; a public
// attribute must be `is rw`; anything else is not assignable.
Value& Interpreter::accessorRef(Value& base, const std::string& name) {
    if (base.t == VT::Hash && base.hashKind == "FileHandle") {
        if (!base.hash()) base.setHash(makePayload<ValueMap>());
        return (*base.hash())[name];
    }
    if (base.t == VT::Object && base.obj()) {
        for (ClassInfo* ci = base.obj()->cls.get(); ci; ci = ci->parent.get())
            for (auto& at : ci->attrs)
                // public @./%. attrs assign through the accessor without `is rw`
                if (at.name == name &&
                    !(at.pub && (at.rw || at.sigil == '@' || at.sigil == '%')))
                    throw RakuError{Value::typeObj("X::Assignment::RO"),
                        "Cannot modify an immutable '" + name + "'"};
        return base.obj()->attrs[name];
    }
    throw RakuError{Value::typeObj("X::Assignment::RO"), "Target is not assignable"};
}

// ---- PseudoStash ----------------------------------------------------------
// A pseudo-package chain names a SCOPE and a way of looking in it:
//   MY        this block's own frame, nothing outside it       'F'
//   OUTER(S)  the lexical outer frame, and outwards from it     'C'
//   LEXICAL   this frame and outwards (a `$*x` goes dynamic)    'L'
//   CALLER    the calling frame, and outwards from it           'C'
//   CALLERS   every caller, innermost first                     'S'
//   UNIT      the compilation unit's own frame, and outwards    'C'
//   CORE / SETTING, GLOBAL, OUR, PROCESS, DYNAMIC               'K' 'G' 'O' 'P' 'D'
// Steps compose left to right: `CALLER::OUTER::` is the caller's outer
// frame, `OUTER::OUTER::` two frames out. A CALLER step leaves a frame only
// the call stack keeps alive, so it is held as a raw pointer and re-proved
// against the live stack at every use.
bool Interpreter::isPseudoChain(const std::string& chain) {
    static const std::set<std::string> ps = {
        "MY", "OUR", "CORE", "GLOBAL", "PROCESS", "DYNAMIC", "CALLER", "CALLERS",
        "OUTER", "OUTERS", "LEXICAL", "UNIT", "SETTING", "CLIENT"};
    if (chain.empty()) return false;
    size_t start = 0;
    for (;;) {
        size_t sep = chain.find("::", start);
        std::string comp = chain.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
        if (!ps.count(comp)) return false;
        if (sep == std::string::npos) return true;
        start = sep + 2;
        if (start >= chain.size()) return true;
    }
}

Value Interpreter::makePseudoStash(const std::string& chainIn) {
    std::string chain = chainIn;
    while (chain.size() >= 2 && chain.compare(chain.size() - 2, 2, "::") == 0) chain.resize(chain.size() - 2);
    if (chain.compare(0, 2, "::") == 0) chain = chain.substr(2);
    std::shared_ptr<Env> env = tctx_.cur;
    escapeChain(env.get());   // PARALLEL-SCALING-PLAN P2: a stash can travel, and it reads by name
    Env* raw = nullptr;                       // set once a CALLER step is taken
    long dynIdx = (long)tctx_.dynStack.size();
    char mode = 'F';
    auto outer = [&]() {
        if (raw) raw = raw->parent.get();
        else if (env && env->parent) env = env->parent;
    };
    // the unit: a loaded module's file frame when we stand in one, else the
    // mainline's, which is the global frame itself
    // (an EVAL's own scope is a unit too — also for a closure it made, run
    // later: `$CALLER::UNIT::x` from inside one names the EVAL's, pseudo-6*.t)
    auto unitOf = [&]() {
        if (raw) {
            Env* r = raw;
            while (r && !r->unitFrame && !r->evalFrame) r = r->parent.get();
            raw = r ? r : global_.get();
        }
        else {
            std::shared_ptr<Env> u = env;
            while (u && !u->unitFrame && !u->evalFrame) u = u->parent;
            env = u ? u : !g_evalUnits.empty() && g_evalUnits.back() ? g_evalUnits.back() : global_;
        }
    };
    size_t start = 0;
    while (start < chain.size()) {
        size_t sep = chain.find("::", start);
        std::string c = chain.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
        start = sep == std::string::npos ? chain.size() : sep + 2;
        if (c == "MY") mode = 'F';
        else if (c == "OUTER" || c == "OUTERS") { outer(); mode = 'C'; }
        else if (c == "LEXICAL") mode = 'L';
        else if (c == "CALLER") {
            if (dynIdx > 0) { raw = tctx_.dynStack[--dynIdx]; env.reset(); }
            mode = 'C';
        }
        else if (c == "CALLERS") { if (dynIdx > 0) --dynIdx; mode = 'S'; }
        else if (c == "UNIT") { unitOf(); mode = 'C'; }
        else if (c == "CORE" || c == "SETTING") mode = 'K';
        else if (c == "GLOBAL") mode = 'G';
        else if (c == "OUR") mode = 'O';
        else if (c == "PROCESS") mode = 'P';
        else if (c == "DYNAMIC") mode = 'D';
        else if (c == "CLIENT") mode = 'C';
    }
    auto od = makePayload<ObjectData>();
    auto cit = classes_.find("PseudoStash");
    if (cit != classes_.end()) od->cls = cit->second;
    od->attrs["chain"] = Value::str(chain);
    od->attrs["mode"] = Value::str(std::string(1, mode));
    // the CORE it belongs to: 6.c's class serves 6.c and 6.d, 6.e has its own
    // (`CORE::v6e::PseudoStash` — a multi told apart by revision)
    od->attrs["\x01rev"] = Value::integer(langRev_);
    od->attrs["dyn"] = Value::integer(dynIdx);
    if (raw) od->attrs["raw"] = Value::integer((long long)(uintptr_t)raw);
    else if (env) { Value ctx = Value::makeHash(); ctx.hashKind = "PseudoCtx"; ctx.extM() = std::static_pointer_cast<void>(env); od->attrs["ctx"] = ctx; }
    // the snapshot the rest of the Hash protocol (`.keys`, `.grep`, …) reads
    Value snap = Value::makeHash();
    Env* e = raw ? raw : env.get();
    // The program's own frame holds, besides its lexicals, every qualified
    // global and every type any unit declared: only what the PROGRAM declared
    // or imported is its lexical (`-M Top1` shows Top1, not the Needed that
    // Top1 needed; no `Mod::EXPORT::…`).
    const UnitStash* progView = nullptr;
    { auto pv = unitStash_.find(""); if (pv != unitStash_.end()) progView = &pv->second; }
    // the PROCESS dynamics the runtime itself installs in that frame are no
    // lexicals of the program's either (Rakudo's MY:: has none of them; a
    // `my $*x` the program declares is listed)
    static const std::set<std::string> kProcessDynamics = {
        "%*ENV", "@*ARGS", "&*EXIT", "$*REPO", "$*LANG", "$*IN-DECL", "%*DATA-NATIVE-CLAIMED",
    };
    auto notProgLexical = [&](Env* f, const std::string& k, const Value& v) {
        if (f != global_.get()) return false;
        if (kProcessDynamics.count(k)) return true;
        // a QUALIFIED global (`Mod::EXPORT::…`, `&Pkg::sub`) is not a lexical;
        // `&term:<A::B>` is — its `::` is inside the name's own brackets
        const size_t c = k.find("::");
        if (c != std::string::npos && c != 0) {
            size_t b = std::strchr("$@%&", k[0]) ? 1 : 0;
            bool ident = b < c;
            for (size_t i = b; i < c && ident; i++)
                if (!(ascii::isalnum((unsigned char)k[i]) || k[i] == '_' || k[i] == '-' || k[i] == '\'')) ident = false;
            if (ident) return true;
        }
        // (only a TYPE some unit declared under that name; the program's own
        // constant or term of the same name is its own)
        return v.t == VT::Type && stashTracked_.count(k) && !(progView && progView->lexical.count(k));
    };
    auto takeFrame = [&](Env* f) {
        if (!f) return;
        for (auto& kv : f->vars)
            if (!snap.hash()->count(kv.first) && !notProgLexical(f, kv.first, *kv.second.deref())) (*snap.hash())[kv.first] = *kv.second.deref();
        if (f->layout)
            for (auto& nm : f->layout->names)
                if (Value* p = f->padFind(nm))
                    if (!snap.hash()->count(nm) && !notProgLexical(f, nm, *p->deref())) (*snap.hash())[nm] = *p->deref();
    };
    if (mode == 'L') for (Env* f = e; f; f = f->parent.get()) takeFrame(f);
    else if (mode == 'F' || mode == 'C') takeFrame(e);
    else if (mode == 'D') {
        // every `$*` name the dynamic lookup could reach: this frame's chain,
        // each caller's, then GLOBAL's `$x` and PROCESS's (pseudo-6e.t)
        auto takeDyn = [&](Env* f) {
            for (; f; f = f->parent.get()) {
                for (auto& kv : f->vars)
                    if (kv.first.size() > 2 && kv.first[1] == '*' && std::strchr("$@%&", kv.first[0]) &&
                        !snap.hash()->count(kv.first)) (*snap.hash())[kv.first] = *kv.second.deref();
                if (f->layout)
                    for (auto& nm : f->layout->names)
                        if (nm.size() > 2 && nm[1] == '*' && !snap.hash()->count(nm))
                            if (Value* p = f->padFind(nm)) (*snap.hash())[nm] = *p->deref();
            }
        };
        takeDyn(tctx_.cur.get());
        for (auto it = tctx_.dynStack.rbegin(); it != tctx_.dynStack.rend(); ++it) takeDyn(*it);
        if (global_)
            for (auto& kv : global_->vars)
                if (kv.first.size() > 9 && kv.first.compare(1, 8, "GLOBAL::") == 0 &&
                    kv.first.find("::", 9) == std::string::npos) {
                    std::string dn = kv.first.substr(0, 1) + "*" + kv.first.substr(9);
                    (*snap.hash())[dn] = kv.second;
                }
    }
    od->attrs["snap"] = snap;
    return Value::object(od);
}

bool Interpreter::frameSymbol(Env* f, const std::string& key, Value& out) {
    if (!f) return false;
    if (Value* p = f->local(key)) { ParStripe rs(*this, p); out = *p; return true; }
    if (f->declStmts) {
        std::vector<const VarExpr*> decls;
        spDeclaredInRaw(*static_cast<const std::vector<StmtPtr>*>(f->declStmts), decls);
        for (auto* v : decls)
            if (v->name == key) { out = typedDefault(v->declType, key[0]); return true; }
    }
    return false;
}

// Before 6.e an EVAL's SETTING is the scope it was called from, so the
// caller's `my $x` — declared further down included — answers
// (6.c/S04-declarations/my-6c.t); a program's own SETTING is CORE, which
// holds no variable of a program's.
bool Interpreter::evalSettingSymbol(const std::string& key, Value& out) {
    if (sixE() || g_evalUnits.empty() || !g_evalUnits.back()) return false;
    Env* unit = g_evalUnits.back().get();
    bool inEval = false;
    for (Env* w = tctx_.cur.get(); w && !inEval; w = w->parent.get()) inEval = w == unit;
    if (!inEval) return false;
    // (the EVAL's own scope is its mainline, not its setting)
    for (Env* f = unit->evalFrame ? unit->parent.get() : unit; f; f = f->parent.get())
        if (frameSymbol(f, key, out)) return true;
    return false;
}

bool Interpreter::pseudoStashGet(const Value& self, const std::string& key, Value& out) {
    if (self.t != VT::Object || !self.obj()) return false;
    auto& at = self.obj()->attrs;
    const char mode = at.count("mode") ? at["mode"].toStr()[0] : 'C';
    Env* e = nullptr;
    if (at.count("raw")) {
        Env* r = (Env*)(uintptr_t)at["raw"].toInt();
        // …only while that frame is still on the call stack (or an outer of one)
        for (Env* d : tctx_.dynStack) {
            for (Env* w = d; w && !e; w = w->parent.get()) if (w == r) e = r;
            if (e) break;
        }
        if (!e) return false;
    }
    else if (at.count("ctx") && at["ctx"].ext()) e = static_cast<Env*>(at["ctx"].ext().get());
    const bool dynName = key.size() > 1 && (key[0] == '$' || key[0] == '@' || key[0] == '%' || key[0] == '&') && key[1] == '*';
    // Walk out from a frame. A block that declares the name FURTHER ON has it
    // already — declarations are compile-time — only not yet initialized, and
    // it shadows any outer one: `{ EVAL 'OUTER::<$x>'; my $x }` reads that
    // block's undefined $x (6.c/S04-declarations/my-6c.t), as EVAL's own
    // undeclared-variable check already rules
    auto findWithPending = [&](Env* from, Value& o) -> bool {
        for (Env* f = from; f; f = f->parent.get()) {
            if (Value* p = f->local(key)) { ParStripe rs(*this, p); o = *p; return true; }
            if (f->declStmts) {
                std::vector<const VarExpr*> decls;
                spDeclaredInRaw(*static_cast<const std::vector<StmtPtr>*>(f->declStmts), decls);
                for (auto* v : decls)
                    if (v->name == key) { o = typedDefault(v->declType, key[0]); return true; }
            }
        }
        return false;
    };
    switch (mode) {
        case 'F':
            if (e) if (Value* p = e->local(key)) { ParStripe rs(*this, p); out = *p; return true; }
            // a loop body's `state` names live in the loop's own state frame,
            // just outside the body's
            if (e && e->parent && e->parent->loopFrame)
                if (Value* p = e->parent->local(key)) { ParStripe rs(*this, p); out = *p; return true; }
            return false;
        case 'C':
            if (e && !dynName && findWithPending(e, out)) return true;
            if (e) if (Value* p = e->find(key)) { ParStripe rs(*this, p); out = *p; return true; }
            return false;
        case 'L':
            if (e) if (Value* p = e->find(key)) { ParStripe rs(*this, p); out = *p; return true; }
            // the root stash names the pseudo-packages too: `::.<MY>`
            if (!key.empty() && !std::strchr("$@%&", key[0]) && isPseudoChain(key)) {
                out = makePseudoStash(key);
                return true;
            }
            if (dynName) if (Value* p = findDynamicLenient(key)) { ParStripe rs(*this, p); out = *p; return true; }
            return false;
        case 'S': {
            // an INLINE block (`if 1 { … }`) is called by the block around it:
            // those scopes, out to the routine, are its first callers
            if (e)
                for (Env* w = e->parent.get(); w; w = w->parent.get()) {
                    if (Value* p = w->local(key)) {
                        if (dynName || w->xr().varDynamic.count(key)) { ParStripe rs(*this, p); out = *p; return true; }
                        break;
                    }
                    if (w->routineFrame) break;
                }
            long d = at.count("dyn") ? (long)at["dyn"].toInt() : (long)tctx_.dynStack.size();
            for (long k = std::min(d, (long)tctx_.dynStack.size() - 1); k >= 0; k--)
                if (Env* f = tctx_.dynStack[k]) if (Value* p = dynInFrame(f, key)) { ParStripe rs(*this, p); out = *p; return true; }
            return false;
        }
        case 'P':
            if (key.size() > 1 && key[1] != '*') {
                std::string dk = key.substr(0, 1) + "*" + key.substr(1);
                if (Value* p = findDynamicLenient(dk)) { ParStripe rs(*this, p); out = *p; return true; }
                out = dynVar(dk);
                return !(out.t == VT::Nil || out.t == VT::Any);
            }
            if (Value* p = findDynamicLenient(key)) { ParStripe rs(*this, p); out = *p; return true; }
            return false;
        case 'D':
            if (dynName) if (Value* p = findDynamicViaGlobal(key)) { ParStripe rs(*this, p); out = *p; return true; }
            return false;
        default: break;
    }
    // CORE / GLOBAL / OUR: the static qualified-name machinery answers these
    if (key.empty()) return false;
    if (mode == 'K' && key[0] == '&') {
        if (const Value* b = builtinRef(key.substr(1))) { out = *b; return true; }
        return false;
    }
    // a user variable under CORE::/SETTING:: is the calling code's (the 6.c/6.d
    // EVAL rule above), declarations further down its blocks included
    if (mode == 'K' && e && key.size() > 1 && std::strchr("$@%", key[0]) &&
        (ascii::isalpha((unsigned char)key[1]) || key[1] == '_') && findWithPending(e, out))
        return true;
    const std::string pk = mode == 'K' ? "CORE" : mode == 'G' ? "GLOBAL" : "OUR";
    if (std::strchr("$@%&", key[0])) {
        VarExpr ve(key);
        ve.viaPseudoPkg = true;
        ve.pseudoPkg = pk;
        try {
            out = eval(&ve);
            if (out.t == VT::Hash && out.hashKind == "Failure") return false;
            return true;
        } catch (RakuError&) { return false; }
    }
    return false;
}

Value Interpreter::pseudoStashCall(const std::string& m, const Value& self, ValueList& args) {
    if (m == "WHO") return self;
    const std::string key = args.empty() ? std::string() : args[0].toStr();
    if (m == "AT-KEY") {
        Value out;
        if (pseudoStashGet(self, key, out)) return out;
        if (!sixE()) return Value::nil();
        Value f = rakuppNewFailure();
        (*f.hash())["exception"] = makeTypedEx("X::NoSuchSymbol", {{"symbol", Value::str(key)}},
                                               "No such symbol '" + key + "'");
        (*f.hash())["message"] = Value::str("No such symbol '" + key + "'");
        return f;
    }
    if (m == "EXISTS-KEY") { Value out; return Value::boolean(pseudoStashGet(self, key, out)); }
    if (m == "BIND-KEY" || m == "ASSIGN-KEY") {
        Value v = args.size() > 1 ? args[1] : Value::any();
        auto& at = self.obj()->attrs;
        const char mode = at.count("mode") ? at["mode"].toStr()[0] : 'C';
        Env* e = nullptr;
        if (at.count("raw")) e = (Env*)(uintptr_t)at["raw"].toInt();
        else if (at.count("ctx") && at["ctx"].ext()) e = static_cast<Env*>(at["ctx"].ext().get());
        Value* slot = nullptr;
        if (mode == 'F' && e) slot = e->local(key);
        else if ((mode == 'C' || mode == 'L') && e) slot = e->find(key);
        if (!slot && mode == 'D' && key.size() > 1 && key[1] == '*') slot = findDynamicViaGlobal(key);
        if (!slot && mode == 'L' && key.size() > 1 && key[1] == '*') slot = findDynamicLenient(key);
        if (!slot && mode == 'K' && key == "$_") slot = tctx_.cur ? tctx_.cur->find("$_") : nullptr;
        if (!slot && (mode == 'F' || mode == 'C' || mode == 'L') && e) slot = &e->define(key, Value::any());
        if (!slot && (mode == 'G' || mode == 'O') && global_) slot = &global_->define(key, Value::any());
        if (!slot) throw RakuError{Value::typeObj("X::Bind"), "Cannot bind to '" + key + "' through this pseudo-package"};
        // a frame reached through `callframe(N).my` lends out only its dynamics
        if (at.count("dynonly") && at["dynonly"].truthy() && e) {
            bool dyn = key.size() > 1 && key[1] == '*';
            for (Env* w = e; w && !dyn; w = w->parent.get()) {
                if (w->xr().varDynamic.count(key)) dyn = true;
                if (w->local(key)) break;
            }
            if (!dyn)
                throw RakuError{Value::typeObj("X::Assignment::RO"),
                                "Cannot modify an immutable '" + key + "' (it is not dynamic)"};
        }
        { ParStripe ws(*this, slot); *slot = v; }   // torn-copy contract: a stash travels
        return v;
    }
    if (m == "DELETE-KEY")
        throw RakuError{Value::typeObj("X::AdHoc"), "Cannot delete from a pseudo-package"};
    return Value::nil();
}

// Proper Raku resolution order for $*foo (t/regression/dynamic-var-caller-chain
// holds the oracle probes): the CURRENT frame first — a routine's own
// `my $*X` beats its caller's — then each CALLER frame innermost-out; a
// caller's declaration beats anything the callee merely closes over.
Value* Interpreter::findDynamicSlot(const std::string& name) {
    ExecContext& t = tctx_;   // one thread-local resolution (see execBlock)
    // inside a gather's block, its own frames stop at the scope that wrote the
    // gather (see dynInFrame); the consumer's frames below them do not
    const Env* stop = nullptr;
    size_t ownFrom = 0;
    if (t.curGather) { stop = gatherDynBoundary(t.curGather); ownFrom = gatherDynBase(t.curGather); }
    if (t.cur) if (Value* p = dynInFrame(t.cur.get(), name, stop)) return p;
    for (size_t i = t.dynStack.size(); i-- > 0;)
        if (Env* e = t.dynStack[i])
            if (Value* p = dynInFrame(e, name, i >= ownFrom ? stop : nullptr)) return p;
    return nullptr;
}

// A `$*x` through DYNAMIC:: as Rakudo resolves it: the call chain, then
// GLOBAL's `$x`, then PROCESS's — so GLOBAL shadows a same-named PROCESS
// symbol (pseudo-6e.t). PROCESS's live in global_ under the `$*x` spelling.
Value* Interpreter::findDynamicViaGlobal(const std::string& name) {
    // (a hit IN global_ under the `$*x` spelling is PROCESS's entry, which the
    // lexical walk reaches last of all — GLOBAL's `$x` comes before it)
    Value* processSlot = global_ ? global_->local(name) : nullptr;
    if (Value* p = findDynamicSlot(name)) if (p != processSlot) return p;
    if (global_ && name.size() > 2 && name[1] == '*')
        if (Value* g = global_->local(name.substr(0, 1) + "GLOBAL::" + name.substr(2))) return g;
    return findDynamicLenient(name);
}

Value* Interpreter::findDynamicLenient(const std::string& name) {
    if (Value* p = findDynamicSlot(name)) return p;
    // Historical fallbacks, kept deliberately: module-scope and closure-carried
    // dynamics (a start-block's spawn scope, a module's `my $*DEBUG` read by
    // its subs) resolved through full chains before the ordering fix, and
    // still should — Rakudo would refuse these, rakupp stays lenient.
    if (tctx_.cur) if (Value* p = tctx_.cur->find(name)) return p;
    for (auto it = tctx_.dynStack.rbegin(); it != tctx_.dynStack.rend(); ++it)
        if (*it) if (Value* p = (*it)->find(name)) return p;
    return nullptr;
}

// Evaluate an attribute's `where {…}` constraint against a candidate value:
// a Code is called with the value (and $_ bound), anything else smartmatches.
bool Interpreter::attrWhereOk(const void* whereExpr, const Value& v) {
    if (!whereExpr) return true;
    auto env = std::make_shared<Env>(); env->parent = tctx_.cur;
    env->define("$_", v);
    auto saved = tctx_.cur; tctx_.cur = env;
    bool ok = true;
    try {
        Value cv = eval(const_cast<Expr*>(static_cast<const Expr*>(whereExpr)));
        if (cv.t == VT::Code && cv.code()) ok = boolify(callCallable(cv, ValueList{v}));
        else ok = boolify(smartmatchValue("~~", v, cv));
    } catch (...) { tctx_.cur = saved; throw; }
    tctx_.cur = saved;
    return ok;
}

Value& Interpreter::dynVarRef(const std::string& name) {
    if (Value* p = findDynamicLenient(name)) return *p;
    return global_->define(name, Value::any());
}
Value& Interpreter::laxVarRef(const std::string& name) {
    if (Value* p = findDynamicLenient(name)) return *p;
    // The PACKAGE the write stands in, not the block that happened to run
    // first: the innermost enclosing class/role/module body, else the unit's
    // own outermost scope. Roast's strict.t asks for both faces of that — a
    // `$foo = 42` inside `class Foo` is readable in the class and is NOT the
    // `$foo` a later `use strict` scope refers to — and the block rule got the
    // ordinary case wrong: Rakudo answers 15 for `no strict; for 1..5 -> $k {
    // $sum += $k }; say $sum`, where a per-block slot answered (Any) because
    // every iteration made its own.
    Value dflt = defaultFor(name.empty() ? '$' : name[0]);
    for (auto e = tctx_.cur; e; e = e->parent)
        if (e->packageFrame || !e->parent) {
            Value& slot = e->define(name, dflt);
            // …and a PACKAGE's lax variable is an `our` of that package: publish
            // the same view an `our` declaration gets, so `$Foo::foo` reads the
            // slot `class Foo { $foo = 42 }` wrote (S02-names/strict.t).
            if (e->packageFrame && global_ && e.get() != global_.get() && !tctx_.pkgPrefix.empty() && name.size() > 1)
                global_->define(ourPublishedName(name, tctx_.pkgPrefix), makeEnvSlotProxy(e, name));
            return slot;
        }
    return (curPkgEnv_ ? curPkgEnv_ : global_)->define(name, dflt);
}

// %*SUB-MAIN-OPTS<named-anywhere> as seen from MAIN auto-invoke. The mainline's
// `my %*SUB-MAIN-OPTS` lives in the mainline env (interpreter) or global_
// (compiled programs route dynamics through dynVarRef), so both faces of the
// dispatcher consult the same two scopes.
bool Interpreter::mainNamedAnywhere() {
    Value* smo = tctx_.cur ? tctx_.cur->find("%*SUB-MAIN-OPTS") : nullptr;
    if (!smo && global_) smo = global_->find("%*SUB-MAIN-OPTS");
    if (!smo || smo->t != VT::Hash || !smo->hash()) return false;
    auto it = smo->hash()->find("named-anywhere");
    return it != smo->hash()->end() && it->second.truthy();
}

// Any other %*SUB-MAIN-OPTS entry (Nil when the program set none)
Value Interpreter::mainOption(const std::string& key) {
    Value* smo = tctx_.cur ? tctx_.cur->find("%*SUB-MAIN-OPTS") : nullptr;
    if (!smo && global_) smo = global_->find("%*SUB-MAIN-OPTS");
    if (!smo || smo->t != VT::Hash || !smo->hash()) return Value::nil();
    auto it = smo->hash()->find(key);
    return it != smo->hash()->end() ? it->second : Value::nil();
}

// Rakudo's RUN-MAIN parses the LIVE @*ARGS, not the saved process argv — a
// mainline `@*ARGS = <...>` (roast S06-other/main.t) or a pre-MAIN .shift
// changes what MAIN sees; an untouched @*ARGS is identical to the argv it
// was defined from, so this is a no-op for ordinary programs.
void Interpreter::refreshArgvFromLiveArgs() {
    Value* av = tctx_.cur ? tctx_.cur->find("@*ARGS") : nullptr;
    if (!av && global_) av = global_->find("@*ARGS");
    if (!av || av->t != VT::Array || !av->arr()) return;
    argv_.clear();
    for (auto& x : *av->arr()) argv_.push_back(x.toStr());
}

// The MAIN command-line protocol, shared by the interpreter's auto-invoke and
// by compiled (--exe) binaries via runCompiledMain. Fills margs and returns -1
// when a candidate accepts the argv; otherwise prints the usage text (a user
// &USAGE hook wins; --help goes to stdout) and returns the process exit code.
int Interpreter::mainProtocol(Value& mainSub, ValueList& margs) {
    refreshArgvFromLiveArgs();
    // `sub MAIN($a is rw)` — nothing on a command line can be written back to
    {
        std::vector<const Value*> cands;
        if (mainSub.code() && mainSub.code()->isMultiDispatcher)
            for (auto& c : mainSub.code()->candidates) cands.push_back(&c);
        else if (mainSub.code()) cands.push_back(&mainSub);
        bool rw = false;
        for (const Value* c : cands)
            if (c->code() && c->code()->params)
                for (auto& p : *c->code()->params) if (p.isRw) rw = true;
        if (rw)
            std::cerr << "Potential difficulties:\n    'is rw' on parameters of 'sub MAIN' usually "
                         "cannot be satisfied.\n    Did you mean 'is copy'?\n";
    }
    // Space-separated option values, Rakudo's exact (oracle-verified)
    // rule: before the options-end boundary, `--foo abc` pairs into
    // :foo<abc> iff the candidate declares :$foo with the Str type —
    // untyped and Int/Num named params do NOT pair (`--n 42` genuinely
    // fails dispatch under Rakudo). Consumption is unconditional:
    // `--foo --verbose` makes foo eq "--verbose". Positionals need no
    // special-casing — a positional token ends option parsing, which
    // is what makes `prog xx --foo abc` fail exactly as in Rakudo.
    // Decided per candidate; implemented as an argv rewrite
    // (`--foo abc` -> `--foo=abc`) into the classic parse.
    const bool namedAnywhere = mainNamedAnywhere();
    auto pairedArgs = [&](const Value& cand) -> ValueList {
        if (!cand.code() || !cand.code()->params) return rtMainArgs(argv_, namedAnywhere, this);
        std::set<std::string> keys;
        for (auto& p : *cand.code()->params) {
            if (!p.named || p.type != "Str") continue;
            std::string k = !p.namedKey.empty() ? p.namedKey
                          : (p.name.size() > 1 ? p.name.substr(1) : "");
            if (!k.empty()) keys.insert(k);
            // `:xen(:$xin)` — the inner variable's name is an option name too
            if (p.aliasBoth && p.name.size() > 1) keys.insert(p.name.substr(1));
            for (auto& al : p.aliasKeys) keys.insert(al);
        }
        if (keys.empty()) return rtMainArgs(argv_, namedAnywhere, this);
        std::vector<std::string> av;
        bool done = false; // pairing obeys the same options-end-at-the-
                           // first-positional boundary as the parse itself
        for (size_t i = 0; i < argv_.size(); i++) {
            const std::string& a = argv_[i];
            if (!done && a == "--") { av.push_back(a); done = true; continue; }
            std::string k;
            if (!done && a.size() > 1 && (a[0] == '-' || a[0] == ':') && a != "--" &&
                a.find('=') == std::string::npos) {
                k = a[0] == ':' ? a.substr(1)                // :foo pairs too
                  : a[1] == '-' ? a.substr(2) : a.substr(1); // --foo / -f / -foo
                if (!k.empty() && k[0] == '/') k.clear();    // --/k negation: not pairable
            }
            if (!k.empty() && keys.count(k) && i + 1 < argv_.size()) {
                av.push_back("--" + k + "=" + argv_[i + 1]);
                i++;
                continue;
            }
            if (!done && !namedAnywhere && !(a.size() > 1 && (a[0] == '-' || a[0] == ':')))
                done = true; // the first positional token (named-anywhere: no boundary)
            av.push_back(a);
        }
        return rtMainArgs(av, namedAnywhere, this);
    };
    // Decide up front whether any MAIN candidate matches the argv. Checking
    // BEFORE the call means a nested X::Multi::NoMatch thrown from inside a
    // matched MAIN body propagates as a real error instead of being mistaken
    // for "no MAIN candidate" and silently printing Usage.
    bool mainMatches = true;
    // An option that binds an `@`-sigil named parameter is a LIST even when it
    // appeared once: `sub MAIN(:@x)` run with `--x=a` gets ("a",), the same
    // shape `--x=a --x=b` produces. A `@` param does not accept a bare Str (in
    // an ordinary call Rakudo type-checks it out), so the listing has to happen
    // here, where MAIN's signatures are in hand. Only names NO candidate spells
    // with another sigil: with both `(:@x)` and `(:$x)` declared, `--x=a` is the
    // scalar one.
    std::set<std::string> listNamed, scalarNamed;
    {
        std::vector<const Value*> cands;
        if (mainSub.code() && mainSub.code()->isMultiDispatcher)
            for (auto& c : mainSub.code()->candidates) cands.push_back(&c);
        else if (mainSub.code()) cands.push_back(&mainSub);
        for (const Value* c : cands) {
            if (!c->code() || !c->code()->params) continue;
            if (c->code()->isProto || c->code()->isProtoBody) continue;
            for (auto& p : *c->code()->params) {
                if (!p.named || p.slurpy) continue;
                std::set<std::string>& into = p.sigil == '@' ? listNamed : scalarNamed;
                if (!p.namedKey.empty()) into.insert(p.namedKey);
                if (p.namedKey.empty() || p.aliasBoth)
                    into.insert(p.name.size() > 1 ? p.name.substr(1) : p.name);
                for (auto& al : p.aliasKeys) into.insert(al);
            }
        }
        for (auto& k : scalarNamed) listNamed.erase(k);
    }
    auto listifyNamed = [&](ValueList& as) {
        if (listNamed.empty()) return;
        for (auto& a : as) {
            if (!isNamedArg(a) || !listNamed.count(a.s)) continue;
            Value v = a.pairVal() ? *a.pairVal() : Value::boolean(true);
            if (v.t == VT::Array || v.t == VT::Range) continue;
            Value lst = Value::array({v}); lst.isList = true;
            a.setPairVal(makePayload<Value>(std::move(lst)));
        }
    };
    if (mainSub.code() && mainSub.code()->isMultiDispatcher) {
        mainMatches = false;
        for (auto& cand : mainSub.code()->candidates) {
            // (an explicit `proto MAIN(|) {*}` is no candidate: its `|` takes
            // anything, and then the real dispatch found none — Cro's CLI
            // declares one, and `cro --help` died where it should print usage)
            if (cand.code() && (cand.code()->isProto || cand.code()->isProtoBody)) continue;
            ValueList mc = pairedArgs(cand);
            listifyNamed(mc);
            if (scoreCandidate(cand, mc) >= 0) { mainMatches = true; margs = std::move(mc); break; }
        }
    }
    else if (mainSub.code() && mainSub.code()->params) { // single MAIN: same bind check
        margs = pairedArgs(mainSub);
        listifyNamed(margs);
        mainMatches = scoreCandidate(mainSub, margs) >= 0;
    }
    else margs = rtMainArgs(argv_, namedAnywhere, this);
    if (mainMatches) return -1;
    // an explicit --help is a REQUEST for the usage text, not a
    // dispatch failure: Rakudo prints it to stdout and exits 0
    // (a bare -h is NOT special — it stays the failure path).
    // Only a --help that PARSES as a named option counts: after a
    // positional it is a literal argument and the dispatch failure
    // stays one (oracle: `prog pos --help` exits 2, usage on stderr).
    ValueList baseArgs = rtMainArgs(argv_, namedAnywhere, this);
    bool wantHelp = rtNamed(baseArgs, "help").truthy();
    // a user-defined USAGE takes over (it prints to stdout, like Rakudo)
    Value* usage = tctx_.cur ? tctx_.cur->find("&USAGE") : nullptr;
    if (!usage && global_) usage = global_->find("&USAGE");
    // …and GENERATE-USAGE(&MAIN, |args) builds the text the failure prints
    Value* genUsage = tctx_.cur ? tctx_.cur->find("&GENERATE-USAGE") : nullptr;
    if (!genUsage && global_) genUsage = global_->find("&GENERATE-USAGE");
    if (usage) {
        try { callCallable(*usage, {}); } catch (ExitEx& e) { return e.code; }
    }
    else if (genUsage && genUsage->t == VT::Code) {
        ValueList ga{mainSub};
        for (auto& a : baseArgs) ga.push_back(a);
        std::string txt;
        try { txt = callCallable(*genUsage, ga).toStr(); } catch (ExitEx& e) { return e.code; }
        (wantHelp ? std::cout : std::cerr) << txt << "\n";
    }
    else if (wantHelp) std::cout << mainUsage();
    else std::cerr << mainUsage();
    return wantHelp ? 0 : 2;
}

// --exe support: adopt the signature-only Program the compiled binary embeds
// (the MAIN candidates, bodies detached, written by the SAME serializer as the
// module cache) and define &MAIN — full Param metadata plus the compiled entry
// point — in the global env. From there mainProtocol()/mainUsage()/$*USAGE see
// exactly what the interpreter sees for the same source. A blob written by a
// different build (kAstSerialVersion mismatch) is ignored: runCompiledMain then
// degrades to the direct call, so the program still runs.
void Interpreter::registerCompiledMain(const unsigned char* blob, size_t len,
                                       Value (*fn)(ValueList&)) {
    Program prog;
    try { deserializeAst(std::string(reinterpret_cast<const char*>(blob), len), prog); }
    catch (AstSerialError&) { return; }
    auto prg = std::make_shared<Program>(std::move(prog));
    std::vector<SubDecl*> cands;
    for (auto& s : prg->stmts)
        if (s->kind == NK::SubDecl) {
            auto* d = static_cast<SubDecl*>(s.get());
            if (d->name == "MAIN") cands.push_back(d);
        }
    if (cands.empty() || !global_) return;
    mainSigProg_ = prg; // the Params below are borrowed from this tree
    auto mk = [&](SubDecl* d) {
        Value v = Value::closure(fn);
        v.code()->name = "MAIN";
        v.code()->params = &d->params;
        v.code()->pod = d->pod;
        return v;
    };
    Value main;
    if (cands.size() == 1 && !cands[0]->isMulti) main = mk(cands[0]);
    else {
        main = Value::closure(fn);
        main.code()->name = "MAIN";
        main.code()->isMultiDispatcher = true;
        for (auto* d : cands) main.code()->candidates.push_back(mk(d));
    }
    global_->define("&MAIN", main);
}

// The compiled binary's MAIN invoke: run the protocol against the registered
// metadata and call the compiled entry on a match. Returns the process exit
// code (0 when MAIN ran; its own `exit` still unwinds as ExitEx past this).
// Without registered metadata this is the legacy direct call — the program
// runs, it just cannot refuse a bad command line.
int Interpreter::runCompiledMain(Value (*fn)(ValueList&)) {
    Value* mainSub = global_ ? global_->find("&MAIN") : nullptr;
    if (!mainSub || mainSub->t != VT::Code || !mainSub->code() ||
        (!mainSub->code()->params && !mainSub->code()->isMultiDispatcher)) {
        refreshArgvFromLiveArgs();
        ValueList margs = rtMainArgs(argv_, mainNamedAnywhere(), this);
        sinkReturnedValue(fn(margs));
        return 0;
    }
    ValueList margs;
    int rc = mainProtocol(*mainSub, margs);
    if (rc >= 0) return rc;
    sinkReturnedValue(fn(margs));   // as in the interpreter: MAIN's value is sunk (#73)
    return 0;
}

// The compiled binary's invoke when the script declares no MAIN: `use M;` may
// have imported one (`multi sub MAIN(...) is export`), and the interpreter
// dispatches whatever &MAIN the mainline ends with (#112). That &MAIN is an
// ordinary interpreted closure with its full Params, so the protocol runs as is.
int Interpreter::runImportedMain() {
    Value* mainSub = tctx_.cur ? tctx_.cur->find("&MAIN") : nullptr;
    if (!mainSub && global_) mainSub = global_->find("&MAIN");
    if (!mainSub || mainSub->t != VT::Code || mainSub == inheritedMainBarrier_) return -1;
    ValueList margs;
    int rc = mainProtocol(*mainSub, margs);
    if (rc >= 0) return rc;
    sinkReturnedValue(callCallable(*mainSub, margs));
    return 0;
}
// Value-level indexing for native codegen (no AST). Read returns Nil when absent.
// The default value for a missing element of a (possibly typed) container:
// a typed container answers its element type object, else Nil.
Value typedElemDefault(const Value& base) {
    if (base.ofType().empty()) return Value::nil();
    std::string first = base.ofType().substr(0, base.ofType().find(','));
    if (first.empty()) return Value::nil();
    // a coercion type's element defaults to its target: `my Int() %h; %h<x>` is (Int)
    if (std::string target = coercionTarget(first); !target.empty()) first = target;
    if (ascii::isupper((unsigned char)first[0])) return Value::typeObj(first);
    // native element types are zero-initialized (my int @a — gaps read as 0)
    if (first == "num" || first == "num32" || first == "num64") return Value::number(0.0);
    if (first == "str") return Value::str("");
    if (first.compare(0, 3, "int") == 0 || first.compare(0, 4, "uint") == 0 ||
        first == "byte" || first == "atomicint")
        return Value::integer(0);
    return Value::nil();
}
Value arrayMissingDefaultPublic(const Value& base) { return arrayMissingDefault(base); }
// What a freshly GROWN slot of a container starts as. `is default(v)` wins over
// the element type: `my Int @a is default(0); @a[1] = 5` leaves @a[0] at 0, not
// at the (Int) type object — and `.raku` prints what a read would give
// (sheet LA-21). An untyped container grows empty holes.
Value containerFill(const Value& base) {
    // A NATIVE element type has no holes — its slots really are zero — so it
    // fills eagerly. Everything else grows an empty HOLE, and the read path
    // turns that into `is default(v)` or the element type object when it is
    // asked for. Filling eagerly there made a deleted trailing slot
    // indistinguishable from a stored value, so the array stopped shrinking.
    if (!base.ofType().empty() && ascii::islower((unsigned char)base.ofType()[0])) {
        Value d = typedElemDefault(base);
        // the slot is NATIVE: a store into it wraps (`@u8[0] = -1` holds 255)
        if (d.t == VT::Int) {
            bool sg = true; int bits = Value::natWidthOfType(base.ofType(), sg);
            if (bits > 0) { d.natBits = bits; d.natSigned = sg; }
        }
        if (d.t != VT::Nil) return d;
    }
    return Value::any();
}

Value rtIndexGet(const Value& base, const Value& key, bool isHash) {
    // A Whatever — or a WhateverCode such as `*-1` — is an END-RELATIVE
    // subscript however it arrived. Native codegen picks idxW when it can SEE
    // the star written in the subscript; when the star came through a variable
    // (`my $i = *-1; @a[$i]`) only the KEY knows, and this read element one.
    if (key.t == VT::Whatever || (key.t == VT::Code && key.code() && key.code()->isWhateverCode))
        if (g_cbInterp) return g_cbInterp->idxW(base, key, isHash);
    // An INFINITE range subscript (`@a[1..Inf]`, `@a[$s ..^ $e]` with $e = Inf)
    // is lazy and stops at the end of the list, as the interpreter's does;
    // flattened, it was thousands of Nils.
    if (key.t == VT::Range && !isHash && key.ofType().empty() && key.rTo() >= 9000000000000000000LL &&
        base.t == VT::Array && base.arr()) {
        Value out = Value::array(); out.isList = true;
        const long long n = (long long)base.arr()->size();
        for (long long i = key.rFrom() + (key.rExFrom() ? 1 : 0); i < n; i++)
            out.arr()->push_back(rtIndexGet(base, Value::integer(i), false));
        return out;
    }
    // a Range/list key is a slice: `@a[1..3]` / `@a[1,3]` / `%h<a b>`
    if (key.t == VT::Range || (key.t == VT::Array && key.arr())) {
        Value out = Value::array(); out.isList = true;
        const ValueList ks = key.flatten();
        for (auto& k : ks) out.arr()->push_back(rtIndexGet(base, k, isHash));
        // a slice of an Array's elements, every index one it has, writes
        // through to the array (ElemView), as the interpreter's slice does
        if (!isHash && base.t == VT::Array && base.arr() && !ks.empty() && out.arr()->size() == ks.size()) {
            bool all = true, run = true;
            long long prev = 0;
            for (size_t q = 0; q < ks.size(); q++) {
                if (ks[q].t != VT::Int || ks[q].big()) { all = false; break; }
                const long long k = ks[q].i;
                if (k < 0 || k >= (long long)base.arr()->size() || k > 0xFFFFFFFFLL) { all = false; break; }
                if (q && k != prev + 1) run = false;
                prev = k;
            }
            if (all) {
                ElemView o;
                if (run) { o.kind = ElemView::Contig; o.a = (size_t)ks[0].i; }
                else {
                    auto vi = std::make_shared<std::vector<uint32_t>>();
                    vi->reserve(ks.size());
                    for (auto& k : ks) vi->push_back((uint32_t)k.i);
                    o.kind = ElemView::Explicit; o.idx = std::move(vi);
                }
                attachElemView(out, base, std::move(o));
            }
        }
        return out;
    }
    // An object with its OWN AT-POS / AT-KEY answers its subscripts, as the
    // interpreter's does: a native module body reading `$o[1]` got Nil.
    if (base.t == VT::Object && base.obj() && base.obj()->cls && g_cbInterp &&
        base.obj()->cls->findMethod(isHash ? "AT-KEY" : "AT-POS"))
        return g_cbInterp->methodCall(base, isHash ? "AT-KEY" : "AT-POS", ValueList{key});
    if (isHash) {
        if ((base.t == VT::Hash || base.t == VT::Match) && base.hash()) { // Match: named captures
            // type-object keys follow the object-keyed rule (hashSubKey)
            auto it = base.hash()->find(hashSubKey(key, &base));
            if (it != base.hash()->end()) return it->second;
        }
        // a Pair is associative on its ONE key; any other key is Nil, as the
        // interpreter's lookup answers (native code read Nil for the key too)
        if (base.t == VT::Pair)
            return key.toStr() == base.s && base.pairVal() ? *base.pairVal() : Value::nil();
        // same rule as the interpreter path above: a defined Str/Int/… is a
        // type error rather than a silent Any
        if (base.t == VT::Str || base.t == VT::Int || base.t == VT::Num ||
            base.t == VT::Rat || base.t == VT::Bool || base.t == VT::Complex)
            // …as a FAILURE, which is what Rakudo hands back: it still detonates
            // the moment the value is used (which is what caught the broken
            // template) but can be tested for (Nil-Any sheet NA-37).
            return refusedSubscript("X::AdHoc",
                "Type " + base.typeName() + " does not support associative indexing.");
        return typedElemDefault(base);
    }
    if (base.t == VT::Range) {
        if (base.rTo() >= 9000000000000000000LL) { // infinite range: index directly, don't materialise
            long long i = key.toInt(); if (i < 0) return Value::nil();
            return Value::integer(base.rFrom() + (base.rExFrom() ? 1 : 0) + i);
        }
        ValueList f = base.flatten();
        long long i = key.toInt(); if (i < 0) return negIndexFailure(i);
        if (i >= 0 && i < (long long)f.size()) return f[i];
        return Value::nil();
    }
    if (base.t == VT::Array && base.arr() && base.ext()) { // lazy seq: force elements up to i
        long long i = key.toInt();
        if (i >= 0) {
            auto st = std::static_pointer_cast<LazySeqState>(base.ext());
            const size_t CAP = 1000000;
            if (st->appendNext)
                while ((long long)base.arr()->size() <= i && base.arr()->size() < CAP && st->appendNext(*base.arr())) {}
        }
    }
    // IO::Path::Parts is Positional as well as Associative: `$parts[0]` is the
    // volume PAIR, in declaration order rather than the map's sorted order.
    if (base.t == VT::Hash && base.hashKind == "IO::Path::Parts" && base.hash()) {
        static const char* kOrder[3] = {"volume", "dirname", "basename"};
        long long i = key.toInt();
        if (i < 0) i += 3;
        if (i >= 0 && i < 3) {
            auto it = base.hash()->find(kOrder[i]);
            return Value::pair(kOrder[i], it != base.hash()->end() ? it->second : Value::str(""));
        }
        return Value::nil();
    }
    if ((base.t == VT::Array || base.t == VT::Match) && base.arr()) { // Match: positional captures
        long long i = key.toInt(), n = (long long)base.arr()->size();
        if (i < 0) return negIndexFailure(i);
        if (i < n) {
            const Value& v = (*base.arr())[i];
            // a deleted slot (undefined hole) in a typed/defaulted array reads as the default
            if (base.t == VT::Array && (v.t == VT::Nil || v.t == VT::Any) &&
                (base.elemDefault() || !base.ofType().empty()))
                return arrayMissingDefault(base);
            return v;
        }
    }
    // A Blob/Buf and a NativeCall CArray index their ELEMENTS, as the
    // interpreter's subscript does. Native module bodies fell through to the
    // missing-element default here, so NativeHelpers::Array's copy-to-array —
    // `$carray[$_] for ^$items` — read back all Nils under --exe and every
    // Math::SparseMatrix::Native matrix printed as zeros.
    if (base.t == VT::Str && (base.hashKind == "Blob" || base.hashKind == "Buf")) {
        long long i = key.toInt();
        if (i < 0) return negIndexFailure(i);
        if (i < base.blobElems()) return base.blobElemAt(i);
    }
    if (base.t == VT::Str && base.hashKind == "CArray") {
        long long i = key.toInt();
        std::string et = base.enumName.empty() ? std::string("int64") : base.enumName.str();
        int w = Interpreter::ncElemSize(et);
        if (i < 0 || (i + 1) * w > (long long)base.s.size()) return Value::any();
        Value el = Interpreter::ncReadElem((long long)(intptr_t)base.s.data(), et, i);
        if (g_cbInterp) el = g_cbInterp->ncClassElem(std::move(el), &base, et, i);
        if (Interpreter::ncIsPointerElem(et) && el.t == VT::Int && g_cbInterp)
            return g_cbInterp->ncMakeLiveCArray(et, (void*)(intptr_t)el.toInt());
        return el;
    }
    if (base.t == VT::Hash && (base.hashKind == "CArray" || base.hashKind == "Pointer") &&
        base.hash() && base.hash()->count("addr")) {
        std::string of = base.hash()->count("of") ? base.hash()->at("of").toStr() : "int64";
        Value el = Interpreter::ncReadElem(base.hash()->at("addr").toInt(), of, key.toInt());
        if (base.hashKind == "CArray" && g_cbInterp) el = g_cbInterp->ncClassElem(std::move(el), nullptr, of, key.toInt());
        if (Interpreter::ncIsPointerElem(of) && el.t == VT::Int && g_cbInterp)
            return g_cbInterp->ncMakeLiveCArray(of, (void*)(intptr_t)el.toInt());
        return el;
    }
    return arrayMissingDefault(base);
}

// Attribute access on `self` for native codegen ($!x / $.x inside a method).
Value rtAttrGet(const Value& self, const std::string& name) {
    if (self.t == VT::Object && self.obj()) {
        auto it = self.obj()->attrs.find(name);
        if (it != self.obj()->attrs.end()) return it->second;
    }
    return Value::any();
}
Value& rtAttrRef(Value& self, const std::string& name) {
    if (self.t != VT::Object || !self.obj()) { // shouldn't happen; keep it safe
        self = Value::object(makePayload<ObjectData>());
    }
    return self.obj()->attrs[name];
}

// Nominal type check for native multi-dispatch.
bool rtTypeMatch(const Value& v, const std::string& type) {
    if (type.empty() || type == "Any" || type == "Mu" || type == "Cool") return true;
    // an enum's TYPE OBJECT: its own type, Enumeration, and its value type's
    // ancestry (`Color ~~ Int`, `my Int $v = Color`) — as typeMatchesResolved
    if (v.t == VT::Array && !v.enumType.empty() && v.enumName.empty() && g_revInterp &&
        isEnumTypeObject(v)) {
        if (type == v.enumType || type == "Enumeration") return true;
        for (auto& a : typeAncestry(g_revInterp->enumBaseType(v.enumType.str()))) if (a == type) return true;
        return false;
    }
    // an enum VALUE matches the Enumeration role, its own enum type, and its name
    if (!v.enumName.empty() &&
        (type == "Enumeration" || type == v.enumType || type == v.enumName)) return true;
    // …and a Str-valued member is a Str (the enum type derives from Str)
    if (!v.enumName.empty() && v.pairVal() && v.pairVal()->t == VT::Str &&
        (type == "Str" || type == "Stringy")) return true;
    // a TAGGED built-in (Failure, IO::Path, FileHandle, Blob, …) matches its own
    // reported type — nqp::istype($result, Failure) is how JSON::Fast rejects a
    // malformed number, and the tag was never consulted here
    if (!v.hashKind.empty() && (type == v.hashKind || type == v.typeName())) return true;
    // a flavored path (IO::Path::Win32 …) is an IO::Path
    if (v.hashKind == "IO" && v.t == VT::Str && (type == "IO::Path" || type == "IO")) return true;
    // …and a Failure IS a Nil (that is why `$f // $default` works on one), so it
    // matches Nil and Nil's own ancestors (Nil-Any sheet NA-10).
    if (v.hashKind == "Failure" && (type == "Nil" || type == "Cool")) return true;
    // …and the QuantHash roles: a Set is Setty, a Bag Baggy, a Mix Mixy (CBOR::Simple
    // encodes a Set with the set tag by exactly this test)
    if (v.t == VT::Hash && !v.hashKind.empty()) {
        const std::string& hk = v.hashKind;
        bool setty = hk == "Set" || hk == "SetHash", baggy = hk == "Bag" || hk == "BagHash",
             mixy = hk == "Mix" || hk == "MixHash";
        if ((type == "Setty" && setty) || (type == "Baggy" && baggy) || (type == "Mixy" && mixy) ||
            (type == "QuantHash" && (setty || baggy || mixy))) return true;
    }
    // …and the ROLE it does, not only its own name. Date and DateTime do
    // Dateish; typeNameConforms says so and `~~` reads it, but this is a THIRD
    // path — nqp::istype and native multi-dispatch — and it matched the tag
    // alone, so `nqp::istype($dt, Dateish)` was False while `$dt ~~ Dateish`
    // was True. Nothing showed it while a hash-backed DateTime still counted as
    // Associative: JSON::Fast tests Associative BEFORE Dateish, so DateTimes
    // went down the wrong branch and came out looking right. Fixing the
    // Associative answer took that cover away and JSON::Fast's own
    // t/07-datetime.t went red with "Don't know how to jsonify DateTime".
    if (type == "Dateish" && (v.hashKind == "DateTime" || v.hashKind == "Date"))
        return true;
    // a stamped Proxy-subclass instance (AttrProxy) answers its class name too
    if (v.t == VT::Hash && v.hashKind == "Proxy" && v.hash()) {
        auto ki = v.hash()->find("\x01cls");
        if (ki != v.hash()->end() && type == ki->second.s) return true;
    }
    // an allomorph (IntStr/RatStr/NumStr/ComplexStr) IS both of its types:
    // `my Str $s = <42>` and `my Int $i = <42>` both hold
    if (v.isAllomorph() && (type == "Str" || type == "Stringy" || type == v.hashKind))
        return true;
    // an Instant/Duration rides on an Int, Rat or Num but is not one: Rakudo's
    // `Instant ~~ Num` is False (Real and Numeric hold) — CBOR::Simple tests Num
    // before Instant and tagged every Instant as a float
    if ((v.t == VT::Int || v.t == VT::Rat || v.t == VT::Num) &&
        (v.hashKind == "Instant" || v.hashKind == "Duration"))
        return type == "Numeric" || type == "Real" || type == v.hashKind;
    switch (v.t) {
        // UInt is `subset UInt of Int where * >= 0` — an Int matches it when it
        // is not negative. (typeNameConforms already knew; this third path did
        // not, so a `UInt` container refused every value once it started
        // checking.)
        case VT::Int:     if (type == "UInt") return !(v.big() ? v.big()->sign < 0 : v.i < 0);
                          return type == "Int" || type == "Numeric" || type == "Real";
        case VT::Num:
            // an Instant/Duration rides on a Num but is not one: Rakudo's
            // `Instant ~~ Num` is False (Real and Numeric hold) — CBOR::Simple
            // tests Num before Instant and tagged every Instant as a float
            if (v.hashKind == "Instant" || v.hashKind == "Duration")
                return type == "Numeric" || type == "Real";
            return type == "Num" || type == "Numeric" || type == "Real";
        case VT::Rat:     return type == "Rat" || type == "Rational" || type == "Numeric" || type == "Real" ||
                                 (type == "FatRat" && v.fatRat());
        case VT::Complex: return type == "Complex" || type == "Numeric";
        case VT::Str:
            // tagged non-strings that ride on VT::Str (IO::Path, Version,
            // IO::Special) are not Str/Stringy; their own names matched above
            if (v.hashKind == "IO" || v.hashKind == "Version" || v.hashKind == "IO::Special")
                return false;
            // A byte buffer rides on VT::Str too, and is NOT a Str: it is a
            // Blob (Stringy and Positional through the role), a Buf only when
            // writable — `utf8` is a Blob, not a Buf. CBOR::Simple asks
            // `istype($_, Str)` before `istype($_, Blob)` and every Buf went
            // out as a text string (major type 3, not 2).
            if (v.hashKind == "Buf" || v.hashKind == "Blob" || v.hashKind == "utf8" ||
                v.hashKind == "CArray") {
                if (type == "Str") return false;
                if (type == "Blob" || type == "Stringy" || type == "Positional") return v.hashKind != "CArray";
                if (type == "Buf") return v.hashKind == "Buf";
                return false;
            }
            return type == "Str" || type == "Stringy";
        // Bool is an Int-backed enum, so `True ~~ Int` / nqp::istype(True, Int)
        // hold (CBOR::Simple classifies Bool via nqp::istype($_, Numeric)).
        case VT::Bool:    return type == "Bool" || type == "Int" ||
                                 type == "Numeric" || type == "Real";
        // a Pair is a Pair (and Associative): Hash::int's STORE sorts each
        // pulled item with `nqp::istype($x, Pair)` before reading its key
        case VT::Pair:    return type == "Pair" || type == "Associative";
        // a real Nil (bound into an array) is a Nil — CBOR::Simple tags it as
        // "absent" rather than null
        case VT::Nil:     return type == "Nil";
        case VT::Array:
            // a Seq is an Array tagged `.s == "Seq"`; a plain Array/List is NOT a
            // Seq (JSON::Fast's jsonify tests `istype($_, Seq)` before Positional
            // — a false match sends it into an infinite `jsonify(.cache)` loop)
            if (type == "Seq") return v.s == "Seq";
            // a NATIVE array (`array[uint8]`, `my num32 @a`) is an `array`, which
            // CBOR::Simple tests before Positional to emit a typed-array tag
            if (type == "array") return !v.isList && isNativeScalarName(v.ofType());
            // …and a Slip is the Slip it says it is: `--> Slip` on highlighter's
            // `matches` failed its own return ("expected Slip but got Slip")
            if (type == "Slip") return v.s == "Slip";
            return type == "Array" || type == "List" || type == "Positional" || type == "Iterable";
        case VT::Hash:    return hashKindIsAssociative(v.hashKind) &&
                                 (type == "Hash" || type == "Associative" || type == "Map");
        case VT::Code:    return type == "Code" || type == "Callable" || type == "Routine" || type == "Block";
        // a Regex is a Method: `/a/ ~~ Callable` (and Code, Block, Routine, Method)
        case VT::Regex:   return type == "Regex" || type == "Method" || type == "Routine" ||
                                 type == "Block" || type == "Code" || type == "Callable";
        case VT::Object: {
            for (ClassInfo* c = v.obj() && v.obj()->cls ? v.obj()->cls.get() : nullptr; c; c = c->parent.get()) {
                if (c->name == type) return true;
                if (c->doesRole(type)) return true; // `does Positional` answers istype too
            }
            // package-relative short name: `has Path $.path` accepts URI::Path
            const std::string& q = aliasType(type);
            if (&q != &type)
                for (ClassInfo* c = v.obj() && v.obj()->cls ? v.obj()->cls.get() : nullptr; c; c = c->parent.get())
                    if (c->name == q) return true;
            return false;
        }
        // A TYPE OBJECT conforms to its own type and its ancestors, which is the
        // same question `~~` asks — and typeMatchesArg already answers it by name
        // (coercion types and parameterisations included). Falling through to the
        // default said `nqp::istype(Str, Str)` was FALSE, so every nqp-level test
        // against a type object failed: CBOR::Simple asks
        // `nqp::istype(%map.keyof, Str)` to tell a string-keyed map from an
        // object-keyed one, and answered "object-keyed" for both.
        case VT::Type: return typeMatchesArg(v, type);
        // `*` is the Whatever it says it is: String::Utils' `stem(str $basename,
        // $parts = *)` asks `nqp::istype($parts, Whatever)`, and a 0 here sent
        // `stem("foo.tar.gz")` down the counted branch, answering ""
        case VT::Whatever: return type == "Whatever";
        default: return false;
    }
}

// ---------------------------------------------------------------------------
// Backtraces (issue #67). The chain is captured WHERE THE THROW HAPPENS,
// in RakuError's constructor, because C++ unwinding runs CFGuard's destructor
// for every frame on the way out: a `catch` sees an empty callFrames. The cost
// is one refcount increment per live frame per throw, and zero on the path
// that does not throw — the alternative (recording frames as they unwind) puts
// a thread-local read on every routine return, which the perf gate would see.
// The live chain, innermost first. Shared by every position we record: a
// throw, a `warn`, the making of a Failure, and a worker breaking a Promise.
thread_local int g_btSettingFrames = 0;

static std::shared_ptr<BtRecord> btCaptureNow() {
    auto& fr = Interpreter::tctx_.callFrames;
    auto out = std::make_shared<BtRecord>();
    out->frames.reserve(fr.size() + 1);
    // `die` and `.throw` are frames of the setting's in Rakudo's list, above
    // the code that called them (kind 2; the printed form leaves them out)
    if (int k = g_btSettingFrames) {
        g_btSettingFrames = 0;
        out->frames.push_back(BtFrame{nullptr, 0, 2});                 // throw
        if (k > 1) out->frames.push_back(BtFrame{nullptr, 0, 3});      // die
    }
    int line = currentStmtLine();
    Env* scope = Interpreter::tctx_.cur.get();
    for (size_t idx = fr.size(); ; idx--) {
        const Value* cv = idx > 0 ? fr[idx - 1].code : nullptr;
        // the bare blocks this activation is running inside, innermost first:
        // each is a Block frame of its own (kind 1), the innermost at the
        // frame's current line and each one out at the line where the block
        // inside it starts. The walk stops at the activation's own scope.
        int bl = line;
        for (Env* e = scope; e && !e->callEnv && !e->routineFrame && !e->unitFrame; e = e->parent.get())
            if (e->btBlock) { out->frames.push_back(BtFrame{nullptr, bl, 1}); bl = e->btLine; }
        out->frames.push_back(BtFrame{cv ? cv->codeS() : nullptr, line});
        if (idx == 0) break;
        line = fr[idx - 1].line;   // the CALL SITE line inside the next frame out
        scope = fr[idx - 1].callerEnv;
    }
    // left EMPTY when it is simply the program being run, which is the common
    // case: the renderer falls back to it, and a Failure is made often enough
    // that copying the path every time showed up
    if (g_cbInterp && g_cbInterp->curDeclFile() != g_cbInterp->srcFileAbs_)
        out->originFile = g_cbInterp->curDeclFile();
    return out;
}

RakuError::RakuError(Value p, std::string m)
    : payload(std::move(p)), message(std::move(m)) {
    bt = btCaptureNow();
}

std::shared_ptr<BtRecord> btCaptureHere() { return btCaptureNow(); }

// A Failure remembers where it was MADE — see the declaration in Interpreter.h.
// Failures are made in BULK by ordinary code (every failed coercion is one), so
// this pays for itself in allocations, not in the walk: the chain is two or
// three frames deep and the string was the expensive part. The origin file is
// stored only when it is NOT the program being run, which is the rare case (a
// module's top level); the renderer falls back to the program otherwise.
Value rakuppNewFailure() {
    Value f = Value::makeHash(); f.hashKind = "Failure";
    // in the Value's OWN spare handle, not a map entry: nothing else puts a
    // Failure's ext to use, and the entry was measurable
    f.extM() = btCaptureNow();
    return f;
}

// One source line of a file, for the excerpt under the origin frame. Read once
// per file and cached; a file we cannot read (an --exe binary, a moved module,
// an EVAL string) simply gets no excerpt.
std::string Interpreter::srcLineOf(const std::string& file, int line) {
    if (file.empty() || line <= 0) return "";
    auto it = srcLineCache_.find(file);
    if (it == srcLineCache_.end()) {
        std::vector<std::string> lines;
        std::ifstream in(file);
        if (in) { std::string l; while (std::getline(in, l)) lines.push_back(l); }
        it = srcLineCache_.emplace(file, std::move(lines)).first;
    }
    if ((size_t)line > it->second.size()) return "";
    std::string l = it->second[line - 1];
    // trim trailing whitespace and a stray CR from a CRLF source
    while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.pop_back();
    return l;
}
// A path as the reader should see it: the program as it was invoked, a module
// under the working directory relative to it, anything else absolute. (The
// `.file` the API answers stays absolute — Log::Async and backtrace-new.raku
// match on it by suffix.)
std::string btDisplayPath(const std::string& file, const std::string& srcAbs,
                                 const std::string& srcAsGiven) {
    if (file.empty()) return "<unknown>";
    if (!srcAbs.empty() && file == srcAbs && !srcAsGiven.empty()) return srcAsGiven;
    static std::string cwd = [] {
        char buf[4096];
        return getcwd(buf, sizeof buf) ? std::string(buf) + "/" : std::string();
    }();
    if (!cwd.empty() && file.rfind(cwd, 0) == 0 && file.size() > cwd.size())
        return file.substr(cwd.size());
    return file;
}

// What to call a frame's routine, Rakudo's spelling: `sub f`, `method m`,
// `submethod BUILD`, `regex r`, `block <unit>` for the mainline, and a bare
// `block ` (two spaces before `at`) for an anonymous one.
static std::string btFrameName(const Callable* c, bool outermost) {
    if (!c) return outermost ? "block <unit>" : "block ";
    std::string kind = c->isSubmethod    ? "submethod"
                     : c->isMethod       ? "method"
                     : c->isRegexRoutine ? "regex"
                     : c->isBlock        ? "block"
                                         : "sub";
    // A method's DECLARING package goes in the name: Rakudo prints a bare
    // `in method new`, which in a program with six classes says nothing about
    // which `new` ran. `Foo::new` costs four characters and answers it, and it
    // stays inside the name part that consumers of `in X at FILE line N` skip.
    std::string nm = c->name;
    if (!c->pkg.empty() && c->pkg != "GLOBAL" && !nm.empty() &&
        nm.find("::") == std::string::npos)
        nm = c->pkg + "::" + nm;
    return kind + " " + nm;
}

// A frame the short form hides: the callables the RUNTIME makes up — Whatever
// currying, `.assuming` wrappers, `wrap` shims. They are Rakudo's `is-hidden`
// frames: real activations, but not places the reader wrote code.
static bool btFrameHidden(const Callable* c) {
    return c && (c->isWhateverCode || (c->name.empty() && c->isBlock && c->langRev == -1));
}

Interpreter::BtStyle Interpreter::btStyleForStderr() {
    BtStyle st;
    std::string mode = envStr("RAKUPP_BACKTRACE");
    if (mode.empty() && llException_) mode = "full";
    st.excerpt = st.typeLine = true;
    if (mode == "full") { st.full = true; st.collapse = false; st.excerpt = st.typeLine = true; }
    else if (mode == "0" || mode == "none" || mode == "off") st.cap = 0; // message only
    // colour only for a terminal, and never against NO_COLOR. On Windows a
    // console is a tty but does not necessarily ACT on escapes — consoleAnsi()
    // reports what setupConsole() managed to turn on there, so a legacy console
    // gets a plain backtrace rather than one wrapped in visible `ESC[31m`.
    std::string force = envStr("RAKUPP_COLOR");
    bool tty = isatty(2) != 0 && consoleAnsi(2);
    st.colour = force == "1" ? true
              : force == "0" ? false
              : (tty && envStr("NO_COLOR").empty() && getenv("NO_COLOR") == nullptr);
    return st;
}

std::string Interpreter::renderFrames(const BtRecord& rec, const BtStyle& st) {
    const std::vector<BtFrame>& fr = rec.frames;
    const std::string& originFile = rec.originFile;
    if (st.cap == 0 && !st.full) return "";
    const char* DIM = st.colour ? "\033[2m" : "";
    const char* BLD = st.colour ? "\033[1m" : "";
    const char* OFF = st.colour ? "\033[0m" : "";
    // one pass to the printable form, then collapse runs of identical lines
    struct Line { std::string what, where, file; int line; };
    std::vector<Line> out;
    // A chain captured on a WORKER thread ends in the thread's own empty base:
    // no routine, and no line, because nothing on that thread has run a
    // statement outside the block. It names nowhere, so it is not a frame.
    size_t n = fr.size();
    while (n > 1 && !fr[n - 1].code && fr[n - 1].line == 0) n--;
    for (size_t i = 0; i < n; i++) {
        if (fr[i].kind) continue;   // bare-block and setting frames are the list's, not the printout's
        const Callable* c = fr[i].code.get();
        if (!st.full && btFrameHidden(c)) continue;
        std::string file = c && !c->declFile.empty() ? c->declFile
                         : (!originFile.empty() ? originFile : srcFileAbs_);
        out.push_back({btFrameName(c, i + 1 == n),
                       btDisplayPath(file, srcFileAbs_, srcFile_), file, fr[i].line});
    }
    auto lineOf = [&](const Line& l) {
        return std::string("  in ") + BLD + l.what + OFF + " at " + DIM + l.where + OFF +
               " line " + std::to_string(l.line);
    };
    auto same = [](const Line& a, const Line& b) {
        return a.what == b.what && a.file == b.file && a.line == b.line;
    };
    std::string s;
    for (size_t i = 0; i < out.size(); ) {
        size_t j = i;
        while (j + 1 < out.size() && same(out[j + 1], out[i])) j++;
        size_t run = j - i + 1;
        // a recursion that died 200 frames deep is 200 identical lines; show a
        // few and say how many more, rather than filling the terminal
        size_t show = (st.collapse && run > 3) ? 3 : run;
        for (size_t k = 0; k < show; k++) {
            s += lineOf(out[i]) + "\n";
            // the excerpt belongs to the ORIGIN frame only, and directly under
            // it: a line of context per frame is four times the height for no
            // more orientation
            if (i == 0 && k == 0 && st.excerpt) {
                std::string src = srcLineOf(out[i].file, out[i].line);
                if (!src.empty())
                    s += std::string("      ") + DIM + std::to_string(out[i].line) + " | " + OFF +
                         src + "\n";
            }
        }
        if (show < run)
            s += std::string("  ") + DIM + "... " + std::to_string(run - show) +
                 " more frames of " + out[i].what + OFF + "\n";
        i = j + 1;
    }
    // …and a cap on the whole thing, with the knob that lifts it
    if (!st.full && st.cap > 0) {
        size_t nl = 0, pos = 0, keep = 0;
        for (; pos < s.size(); pos++) if (s[pos] == '\n' && ++nl == (size_t)st.cap) { keep = pos + 1; break; }
        if (keep && keep < s.size()) {
            size_t rest = (size_t)std::count(s.begin() + keep, s.end(), '\n');
            s = s.substr(0, keep) + "  " + DIM + "… " + std::to_string(rest) +
                " more frames (RAKUPP_BACKTRACE=full for all)" + OFF + "\n";
        }
    }
    return s;
}

// The whole diagnostic: message, type, frames, and the second position when
// the error has one. ONE function, so the uncaught printer, `warn`, the REPL
// and an embedding host never drift apart on what an error looks like.
std::string Interpreter::renderError(const RakuError& e, const BtStyle& st) {
    const char* DIM = st.colour ? "\033[2m" : "";
    const char* BLD = st.colour ? "\033[1m" : "";
    const char* OFF = st.colour ? "\033[0m" : "";
    std::string out = std::string(BLD) + e.message + OFF + "\n";
    std::string frames = e.bt ? renderFrames(*e.bt, st) : std::string();
    if (st.typeLine && !frames.empty() && e.payload.t == VT::Object &&
        e.payload.obj() && e.payload.obj()->cls) {
        // the type is what a reader needs in order to write a CATCH; the ad-hoc
        // one carries nothing the message does not already say
        const std::string& tn = e.payload.obj()->cls->name;
        if (tn != "X::AdHoc" && !tn.empty())
            out += std::string("  ") + DIM + "(" + tn + ")" + OFF + "\n";
    }
    out += frames;
    if (e.altBt && !e.altLabel.empty()) {
        // the second section is a POSITION, not a second incident: one excerpt
        // per error, under the frame the error actually came from
        BtStyle alts = st; alts.excerpt = false;
        std::string alt = renderFrames(*e.altBt, alts);
        if (!alt.empty()) out += "\n" + e.altLabel + "\n" + alt;
    }
    return out;
}

// Where a `warn` was warned from. One frame — the innermost — because a
// warning is a pointer to a line, not an incident report; RAKUPP_BACKTRACE=full
// asks for the whole chain, and =0 turns it off with everything else.
std::string Interpreter::warnFrame() {
    BtStyle st = btStyleForStderr();
    st.excerpt = st.typeLine = st.colour = false;   // it goes through $*ERR, which may be a file
    if (st.cap == 0 && !st.full) return "";
    auto rec = btCaptureNow();
    if (rec) rec->frames.erase(std::remove_if(rec->frames.begin(), rec->frames.end(),
                                              [](const BtFrame& f) { return f.kind != 0; }),
                               rec->frames.end());
    if (!rec || rec->frames.empty()) return "";
    if (!st.full && rec->frames.size() > 1) rec->frames.resize(1);
    return renderFrames(*rec, st);
}

// The same renderer over a MATERIALIZED Backtrace (a list of BacktraceFrame
// hashes) — what `$!.backtrace.Str` and `Backtrace.new.Str` print.
std::string Interpreter::renderBacktraceValue(const Value& bt, const BtStyle& st) {
    BtRecord rec;
    if (bt.t == VT::Array && bt.arr())
        for (auto& f : *bt.arr()) {
            if (f.t != VT::Hash || !f.hash()) continue;
            auto& h = *f.hash();
            auto ci = h.find("code"), li = h.find("line"), fi = h.find("file"), ki = h.find("bt-kind");
            const unsigned char kind = ki != h.end() ? (unsigned char)ki->second.toInt() : 0;
            // the mainline's stand-in `<unit>` Block is no routine of the program's
            rec.frames.push_back(BtFrame{ci != h.end() && !h.count("unit") ? ci->second.codeS() : nullptr,
                                         li != h.end() ? (int)li->second.toInt() : 0, kind});
            // the frames with no declaring routine fall back to this; the
            // OUTERMOST frame is the mainline, so its file is the right one
            // (the first frame's may be a module's)
            if (fi != h.end() && !kind && !rec.frames.back().code) rec.originFile = fi->second.toStr();
        }
    return renderFrames(rec, st);
}

// The code objects that stand in for the frames Raku++ has no Callable for:
// 0 the mainline's `<unit>` Block, 1 the setting's `throw` method, 2 its `die`
// sub, 3 a bare block. Made once and shared; a frame only reads them.
static Value btStandInCode(int which) {
    static const std::vector<Value> codes = [] {
        auto mk = [](const char* name, bool block, bool method) {
            Value cv; cv.t = VT::Code;
            auto c = makePayload<Callable>();
            c->name = name; c->isBlock = block; c->isMethod = method;
            cv.setCode(c);
            return cv;
        };
        return std::vector<Value>{mk("<unit>", true, false), mk("throw", false, true),
                                  mk("die", false, false), mk("", true, false)};
    }();
    return codes[(size_t)which];
}

// The frames a caught exception carries. exceptionFor stores them as an opaque
// handle (one pointer store per catch); the Backtrace list of BacktraceFrame
// hashes is built the first time a program actually asks, and cached back.
Value Interpreter::backtraceOf(const Value& exObj) {
    if (exObj.t != VT::Object || !exObj.obj()) return captureBacktrace();
    auto& at = exObj.obj()->attrs;
    auto it = at.find("__bt");
    if (it == at.end()) return captureBacktrace();
    if (it->second.t == VT::Array) return it->second;   // already materialized
    auto raw = btOf(it->second);
    if (!raw) return captureBacktrace();
    const std::string& origin = raw->originFile;
    Value bt = Value::array(); bt.isList = true; bt.s = "Backtrace";
    const auto& frs = raw->frames;
    auto fileOf = [&](const Callable* c) {
        return c && !c->declFile.empty() ? c->declFile : (!origin.empty() ? origin : srcFileAbs_);
    };
    for (size_t i = 0; i < frs.size(); i++) {
        const BtFrame& f = frs[i];
        Value h = Value::makeHash(); h.hashKind = "BacktraceFrame";
        if (f.kind >= 2) {   // the setting's throw / die (see btCaptureNow)
            (*h.hash())["file"] = Value::str(f.kind == 2 ? "SETTING::src/core.c/Exception.rakumod"
                                                         : "SETTING::src/core.c/control.rakumod");
            // the lines Rakudo 2026.08's setting reports for them: there is no
            // setting source here to point into, and a frame's line is > 0
            (*h.hash())["line"] = Value::integer(f.kind == 2 ? 65 : 253);
            (*h.hash())["code"] = btStandInCode(f.kind == 2 ? 1 : 2);
            (*h.hash())["setting"] = Value::boolean(true);
        }
        else {
            // a bare block is in the file of the activation it runs inside:
            // the next activation frame out
            const Callable* owner = f.code.get();
            if (f.kind == 1)
                for (size_t j = i + 1; j < frs.size(); j++)
                    if (!frs[j].kind) { owner = frs[j].code.get(); break; }
            (*h.hash())["file"] = Value::str(fileOf(owner));
            (*h.hash())["line"] = Value::integer(f.line);
            if (f.kind == 1) (*h.hash())["code"] = btStandInCode(3);
            else if (f.code) { Value cv; cv.t = VT::Code; cv.setCode(f.code); (*h.hash())["code"] = cv; }
            else {   // the mainline: Rakudo's frame holds its `<unit>` Block
                (*h.hash())["code"] = btStandInCode(0);
                (*h.hash())["unit"] = Value::boolean(true);
            }
        }
        if (f.kind) (*h.hash())["bt-kind"] = Value::integer(f.kind);
        bt.arr()->push_back(std::move(h));
    }
    at["__bt"] = bt;   // cached: a program that walks it twice pays once
    return bt;
}

// The call chain as a list of BacktraceFrame values, innermost first — what
// Exception.throw records and .backtrace answers. Each frame carries the file
// its routine was DECLARED in (declFile), so Log::Async's Context can walk
// past its own module's frames by path, and the line executing in that
// activation (the innermost's current line; an outer one's call-site line).
Value Interpreter::captureBacktrace() {
    Value bt = Value::array(); bt.isList = true; bt.s = "Backtrace";
    auto& fr = tctx_.callFrames;
    long long line = curLine_;
    for (size_t idx = fr.size(); ; idx--) {
        const Value* code = idx > 0 ? fr[idx - 1].code : nullptr;
        Value f = Value::makeHash(); f.hashKind = "BacktraceFrame";
        std::string file;
        if (code && code->code() && !code->code()->declFile.empty()) file = code->code()->declFile;
        // no declaring routine (the mainline, or a bare block): the file whose
        // top level is running — the program, or the module / EVALFILE'd file
        // that switched curDeclFile_ underneath it
        else file = curDeclFile();
        (*f.hash())["file"] = Value::str(file);
        (*f.hash())["line"] = Value::integer(line);
        if (code) (*f.hash())["code"] = *code;
        bt.arr()->push_back(std::move(f));
        if (idx == 0) break;
        line = fr[idx - 1].line;
    }
    return bt;
}

// Reduction metaop for native codegen: fold `op` over a flattened list.
// `@a <<+=>> 2019` / `($t, $y) »+=« (a, b)` — hyper compound assignment, ONE
// implementation for all three operator ladders. The base op applies
// elementwise and MUTATES the left side: an @array's Value shares storage with
// the caller's container, so writing through l.arr() reaches it. A
// PARENTHESISED list of scalars is a fresh List of copies, so mutating l.arr
// reaches nobody — the assignment silently did nothing and runge-kutta.raku
// sat at t=0 forever; when the caller has the AST (lhsExpr), each scalar is
// written through its own container instead. Before this helper the fix lived
// only in evalBinary's copy of three.
// Z / X and their Z<op> / X<op> metaop forms — the ONE implementation behind
// all three operator ladders (applyArith reaches it through g_cbInterp).
// One-level element model: sublists stay whole ((1,0) X (a,b),(c,d) is 2x2,
// not 2x4 — the applyArith copies used a DEEP flatten and answered 8 pairs);
// a Blob/Buf spreads to its elements (`$H Z+ $M` in Digest::SHA1).
Value Interpreter::zxOp(const std::string& op, Value l, Value r) {
    std::string sub = op.substr(1);
    auto oneLevel = [](const Value& v) -> ValueList {
        // an ITEMIZED array is ONE element: `$[1,2] X~ "a"` is ("1 2a"), not
        // two crossings (Z/X are not nodal — `.item` is what stops the spread)
        if (v.t == VT::Array && v.arr() && v.itemized) return ValueList{v};
        if (v.t == VT::Array && v.arr()) return *v.arr();
        if (v.t == VT::Range) return v.flatten();
        if (v.t == VT::Str && !v.itemized && (v.hashKind == "Blob" || v.hashKind == "Buf"))
            return v.blobList();
        return ValueList{v};
    };
    // A LAZY operand (`7 xx *`, an infinite sequence) carries only its
    // materialized PREFIX in `arr` — usually one element — so a zip against one
    // stopped after a single pair. Z stops at the shortest side, so force the
    // lazy side out to the other's length. `@$key Z[+^] $i xx *` is how Digest's
    // HMAC builds its key pad, and it was yielding one byte.
    // Cross (X) against an infinite side has no finite answer; leave it alone.
    auto isLazy = [](const Value& v) { return v.t == VT::Array && v.ext(); };
    // An ENDLESS side — an infinite Range (`2..*`) or an infinite lazy list.
    // Zipping two of them has no finite answer to compute up front, and
    // Rakudo's answer is itself lazy: `2..* Z* 2..*` is the perfect squares.
    // Produce a lazy list that pulls one element from each side per step.
    auto endlessInt = [&](const Value& v) {
        if (v.t == VT::Range && !v.rNum() && v.rTo() >= 9000000000000000000LL) return true;
        if (v.t == VT::Array && v.ext()) {
            auto st = std::static_pointer_cast<LazySeqState>(v.ext());
            return st && st->infinite;
        }
        return false;
    };
    if (op[0] == 'Z' && endlessInt(l) && endlessInt(r)) {
        // the i-th element of a side, materialising a lazy one as it goes
        auto nth = [this](Value src, size_t i) -> Value {
            if (src.t == VT::Range) return Value::integer(src.rFrom() + (long long)i);
            materializeLazy(src, i + 1);
            return src.arr() && i < src.arr()->size() ? (*src.arr())[i] : Value::any();
        };
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        auto st = std::make_shared<LazySeqState>(); st->infinite = true;
        auto idx = std::make_shared<size_t>(0);
        Value lv = l, rv = r; std::string inner = sub;
        st->appendNext = [this, nth, idx, lv, rv, inner](ValueList& cache) -> bool {
            size_t i = (*idx)++;
            Value x = nth(lv, i), y = nth(rv, i);
            cache.push_back(inner.empty() || inner == "," ? Value::list(ValueList{x, y})
                                                          : applyBinOp(inner, x, y));
            return true;
        };
        out.extM() = st;
        return out;
    }
    // `1..* X 42` / `42 X 1..*` — a cross with an endless side is endless and
    // LAZY. Its order is the usual one (left side slowest), so an endless LEFT
    // walks its elements against the whole right side, and an endless RIGHT
    // never gets past the first left element — which is what Rakudo yields.
    if (op[0] == 'X' && (endlessInt(l) || endlessInt(r))) {
        auto nth = [this](Value src, size_t i) -> Value {
            if (src.t == VT::Range) return Value::integer(src.rFrom() + (src.rExFrom() ? 1 : 0) + (long long)i);
            materializeLazy(src, i + 1);
            return src.arr() && i < src.arr()->size() ? (*src.arr())[i] : Value::any();
        };
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        auto st = std::make_shared<LazySeqState>(); st->infinite = true;
        auto idx = std::make_shared<size_t>(0);
        bool leftEndless = endlessInt(l);
        ValueList fin = leftEndless ? oneLevel(r) : oneLevel(l);
        if (fin.empty()) { Value e = Value::array(); e.isList = true; e.s = "Seq"; return e; }
        Value endless = leftEndless ? l : r; std::string inner = sub;
        st->appendNext = [this, nth, idx, fin, endless, leftEndless, inner](ValueList& cache) -> bool {
            size_t i = (*idx)++;
            Value x, y;
            if (leftEndless) { x = nth(endless, i / fin.size()); y = fin[i % fin.size()]; }
            else { x = fin[0]; y = nth(endless, i); }
            cache.push_back(inner.empty() || inner == "," ? Value::list(ValueList{x, y})
                                                          : applyBinOp(inner, x, y));
            return true;
        };
        out.extM() = st;
        return out;
    }
    // A side that has not been pulled yet — a gather, or a map/grep/skip view
    // of one — is read only as far as the result is: Z takes one position from
    // each side per element, X walks its LEFT side lazily against a right side
    // it has to read whole anyway. The result is a gather-like Seq, lazy only
    // if a side was declared so (lazy-lists.t: `(g Z @b)[^3]` runs g three takes
    // deep).
    auto unpulled = [](const Value& v) {
        if (!(v.t == VT::Array && v.ext() && v.arr() && !v.itemized)) return false;
        auto st = std::static_pointer_cast<LazySeqState>(v.ext());
        return st->gatherSeq && !st->exhausted;
    };
    auto declLazy = [](const Value& v) {
        return v.t == VT::Array && v.ext() &&
               std::static_pointer_cast<LazySeqState>(v.ext())->declaredLazy;
    };
    if ((op[0] == 'Z' && (unpulled(l) || unpulled(r))) || (op[0] == 'X' && unpulled(l))) {
        const bool zip = op[0] == 'Z';
        if (!zip) forceLazy(r);
        // a side read in place (lazy) or as its one-level elements, once
        struct Side { Value src; bool lazy; ValueList items; };
        auto side = [&](const Value& v) {
            Side s{v, unpulled(v), {}};
            if (!s.lazy) s.items = oneLevel(v);
            return std::make_shared<Side>(std::move(s));
        };
        auto ls = side(l), rs = side(r);
        if (!zip && rs->items.empty()) { Value e = Value::array(); e.isList = true; e.s = "Seq"; return e; }
        Value out = Value::array(); out.isList = true; out.s = "Seq";
        auto st = std::make_shared<LazySeqState>();
        st->gatherSeq = true;
        st->declaredLazy = declLazy(l) || declLazy(r);
        auto idx = std::make_shared<size_t>(0);
        std::string inner = sub;
        st->appendNext = [this, ls, rs, idx, inner, zip](ValueList& cache) -> bool {
            auto nth = [this](Side& s, size_t i, Value& out) -> bool {
                if (!s.lazy) { if (i >= s.items.size()) return false; out = s.items[i]; return true; }
                materializeLazy(s.src, i + 1);
                if (i >= s.src.arr()->size()) return false;
                out = (*s.src.arr())[i];
                return true;
            };
            size_t i = *idx;
            Value x, y;
            if (zip) { if (!nth(*ls, i, x) || !nth(*rs, i, y)) return false; }
            else {
                const size_t n = rs->items.size();
                if (!nth(*ls, i / n, x)) return false;
                y = rs->items[i % n];
            }
            ++*idx;
            if (inner == "=>") {
                Value p = Value::pair(x.toStr(), y);
                if (x.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(x);
                cache.push_back(p);
            }
            else if (inner.empty() || inner == ",") cache.push_back(Value::list(ValueList{x, y}));
            else cache.push_back(applyBinOp(inner, x, y));
            return true;
        };
        out.extM() = st;
        return out;
    }
    if (op[0] == 'X') forceLazy(r);   // read many times over: all of it
    if (op[0] == 'Z') {
        if (isLazy(l) && !isLazy(r)) materializeLazy(l, oneLevel(r).size());
        else if (isLazy(r) && !isLazy(l)) materializeLazy(r, oneLevel(l).size());
    }
    ValueList a = oneLevel(l), bb = oneLevel(r);
    Value out = Value::array(); out.isList = true; out.s = "Seq"; // Z/X are lazy (Rakudo)
    auto emit = [&](const Value& x, const Value& y) {
        // `1 Z=> 3` keeps the Int key — forcing .toStr() made it "1" => 3
        if (sub == "=>") {
            Value p = Value::pair(x.toStr(), y);
            if (x.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(x);
            out.arr()->push_back(p);
        }
        else if (sub.empty() || sub == ",") { // Z / X / Z, / X, — tuples
            Value t = Value::array(); t.isList = true;
            t.arr()->push_back(x); t.arr()->push_back(y);
            out.arr()->push_back(t);
        }
        else out.arr()->push_back(applyBinOp(sub, x, y));
    };
    if (op[0] == 'Z') {
        // A side that ENDS in `*` extends: its last real element repeats for
        // as long as the other side goes (`1, 2, 3, * Z 10..50`), and with
        // both extended the zip never ends
        auto extended = [](ValueList& v) {
            if (v.size() > 1 && v.back().t == VT::Whatever) { v.pop_back(); return true; }
            return false;
        };
        bool ea = extended(a), eb = extended(bb);
        if (ea && eb) {
            auto st = std::make_shared<LazySeqState>(); st->infinite = true;
            auto idx = std::make_shared<size_t>(0);
            std::string inner = sub;
            st->appendNext = [this, idx, a, bb, inner](ValueList& cache) -> bool {
                size_t i = (*idx)++;
                const Value& x = a[std::min(i, a.size() - 1)];
                const Value& y = bb[std::min(i, bb.size() - 1)];
                cache.push_back(inner.empty() || inner == "," ? Value::list(ValueList{x, y})
                                                              : applyBinOp(inner, x, y));
                return true;
            };
            out.extM() = st;
            return out;
        }
        if (ea) while (a.size() < bb.size()) a.push_back(a.back());
        if (eb) while (bb.size() < a.size()) bb.push_back(bb.back());
        for (size_t i = 0; i < a.size() && i < bb.size(); i++) emit(a[i], bb[i]);
    }
    else { for (auto& x : a) for (auto& y : bb) emit(x, y); }
    return out;
}

bool Interpreter::isHyperCompoundAssign(const std::string& inner, const Value& l) {
    static const std::set<std::string> eqTailCmp = {"==", "!=", "<=", ">=", "===", "=:="};
    return inner.size() >= 2 && inner.back() == '=' && !eqTailCmp.count(inner) &&
           l.t == VT::Array && l.arr();
}
Value Interpreter::hyperCompoundAssign(const std::string& inner, const Value& l, const Value& r,
                                       Expr* lhsExpr, bool strictL, bool strictR) {
    std::string base = inner.substr(0, inner.size() - 1);
    ValueList rhs = r.flatten();
    size_t la = lhsExpr && lhsExpr->kind == NK::ListExpr
                    ? static_cast<ListExpr*>(lhsExpr)->items.size()
                    : l.arr()->size();
    // The pointing of the markers decides the lengths (measured against
    // Rakudo): »op=« with unequal lengths — a scalar RHS is length 1 — throws;
    // «op=« touches only the first RHS-length elements; »op=» and the dwimmy
    // <<op=>> cycle the RHS over the whole array. hyperCore already enforced
    // this for the plain hyper forms; the compound path silently cycled.
    if (strictL && strictR && la != rhs.size())
        throwTypedV("X::HyperOp::NonDWIM",
            {{"left-elems", Value::integer((long long)la)}, {"right-elems", Value::integer((long long)rhs.size())},
             {"operator", Value::str(inner)}, {"recursing", Value::boolean(false)}},
            "Lists on either side of non-dwimmy hyperop of infix:<" + inner + "> are not of the same length\n"
            "left: " + std::to_string(la) + " elements, right: " + std::to_string(rhs.size()) + " elements");
    size_t n = (!strictL && strictR && rhs.size() < la) ? rhs.size() : la;
    if (!rhs.empty() && lhsExpr && lhsExpr->kind == NK::ListExpr) {
        auto& items = static_cast<ListExpr*>(lhsExpr)->items;
        for (size_t i = 0; i < n && i < items.size(); i++) {
            Value* slot = nullptr;
            try { slot = lvalue(items[i].get()); } catch (RakuError&) {}
            if (!slot) continue;             // not assignable: leave it be
            *slot = applyBinOp(base, *slot, rhs[i % rhs.size()]);
            if (i < l.arr()->size()) (*l.arr())[i] = *slot;
        }
        return l;
    }
    if (!rhs.empty())
        for (size_t i = 0; i < n; i++)
            (*l.arr())[i] = applyBinOp(base, (*l.arr())[i], rhs[i % rhs.size()]);
    // A SLICE target (`@a[0 ..^ *-1] >>~=>> "x"`) was read into a fresh list,
    // so the new values go back through the slice itself — an ordinary
    // `@a[…] = …` over the same subscript, which resolves `*-1` and friends
    if (!rhs.empty() && lhsExpr && lhsExpr->kind == NK::Index) {
        auto* ix = static_cast<Index*>(lhsExpr);
        if (ix->index && !ix->multiDim && ix->adverb.empty() &&
            (ix->index->kind == NK::Range || ix->index->kind == NK::ListExpr ||
             ix->index->kind == NK::ArrayLit)) {
            auto tmpEnv = std::make_shared<Env>(); tmpEnv->parent = tctx_.cur;
            Value vals = l; vals.isList = true; vals.itemized = false;
            tmpEnv->define("@__hyper_assign", vals);
            Assign tmpA;
            tmpA.op = "=";
            tmpA.target.reset(lhsExpr);               // borrowed: released below
            tmpA.value = std::make_unique<VarExpr>("@__hyper_assign");
            struct Back { Assign& a; ExecContext& t; std::shared_ptr<Env> s;
                ~Back() { (void)a.target.release(); t.cur = s; } } back{tmpA, tctx_, tctx_.cur};
            tctx_.cur = tmpEnv;
            evalAssign(&tmpA, true);
        }
    }
    return l;
}

Value rtReduce(Interpreter& I, const std::string& op, const Value& list, bool operands) {
    // The fold itself is applyReduce — the same implementation the interpreter
    // uses, so a compiled binary chains comparisons, runs scan forms and honors
    // the R-metaop exactly as the interpreted program does. This used to be a
    // plain left fold: `[<] 3,1,2` was False interpreted and True compiled,
    // and `[\+]` folded with the literal op string (REVIEW-3.7 finding 1).
    if (!op.empty() && op[0] != '\\') { // an endless operand is answered whole
        Value r;
        if (endlessReduce(op, list, r)) return r;
        if (list.t == VT::Array && list.arr())
            for (auto& v : *list.arr()) if (endlessReduce(op, v, r)) return r;
    }
    // SEVERAL comma items are that many operands, each as it is — `[Z] $l, $m`
    // zips the two Lists, `[+] (1,2), (3,4)` adds the counts — a Slip spreading
    // into them, as the interpreter's reduce takes them (+@list). Flattening
    // them merged the Lists into one.
    ValueList items;
    if (operands && list.t == VT::Array && list.arr()) {
        for (auto& v : *list.arr()) {
            if (v.t == VT::Array && v.arr() && v.s == "Slip") { for (auto& x : *v.arr()) items.push_back(x); }
            else items.push_back(v);
        }
    }
    else items = list.flatten();
    return I.applyReduce(op, items); // empty-list identities live there too
}

// --- argument binding for native codegen (flexible signatures) ---
Value rtPos(const ValueList& a, size_t idx) {
    size_t p = 0; for (auto& v : a) { if (isNamedArg(v)) continue; if (p == idx) return v; p++; }
    return Value::any();
}
bool rtHasPos(const ValueList& a, size_t idx) {
    size_t p = 0; for (auto& v : a) { if (isNamedArg(v)) continue; if (p == idx) return true; p++; }
    return false;
}
Value rtNamed(const ValueList& a, const std::string& key) {
    for (auto& v : a) if (isNamedArg(v) && v.s == key) return v.pairVal() ? *v.pairVal() : Value::any();
    return Value::any();
}
bool rtHasNamed(const ValueList& a, const std::string& key) {
    for (auto& v : a) if (isNamedArg(v) && v.s == key) return true;
    return false;
}
static Param paramFromDesc(const RtSigParam& d) {
    Param p;
    p.name = d.name; p.sigil = d.sigil; p.type = d.type; p.namedKey = d.namedKey;
    p.coerceFrom = d.coerceFrom; p.defaultRaku = d.defaultRaku;
    for (const char* q = d.aliasKeys; *q; ) {
        const char* e = q; while (*e && *e != ' ') e++;
        if (e > q) p.aliasKeys.emplace_back(q, e);
        q = *e ? e + 1 : e;
    }
    p.named = d.flags & RSP_NAMED; p.slurpy = d.flags & RSP_SLURPY;
    p.optional = d.flags & RSP_OPTIONAL; p.required = d.flags & RSP_REQUIRED;
    p.invocant = d.flags & RSP_INVOCANT; p.pastDoubleSemi = d.flags & RSP_PASTSEMI;
    p.coerce = d.flags & RSP_COERCE; p.isRw = d.flags & RSP_RW; p.isCopy = d.flags & RSP_COPY;
    p.hadWhere = d.flags & RSP_WHERE; p.aliasBoth = d.flags & RSP_ALIASBOTH;
    p.isRaw = d.flags & RSP_RAW;
    p.slurpyKind = d.slurpyKind; p.defConstraint = d.defConstraint;
    return p;
}
const Param& rtParamOf(const RtSigParam& d) { return *new Param(paramFromDesc(d)); }   // (a function-local static holds it)
Value Interpreter::bindTypedParam(Value v, const Param& p) {
    if (p.coerce) coerceParam(p, v);
    else {
        v = coerceViaSubset(v, p.type);
        typeCheckBind(p, v, /*blockParam=*/false, /*whereVerified=*/false, nullptr);
    }
    bindNativeParam(p, v);
    return v;
}
// What binding does to the value once the type check has passed: a native
// parameter takes it as that native (an `int8` wraps, a BigInt no machine word
// holds is refused), and a boxed one takes the value, not a native.
void Interpreter::bindNativeParam(const Param& p, Value& v) {
    // a native-int param truncates on bind (see the slow path)
    // a machine-width `int` does not wrap, but a bigint does not fit it either
    if (v.t == VT::Bool && p.type == "int") v = Value::integer(v.b ? 1 : 0);
    if (v.t == VT::Int && v.big() && p.type == "int" && !v.big()->fitsLL())
        throw RakuError{Value::typeObj("X::AdHoc"),
            "Cannot unbox " + std::to_string(v.big()->bitLength() + 1) +
            " bit wide bigint into native integer"};
    int spec = paramNatSpec(p);
    if (spec >> 1) {
        // (…but refuses a value no machine word holds at all)
        if (v.t == VT::Int && v.big() &&
            ((spec & 1) ? !v.big()->fitsLL() : v.big()->bitLength() > 64))
            throw RakuError{Value::typeObj("X::AdHoc"),
                "Cannot unbox " + std::to_string(v.big()->bitLength() + (spec & 1)) +
                " bit wide bigint into native integer"};
        wrapNative(v, spec >> 1, spec & 1);
    }
    // a boxed parameter takes the VALUE, not the native (see the slow path)
    else if (v.natBits && !p.isRw && !p.isRaw && !paramIsNative(p))
        dropNativeTags(v);
}
RtRoutineFrame::RtRoutineFrame(Interpreter& I)
    : t(I.tctx_), id(++I.tctx_.frameTop), rf(I.tctx_.curRoutineFrame), live(std::make_shared<bool>(true)) {
    t.curRoutineFrame = id;
}
RtRoutineFrame::~RtRoutineFrame() {
    *live = false;
    t.frameTop = id - 1;
    t.curRoutineFrame = rf;
}
void rtReturnFrom(Value v, uint64_t id, const std::shared_ptr<bool>& live) {
    if (!*live)
        throw RakuError{Value::typeObj("X::ControlFlow::Return"),
            "Attempt to return outside of immediately-enclosing Routine (i.e. `return` execution "
            "is outside the dynamic scope of the Routine where `return` was used)"};
    throw ReturnEx{std::move(v), id};
}
// What `++` / `--` store: they ARE .succ / .pred. A Str always steps as a
// string in Rakudo — even a numeric-looking one ("42"++ is Str "43", "10"--
// keeps its width, "09") — and strSucc/strPred pick the magic window
// themselves. Bool saturates (True++ stays True), an object whose class
// defines succ/pred dispatches there (autoincrement.t's Incrementor), an
// undefined Num steps from 0e0, and everything else is numeric, a native
// wrapping at its width.
Value Interpreter::stepValue(const Value& cur, bool up) {
    if (cur.t == VT::Bool || (cur.t == VT::Type && cur.s == "Bool")) return Value::boolean(up);
    if (cur.t == VT::Str) {
        if (up) return Value::str(strSucc(cur.s));
        bool ok; std::string r = strPred(cur.s, ok);
        return ok ? Value::str(r) : armedFailure("X::AdHoc", "Decrement out of range");
    }
    if (cur.t == VT::Object && cur.obj() && cur.obj()->cls) {
        const char* m = up ? "succ" : "pred";
        for (ClassInfo* c = cur.obj()->cls.get(); c; c = c->parent ? c->parent.get() : nullptr) {
            bool has = c->methods.count(m) > 0;
            for (auto& ep : c->extraParents) if (ep && ep->methods.count(m)) has = true;
            if (has) return methodCall(cur, m, {});
        }
    }
    if (cur.t == VT::Type && cur.s == "Num") return Value::number(up ? 1.0 : -1.0);
    Value n = applyArith(up ? "+" : "-", cur, Value::integer(1));
    if (cur.natBits) wrapNative(n, cur.natBits, cur.natSigned, cur.natFloat);   // native int wraparound
    return n;
}
void rtCatAppendText(Value& l, const std::string& r) {
    for (unsigned char c : r)
        if (c >= 0x80) { nfcAppendCow(l.s, r); return; }
    l.s.appendAscii(r);   // (a body others hold grows a buffer, not a copy: APPEND-PLAN.md)
}
void rtCatPrependText(Value& l, const std::string& x) {
    // ASCII at the join (the string's own first byte) cannot combine with what
    // goes in front of it; anything else is renormalized where they meet
    if (l.s.firstByte() >= 0x80) { nfcPrependCow(l.s, x.data(), x.size()); return; }
    l.s.prependText(x);
}
void rtViewSyncSlow(const Value& base) {
    const ElemView* vw = base.elemView();
    if (!vw || !vw->src || !base.arr() || vw->src->size() != vw->srcSize) return;
    // a typed array refuses what its type does not take, as a store into it
    // does — before anything is copied back
    if (!vw->elemType.empty())
        for (auto& v : *base.arr())
            if (isDefined(v) && !rtTypeMatch(v, vw->elemType))
                throw RakuError{Value::typeObj("X::TypeCheck::Assignment"),
                    "Type check failed in assignment; expected " + vw->elemType + " but got " + v.typeName()};
    for (size_t i = 0; i < base.arr()->size(); i++) {
        const size_t si = vw->at(i);
        if (si < vw->src->size()) (*vw->src)[si] = (*base.arr())[i];
    }
}
Value rtSig(Value c, const RtSigParam* ps, size_t n, const char* name, const char* retType, unsigned cflags) {
    if (c.t != VT::Code || !c.code()) return c;
    // one Param list per descriptor table: every evaluation of the closure
    // (a fresh Callable each time) borrows the same list, as the AST's would be
    static std::mutex mu;
    static std::unordered_map<const RtSigParam*, std::unique_ptr<std::vector<Param>>> built;
    const std::vector<Param>* list;
    {
        std::lock_guard<std::mutex> lk(mu);
        auto& slot = built[n ? ps : nullptr];
        if (!slot) {
            slot = std::make_unique<std::vector<Param>>();
            slot->reserve(n);
            for (size_t k = 0; k < n; k++) slot->push_back(paramFromDesc(ps[k]));
        }
        list = slot.get();
    }
    Callable* cc = c.code();
    cc->params = list;
    if (name && *name) cc->name = name;
    if (retType && *retType) cc->retType = retType;
    if (cflags & RSC_BLOCK) cc->isBlock = true;
    if (cflags & RSC_HADSIG) cc->hadSig = true;
    return c;
}
Value rtSlurpyPos(const ValueList& a, size_t from) {
    Value o = Value::array(); size_t p = 0;
    for (auto& v : a) { if (isNamedArg(v)) continue; if (p >= from) o.arr()->push_back(v); p++; }
    return o;
}
Value rtSlurpyNamed(const ValueList& a) {
    Value o = Value::makeHash();
    for (auto& v : a) if (isNamedArg(v)) (*o.hash())[v.s] = v.pairVal() ? *v.pairVal() : Value::any();
    return o;
}
// `my %h = …` / `%( … )` / `{ … }` for native codegen. ONE definition of what a
// list means as a hash — the interpreter's coerceHash, with the `store` flag an
// assignment to a `%` container uses. This was a second, thinner implementation
// of the same idea, and every place it fell short was a SILENT wrong answer in a
// compiled binary and nowhere else:
//   * `my %c = %b` handed back %b's own map instead of copying the entries, so
//     a later write through %c changed %b;
//   * a Hash INSIDE the list was not flattened — it went to the flat key/value
//     branch and became a KEY, its gist stringified, so `%h = (%h, 5 => 4)`
//     answered a one-element hash. That is the written-out spelling of `%h ,=
//     5 => 4`, which is how it was found (issue #85);
//   * a flat key/value list mixed with pairs went out of step by one.
// It also never knew about object-hash keys or junction keys, which coerceHash
// has handled all along.
Value rtCoerceHash(const Value& v) { return coerceHash(v, /*store=*/true); }
// `:{ ... }` for the compiling backend: the same composer, object-keyed.
Value rtObjHash(const Value& v) {
    Value h = v.t == VT::Hash && v.hashKind.empty() ? v
                                                    : coerceHash(v, /*store=*/false, /*objKeyed=*/true);
    h.ofTypeM() = "Mu,Mu,Any";   // as the interpreter's `ctx%{}` arm; see there
    h.objKeyed = true;
    return h;
}

// Writable element reference for native codegen (autovivifies base and slot).
// A write into an element of an immutable Pair, List or Range, refused as the
// interpreter refuses it: `=` is X::Assignment::RO (with its typename and value
// attributes), and a `++`/`--` (stepOp), which binds `is rw`, matches no
// candidate and is X::Multi::NoMatch.
[[noreturn]] static void refuseImmutablePlace(const Value& base, const Value& elem, const char* ty,
                                              bool withGist, const char* stepOp) {
    if (stepOp)
        throw RakuError{Value::typeObj("X::Multi::NoMatch"),
            std::string("Cannot resolve caller ") + stepOp + "(" + elem.typeName() +
            ":D); the following candidates match the type but require mutable arguments"};
    const std::string msg = std::string("Cannot modify an immutable ") + ty +
                            (withGist ? " (" + base.gist() + ")" : std::string());
    if (g_cbInterp)
        g_cbInterp->throwTypedV("X::Assignment::RO", {{"typename", Value::str(ty)}, {"value", base}}, msg);
    throw RakuError{Value::typeObj("X::Assignment::RO"), msg};
}
// stepOp: the `postfix:<++>` / `prefix:<-->` a step site resolves the place for
Value& rtIndexRef(Value& base, const Value& key, bool isHash, const char* stepOp) {
    if (isHash) {
        // A Pair is Associative but immutable: `$p<a> = 9` is X::Assignment::RO,
        // as the interpreter's lvalue refuses it. Vivifying below REPLACED the
        // Pair with a Hash and accepted the write. (`my $r := $p<a>` binds
        // through rtIndexGet, so no binding comes this way.)
        if (base.t == VT::Pair) refuseImmutablePlace(base, rtIndexGet(base, key, true), "Pair", false, stepOp);
        // what a `$` scalar vivifies is ITEMIZED, as the interpreter's is:
        // `my $u; $u<k> = 3; say $u.raku` is `${:k(3)}`
        if (base.t != VT::Hash || !base.hash()) { base = Value::makeHash(); base.itemized = true; }
        return (*base.hash())[key.toStr()];
    }
    // A Range and a List are immutable too, as the interpreter's lvalue() has
    // them: a write REPLACED the Range with a fresh Array and wrote into the
    // List. A list of an Array's elements (ElemView) takes the write, which
    // rtViewSync mirrors; so does an element that IS a container.
    if (base.t == VT::Range) refuseImmutablePlace(base, rtIndexGet(base, key, false), "Range", true, stepOp);
    if (base.t == VT::Array && base.arr() && base.isList && base.s != "Seq" && base.enumName.empty() &&
        !base.elemView()) {
        const long long li = key.toInt();
        const bool inList = li >= 0 && li < (long long)base.arr()->size();
        if (!(inList && g_cbInterp && g_cbInterp->isContainerElem((*base.arr())[li])))
            refuseImmutablePlace(base, inList ? (*base.arr())[li] : Value::any(), "List", true, stepOp);
    }
    if (base.t != VT::Array || !base.arr()) { base = Value::array(); base.itemized = true; }
    long long i = writeIndexInt(key);
    if (i < 0) negIndexThrow(i);
    if (i >= (long long)base.arr()->size())
        // the gaps read as the container's own default — `is default(v)` first,
        // then the element type's object (sheet LA-21)
        base.arr()->resize(i + 1, containerFill(base));
    return (*base.arr())[i];
}
// `@$h[$i] = v` / `%$h<k>++` for native codegen: the scalar's own Array (Hash)
// is the subscript base, as the interpreter's lvalue() takes it (#122). Any
// other content is coerced to a fresh value there, so it is not assignable.
Value& rtDerefRef(Value& v, bool isHash) {
    if (isHash ? (v.t == VT::Hash && v.hash() && v.hashKind.empty())
               : (v.t == VT::Array && v.arr() && !v.isList))
        return v;
    throw RakuError{Value::typeObj("X::Assignment::RO"), "Target is not assignable"};
}
// Native read-write loops (Codegen::forStmt): `for @a <-> $x`, `for @$h.kv ->
// $i, $x is rw`, `for %$h.kv -> $k, $v is rw`. The interpreter's model, step
// for step: each slot is copied into the loop variable and copied back after
// the body. The source is the Array (Hash) itself when the variable holds one;
// anything else is iterated as a coerced copy, whose writes go nowhere — the
// interpreter's ordinary path.
Value* rtRwSource(Value& v, bool isHash, Value& hold) {
    // (an object-keyed hash files its entries under a key the subscript does not spell)
    if (isHash ? (v.t == VT::Hash && v.hash() && v.hashKind.empty() && !v.objKeyed)
               : (v.t == VT::Array && v.arr() && !v.isList && !v.ext()))
        return &v;
    hold = isHash ? rtCoerceHash(v) : rtArrayVal(v);
    return &hold;
}
// the keys a read-write loop walks, taken before the first iteration
Value rtRwKeys(const Value& src) {
    Value out = Value::array();
    if (src.t == VT::Array && src.arr())
        for (size_t i = 0; i < src.arr()->size(); i++) out.arr()->push_back(Value::integer((long long)i));
    else if (src.t == VT::Hash && src.hash())
        for (auto& kv : *src.hash()) out.arr()->push_back(hashEntryKey(src, kv.first, kv.second));
    return out;
}
// …and the slot for one of them, nullptr once it is gone (a shrunk array, a deleted key)
Value* rtRwSlot(Value& src, const Value& key) {
    if (src.t == VT::Array && src.arr()) {
        long long i = key.toInt();
        return i >= 0 && i < (long long)src.arr()->size() ? &(*src.arr())[(size_t)i] : nullptr;
    }
    if (src.t == VT::Hash && src.hash()) {
        auto it = src.hash()->find(key.toStr());
        return it == src.hash()->end() ? nullptr : &it->second;
    }
    return nullptr;
}
// `--doc` — the unit's `$=pod` through Pod::To::Text, declarator blocks with
// the declaration they document ("sub foo()\nits doc"), as Rakudo prints it
std::string Interpreter::docModeText() {
    if (podDom_.empty()) return podData_;
    auto declHeader = [&](long long line) -> std::string {
        std::string L = srcLineOf(srcFile_, (int)line);
        size_t p = L.find_first_not_of(" \t");
        if (p == std::string::npos) return "";
        auto word = [&]() {
            size_t b = p;
            while (p < L.size() && (ascii::isalnum((unsigned char)L[p]) || L[p] == '_' || L[p] == '-' ||
                                    L[p] == ':' || L[p] == '\'')) p++;
            std::string w = L.substr(b, p - b);
            while (p < L.size() && (L[p] == ' ' || L[p] == '\t')) p++;
            return w;
        };
        std::string kw = word();
        while (kw == "multi" || kw == "proto" || kw == "our" || kw == "my" || kw == "only") kw = word();
        std::string name = word();
        if (kw == "class" || kw == "role" || kw == "grammar" || kw == "module" || kw == "package")
            return kw + " " + name;
        if (kw != "sub" && kw != "method" && kw != "submethod") return "";
        std::string params;
        if (p < L.size() && L[p] == '(') {
            size_t e = L.find(')', p);
            if (e != std::string::npos) params = L.substr(p + 1, e - p - 1);
        }
        std::vector<std::string> ps;
        for (size_t a = 0; a <= params.size();) {
            size_t c = params.find(',', a);
            std::string one = params.substr(a, c == std::string::npos ? std::string::npos : c - a);
            size_t x = one.find_first_not_of(" \t"), y = one.find_last_not_of(" \t");
            if (x != std::string::npos) ps.push_back(one.substr(x, y - x + 1));
            if (c == std::string::npos) break;
            a = c + 1;
        }
        std::string sig = "(";
        if (!ps.empty()) {
            for (auto& one : ps) sig += "\n\t" + one;
            sig += "\n";
        }
        return kw + " " + name + sig + ")";
    };
    std::string out;
    for (auto& p : podDom_) {
        std::string piece;
        if (p.t == VT::Hash && p.hash() && p.hash()->count("podclass") &&
            (*p.hash())["podclass"].toStr() == "Pod::Block::Declarator") {
            long long line = p.hash()->count("declLine") ? (*p.hash())["declLine"].toInt() : 0;
            std::string h = line > 0 ? declHeader(line) : std::string();
            if (h.empty() && line > 1 && p.hash()->count("trailingPod")) h = declHeader(line - 1);
            std::string body = pod2text((*p.hash())["contents"]);
            // (the contents are one Str whose line breaks are kept)
            piece = h.empty() ? body : h + "\n" + body;
        }
        else piece = pod2text(p);
        if (piece.empty()) continue;
        if (!out.empty()) out += "\n\n";
        out += piece;
    }
    return out.empty() ? out : out + "\n";
}
Value Interpreter::roleCandidates(ClassInfo* ci) {
    Value out = Value::array(); out.isList = true;
    auto push = [&](const std::shared_ptr<ClassInfo>& c) {
        Value t = Value::typeObj(ci->name);
        if (c) { auto r = std::make_shared<RoleCandidateRef>(); r->ci = c; t.extM() = std::static_pointer_cast<void>(r); }
        out.arr()->push_back(t);
    };
    for (auto& v : ci->roleVariants) push(v);
    auto self = classes_.find(ci->name);
    push(self != classes_.end() && self->second.get() == ci ? self->second : nullptr);
    return out;
}
// A call to a routine nothing declares, found BEFORE the code runs: `say 42;
// nope()` prints nothing. Only for code the call walker sees all of (plain
// statements — any declaration or other statement kind makes it opaque), and
// only names no scope, builtin or type answers.
void Interpreter::checkUndeclaredCalls(const std::vector<StmtPtr>& stmts) {
    std::set<std::string> calls, declared;
    for (auto& st : stmts) {
        spCallsS(st.get(), calls);
        // `our constant &f = …` / `my &f = …` declares a routine name here
        if (st && st->kind == NK::ExprStmt) {
            const Expr* e = static_cast<const ExprStmt*>(st.get())->e.get();
            if (e && e->kind == NK::Assign) e = static_cast<const Assign*>(e)->target.get();
            if (e && e->kind == NK::VarExpr) {
                auto* ve = static_cast<const VarExpr*>(e);
                if (ve->declare && ve->name.size() > 1 && ve->name[0] == '&') declared.insert(ve->name.substr(1));
            }
        }
    }
    if (calls.count("\x01other")) return;
    static const std::set<std::string> kSpecial = {
        "...", "!!!", "???", "defined", "let", "temp", "lazy", "eager", "hyper", "race",
        "take", "take-rw", "undefine", "cas", "so", "not", "EVAL", "EVALFILE", "return",
        "return-rw", "last", "next", "redo", "proceed", "succeed", "leave", "emit", "done",
        "callsame", "callwith", "nextsame", "nextwith", "samewith", "nextcallee", "lastcall",
        "sink", "quietly", "do", "gather", "start", "await", "item", "list", "flat", "slip"};
    for (auto& n : calls) {
        if (n.empty() || n.find(':') != std::string::npos || n.rfind("__", 0) == 0) continue;
        if (declared.count(n)) continue;
        if (ascii::isupper((unsigned char)n[0]) || kSpecial.count(n) || n.rfind("atomic-", 0) == 0) continue;
        if (builtins_.count(n) || builtinRef(n) || classes_.count(n) || isKnownTypeName(n)) continue;
        if (tctx_.cur && tctx_.cur->find("&" + n)) continue;
        if (global_ && global_->find("&" + n)) continue;
        undeclaredRoutine(n);
    }
}
bool Interpreter::fatalHere() const {
    for (const Env* e = tctx_.cur.get(); e; e = e->parent.get())
        if (e->fatalPragma) return e->fatalPragma > 0;
    return false;
}

static bool ncIsFloatType(const std::string& t) {
    return t == "num" || t == "num32" || t == "num64" || t == "Num" ||
           t == "Rat" || t == "Real" || t == "double" || t == "Numeric";
}

// The machine word the blind path passes. NOT `long`: that is 32 bits on
// Windows (LLP64, MSVC and MinGW-w64 alike), so every pointer argument, every
// `is rw` slot and every callback argument lost its top half there — an HWND
// or an HMODULE, which on x64 lives above 4 GB, arrived as garbage.
using NcWord = long long;

// Over-wide fixed signatures for the FFI call. On the SysV-AMD64 and AArch64
// ABIs the integer and float register banks are independent and a callee simply
// ignores the argument registers it doesn't declare, so ONE 16-int + 8-float
// signature dispatches every call whose arguments fit in registers — including
// MIXED int/float, which the old per-arity dispatch rejected. Unused slots are
// padded with zero; args reorder into their bank in declaration order, which is
// exactly how the callee reads them.
//
// Sixteen integers rather than eight: a caller may always pass MORE arguments
// than the callee declares (every ABI this builds for is caller-cleaned — the
// extras land in registers or stack slots the callee never reads), and the
// Win32 API routinely goes past eight — CreateWindowExW takes twelve,
// CreateFontW fourteen. Eight made those calls throw on any host without
// libffi, which on Windows is the normal case.
typedef long long (*NcFnI)(NcWord,NcWord,NcWord,NcWord,NcWord,NcWord,NcWord,NcWord,
                           NcWord,NcWord,NcWord,NcWord,NcWord,NcWord,NcWord,NcWord,
                           double,double,double,double,double,double,double,double);
typedef double    (*NcFnD)(NcWord,NcWord,NcWord,NcWord,NcWord,NcWord,NcWord,NcWord,
                           NcWord,NcWord,NcWord,NcWord,NcWord,NcWord,NcWord,NcWord,
                           double,double,double,double,double,double,double,double);
// native scalar type → byte width and signedness (for is-rw copy-back, cglobal,
// Pointer.deref). 0 ⇒ not a plain native scalar.
int ncScalarWidth(const std::string& t, bool& sign, bool& isFloat) {
    isFloat = false; sign = true;
    // Every name below starts with one of these letters. This runs per CArray
    // element read and write, so a pointer or class type skips the whole chain.
    if (t.empty()) return 0;
    switch (t[0]) {
        case 'n': case 'N': case 'i': case 'I': case 'c': case 'u': case 'b':
        case 'B': case 'l': case 's': break;
        default: return 0;
    }
    if (t == "num32") { isFloat = true; return 4; }
    if (t == "num" || t == "num64" || t == "Num") { isFloat = true; return 8; }
    if (t == "int8"  || t == "char")   { return 1; }
    if (t == "uint8" || t == "byte")   { sign = false; return 1; }
    if (t == "int16")  return 2;
    if (t == "uint16") { sign = false; return 2; }
    if (t == "int32" || t == "long32") return 4;
    if (t == "uint32") { sign = false; return 4; }
    if (t == "int" || t == "int64" || t == "long" || t == "longlong" || t == "ssize_t" || t == "Int") return 8;
    if (t == "uint" || t == "uint64" || t == "ulong" || t == "ulonglong" || t == "size_t") { sign = false; return 8; }
    // C's `bool` is ONE byte, not a machine word. It used to answer 8 here,
    // which made `nativesizeof(bool)` and every CArray[bool] stride wrong and
    // read a whole register back from a one-byte return.
    if (t == "bool" || t == "Bool") { sign = false; return 1; }
    return 0;
}

// element type inside `Pointer[T]` / `CArray[T]` ("" for a bare Pointer)
static std::string ncElemType(const std::string& t) {
    size_t b = t.find('[');
    return b == std::string::npos ? "" : t.substr(b + 1, t.size() - b - 2);
}
// Does this spelling name a return type the marshaller already understands?
// Only a name that does NOT is worth a closure lookup for a constant alias.
static bool ncKnownRetSpelling(const std::string& t) {
    if (t.empty() || t == "void" || t == "Nil" || t == "Str" ||
        t == "Bool" || t == "bool") return true;
    if (t == "Pointer" || t.rfind("Pointer[", 0) == 0) return true;
    if (t == "CArray"  || t.rfind("CArray[", 0)  == 0) return true;
    bool sgn, flt; return ncScalarWidth(t, sgn, flt) > 0;
}

// A live Pointer holds { addr, of } — deref reads native memory of type `of`.
Value Interpreter::ncMakePointer(const std::string& type, void* p) {
    Value v = Value::makeHash(); v.hashKind = "Pointer";
    (*v.hash())["addr"] = Value::integer((long long)(intptr_t)p);
    (*v.hash())["of"]   = Value::str(ncElemType(type));
    return v;
}
// A live CArray[T] holds { addr, of } — element access reads native memory.
Value Interpreter::ncMakeLiveCArray(const std::string& type, void* p) {
    Value v = Value::makeHash(); v.hashKind = "CArray";
    (*v.hash())["addr"] = Value::integer((long long)(intptr_t)p);
    std::string of = ncElemType(type);
    (*v.hash())["of"]   = Value::str(of.empty() ? "int64" : of);
    return v;
}
// Is this CArray element type itself a pointer? `CArray[Pointer]`,
// `CArray[CArray[uint8]]` (an `unsigned char **`), `CArray[Str]`.
bool Interpreter::ncIsPointerElem(const std::string& t) {
    return t == "Pointer" || t == "Str" || t == "CArray" ||
           t.rfind("Pointer[", 0) == 0 || t.rfind("CArray[", 0) == 0;
}
// Byte width of a CArray element. ncScalarWidth answers 0 for anything that
// isn't a native scalar — those are machine pointers, not the int32 that the
// old ad-hoc size tables fell back to.
int Interpreter::ncElemSize(const std::string& t) {
    bool sgn, isFlt; int w = ncScalarWidth(t, sgn, isFlt);
    if (w) return w;
    return (int)sizeof(void*);
}
// Read one native element at addr[index] as the given native type.
Value Interpreter::ncReadElem(long long addr, const std::string& ofType, long long index) {
    if (!addr) return Value::any();
    bool sgn, isFlt; int w = ncScalarWidth(ofType, sgn, isFlt);
    if (w == 0) w = 8; // pointer-sized default (Pointer[T], opaque)
    const char* base = (const char*)(intptr_t)addr + index * w;
    if (isFlt) {
        if (w == 4) { float x; std::memcpy(&x, base, 4); return Value::number((double)x); }
        double x; std::memcpy(&x, base, 8); return Value::number(x);
    }
    long long val = 0;
    switch (w) {
        case 1: { if (sgn) { int8_t x; std::memcpy(&x, base, 1); val = x; } else { uint8_t x; std::memcpy(&x, base, 1); val = x; } break; }
        case 2: { if (sgn) { int16_t x; std::memcpy(&x, base, 2); val = x; } else { uint16_t x; std::memcpy(&x, base, 2); val = x; } break; }
        case 4: { if (sgn) { int32_t x; std::memcpy(&x, base, 4); val = x; } else { uint32_t x; std::memcpy(&x, base, 4); val = x; } break; }
        default:{ std::memcpy(&val, base, 8); break; }
    }
    // A CArray[Str] slot is a char* — hand back the STRING it points at, not
    // the pointer bits. NULL is the undefined Str: that is the terminator
    // getgrnam-style name lists are walked by (`with $members[$_] … else
    // last`), and a raw-Int pointer was always defined, so the walk ran off
    // the end of the list and SIGBUSed (P5getgrnam).
    if (ofType == "Str")
        return val ? Value::str((const char*)(intptr_t)val) : Value::typeObj("Str");
    return Value::integer(val);
}
// Write one native scalar `val` at addr[index] of type ofType.
void Interpreter::ncWriteElem(long long addr, const std::string& ofType, long long index, const Value& val) {
    if (!addr) return;
    bool sgn, isFlt; int w = ncScalarWidth(ofType, sgn, isFlt); if (w == 0) w = 8;
    char* base = (char*)(intptr_t)addr + index * w;
    if (isFlt) {
        if (w == 4) { float x = (float)val.toNum(); std::memcpy(base, &x, 4); }
        else        { double x = val.toNum();       std::memcpy(base, &x, 8); }
        return;
    }
    // a byte-backed CArray/Buf value stores the ADDRESS of its bytes — through
    // ncRawAddr, which promotes and RETAINS the storage: `$out[0] =
    // CArray[uint8].new(…)` inside an OpenSSL ALPN callback must outlive the
    // statement that wrote it, since the library reads the protocol name after
    // the callback returns
    long long v = val.t == VT::Object || val.t == VT::Hash ? Interpreter::ncRawAddr(val)
                : val.t == VT::Str && (val.hashKind == "CArray" || val.hashKind == "Buf" || val.hashKind == "Blob")
                    ? Interpreter::ncRawAddr(val)
                : val.t == VT::Str ? (long long)(intptr_t)val.s.c_str() : val.toInt();
    switch (w) {
        case 1: { int8_t  x = (int8_t)v;  std::memcpy(base, &x, 1); break; }
        case 2: { int16_t x = (int16_t)v; std::memcpy(base, &x, 2); break; }
        case 4: { int32_t x = (int32_t)v; std::memcpy(base, &x, 4); break; }
        default:{ std::memcpy(base, &v, 8); break; }
    }
}
// C struct layout for a `repr('CStruct')` class: byte offset of `field` (with its
// type), plus the total padded struct size. Natural alignment (align == size,
// capped at 8); non-scalar fields (Str/Pointer/CArray/CStruct) are pointer-sized.
// A `repr('CUnion')` class lays every field at offset 0 and is as big as its
// widest member — C's union, which is what NativeCall's CUnion means.
// A CStruct field's type may be a CONSTANT ALIAS for a native one —
// `constant my_bool = int8;` and `constant intptr = ptrsize == 8 ?? uint64 !!
// uint32;`, both of which DBDish::mysql::Native uses for real fields.
// ncScalarWidth only knows the native spellings, so an alias fell through to
// the pointer-sized default: MYSQL_BIND measured 144 bytes instead of 112 and
// every field past the first `my_bool` sat at the wrong offset, which is what
// mysql_stmt_bind_result then handed the server. Only a LOWERCASE name is
// looked up: native type names are lowercase and class/Pointer/Str names are
// not, so a class-typed field still costs no lookup.
// What a byte-backed CArray keeps on its `ext`: the strings its CArray[Str]
// slots point into, and the object last stored in each slot of a
// CArray[SomeCStruct].
struct NcCArrayKeep {
    std::vector<std::shared_ptr<std::string>> strs;
    std::unordered_map<long long, Value> objs;
};
static NcCArrayKeep& ncCArrayKeep(Value& arr) {
    auto k = std::static_pointer_cast<NcCArrayKeep>(arr.ext());
    if (!k) { k = std::make_shared<NcCArrayKeep>(); arr.extM() = k; }
    return *k;
}
// See the header: a CArray[Str] element is a char* into memory the array owns.
// Writing the SOURCE Value's own buffer (what ncWriteElem does for a bare Str)
// left every slot pointing at a temporary that died with the statement, so the
// array read back as garbage — DBDish::Pg builds its whole PQexecPrepared
// parameter array exactly this way.
long long Interpreter::ncOwnStrElem(Value& arr, const Value& v) {
    if (!isDefined(v)) return 0;   // an undefined Str is C's NULL
    auto& strs = ncCArrayKeep(arr).strs;
    strs.push_back(std::make_shared<std::string>(v.toStr()));
    return (long long)(intptr_t)strs.back()->c_str();
}

// The element type of `CArray[N-Error]` was read as an 8-byte integer: the
// slot's pointer came back as a number and `g_set_error_literal($e, …)` could
// never be read through (#136). A class name only — native type names are
// lowercase, as ncClass in callNative also assumes.
std::shared_ptr<ClassInfo> Interpreter::ncElemClass(const std::string& t) {
    if (t.empty() || (!ascii::isupper((unsigned char)t[0]) && t.find("::") == std::string::npos)) return nullptr;
    if (ncIsPointerElem(t)) return nullptr;
    auto it = classes_.find(t);
    if (it == classes_.end()) it = classes_.find(resolveClassAlias(t));
    if (it == classes_.end()) return nullptr;
    const std::string& r = it->second->repr;
    return r == "CStruct" || r == "CPPStruct" || r == "CUnion" || r == "CPointer" ? it->second : nullptr;
}
Value Interpreter::ncClassElem(Value el, const Value* arr, const std::string& t, long long index) {
    if (el.t != VT::Int) return el;
    auto ci = ncElemClass(t);
    if (!ci) return el;
    long long p = el.toInt();
    if (!p) return Value::typeObj(ci->name);
    // the object stored here, while the slot still points at it: `$c[0] === $x`
    if (arr && arr->ext()) {
        auto k = std::static_pointer_cast<NcCArrayKeep>(arr->ext());
        auto it = k->objs.find(index);
        if (it != k->objs.end() && it->second.t == VT::Object && it->second.obj()) {
            auto pa = it->second.obj()->attrs.find("__native_ptr");
            if (pa != it->second.obj()->attrs.end() && pa->second.toInt() == p) return it->second;
        }
    }
    // a pointer C put there: a view onto its memory, as a CStruct return is
    Value o; o.t = VT::Object; o.setObj(makePayload<ObjectData>());
    o.obj()->cls = ci; o.obj()->attrs["__native_ptr"] = Value::integer(p);
    return o;
}
void Interpreter::ncKeepClassElem(Value& arr, long long index, const Value& v) {
    if (v.t == VT::Object && v.obj()) ncCArrayKeep(arr).objs[index] = v;
    else if (arr.ext()) ncCArrayKeep(arr).objs.erase(index);
}
Value Interpreter::ncLocalAt(const Value& arr, long long i) {
    const std::string et = arr.enumName.empty() ? std::string("int64") : arr.enumName.str();
    const int w = ncElemSize(et);
    if (i < 0 || (i + 1) * w > (long long)arr.s.size()) {
        // past the end: a class element reads as its type object, a Str as
        // (Str), a number as 0 (Rakudo); a pointer element is left undefined
        if (i < 0) return Value::any();
        if (auto ci = ncElemClass(et)) return Value::typeObj(ci->name);
        if (et == "Str") return Value::typeObj("Str");
        if (ncIsPointerElem(et)) return Value::any();
        return et.compare(0, 3, "num") == 0 ? Value::number(0.0) : Value::integer(0);
    }
    Value el = ncClassElem(ncReadElem((long long)(intptr_t)arr.s.data(), et, i), &arr, et, i);
    // an element that is ITSELF a pointer stays usable as one, so
    // `$out[0][^$n]` can read through what a native call wrote there
    // (a Str element already came back dereferenced — leave it be)
    // …but an UNTYPED pointer element (`CArray[Pointer]`, C's `void **`) is a
    // Pointer, as Rakudo reads it — there is nothing to index through
    // (NULL included: an element is `Pointer.new(0)`, unlike a NULL field)
    if (et == "Pointer" && el.t == VT::Int) return ncMakePointer(et, (void*)(intptr_t)el.toInt());
    if (ncIsPointerElem(et) && el.t == VT::Int) return ncMakeLiveCArray(et, (void*)(intptr_t)el.toInt());
    return el;
}
void Interpreter::ncLocalAssign(Value& arr, long long i, const Value& v) {
    if (i < 0) return;
    const std::string et = arr.enumName.empty() ? std::string("int64") : arr.enumName.str();
    const int esz = ncElemSize(et);
    const size_t need = (size_t)(i + 1) * (size_t)esz;
    if (arr.s.size() < need) arr.s.resize(need, '\0');
    if (et == "Str") { // the slot is a char* into memory the array owns
        long long p = ncOwnStrElem(arr, v);
        // IN PLACE: `mut()` forks the shared buffer, and the
        // buffer is what C was handed (see CArray.new).
        std::memcpy(arr.s.mutInPlace() + (size_t)i * esz, &p, sizeof p);
    }
    else {
        ncWriteElem((long long)(intptr_t)arr.s.data(), et, i, v);
        if (ncElemClass(et)) ncKeepClassElem(arr, i, v);
    }
}
// Value::toStr's view of a CArray built here: its elements, space-joined
static std::string carrayStrImpl(const Value& c) {
    const std::string et = c.enumName.str();
    const int w = Interpreter::ncElemSize(et);
    std::string out;
    for (long long i = 0; w > 0 && (i + 1) * w <= (long long)c.s.size(); i++) {
        if (i) out += ' ';
        out += Interpreter::ncReadElem((long long)(intptr_t)c.s.data(), et, i).toStr();
    }
    return out;
}
extern std::string (*g_carrayStr)(const Value&);   // Value.cpp
static const bool g_carrayStrSet = ((g_carrayStr = &carrayStrImpl), true);

// Store `rhs` into a CStruct's field at `off` (of native `type`), on `inv`.
// A CArray/Pointer field stores the ADDRESS of its value's buffer. When that
// value is a byte-backed CArray/Buf the buffer belongs to the VALUE, and
// ncWriteElem would record the address of the temporary `rhs` — leaving the
// field pointing into freed heap the moment the statement ended. Keep the
// buffer alive ON THE STRUCT and take the address from the copy that now lives
// as long as the object does. (Rakudo refuses this assignment outright.
// Recording a dangling pointer was already the worse answer; once a write
// actually reaches the pointer — which it now does, see the live-CArray arm in
// evalAssignInner — it corrupts the heap instead of being quietly dropped.)
// A LIVE CArray (nativecast over a Blob) carries the Blob's own address and is
// written as that address, so C fills the caller's buffer, not a copy.
void Interpreter::ncStoreStructField(Value& inv, const std::string& field, const std::string& type,
                                     long long off, const Value& rhs) {
    std::string bt = type.substr(0, type.find('['));
    if ((bt == "CArray" || bt == "Pointer") && rhs.t == VT::Str &&
        (rhs.hashKind == "CArray" || rhs.hashKind == "Buf" || rhs.hashKind == "Blob")) {
        Value& kept = (inv.obj()->attrs["__native_keep_" + field] = rhs);
        ncWriteElem(inv.obj()->attrs["__native_ptr"].toInt() + off, "int64", 0,
                    Value::integer((long long)(intptr_t)kept.s.data()));
    }
    else ncWriteElem(inv.obj()->attrs["__native_ptr"].toInt() + off, type, 0, rhs);
}

// `has GType $.g-type` where `constant GType is export = uint64` (Gnome::N's
// GlibToRakuTypes, and its `void-ptr` = Pointer[void]): the attribute's declared
// type is a constant ALIASING a type. Resolved once, when the class is declared,
// in the declaring scope — every later check (assignment, construction, the
// CStruct layout) then sees the real name. Only a name that is not itself a
// class or subset is looked up, so a type that merely shares its name with a
// term keeps winning; and only a TYPE value counts.
std::string Interpreter::resolveAttrTypeAlias(const std::string& t, const std::string& pkg) {
    if (t.empty()) return t;
    // A NESTED class shadows an outer one of the same name for the names written
    // in its parent's body: `class Outer { class Inner {…}; has Inner @.xs }`
    // means Outer::Inner even where another `Inner` is in scope. JSON::Unmarshal's
    // own suite declares both in one file, and the attribute took the outer one —
    // so every element the custom unmarshaller built failed its own type check.
    if (!pkg.empty() && t.find("::") == std::string::npos &&
        classes_.count(pkg + "::" + t)) return pkg + "::" + t;
    if (classes_.count(t) || subsets_.count(t)) return t;
    Value* v = tctx_.cur ? tctx_.cur->find(t) : nullptr;
    if (v && v->t == VT::Type && !v->s.empty() && v->s != t) return v->s;
    return t;
}

// The names rakulib/ shadows on purpose — see the resolver's search order.
bool Interpreter::isShadowedModule(const std::string& name) {
    return name == "NativeHelpers::Blob" || name == "NativeHelpers::CStruct" ||
           name == "NativeHelpers::Pointer";
}

// The library an `is native(EXPR)` names. A Str is taken as written; the
// (name, Version) pair NativeCall's `is native('cairo', v2)` also accepts as a
// two-element list — `our $cairolib = ('cairo', v2)` in Cairo — becomes the
// platform's versioned file name, as Rakudo's guess_library_name spells it:
// libcairo.2.dylib here, libcairo.so.2 on Linux, cairo-2.dll on Windows.
std::string Interpreter::ncLibNameOf(const Value& r) {
    if (r.t == VT::Array && r.arr() && r.arr()->size() == 2) {
        std::string name = (*r.arr())[0].toStr();
        std::string ver = (*r.arr())[1].toStr();
        if (!ver.empty() && ver[0] == 'v') ver = ver.substr(1);
        if (name.rfind("lib", 0) != 0) name = "lib" + name;
#if defined(_WIN32)
        return name.substr(3) + "-" + ver + ".dll";
#elif defined(__APPLE__)
        return name + "." + ver + ".dylib";
#else
        return name + ".so." + ver;
#endif
    }
    return r.toStr();
}

std::string Interpreter::ncResolveTypeAlias(ClassInfo* ci, const std::string& t) {
    // An UPPERCASE alias is the normal spelling in NativeCall code — Font::FreeType
    // declares `constant FT_Int is export = int32` and writes `FT_Int` in every
    // signature — so the name's case cannot decide this. What keeps a real class
    // out is the check below: the alias is accepted only when it resolves to a
    // native SCALAR name, which no class ever does.
    if (t.empty()) return t;
    bool sgn, isF;
    if (ncScalarWidth(t, sgn, isF)) return t;   // already a native name
    // The DECLARING scope is where the alias lives, and its parent chain ends at
    // the global one, so this finds a module constant and a program-level one alike.
    Value* v = (ci && ci->declEnv) ? ci->declEnv->find(t) : nullptr;
    // Accept the alias only when it names a native SCALAR — anything else keeps
    // the pointer-sized default it had.
    if (v && v->t == VT::Type && v->s != t && ncScalarWidth(v->s, sgn, isF)) return v->s;
    return t;
}
// The struct a `HAS` member inlines, looked up when the OUTER class is declared.
// (An inner struct C's header declared later is not a struct this can lay out —
// the member falls back to pointer width, as it did before `HAS` existed.)
std::shared_ptr<ClassInfo> Interpreter::ncInlineClass(const std::string& type) {
    auto it = classes_.find(type);
    if (it == classes_.end()) it = classes_.find(resolveClassAlias(type));
    if (it == classes_.end()) return nullptr;
    const std::string& r = it->second->repr;
    if (r != "CStruct" && r != "CPPStruct" && r != "CUnion") return nullptr;
    return it->second;
}

// The width and alignment one member contributes to its struct's layout. A
// plain member is a scalar or a machine pointer; a `HAS` member is the WHOLE
// inner struct, laid out in place — C's `struct T t;` beside `struct T *t;` —
// so it is as wide as that struct and aligned like its widest field. Taking the
// size for the alignment too would be wrong the moment it is not a power of two
// (a 12-byte struct aligns to 4, not 12).
void Interpreter::ncMemberLayout(const ClassAttr& a, const std::string& at,
                                 long long& w, long long& align) {
    // A struct that inlines itself (only reachable by reopening one) has no
    // layout at all — C forbids it. Bound the walk rather than recurse forever.
    static thread_local int depth = 0;
    if (a.inlined && a.inlineCls && depth < 16) {
        ++depth;
        w = ncStructSize(a.inlineCls.get());
        align = ncStructAlign(a.inlineCls.get());
        --depth;
        if (w <= 0) w = align = 1;
        return;
    }
    bool sgn, isF; int sw = ncScalarWidth(at, sgn, isF);
    w = align = sw ? sw : 8;
}
// A struct's own alignment: the widest alignment any member needs.
long long Interpreter::ncStructAlign(ClassInfo* ci) {
    long long maxA = 1;
    for (auto& a : ci->attrs) {
        long long w, al; ncMemberLayout(a, ncResolveTypeAlias(ci, a.type), w, al);
        if (al > maxA) maxA = al;
    }
    return maxA;
}
// Every field's offset and type, worked out once per class. Laying the struct
// out again on each access (resolving every member's type alias on the way)
// was most of the cost of `$s.field`.
struct NcLayout {
    size_t nattrs = 0;   // the attribute count it was made from
    struct Field { std::string name, type; long long off; };
    std::vector<Field> fields;
};
NcLayoutSlot& NcLayoutSlot::operator=(const NcLayoutSlot&) {
    delete p.exchange(nullptr, std::memory_order_acq_rel);
    return *this;
}
NcLayoutSlot::~NcLayoutSlot() { delete p.load(std::memory_order_acquire); }

static const NcLayout* ncLayoutOf(ClassInfo* ci) {
    const NcLayout* have = ci->ncLayout.p.load(std::memory_order_acquire);
    if (have && have->nattrs == ci->attrs.size()) return have;
    auto* l = new NcLayout;
    l->nattrs = ci->attrs.size();
    const bool uni = (ci->repr == "CUnion");
    long long off = 0;
    for (auto& a : ci->attrs) {
        std::string at = Interpreter::ncResolveTypeAlias(ci, a.type);
        long long w, align; Interpreter::ncMemberLayout(a, at, w, align);
        if (!uni) off = (off + align - 1) / align * align;
        std::string an = a.name; if (!an.empty() && (an[0]=='$'||an[0]=='@'||an[0]=='%')) an = an.substr(1);
        if (!an.empty() && (an[0]=='!'||an[0]=='.')) an = an.substr(1);
        // An INLINE member answers its offset with the type marked, so the read
        // path hands back a view ONTO those bytes instead of dereferencing them
        // as a pointer.
        std::string type = at.empty() ? "int64" : at;
        if (a.inlined) type = "HAS " + type;
        l->fields.push_back({std::move(an), std::move(type), uni ? 0 : off});
        if (!uni) off += w;
    }
    // A layout made before an attribute was added is left in place, not freed:
    // another thread may still be reading it.
    if (ci->ncLayout.p.compare_exchange_strong(have, l, std::memory_order_acq_rel, std::memory_order_acquire))
        return l;
    delete l;
    return have;
}

long long Interpreter::ncFieldOffset(ClassInfo* ci, const std::string& field, std::string& type) {
    for (auto& f : ncLayoutOf(ci)->fields)
        if (f.name == field) { type = f.type; return f.off; }
    return -1;
}
long long Interpreter::ncStructSize(ClassInfo* ci) {
    const bool uni = (ci->repr == "CUnion");
    long long off = 0, maxA = 1;
    for (auto& a : ci->attrs) {
        long long w, align; ncMemberLayout(a, ncResolveTypeAlias(ci, a.type), w, align);
        if (align > maxA) maxA = align;
        if (uni) { if (w > off) off = w; continue; }   // a union is as wide as its widest member
        off = (off + align - 1) / align * align; off += w;
    }
    // A union pads out to its own alignment exactly as a struct does — C's
    // `union { struct { int x, y, z; } t; long n; }` is 16 bytes, not 12, and a
    // struct that embeds one is laid out on that 16. Rakudo answers 12 here;
    // that is MoarVM under-reporting, and copying it would mis-place every field
    // after such a member. (Only reachable at all through `HAS`: a union of
    // scalars is already a multiple of its widest member.)
    return off ? (off + maxA - 1) / maxA * maxA : 0;
}

// Extract a raw native address from any native-pointer value (0 if none).
long long Interpreter::ncRawAddr(const Value& v) {
    if (v.t == VT::Object && v.obj() && v.obj()->attrs.count("__native_ptr")) return v.obj()->attrs.at("__native_ptr").toInt();
    if (v.t == VT::Hash && v.hash()) { auto it = v.hash()->find("addr"); if (it != v.hash()->end()) return it->second.toInt(); }
    // A BYTE-BACKED CArray — one built here rather than handed back by C — has a
    // real address too: its own storage. Answering 0 made `nativecast(Pointer, $c)`
    // a NULL pointer, and the next native call dereferenced it (`strlen` on the
    // result is a segfault, not a wrong answer). The storage is a shared body
    // (CArray.new promotes it), so this is the same buffer every copy of the
    // value sees; the body is retained in a small ring so a Pointer taken from a
    // TEMPORARY CArray outlives the statement that made it.
    // …and a Buf/Blob is byte storage the same way: `nativecast(CArray[uint8],
    // $blob)` must yield a view ONTO the blob (Compress::Zlib::Raw's
    // z_stream.set-input/set-output hand zlib the caller's buffers exactly so),
    // and a 0 here made every such view a NULL pointer — deflate answered
    // Z_STREAM_ERROR with all its counters correctly set.
    if (v.t == VT::Str && (v.hashKind == "CArray" || v.hashKind == "Buf" || v.hashKind == "Blob")) {
        // Promote when it is not already shared. An inline buffer lives INSIDE
        // this Value — which is routinely a temporary copy of the caller's — so
        // its address would dangle the moment the statement ended. Promotion
        // changes the storage, never the string, so the const is honest.
        const_cast<Value&>(v).s.promote();
        if (auto body = v.s.bodyPtr()) {
            static std::mutex m;
            static std::deque<Ref<const StrBody>> retained;
            std::lock_guard<std::mutex> lk(m);
            retained.push_back(body);
            if (retained.size() > 256) retained.pop_front();
            return (long long)(intptr_t)body->str().data();
        }
        return (long long)(intptr_t)v.s.data();
    }
    if (v.t == VT::Int) return v.i;
    return 0;
}

// ---- NativeCall callbacks --------------------------------------------------
// A Raku Callable passed to a native function is marshalled to a C function
// pointer via a fixed pool of trampolines. The C library calls the trampoline
// (with C-ABI integer/pointer args), which routes back into the interpreter.
// Synchronous callbacks (qsort/bsearch, invoked during the native call while the
// GIL is held) work; async ones (stored by C, fired later from another thread)
// are not supported. Pointer/int args reach the callback as Int addresses.
// (g_cbInterp is defined up near the other file-scope interpreter state so the
// constructor can set it.)
static ValueList g_cbSlots;   // slot → Raku Callable (never shrinks)

// RAKUPP_FFI_TRACE=1 — one line per native crossing on stderr. An external
// tracer (lldb, ltrace, dtrace) cannot do this job: it sees C symbols, and the
// interpreter's own C++ calls strlen/malloc constantly, so program-level
// crossings are indistinguishable from internal ones. Only this layer knows
// which call the *Raku* program asked for.
static const bool g_ncTrace = [] {
    const char* e = std::getenv("RAKUPP_FFI_TRACE");
    return e && *e && std::strcmp(e, "0") != 0;
}();

long long Interpreter::runCallback(int slot, long long a0, long long a1, long long a2,
                                  long long a3, long long a4, long long a5) {
    if (slot < 0 || slot >= (int)g_cbSlots.size()) return 0;
    Value cb = g_cbSlots[slot];
    if (cb.t != VT::Code) return 0;
    NcWord raw[6] = {a0, a1, a2, a3, a4, a5};
    size_t arity = (cb.code() && cb.code()->params) ? cb.code()->params->size() : 2;
    if (arity > 6) arity = 6;
    ValueList as;
    for (size_t i = 0; i < arity; i++) {
        std::string pt = (cb.code() && cb.code()->params && i < cb.code()->params->size()) ? (*cb.code()->params)[i].type : "";
        if (pt == "Pointer" || pt.rfind("Pointer[", 0) == 0) as.push_back(ncMakePointer(pt, (void*)(intptr_t)raw[i]));
        else if (pt == "num" || pt == "num64" || pt == "num32") { double d; std::memcpy(&d, &raw[i], 8); as.push_back(Value::number(d)); }
        else as.push_back(Value::integer(raw[i]));
    }
    Value r;
    try { r = callCallable(cb, as); } catch (...) { return 0; }
    return (NcWord)r.toInt();
}

// The trampoline is a plain C function, so the platform's own ABI hands it the
// arguments — but its parameters must be pointer-wide or a 64-bit LPARAM (and
// the LRESULT going back) is truncated on Windows. A WNDPROC is exactly this
// shape, and its lParam carries pointers.
template<int N> static NcWord cbTramp(NcWord a, NcWord b, NcWord c, NcWord d, NcWord e, NcWord f) {
    return g_cbInterp ? g_cbInterp->runCallback(N, a, b, c, d, e, f) : 0;
}
template<int... Is> static void cbFill(void** t, std::integer_sequence<int, Is...>) {
    ((t[Is] = (void*)&cbTramp<Is>), ...);
}
static void* g_cbTable[64];
static bool g_cbTableInit = [] { cbFill(g_cbTable, std::make_integer_sequence<int, 64>{}); return true; }();

static ffi::Type* ncFfiRetType(const std::string& rt); // defined with the call marshalling below

// ---- callbacks, mark two: ffi_closure --------------------------------------
// A Raku Callable handed to C as a function pointer. The closure's signature
// comes from the Callable's own parameter list — declared native types where
// the user wrote them, pointer-sized integers where they didn't — which is
// strictly more than the fixed pool above can express: floats, arbitrary
// arity, and a typed return instead of `long`.
struct NcClosure {
    Value                    fn;        // holds the Callable alive; also the map key's owner
    std::vector<ffi::Type*>  atypes;
    std::vector<std::string> ptypes;    // declared Raku types, for boxing the arguments
    ffi::Type*               rtype = nullptr;
    ffi::Cif                 cif;
    void*                    writable = nullptr;  // ffi_closure_alloc's writable mapping
    void*                    code     = nullptr;  // its executable alias — this is what C gets
};
// One closure per Callable, for the life of the process: a C library that
// stores a callback must keep seeing the same address, and it can fire at any
// later point, so these are deliberately never reclaimed.
static std::map<Callable*, NcClosure*> g_ncClosures;

// A callback parameter's declared Raku type → its libffi type. An undeclared
// parameter is a pointer-sized integer, which is what the old trampoline pool
// always assumed.
static ffi::Type* ncFfiCbParamType(const std::string& pt) {
    const ffi::Lib& F = ffi::lib();
    if (pt.empty()) return F.t_sint64;
    if (pt == "Str" || pt == "Pointer" || pt.rfind("Pointer[", 0) == 0 ||
        pt == "CArray" || pt.rfind("CArray[", 0) == 0) return F.t_pointer;
    bool sgn, isFlt; int w = ncScalarWidth(pt, sgn, isFlt);
    if (w) return ffi::scalar(w, sgn, isFlt);
    return F.t_pointer; // a CStruct/CPointer class
}
// A callback's return type. An undeclared one stays pointer-sized-integer, as
// the old pool's `long` return was — a C caller that wanted `void` simply
// ignores the register.
static ffi::Type* ncFfiCbRetType(const std::string& rt) {
    const ffi::Lib& F = ffi::lib();
    if (rt.empty()) return F.t_sint64;
    if (rt == "void" || rt == "Nil") return F.t_void;
    return ncFfiRetType(rt);
}

static void ncClosureTramp(void*, void* ret, void** args, void* user) {
    if (g_cbInterp) g_cbInterp->runFfiClosure(user, ret, args);
    else if (ret)   std::memset(ret, 0, sizeof(long long));
}

void Interpreter::runFfiClosure(void* user, void* ret, void** args) {
    NcClosure* cl = (NcClosure*)user;
    const ffi::Lib& F = ffi::lib();
    auto zero = [&] { if (ret && cl->rtype != F.t_void) std::memset(ret, 0, sizeof(long long)); };
    if (!onRakuThread()) {
        // The C library stored this callback and is firing it from a thread of
        // its own. There is no interpreter state to run in; returning zero is
        // wrong, but it is a great deal less wrong than running anyway.
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::fputs("NativeCall: a callback fired from a thread Raku does not own — ignored "
                       "(only synchronous callbacks are supported)\n", stderr);
        }
        zero(); return;
    }
    ValueList as;
    for (size_t i = 0; i < cl->atypes.size(); i++) {
        ffi::Type* t = cl->atypes[i];
        const std::string& pt = cl->ptypes[i];
        if (t == F.t_float)       { float x;  std::memcpy(&x, args[i], 4); as.push_back(Value::number(x)); }
        else if (t == F.t_double) { double x; std::memcpy(&x, args[i], 8); as.push_back(Value::number(x)); }
        else if (t == F.t_pointer) {
            void* p = nullptr; std::memcpy(&p, args[i], sizeof p);
            if (pt == "Str")                     as.push_back(p ? Value::str((const char*)p) : Value::any());
            else if (pt.rfind("CArray", 0) == 0) as.push_back(ncMakeLiveCArray(pt, p));
            else                                 as.push_back(ncMakePointer(pt.empty() ? "Pointer" : pt, p));
        }
        else {
            long long x = 0;
            size_t n = t->size > 8 ? 8 : t->size;
            std::memcpy(&x, args[i], n);
            if (n < 8 && (t->type == ffi::T_SINT8 || t->type == ffi::T_SINT16 || t->type == ffi::T_SINT32)) {
                unsigned long long mask = (1ULL << (n * 8)) - 1;          // sign-extend to the full width
                unsigned long long u = (unsigned long long)x & mask;
                x = (u & (1ULL << (n * 8 - 1))) ? (long long)(u | ~mask) : (long long)u;
            }
            as.push_back(Value::integer(x));
        }
    }
    Value r;
    try { r = callCallable(cl->fn, as); } catch (...) { zero(); return; }
    if (!ret || cl->rtype == F.t_void) return;
    if (cl->rtype == F.t_float)        { float x = (float)r.toNum();  std::memcpy(ret, &x, 4); }
    else if (cl->rtype == F.t_double)  { double x = r.toNum();        std::memcpy(ret, &x, 8); }
    else if (cl->rtype == F.t_pointer) { void* p = (void*)(intptr_t)ncRawAddr(r); std::memcpy(ret, &p, sizeof p); }
    else { // libffi's closure contract: integers narrower than ffi_arg are written full-width
        long long x = r.toInt(); std::memcpy(ret, &x, sizeof(long long));
    }
}

// A Raku Callable passed where C wants a function pointer.
// `declSig` is the signature the NATIVE routine declared for its callback
// parameter — `&callback (SSL, CArray[CArray[uint8]], CArray[uint8], CArray[uint8],
// uint8, Pointer --> int32)` in IO::Socket::Async::SSL — and it is what C will
// actually pass. The Raku block handed over is usually UNTYPED (`-> $ssl, $out,
// $outlen, $in, $inlen, $arg { … }`), and reading its `uint8` as a 64-bit
// integer picked up whatever the register held above the byte: the ALPN
// selector looped over a length in the billions and the handshake never ended.
// A type the block wrote itself still wins.
static void* ncCallbackPtr(const Value& v, const std::vector<Param>* declSig = nullptr) {
    const ffi::Lib& F = ffi::lib();
    if (F.ok && F.closure_alloc && F.prep_closure_loc && v.code()) {
        auto it = g_ncClosures.find(v.code());
        if (it != g_ncClosures.end()) return it->second->code;
        auto* cl = new NcClosure();
        cl->fn = v;
        const std::vector<Param>* ps = v.code()->params;
        // No signature at all: assume the two-argument comparator shape, which
        // is what the fixed pool defaulted to and covers qsort/bsearch.
        size_t arity = declSig && !declSig->empty() ? declSig->size() : ps ? ps->size() : 2;
        for (size_t i = 0; i < arity; i++) {
            std::string pt = (ps && i < ps->size()) ? (*ps)[i].type : "";
            if (pt.empty() && declSig && i < declSig->size()) pt = (*declSig)[i].type;
            cl->ptypes.push_back(pt);
            cl->atypes.push_back(ncFfiCbParamType(pt));
        }
        cl->rtype = ncFfiCbRetType(retTypeName(v.code()->retType));
        cl->writable = F.closure_alloc(512, &cl->code);
        if (cl->writable && cl->code && cl->rtype &&
            F.prep(cl->cif.buf, F.abi, (unsigned)cl->atypes.size(), cl->rtype,
                   cl->atypes.empty() ? nullptr : cl->atypes.data()) == 0 &&
            F.prep_closure_loc(cl->writable, cl->cif.buf, ncClosureTramp, cl, cl->code) == 0) {
            g_ncClosures[v.code()] = cl;
            return cl->code;
        }
        // A W^X policy (hardened runtime, SELinux) can refuse the executable
        // mapping. Fall through to the fixed pool rather than fail the call.
        if (cl->writable && F.closure_free) F.closure_free(cl->writable);
        delete cl;
    }
    for (size_t s = 0; s < g_cbSlots.size(); s++)
        if (g_cbSlots[s].code() == v.code()) return g_cbTable[s];
    if (g_cbSlots.size() >= 64)
        // Handing C a null function pointer is not a degraded service, it is a
        // trap: the library calls it, and the program crashes or hangs a long
        // way from here with nothing to point at. Say which limit was hit.
        throw RakuError{Value::typeObj("X::NYI"),
            "NativeCall: more than 64 distinct callbacks needs libffi, which is not available ("
            + F.why + ")"};
    g_cbSlots.push_back(v);
    return g_cbTable[g_cbSlots.size() - 1];
}

// Declared Raku return type → the libffi type of the C return value. Null when
// libffi is unavailable (the caller then takes the fixed-prototype path).
static ffi::Type* ncFfiRetType(const std::string& rt) {
    const ffi::Lib& F = ffi::lib();
    if (!F.ok) return nullptr;
    if (rt.empty() || rt == "void" || rt == "Nil")     return F.t_void;
    if (rt == "Str")                                   return F.t_pointer;
    if (rt == "Pointer" || rt.rfind("Pointer[", 0) == 0 ||
        rt == "CArray"  || rt.rfind("CArray[", 0)  == 0) return F.t_pointer;
    bool sgn, isFlt; int w = ncScalarWidth(rt, sgn, isFlt);
    if (w) return ffi::scalar(w, sgn, isFlt);
    return F.t_pointer; // a CStruct/CPointer class, or an opaque handle
}

// NativeCall (`is native`): resolve the C symbol via dlsym and call it.
// Arguments and the return are classified by their *declared* type and passed
// through libffi (src/Ffi.h — loaded at runtime, never linked). That gives
// exact argument widths, real `num32`, and any number of arguments.
//
// Where no libffi can be loaded the call falls back to one over-wide fixed
// prototype, which places arguments correctly only while they all fit the
// integer and float register banks; anything outside that throws rather than
// calling wrongly. `is rw` out-params are marshalled as `T*` with copy-back,
// and CStruct/CPointer/CArray/Pointer returns are boxed as live handles.
// Still unsupported on both paths: C structs passed or returned BY VALUE.
Value Interpreter::callNative(Callable& c, ValueList& args, const std::vector<ExprPtr>* rwArgs, size_t rwArgOff) {
    // --sandbox: native code can do anything the process can (src/Sandbox.h)
    if (g_sandboxChecks) sandboxRefuse(*this, (c.name.empty() ? std::string("a routine") : c.name) + " (is native)",
                                   SandboxCap::Ffi);
    // Resolve the symbol ONCE per Callable and cache the function pointer. This
    // used to run on every call, and the dlopen candidate loop is the expensive
    // part: each candidate that does NOT match (e.g. "sqlite3" before
    // "libsqlite3.dylib" hits) makes dyld scan its whole search path, which came
    // to a flat ~67 µs per crossing regardless of signature — 131–258× Rakudo
    // (cognates finding 14: a 250k-crossing import took 197 s vs Rakudo's 5.7 s).
    // The cache lives on the Callable, which every copy of the sub's Value
    // shares through its shared_ptr. Benign race: two threads resolving the
    // same sub store the same pointer. dlclose is never called, so the pointer
    // stays valid for the life of the process.
    void* sym = c.nativeSymCache;
    if (!sym) {
        void* handle = RTLD_DEFAULT;
        // `is native(&sub)`: call the sub now to get the library path (a full
        // path or bare name). Resolved once here, like Rakudo.
        std::string lib = c.nativeLib;
        if (lib.empty() && !c.nativeLibSub.empty()) {
            // the provider sub lives in the DECLARING module — resolve it in
            // the sub's own closure first; the caller's env only as a fallback
            Value* f = nullptr;
            if (c.closure) f = c.closure->find("&" + c.nativeLibSub);
            if (!f) f = tctx_.cur->find("&" + c.nativeLibSub);
            if (f) {
                ValueList none; Value r = callCallable(*f, none);
                lib = r.toStr();
            }
        }
        if (lib.empty() && c.nativeLibExpr) {
            // Declaration-time eval failed or answered nothing — retry in the
            // sub's OWN closure (its module's constants live there, and the
            // caller's env can hold a SAME-NAMED constant from another module:
            // DBDish::SQLite's natives dlopen'd Pg's `LIB` once both drivers
            // were up). An expr that evaluates to an UNDEFINED value is not a
            // failure: `constant LIB = … !! Str` on non-Windows MEANS "no
            // library — bind from what the process already loaded".
            auto savedEnv = tctx_.cur;
            if (c.closure) tctx_.cur = c.closure;
            try {
                Value r = eval(const_cast<Expr*>(c.nativeLibExpr));
                if (r.t == VT::Code) { ValueList none; r = callCallable(r, none); }
                if (isDefined(r)) lib = ncLibNameOf(r);
            } catch (RakuError&) {}
            tctx_.cur = savedEnv;
        }
        if (!lib.empty()) handle = dlopenLib(lib); // name as-is, then platform-decorated forms
        if (c.nativeSymExpr) {
            // A computed `is symbol(…)` the declaration was too early to evaluate:
            // now the library is loaded, so the version probe it asks can answer.
            // Evaluate it in the sub's OWN scope, not the caller's — the helper
            // that computes the name is typically a module-private `my sub`
            // (OpenSSL::Stack's real_symbol, which spells `sk_num` as
            // `OPENSSL_sk_num` from OpenSSL 1.1 on) and is invisible from
            // wherever the call happens to be.
            auto savedEnv = tctx_.cur;
            if (c.closure) tctx_.cur = c.closure;
            try {
                Value r = eval(const_cast<Expr*>(c.nativeSymExpr));
                if (r.t == VT::Code) { ValueList none; r = callCallable(r, none); }
                if (isDefined(r) && !r.toStr().empty()) c.nativeSym = r.toStr();
            } catch (RakuError&) {}
            tctx_.cur = savedEnv;
            c.nativeSymExpr = nullptr; // once, not per call
        }
        sym = dlsym(handle, c.nativeSym.c_str());
        if (!sym) {
            // Some libraries expose a renamed symbol behind a compat macro the header
            // resolves at C compile time but that isn't a real exported symbol (so a
            // module's `is native` on the old name can't dlsym it). Fall back to the
            // known aliases — e.g. OpenSSL 3.x's SSL_get_peer_certificate is now only
            // SSL_get1_peer_certificate.
            static const std::map<std::string, std::string> aliases = {
                {"SSL_get_peer_certificate", "SSL_get1_peer_certificate"},
                // The same rename read the other way. A dist picks the spelling
                // from a VERSION probe (OpenSSL::Version::version_num, asked of
                // whichever libcrypto answered first) and then binds against
                // whichever libssl its own name resolved to — the two need not
                // be the same OpenSSL, and a 1.1 or LibreSSL libssl carries only
                // the old name. Both spellings are one function, and both hand
                // back a reference the caller frees, so either may stand in for
                // the other. (SSL_get0_peer_certificate must NOT: it does not
                // take the reference, and the caller's X509_free would over-free.)
                {"SSL_get1_peer_certificate", "SSL_get_peer_certificate"},
            };
            auto it = aliases.find(c.nativeSym);
            if (it != aliases.end()) sym = dlsym(handle, it->second.c_str());
        }
        // Rakudo's exact wording, deliberately: this text is a de-facto INTERFACE.
        // DBIish pattern-matches `Cannot locate symbol '…' in native library '…'`
        // (and the library form below) to turn a missing client library into
        // X::DBIish::LibraryMissing, which is what makes its suite SKIP a driver
        // whose library is absent instead of dying.
        if (!sym) throw RakuError{Value::typeObj("X::AdHoc"),
            "Cannot locate symbol '" + c.nativeSym + "' in native library '" + lib + "'"};
        c.nativeSymCache = sym;
    }

    const std::vector<Param>* prm = c.params;
    // A NativeCall METHOD prepends its invocant to `args`. If the signature
    // SPELLS the invocant (`method mysql_query(MYSQL:D: Str $sql)`) the two line
    // up already; if it does not (`method PQescapeByteaConn(Buf, size_t, size_t
    // is rw)`, which is how DBDish::Pg writes every one) each argument would be
    // typed by the PREVIOUS parameter — the `is rw` out-param landed a slot
    // early and libpq wrote the length through the buffer pointer. Shift the
    // parameter view by one instead, and with it the rwArgs mapping.
    size_t pOff = 0;   // how many leading args have no parameter of their own
    if (rwArgOff == 1 && prm) {
        if (!prm->empty() && (*prm)[0].invocant) rwArgOff = 0; // spelled: args and params already line up
        else pOff = 1;                                         // unspelled: args[0] is the invocant
    }
    std::vector<std::string> keep; keep.reserve(args.size()); // keep Str buffers alive across the call
    // is-rw out-params: backing slots (stable addresses via deque) + copy-back list
    std::deque<NcWord> rwI;
    std::deque<double> rwD;
    struct RwBack { size_t arg; const NcWord* i; const double* d; std::string ptrType;
                    std::shared_ptr<ClassInfo> cls; };
    std::vector<RwBack> rwbacks;

    // A declared type name that stands for a NativeCall class (CStruct/CPointer/
    // CUnion) → its ClassInfo. Used both for `is rw` out-params of a CPointer
    // class and for boxing such a class as the return value; the two used to
    // disagree, which is why it is one lambda.
    auto ncClass = [&](const std::string& tn) -> std::shared_ptr<ClassInfo> {
        if (tn.empty()) return nullptr;
        if (!ascii::isupper((unsigned char)tn[0]) && tn.find("::") == std::string::npos) return nullptr;
        auto it = classes_.find(tn);
        if (it != classes_.end()) return it->second;
        // The DECLARING module's own scope decides what a short name means —
        // and it has to be asked BEFORE any suffix match, because two classes
        // can end in the same name: `SQLite` inside DBDish::SQLite::Native is
        // the CPointer handle, while `DBDish::SQLite` is the driver class, and
        // sorting order handed the driver to sqlite3_open's out-param.
        // It also resolves `--> PwStruct` where PwStruct is a CONSTANT aliasing
        // the real class (P5getpwnam picks its per-kernel struct at load time).
        Value* alias = c.closure ? c.closure->find(tn) : nullptr;
        if (!alias && tctx_.cur) alias = tctx_.cur->find(tn);
        if (alias && alias->t == VT::Type) {
            auto it2 = classes_.find(alias->s);
            if (it2 != classes_.end()) return it2->second;
        }
        for (auto& kv : classes_) { // a qualified name ending in ::tn
            const std::string& nm = kv.first;
            if (nm.size() > tn.size() + 2 && nm.compare(nm.size() - tn.size() - 2, tn.size() + 2, "::" + tn) == 0)
                return kv.second;
        }
        return nullptr;
    };
    struct CABack { size_t arg; size_t keep; }; // byte-backed CArray → copy bytes back
    std::vector<CABack> cabacks;

    // ONE marshalling pass feeds both call tails. Every argument becomes a slot
    // holding the raw bytes at its DECLARED width (what libffi reads) plus the
    // value the fixed-prototype fallback would push (always widened to
    // long/double). `needFfi` names the first thing the fallback cannot
    // express, so that without libffi we throw instead of computing garbage.
    const ffi::Lib& F = ffi::lib();
    struct NcSlot {
        alignas(8) unsigned char raw[16] = {};
        ffi::Type* type = nullptr;
        NcWord fbInt   = 0;
        double fbNum   = 0;
        bool   fbFloat = false;
        bool   fbPtr   = false;   // for the trace: render as an address
    };
    std::vector<NcSlot> slots(args.size());
    std::string needFfi;

    // A slurpy in a native signature marks where C's `...` begins:
    //     sub snprintf(Blob, size_t, Str, *@args --> int32) is native {*}
    // Everything before it is fixed; everything after is a variadic argument,
    // typed from its runtime value under C's default argument promotions (which
    // is what the marshalling loop below already does for an undeclared
    // parameter). Rakudo has no variadic NativeCall, so this spelling is a
    // Raku++ extension — but without it a variadic call is silently wrong on
    // every ABI that passes `...` arguments on the stack, Apple ARM64 included.
    int nfixed = -1;
    if (prm) for (size_t i = 0; i < prm->size(); i++) if ((*prm)[i].slurpy) { nfixed = (int)(i + pOff); break; }
    const bool variadic = nfixed >= 0;
    if (variadic) needFfi = "a variadic native call";

    auto putPtr = [&F](NcSlot& s, const void* p) {
        void* q = const_cast<void*>(p);
        std::memcpy(s.raw, &q, sizeof q);
        s.type  = F.t_pointer;
        s.fbInt = (NcWord)(intptr_t)q;
        s.fbPtr = true;
    };
    auto putInt = [](NcSlot& s, long long v, int w, bool sgn) {
        if (w == 0) { w = 8; sgn = true; }
        switch (w) { // narrow to the declared width — libffi reads exactly `w` bytes
            case 1: { int8_t  x = (int8_t)v;  std::memcpy(s.raw, &x, 1); break; }
            case 2: { int16_t x = (int16_t)v; std::memcpy(s.raw, &x, 2); break; }
            case 4: { int32_t x = (int32_t)v; std::memcpy(s.raw, &x, 4); break; }
            default:{ std::memcpy(s.raw, &v, 8); break; }
        }
        s.type  = ffi::scalar(w, sgn, false);
        s.fbInt = (NcWord)v;
    };
    auto putNum = [&F, &needFfi](NcSlot& s, double v, int w) {
        if (w == 4) { // a real 32-bit float: the fallback would pass a double
            float x = (float)v; std::memcpy(s.raw, &x, 4); s.type = F.t_float;
            if (needFfi.empty()) needFfi = "a num32 argument";
        }
        else { std::memcpy(s.raw, &v, 8); s.type = F.t_double; }
        s.fbNum = v; s.fbFloat = true;
    };

    for (size_t i = 0; i < args.size(); i++) {
        Value& v = args[i];
        // A class that COMPOSES Blob or Buf (`unit class PDF::IO::Blob does
        // Blob[uint8]`, which is how PDF carries every encoded stream) is an
        // OBJECT backed by one. Marshal the bytes it holds: without this it fell
        // past every buffer arm below to the integer one, which passed the
        // object's own address as the pointer — libz dereferenced that and the
        // process died inside `inflate`.
        if (v.t == VT::Object && v.obj() && v.obj()->hasBoxed &&
            v.obj()->boxed().t == VT::Str &&
            (v.obj()->boxed().hashKind == "Buf" || v.obj()->boxed().hashKind == "Blob" ||
             v.obj()->boxed().hashKind == "utf8"))
            { Value unboxed = v.obj()->boxed(); v = std::move(unboxed); }
        NcSlot& s = slots[i];
        const Param* p = (prm && i >= pOff && i - pOff < prm->size()) ? &(*prm)[i - pOff] : nullptr;
        std::string pt = p ? p->type : "";
        // …through any `constant` alias standing for a native type. Without this
        // `FT_Int is rw` was not a native scalar at all, so the out-parameter was
        // never written back and Font::FreeType read an empty version string.
        if (!pt.empty()) {
            bool asgn, aflt;
            if (!ncScalarWidth(pt, asgn, aflt)) {
                Value* av = c.closure ? c.closure->find(pt) : nullptr;
                if (av && av->t == VT::Type && av->s != pt) {
                    bool bsgn, bflt;
                    if (ncScalarWidth(av->s.str(), bsgn, bflt)) pt = av->s.str();
                }
            }
        }
        bool sgn, isFlt; int w = ncScalarWidth(pt, sgn, isFlt);
        bool fp = isFlt || (pt.empty() && (v.t == VT::Num || v.t == VT::Rat));
        bool rwPtr = p && p->isRw && (pt == "Pointer" || pt.rfind("Pointer[", 0) == 0);
        // A CPointer-repr CLASS declared `is rw` is the same sqlite3** shape,
        // only spelled with a name: `sqlite3_open(Str, SQLite $handle is rw)`,
        // `sqlite3_prepare_v2(…, STMT $sth is rw, …)`, all of Oracle's OCI
        // handle-getters. Without this it fell to the object branch below and
        // passed the NULL the fresh handle holds, so sqlite3_open saw ppDb == 0
        // and answered SQLITE_MISUSE — every DBDish::SQLite connection failed.
        std::shared_ptr<ClassInfo> rwCls;
        if (p && p->isRw && !rwPtr && !w) {
            auto ci = ncClass(pt);
            if (ci && ci->repr == "CPointer") rwCls = ci;
        }
        if (rwPtr || rwCls) {
            // `Pointer is rw` out-param (sqlite3_open's sqlite3** shape): the C
            // function wants a place to WRITE a pointer, so pass the address of a
            // slot holding the current value — not the value itself, which for a
            // fresh Pointer is NULL and reads as "ppDb was NULL" (SQLITE_MISUSE).
            // Copy-back rebuilds a live Pointer from what the callee stored.
            NcWord cur = 0;
            if (v.t == VT::Hash && v.hash() && v.hash()->count("addr")) cur = (NcWord)(*v.hash())["addr"].toInt();
            else if (v.t == VT::Object && v.obj() && v.obj()->attrs.count("__native_ptr"))
                cur = (NcWord)v.obj()->attrs["__native_ptr"].toInt();
            rwI.push_back(cur); putPtr(s, &rwI.back());
            // The ARGUMENT's own element type outlives the call. A signature
            // says `Pointer is rw` — bare, because the C side only wants somewhere
            // to put a pointer — while the caller passed a `Pointer[FT_Library]`,
            // and it is the caller who knows what is on the other end. Rebuilding
            // from the DECLARED type handed back a plain Pointer, so the very
            // next `.deref` read raw machine words and answered an Int: that is
            // where Font::FreeType (and the thirteen dists behind it) stopped.
            std::string backType = rwCls ? std::string() : pt;
            if (!rwCls && (backType.empty() || backType == "Pointer") &&
                v.t == VT::Hash && v.hash() && v.hashKind == "Pointer") {
                auto ofIt = v.hash()->find("of");
                if (ofIt != v.hash()->end() && !ofIt->second.toStr().empty())
                    backType = "Pointer[" + ofIt->second.toStr() + "]";
            }
            rwbacks.push_back({i, &rwI.back(), nullptr, backType, rwCls});
        }
        else if (p && p->isRw && w) { // `is rw` scalar → pass a pointer to a backing slot
            if (isFlt) { rwD.push_back(v.toNum()); putPtr(s, &rwD.back()); rwbacks.push_back({i, nullptr, &rwD.back(), "", nullptr}); }
            else       { rwI.push_back(v.toInt()); putPtr(s, &rwI.back()); rwbacks.push_back({i, &rwI.back(), nullptr, "", nullptr}); }
        }
        else if (v.t == VT::Str && v.hashKind == "CArray") { keep.push_back(v.s); putPtr(s, keep.back().data()); cabacks.push_back({i, keep.size() - 1}); }
        else if (v.t == VT::Str && (v.hashKind == "Buf" || v.hashKind == "Blob" || v.hashKind == "utf8")) {
            // A Buf/blob8 is a mutable native buffer: pass its bytes and copy back
            // after the call (BIO_read/SSL_read/recv fill it in place).
            //
            // An IMMUTABLE buffer instead passes the Blob's OWN storage, because
            // its pointer has to outlive the CALL: some C APIs RETAIN it rather
            // than reading it while we are inside. BIO_new_mem_buf is the
            // canonical one — it deliberately does not copy — so a per-call
            // temporary left the BIO pointing at freed memory, and by the time
            // PEM_read_bio_RSAPrivateKey looked, it read garbage: OpenSSL
            // answered `DECODER routines::unsupported`, RSAKey.new stored the
            // resulting null (`defined(0)` is True, so the module's own guard
            // waved it through) and the next call segfaulted in RSA_size.
            // A promoted CowStr body is shared with every copy of the value and
            // lives as long as the Raku object does, which is exactly the
            // lifetime Rakudo gives the callee — and it costs no copy at all.
            const StrBody* sb = (v.hashKind == "Buf") ? nullptr : v.s.body();
            if (sb) putPtr(s, (void*)sb->str().data());
            else {
                // Short buffers are held inline, so there is no shared body to
                // point at and the copy is all we have. A retained pointer to a
                // buffer under CowStr's promotion threshold is the one case this
                // still gets wrong; fixing it means giving blob-ish values a
                // shared body at construction, in all 22 places that make one.
                keep.push_back(v.s); putPtr(s, keep.back().data());
                if (v.hashKind == "Buf") cabacks.push_back({i, keep.size() - 1});
            }
        }
        else if (v.t == VT::Str || (v.t == VT::Hash && v.hashKind == "IO")) { keep.push_back(v.toStr()); putPtr(s, keep.back().c_str()); }
        else if (v.t == VT::Object && v.obj() && v.obj()->attrs.count("__native_ptr")) putPtr(s, (void*)(intptr_t)v.obj()->attrs["__native_ptr"].toInt());
        else if (v.t == VT::Hash && (v.hashKind == "Pointer" || v.hashKind == "CArray") && v.hash()->count("addr"))
            putPtr(s, (void*)(intptr_t)(*v.hash())["addr"].toInt()); // live Pointer / CArray handle
        else if (v.t == VT::Code) putPtr(s, ncCallbackPtr(v, p ? p->subSig.get() : nullptr)); // Raku callback → C function pointer
        else if (pt == "Str" && v.t != VT::Any && v.t != VT::Type && v.t != VT::Nil) {
            // Declared `Str`, given a defined non-Str — a `<7 8 9>` word-list
            // element is an Int here, where Rakudo's IntStr allomorph still
            // IS-A Str. Marshal its string form: the old fall-through passed
            // the raw integer as the char*, and the callee's strlen segfaulted.
            // Nil is NOT that case: it binds to a `Str` parameter as the type
            // object, and the type object is NULL. `ERR_error_string($e, Nil)`
            // asks OpenSSL for its static buffer that way; stringifying Nil to
            // "" handed it a one-byte string to write 256 bytes into, and the
            // heap corruption surfaced as an intermittent crash in OpenSSL's
            // own test suite (10-client-ca-file), two runs in three.
            keep.push_back(v.toStr()); putPtr(s, keep.back().c_str());
        }
        else if (fp) putNum(s, v.toNum(), w == 4 ? 4 : 8);
        else         putInt(s, v.toInt(), w, sgn);
    }

    // The declared return type may be a CONSTANT aliasing a native one —
    // `constant void-ptr = Pointer[void];` and then `--> void-ptr`, which is the
    // spelling issue #57 reported and the reason the alias is declared at all.
    // Resolve it ONCE here, so the libffi return type, the boxing and the width
    // truncation below all see the type the constant names: unresolved, the name
    // matched no arm and `malloc` handed back a bare Int instead of a Pointer.
    // (`--> PwStruct` already resolved, but only in the class arm, via ncClass.)
    std::string rtAlias;
    if (!c.retType.empty() && c.closure && !ncKnownRetSpelling(retTypeName(c.retType)))
        if (Value* cv = c.closure->find(retTypeName(c.retType)))
            if (cv->t == VT::Type && !cv->s.empty())
                rtAlias = (!cv->ofType().empty() && cv->s.find('[') == std::string::npos)
                        ? cv->s.str() + "[" + std::string(cv->ofType()) + "]" : cv->s.str();
    const std::string rt = rtAlias.empty() ? retTypeName(c.retType) : rtAlias;
    bool retFP  = ncIsFloatType(rt);
    bool retF32 = (rt == "num32");
    if (retF32 && needFfi.empty()) needFfi = "a num32 return value";
    ffi::Type* rtype = ncFfiRetType(rt);
    NcWord ri = 0; double rd = 0;

    bool useFfi = F.ok && rtype;
    if (useFfi) for (auto& s : slots) if (!s.type) { useFfi = false; break; }

    if (useFfi) {
        // Stack-first: a heap allocation costs about as much as the C call, and
        // native signatures are short. Only a freakishly wide one pays for one.
        const size_t na = slots.size();
        ffi::Type* atStack[16]; void* avStack[16];
        std::vector<ffi::Type*> atHeap; std::vector<void*> avHeap;
        ffi::Type** atypes = atStack; void** avalues = avStack;
        if (na > 16) {
            atHeap.resize(na); avHeap.resize(na);
            atypes = atHeap.data(); avalues = avHeap.data();
        }
        for (size_t k = 0; k < na; k++) { atypes[k] = slots[k].type; avalues[k] = slots[k].raw; }
        // Prepare the cif ONCE per signature and hang it off the Callable, the
        // same way the resolved symbol is cached: ffi_prep_cif costs about as
        // much as the call itself, so per-call preparation shows up as a flat
        // ~20% on a native-call-heavy loop. A sub whose parameters are all
        // declared always presents the same signature, so the first call's cif
        // serves every later one; anything else (untyped parameters typed from
        // the runtime value) falls back to a stack cif.
        unsigned char* cifp = nullptr;
        if (variadic && !F.prep_var)
            throw RakuError{Value::typeObj("X::NYI"),
                "NativeCall: a variadic call needs ffi_prep_cif_var, which " + F.path + " does not export"};
        // Preparing a cif is `prep` for a fixed signature and `prep_var` for a
        // variadic one — the two are NOT interchangeable: an ABI that passes
        // `...` arguments differently (Apple ARM64) only learns about it here.
        auto prepInto = [&](unsigned char* buf, ffi::Type** at) {
            return variadic ? F.prep_var(buf, F.abi, (unsigned)nfixed, (unsigned)na, rtype, na ? at : nullptr)
                            : F.prep(buf, F.abi, (unsigned)na, rtype, na ? at : nullptr);
        };
        NcCif* cached = (NcCif*)c.nativeCifCache.p;
        if (cached && cached->rtype == rtype && cached->variadic == variadic &&
            cached->nfixed == (unsigned)(variadic ? nfixed : 0) &&
            cached->atypes.size() == na &&
            std::equal(cached->atypes.begin(), cached->atypes.end(), atypes))
            cifp = cached->cif.buf;
        if (!cifp && !c.nativeCifCache.p) {
            auto* nc = new NcCif();
            nc->atypes.assign(atypes, atypes + na); nc->rtype = rtype;
            nc->variadic = variadic; nc->nfixed = (unsigned)(variadic ? nfixed : 0);
            if (prepInto(nc->cif.buf, nc->atypes.data()) == 0) {
                c.nativeCifCache.p = nc;   // published once; a racing thread stores the same shape
                cifp = nc->cif.buf;
            }
            else delete nc;
        }
        ffi::Cif local;   // only touched on a cache miss — its 512 zeroed bytes
        if (!cifp) {      // are not worth memsetting on every crossing
            if (prepInto(local.buf, atypes) != 0)
                throw RakuError{Value::typeObj("X::NYI"),
                    "NativeCall: libffi cannot describe the signature of '" + c.nativeSym + "'"};
            cifp = local.buf;
        }
        // The return buffer must be at least ffi_arg wide: libffi widens any
        // integer return narrower than that before storing it.
        alignas(16) unsigned char rbuf[32] = {};
        F.call(cifp, (void (*)(void))sym, rbuf, na ? avalues : nullptr);
        if (retFP) {
            if (retF32) { float x; std::memcpy(&x, rbuf, 4); rd = x; }
            else        std::memcpy(&rd, rbuf, 8);
        }
        else if (rtype != F.t_void) {
            unsigned long long u = 0;
            std::memcpy(&u, rbuf, sizeof(void*) >= 8 ? 8 : 4);
            ri = (NcWord)u;
        }
    }
    else {
        // No libffi: the fixed over-wide prototype. It cannot place arguments
        // that are not register-sized, so say so rather than call wrongly.
        if (!needFfi.empty())
            throw RakuError{Value::typeObj("X::NYI"),
                "NativeCall: " + needFfi + " needs libffi, which is not available (" + F.why + ")"};
        std::vector<NcWord> g;  // integer/pointer args, in declaration order
        std::vector<double> f;  // float args, in declaration order
        for (auto& s : slots) { if (s.fbFloat) f.push_back(s.fbNum); else g.push_back(s.fbInt); }
        if (g.size() > 16 || f.size() > 8)
            throw RakuError{Value::typeObj("X::NYI"),
                "NativeCall: too many register arguments (max 16 integer + 8 float) — more needs libffi, which is not available (" + F.why + ")"};
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
        // Win64 assigns argument registers by POSITION, not by bank: a double is
        // read from XMM2 only if it is the THIRD argument. This prototype can
        // only place floats after its integers, so a mixed or float signature
        // would be called with the value in the wrong place — a wrong answer,
        // silently. SysV-AMD64 and AArch64 have independent banks and are fine.
        if (!f.empty())
            throw RakuError{Value::typeObj("X::NYI"),
                "NativeCall: a floating-point argument on Windows x64 needs libffi, which is not available (" + F.why + ")"};
#endif
        NcWord G[16] = {0}; for (size_t k = 0; k < g.size(); k++) G[k] = g[k];
        double D[8]  = {0}; for (size_t k = 0; k < f.size(); k++) D[k] = f[k];
        if (retFP) rd = ((NcFnD)sym)(G[0],G[1],G[2],G[3],G[4],G[5],G[6],G[7],
                                     G[8],G[9],G[10],G[11],G[12],G[13],G[14],G[15],
                                     D[0],D[1],D[2],D[3],D[4],D[5],D[6],D[7]);
        else       ri = ((NcFnI)sym)(G[0],G[1],G[2],G[3],G[4],G[5],G[6],G[7],
                                     G[8],G[9],G[10],G[11],G[12],G[13],G[14],G[15],
                                     D[0],D[1],D[2],D[3],D[4],D[5],D[6],D[7]);
    }

    if (g_ncTrace) {
        // Rendered from what ACTUALLY crossed — the marshalled slots and the raw
        // return — not from the Raku values, so a marshalling bug shows up here
        // rather than being papered over by re-printing the input.
        static const bool banner = [] {
            std::fputs(("[ffi] backend: " + ffi::describe() + "\n").c_str(), stderr); return true;
        }();
        (void)banner;
        auto esc = [](std::string t) {            // a "%d\n" format must not break the line
            std::string o;
            for (unsigned char ch : t) {
                if (ch == '\n') o += "\\n";
                else if (ch == '\t') o += "\\t";
                else if (ch == '\r') o += "\\r";
                else if (ch == '"')  o += "\\\"";
                else if (ch < 0x20)  { char h[8]; std::snprintf(h, sizeof h, "\\x%02x", ch); o += h; }
                else o += (char)ch;
            }
            return o;
        };
        std::string line = "[ffi] ";
        if (!c.nativeLib.empty()) line += c.nativeLib + ":";
        line += c.nativeSym + "(";
        for (size_t i = 0; i < slots.size(); i++) {
            if (i) line += ", ";
            const Value& v = args[i];
            char b[32];
            if (v.t == VT::Str && v.hashKind.empty())
                line += "\"" + esc(v.s.size() > 24 ? v.s.substr(0, 24) + "..." : v.s) + "\"";
            else if (slots[i].fbFloat) { cnum::snprintf(b, sizeof b, "%g", slots[i].fbNum); line += b; }
            else if (slots[i].fbPtr)   { std::snprintf(b, sizeof b, "0x%llx", (unsigned long long)slots[i].fbInt); line += b; }
            else                       line += std::to_string(slots[i].fbInt);
        }
        line += ") -> ";
        char rb[32];
        if (rt.empty() || rt == "void" || rt == "Nil") line += "void";
        else if (retFP)    { cnum::snprintf(rb, sizeof rb, "%g", rd); line += rb; }
        else if (rt == "Str") line += ri ? "\"" + esc(std::string((const char*)(intptr_t)ri).substr(0, 24)) + "\"" : "Str";
        // F.ok matters: without libffi both sides are null and everything would
        // print as an address.
        else if (F.ok && rtype == F.t_pointer) { std::snprintf(rb, sizeof rb, "0x%llx", (unsigned long long)ri); line += rb; }
        else               line += std::to_string(ri);
        if (!useFfi) line += "   [no libffi]";
        line += "\n";
        std::fputs(line.c_str(), stderr);
    }

    // is-rw copy-back: write each out-param's slot to the caller's lvalue
    for (auto& rb : rwbacks) {
        if (!rwArgs || rb.arg < rwArgOff || rb.arg - rwArgOff >= rwArgs->size()) continue;
        Value nv;
        if (rb.cls) {
            // a CPointer class out-param: box what the callee stored as an
            // object of that class, and NULL as the type object — the same
            // shape a CPointer RETURN gets, so `with $handle` reads right.
            if (*rb.i == 0) nv = Value::typeObj(rb.cls->name);
            else {
                nv.t = VT::Object; nv.setObj(makePayload<ObjectData>());
                nv.obj()->cls = rb.cls;
                nv.obj()->attrs["__native_ptr"] = Value::integer(*rb.i);
            }
        }
        else nv = !rb.ptrType.empty() ? ncMakePointer(rb.ptrType, (void*)(intptr_t)*rb.i)
                 : rb.i                ? Value::integer(*rb.i)
                                       : Value::number(*rb.d);
        try {
            if (Value* lv = lvalue((*rwArgs)[rb.arg - rwArgOff].get())) {
                int nb = lv->natBits; bool ns = lv->natSigned;
                *lv = nv;
                if (nb && rb.ptrType.empty() && !rb.cls) wrapNative(*lv, nb, ns);
            }
        } catch (RakuError&) {}
    }
    // CArray copy-back: a native function may mutate the array in place (qsort,
    // fill buffers), so write the possibly-changed bytes back to the caller.
    for (auto& cb : cabacks) {
        if (!rwArgs || cb.arg < rwArgOff || cb.arg - rwArgOff >= rwArgs->size()) continue;
        try { if (Value* lv = lvalue((*rwArgs)[cb.arg - rwArgOff].get()))
                  if (lv->t == VT::Str && (lv->hashKind == "CArray" || lv->hashKind == "Buf")) {
                      // IN PLACE while the length is unchanged (which it is — C
                      // wrote into a same-sized copy). The buffer's ADDRESS is
                      // what a Pointer taken from this array holds, and assigning
                      // the string forks it: a short one lands back in inline
                      // storage, so the next `nativecast(Pointer, $c)` pointed at
                      // a temporary instead of the array.
                      if (lv->s.size() == keep[cb.keep].size() && !keep[cb.keep].empty()) {
                          if (char* d = lv->s.mutInPlace()) std::memcpy(d, keep[cb.keep].data(), keep[cb.keep].size());
                      }
                      else lv->s = keep[cb.keep];
                  }
        } catch (RakuError&) {}
    }

    if (rt.empty() || rt == "void" || rt == "Nil") return Value::nil();
    // A NULL char* is "no string", not the empty one: returning "" lost the
    // difference between a C function saying nothing and saying nothing much
    // (getenv of an unset name, sqlite3_column_text of a NULL column).
    if (rt == "Str") return ri ? Value::str(std::string((const char*)(intptr_t)ri))
                               : Value::typeObj("Str");
    if (retFP) return Value::number(rd);
    if (rt == "bool" || rt == "Bool") return Value::boolean(ri != 0);
    // Pointer / CArray return: box the raw pointer in a live-pointer value whose
    // element/deref access reads native memory (see ncMakeLivePointer/CArray).
    if (rt == "Pointer" || rt.rfind("Pointer[", 0) == 0)
        return ncMakePointer(rt, (void*)(intptr_t)ri);
    if (rt == "CArray" || rt.rfind("CArray[", 0) == 0)
        return ncMakeLiveCArray(rt, (void*)(intptr_t)ri);
    // CStruct/CPointer return: box the pointer as an object of the return class so
    // it satisfies the type check and round-trips into later native calls.
    if (!rt.empty() && (ascii::isupper((unsigned char)rt[0]) || rt.find("::") != std::string::npos)) {
        // ncClass also resolves `--> PwStruct` where PwStruct is a CONSTANT
        // aliasing the real class (P5getpwnam picks its per-kernel struct at
        // load time: `my constant PwStruct = $*KERNEL.name eq 'darwin' ?? … !! …`).
        std::shared_ptr<ClassInfo> ci = ncClass(rt);
        if (ci) {
            // NULL is the TYPE OBJECT, not an instance holding address 0 — that is
            // how C reports "no result", and how the caller is expected to test it.
            // OpenSSL.use-client-ca-file is `unless my $s = SSL_load_client_CA_file(…)`
            // and never raised on a missing file, because a boxed null is defined
            // and true.
            if (ri == 0) return Value::typeObj(ci->name);
            Value o; o.t = VT::Object; o.setObj(makePayload<ObjectData>());
            o.obj()->cls = ci; o.obj()->attrs["__native_ptr"] = Value::integer(ri);
            return o;
        }
    }
    // Integer return: the value comes back in a 64-bit register but a narrower
    // return type (int32/uint16/…) only fills the low bits — truncate and
    // sign/zero-extend to the DECLARED width, or a returned int32 -1 reads back as
    // 0xFFFFFFFF (4294967295) and `$rc < 0` never holds (BIO_read's would-block).
    {
        bool rsgn, rflt; int rw = ncScalarWidth(rt, rsgn, rflt);
        if (rw > 0 && rw < 8 && !rflt) {
            unsigned long long mask = (1ULL << (rw * 8)) - 1;
            unsigned long long u = (unsigned long long)ri & mask;
            if (rsgn && (u & (1ULL << (rw * 8 - 1)))) ri = (NcWord)(u | ~mask);
            else ri = (NcWord)u;
        }
    }
    return Value::integer(ri);
}

// guess_library_name(lib): the file name `is native(lib)` would actually open.
// Rakudo's NativeCall exports this (under :ALL) and dists call it to REPORT what
// they bound to — so it must answer the same candidate the FFI picks, not a
// platform-decorated guess. The first spelling that dlopens wins; with none, the
// decorated default is returned so the answer is still a usable name.
std::string Interpreter::ncGuessLibraryName(const std::string& lib) {
    if (lib.empty()) return "";
    for (const std::string& cand : libCandidates(lib))
        if (void* h = dlopen(cand.c_str(), RTLD_LAZY | RTLD_GLOBAL)) { (void)h; return cand; }
    dlerror(); // clear the failures we provoked
#if defined(__APPLE__)
    return "lib" + lib + ".dylib";
#elif defined(_WIN32)
    return lib + ".dll";
#else
    return "lib" + lib + ".so";
#endif
}

// cglobal(lib, symbol, Type): resolve a C global variable's address via dlsym and
// read its current value per the declared type (Pointer types return a live
// Pointer; scalar types return the value).
Value Interpreter::cglobal(const std::string& lib, const std::string& sym, const std::string& type) {
    void* handle = RTLD_DEFAULT;
    if (!lib.empty()) handle = dlopenLib(lib);
    void* addr = dlsym(handle, sym.c_str());
    if (!addr) throw RakuError{Value::typeObj("X::AdHoc"),   // Rakudo's wording — modules match it
        "Cannot locate symbol '" + sym + "' in native library '" + lib + "'"};
    if (type == "Pointer" || type.rfind("Pointer[", 0) == 0) {
        // The SYMBOL'S ADDRESS, not what is stored there. That is Rakudo's
        // answer, and it is what makes the documented function-pointer idiom
        // work: `nativecast($signature, cglobal($lib, 'f', Pointer))` is how a
        // program reaches a C function whose signature is only known at run time
        // (issue #84). We used to dereference on the assumption that a global
        // asked for as a Pointer holds one — which read the first eight bytes of
        // `f`'s own code as an address, and a module written against Rakudo that
        // reads a `void **` global got a value one level too deep. `.deref` is
        // how to read what the pointer points AT, there and here.
        return ncMakePointer(type, addr);
    }
    if (type == "Str") {
        // a `char *` global: the variable holds a pointer, the string lives
        // behind it (gsl_version is read exactly this way)
        const char* p = *(const char**)addr;
        return p ? Value::str(std::string(p)) : Value::typeObj("Str");
    }
    return ncReadElem((long long)(intptr_t)addr, type, 0);
}


// A literal parameter — `sub f("a")`, `multi m(0)`, `-> 'about' { }` — takes
// exactly that value. It is `Int $ where 0`: TYPED by the literal, so "0", 0.0
// and 0e0 do not match `f(0)` (Rakudo), and NaN is its own literal (`multi
// f(NaN)` takes a NaN, which `==` never does). Shared by multi dispatch and by
// binding, so a plain sub and a candidate agree on what matches.
bool Interpreter::literalParamAccepts(const Param& p, const Value& v) {
    return literalAccepts(eval(p.litVal.get()), v);
}
bool Interpreter::literalAccepts(Value lv, const Value& v) {
    // the usual pair, answered before the type's name is built and looked up:
    // a plain Int against an Int literal, a plain Str against a Str one
    if (lv.t == v.t && lv.hashKind.empty() && v.hashKind.empty() && v.enumName.empty()) {
        if (v.t == VT::Int && !lv.big() && !v.big() && !v.natBits) return lv.i == v.i;
        if (v.t == VT::Str) return lv.s.str() == v.s.str();
    }
    // an angle literal `<1/2>`, `<−1+2i>` is the NUMBER (val gives the
    // allomorph): its numeric value is both the literal's type and what to compare
    if (lv.isAllomorph()) lv = methodCall(lv, "Numeric", {});
    bool nanLit = lv.t == VT::Num && std::isnan(lv.toNum());
    std::string litType = lv.typeName();
    if (!typeMatchesArg(v, litType)) return false;
    if (nanLit) return v.t == VT::Num && std::isnan(v.toNum());
    // numbers compare as `==` does — a Complex by both parts (`f(<−1+2i>)`
    // takes 1+2i's negative, which a real-part toNum compare got wrong)
    if (v.isNumeric() && lv.isNumeric()) {
        if (v.t == VT::Complex || lv.t == VT::Complex) return applyArith("==", v, lv).truthy();
        return v.toNum() == lv.toNum();
    }
    return v.toStr() == lv.toStr();
}

// The Parameter object of `p` — introspection's own, from the signature
// bindParams is binding (it renders the whole list and takes p's place in it).
// Undefined when p is not in it.
Value Interpreter::paramObjectFor(const Param& p) {
    const std::vector<Param>* ps = tctx_.bindingParams;
    if (!ps || ps->empty() || &p < ps->data() || &p >= ps->data() + ps->size()) return Value::any();
    Callable tmp;
    tmp.params = ps;
    tmp.hadSig = true;
    Value sig = makeSignature(&tmp);
    if (sig.t != VT::Hash || !sig.hash()) return Value::any();
    auto it = sig.hash()->find("params");
    if (it == sig.hash()->end() || !it->second.arr()) return Value::any();
    // makeSignature skips no parameter of a plain list, so the index carries over
    const size_t idx = (size_t)(&p - ps->data());
    return idx < it->second.arr()->size() ? (*it->second.arr())[idx] : Value::any();
}

// A binding failure carries the PARAMETER it failed on, as Rakudo's does:
// `$!.parameter` is that parameter's Parameter object (`.name` `$user`,
// `.named` …). The many throw sites below keep their own message and
// attributes; the parameter is attached here, on the way out.
void Interpreter::typeCheckBind(const Param& p, const Value& v, bool blockParam,
                                bool whereVerified, Env* sigEnv) {
    try { typeCheckBindImpl(p, v, blockParam, whereVerified, sigEnv); }
    catch (RakuError& e) {
        const Value& pl = e.payload;
        const bool isObj = pl.t == VT::Object && pl.obj() && pl.obj()->cls;
        const std::string tn = isObj ? pl.obj()->cls->name : pl.t == VT::Type ? pl.s.str() : std::string();
        if (tn == "X::TypeCheck::Binding::Parameter") {
            Value po = paramObjectFor(p);
            if (po.t == VT::Hash) {
                if (isObj) {
                    if (!pl.obj()->attrs.count("parameter")) {
                        e.payload.obj()->attrs["parameter"] = po;
                        // declared on the class too, as makeTypedEx declares what
                        // it sets: the accessor and .^attributes read the class
                        auto& ci = e.payload.obj()->cls;
                        bool have = false;
                        for (auto& a : ci->attrs) if (a.name == "parameter") { have = true; break; }
                        if (!have) { ClassAttr a; a.name = "parameter"; a.sigil = '$'; a.pub = true; ci->attrs.push_back(a); }
                    }
                }
                else if (g_makeTypedEx) e.payload = g_makeTypedEx(tn, {{"parameter", po}}, e.message);
            }
        }
        throw;
    }
}

// Multi dispatch over an OMITTED optional positional runs its `where` — against
// its default, or else the type object (an empty Array or Hash for `@`/`%`) —
// as binding runs it: `multi g($x, $y? where Int)` does not take g(1), and a
// trailing `$? where { $*KERNEL.bits == 64 }` guard that passes makes its
// candidate the narrower one. (Optional parameters keep a multi out of the
// dispatch cache, so its `assume` never covers these.)
bool Interpreter::omittedWherePasses(const Value& cand, const Param& p, size_t i,
                                     const Param* const* positional, size_t npositional,
                                     const ValueList& pos, const Value* selfForWhere) {
    Value v = p.sigil == '@' ? Value::array() : p.sigil == '%' ? Value::makeHash()
            : !p.type.empty() ? Value::typeObj(p.type) : Value::any();
    if (p.defaultVal) {
        auto denv = std::make_shared<Env>(); denv->parent = tctx_.cur;
        for (size_t j = 0; j < pos.size() && j < npositional; j++)
            if (!positional[j]->name.empty() && !positional[j]->subSig)
                denv->define(positional[j]->name, pos[j]);
        auto dsaved = tctx_.cur; tctx_.cur = denv;
        try { v = eval(p.defaultVal.get()); }
        catch (...) { tctx_.cur = dsaved; return false; }
        tctx_.cur = dsaved;
    }
    return paramWherePasses(cand, p, v, i, pos, selfForWhere);
}

// Did the binder hand the parameter exactly the argument's value? Identity of
// the payload for the reference kinds, the scalar fields otherwise — cheap, and
// false for anything a coercion, a native wrap or a misaligned argument changed.
static bool sameBoundValue(const Value& a, const Value& b) {
    if (a.t != b.t || a.pk_ != b.pk_ || a.x_ != b.x_) return false;
    if (a.p_ || b.p_) return a.p_ == b.p_;
    switch (a.t) {
        case VT::Int:  return a.i == b.i;
        case VT::Num:  return a.n == b.n || (a.n != a.n && b.n != b.n);
        case VT::Bool: return a.b == b.b;
        case VT::Str:  return a.s == b.s;
        default:       return a.s == b.s;
    }
}

bool Interpreter::bindArgCell(const Param& p, Expr* ae, std::shared_ptr<Env>& env) {
    // (a SIGILLESS `\t` has a one-character name: it binds the caller's
    // container as surely as `$t is rw` does — `$!t := t` then aliases it)
    if (!ae || p.coerce || p.isCopy || p.name.empty() ||
        (p.name.size() < 2 && p.sigil != '\\') || p.name == "$_" || !tctx_.cur)
        return false;
    // a SIGILLESS argument that holds a container hands THAT container on:
    // `method new(\times) { self.bless!SET-SELF: times }` passes $times's cell
    // down a second sigilless parameter (the argument parses as a bare name)
    if (ae->kind == NK::NameTerm) {
        const std::string& an = static_cast<NameTerm*>(ae)->name;
        if (an.empty() || !(ascii::isalpha((unsigned char)an[0]) || an[0] == '_')) return false;
        Value* craw = tctx_.cur->findRaw(an);
        Value* praw = env->localRaw(p.name);
        if (!craw || !praw || !craw->isCell() || praw->isCell()) return false;
        if (!sameBoundValue(*craw->deref(), *praw)) return false;
        *praw = Value::cellHolder(craw->promoteToCell());
        env->x().rwCelled.insert(p.name);
        return true;
    }
    if (ae->kind != NK::VarExpr) return false;
    const std::string& an = static_cast<VarExpr*>(ae)->name;
    if (an.size() < 2 || an[0] != '$' || an == "$_" ||
        !(ascii::isalpha((unsigned char)an[1]) || an[1] == '_'))
        return false;
    Env* own = nullptr;
    Value* craw = tctx_.cur->findRaw(an, &own);
    Value* praw = env->localRaw(p.name);
    if (!craw || !praw || !own || praw->isCell()) return false;
    if (own->ex && (own->ex->rwLinks.count(an) || own->ex->rwDirect.count(an))) return false;
    if (!craw->isCell() && craw->t == VT::Hash && craw->hashKind == "Proxy") return false;
    // (a worker may be reading the caller's slot: an rw link instead, see promotionSafe)
    if (!craw->isCell() && !promotionSafe(own, craw)) return false;
    if (!sameBoundValue(*craw->deref(), *praw)) return false;
    *praw = Value::cellHolder(craw->promoteToCell());
    env->x().rwCelled.insert(p.name);
    {   // the original variable, through a chain of rw parameters
        std::string origin = an;
        if (own->ex) { auto oi = own->ex->rwOrigin.find(an); if (oi != own->ex->rwOrigin.end()) origin = oi->second; }
        env->x().rwOrigin[p.name] = std::move(origin);
    }
    return true;
}

// Sink a VALUE: what happens to a statement's result nobody looks at, and to
// a block's result a caller discards (throws-like/dies-ok/lives-ok). An
// unhandled Failure detonates; a Proc that exited unsuccessfully throws
// X::Proc::Unsuccessful (Rakudo's Proc.sink). One rule, wherever the sink is.
void Interpreter::sinkValue(const Value& r) {
    // A sunk lazy `.map` over an ENDLESS source runs until its block says
    // `last`, as Rakudo iterates any sunk Seq (`(^Inf).map({ last if …; … });`).
    // Nothing keeps what it produces, so the buffer is not left to grow.
    if (r.t == VT::Array && r.ext() && r.arr() && !r.itemized) {
        auto st = std::static_pointer_cast<LazySeqState>(r.ext());
        if (st->mapView && st->infinite && st->appendNext && !st->exhausted) {
            ValueList& buf = *r.arr();
            while (st->appendNext(buf)) if (buf.size() > 1024) buf.clear();
            st->exhausted = true;
            st->infinite = false;
            return;
        }
    }
    // a sunk `$fh.lines` iterates, reading the handle to its end (`.eof` after)
    // (…but an ITEM is a container, and sinking a container reads nothing:
    // `lives-ok { my $s = (gather die)[] }` lives — S02-types/array.t)
    // …and so does a sunk gather — its block runs to the end, `lazy` or not,
    // which is how `gather { … };` as a statement does its work (sink-all).
    // A LIST view of one (`.list`, `.cache`) is not a Seq, and sinking a List
    // reads nothing: `(gather { … }).cache;` runs none of the block.
    if (r.t == VT::Array && r.ext() && r.arr() && !r.itemized &&
        ((std::static_pointer_cast<LazySeqState>(r.ext())->finiteSource &&
          !std::static_pointer_cast<LazySeqState>(r.ext())->listView) ||
         std::static_pointer_cast<LazySeqState>(r.ext())->diedProbe ||
         // …and a lazy `.map`: `(^Inf).map({ last if …; … });` runs until its `last`
         std::static_pointer_cast<LazySeqState>(r.ext())->mapView ||
         (RAKUPP_HAVE_CORO && std::static_pointer_cast<LazySeqState>(r.ext())->gatherSeq && r.s == "Seq"))) {
        forceLazy(r); return;
    }
    if (r.t != VT::Hash) return;
    if (r.hashKind == "Failure") { failureDetonate(r); return; }
    if (r.hashKind == "Proc") {
        procSettleLive(this, r);   // a child over live pipes ends first
        long long ec = r.hash()->count("exitcode") ? (*r.hash())["exitcode"].toInt() : 0;
        long long sg = r.hash()->count("signal") ? (*r.hash())["signal"].toInt() : 0;
        if (ec == 0 && sg == 0) return;
        std::string cmd;
        auto it = r.hash()->find("argv");
        if (it != r.hash()->end() && it->second.arr() && !it->second.arr()->empty()) cmd = (*it->second.arr())[0].toStr();
        std::string msg = "The spawned command '" + cmd + "' exited unsuccessfully (exit code: " +
                          std::to_string(ec) + ", signal: " + std::to_string(sg) + ")";
        // …and, when the command never started, WHY it did not — the line
        // Rakudo appends for a failed spawn (roast S29-os/system.t pins both
        // halves of it: the -1 exit code and the `OS error = ` clause).
        auto oe = r.hash()->find("os-error");
        if (oe != r.hash()->end() && !oe->second.toStr().empty())
            msg += "\n(OS error = " + oe->second.toStr() + ")";
        throw RakuError{Value::typeObj("X::Proc::Unsuccessful"), msg};
    }
}

// Is `src` a source that makes a list it is slipped into LAST endless: an
// endless integer Range, or a lazy list that is infinite or lazy by
// declaration (`1, |(lazy gather {…})` runs none of the gather)?
bool isEndlessTailSource(const Value& src) {
    const bool infRange = src.t == VT::Range && !src.rNum() &&
                          src.rTo() == 9223372036854775807LL && !rangeEnds(src);
    const bool infLazy = src.t == VT::Array && src.arr() && src.ext() &&
                         (std::static_pointer_cast<LazySeqState>(src.ext())->infinite ||
                          std::static_pointer_cast<LazySeqState>(src.ext())->declaredLazy);
    return infRange || infLazy;
}

// The lazy List of `prefix` followed by `src` (isEndlessTailSource), pulled as
// far as it is read: `0, |(1..*)`, `0, ([\+] 1..*).Slip`, `[0, |(1...*)]`.
Value Interpreter::lazyTailOver(const ValueList& prefix, const Value& src) {
    Value out = Value::array(); out.isList = true;
    for (auto& x : prefix) out.arr()->push_back(x);
    auto st = std::make_shared<LazySeqState>();
    const bool declLazy = src.t == VT::Array && src.ext() &&
                          std::static_pointer_cast<LazySeqState>(src.ext())->declaredLazy;
    // a declared-lazy tail may still END: it is a gather-like unknown, which
    // `.eager` finishes, not an endless source
    if (declLazy) { st->gatherSeq = true; st->declaredLazy = true; }
    else st->infinite = true;
    auto idx = std::make_shared<long long>(0);
    const bool infRange = src.t == VT::Range;
    Value s2 = src;
    st->appendNext = [this, s2, idx, infRange](ValueList& cache) -> bool {
        if (infRange) {
            cache.push_back(Value::integer(s2.rFrom() + (s2.rExFrom() ? 1 : 0) + (*idx)++));
            return true;
        }
        materializeLazy(s2, (size_t)*idx + 1);
        if ((size_t)*idx >= s2.arr()->size()) return false;
        cache.push_back((*s2.arr())[(size_t)(*idx)++]);
        return true;
    };
    out.extM() = st;
    return out;
}


// `$<p>:exists`, `$/[1]:!exists`, `$<a>:kv` / `:k` / `:v` / `:p` on a Match:
// whether the capture is there, and the forms over that one capture (nothing
// when it is absent). `done` says whether the adverb was one of these.
Value Interpreter::matchSubscriptAdverb(const Value& base, Index* idx, bool& done) {
    if ((idx->adverb == "exists" || idx->adverb == "!exists") && idx->index) {
        Value kv = eval(idx->index.get());
        bool has = false;
        if (idx->isHash) has = base.hash() && base.hash()->count(hashSubKey(kv));
        else { long long n = kv.toInt(); has = n >= 0 && base.arr() && n < (long long)base.arr()->size(); }
        done = true; return Value::boolean(idx->adverb == "exists" ? has : !has);
    }
    // `$<a>:kv` / `:k` / `:v` / `:p` — over the one capture, nothing when it is absent
    if ((idx->adverb == "kv" || idx->adverb == "k" || idx->adverb == "v" || idx->adverb == "p") && idx->index) {
        Value kv = eval(idx->index.get());
        Value key = idx->isHash ? Value::str(hashSubKey(kv)) : Value::integer(kv.toInt());
        Value got; bool has = false;
        if (idx->isHash) {
            if (base.hash()) { auto it = base.hash()->find(hashSubKey(kv)); if (it != base.hash()->end()) { got = it->second; has = true; } }
        }
        else { long long n = kv.toInt(); if (n >= 0 && base.arr() && n < (long long)base.arr()->size()) { got = (*base.arr())[n]; has = true; } }
        Value out = Value::array(); out.isList = true;
        done = true;
        if (!has) return out;
        if (idx->adverb == "k") return key;
        if (idx->adverb == "v") return got;
        if (idx->adverb == "p") return Value::pair(key.toStr(), got);
        out.arr()->push_back(key); out.arr()->push_back(got);
        return out;
    }
    return Value();
}

// `cmp` of an ENDLESS lazy list and a finite one: the endless side is read
// only as far as the comparison needs, and equal so far it is the longer.
bool Interpreter::cmpEndlessLazy(const Value& l, const Value& r, Value& out) {
    const bool le = endlessLazy(l), re = endlessLazy(r);
    if (le != re) {
        const Value& fin = le ? r : l;
        const Value& inf = le ? l : r;
        if (fin.t == VT::Array && fin.arr() && fin.enumName.empty()) {
            const size_t n = fin.arr()->size();
            materializeLazy(inf, n + 1);
            for (size_t k = 0; k < n && k < inf.arr()->size(); k++) {
                int c = valueCmp(le ? (*inf.arr())[k] : (*fin.arr())[k],
                                 le ? (*fin.arr())[k] : (*inf.arr())[k]);
                if (c) { out = Value::orderVal(c); return true; }
            }
            out = Value::orderVal(le ? 1 : -1);
            return true;
        }
    }
    return false;
}


// A role seen from its own body (rolePackageValue) is made by
// ParametricRoleHOW; the bare role name answers its group's HOW.
Value Interpreter::roleBodyHow(const Value& inv) {
    if (!howParamRoleClsInfo_) {
        howParamRoleClsInfo_ = std::make_shared<ClassInfo>();
        howParamRoleClsInfo_->name = "Metamodel::ParametricRoleHOW";
    }
    Value h; h.t = VT::Object; h.setObj(makePayload<ObjectData>());
    h.obj()->cls = howParamRoleClsInfo_;
    h.obj()->attrs["__type"] = Value::typeObj(inv.s);
    return h;
}


// A `next`/`last` PAYLOAD (6.e) joins a collecting loop's values — a Slip as
// its elements, the Slip TYPE as itself.
void pushLoopValue(ValueList& out, const Value& v) {
    if (v.t == VT::Array && v.arr() && v.s == "Slip") { for (auto& e : *v.arr()) out.push_back(e); return; }
    out.push_back(v);
}

// `but`: an UNDEFINED value (`$r<and>` on a type object) and the plain value
// classes are no roles (X::Mixin::NotComposable, as Rakudo).
void Interpreter::refuseValueMixin(const Value& v, const Value& base) {
    const bool undef = v.t == VT::Any || v.t == VT::Nil;
    if (undef || (v.t == VT::Type && (v.s == "Any" || v.s == "Mu" || v.s == "Int" || v.s == "Str" ||
                                      v.s == "Num" || v.s == "Rat" || v.s == "Complex")))
        throwTypedV("X::Mixin::NotComposable",
                    {{"target", base}, {"rolish", undef ? Value::typeObj("Any") : v}},
                    "Cannot mix in non-composable type " + (undef ? std::string("Any") : v.s.str()) +
                    " into object of type " + base.typeName());
}

// A method call — and under `use fatal` (a `try` block's scope is) one handing
// back a Failure throws it, as a sub call does.
Value Interpreter::fatalCheckedMethodCall(Expr* e) {
    Value r = evalMethodCallExpr(e);
    if (RAKUPP_UNLIKELY(r.t == VT::Hash && r.hashKind == "Failure") &&
        !static_cast<MethodCall*>(e)->fatalExempt && fatalHere())
        failureDetonate(r);
    return r;
}

// `&?BLOCK` in an INLINE block (`with 5 { … }`, a bare `{ … }` at the top
// level), which has no Block value of its own: a plain one, implicit `$_`.
Value Interpreter::inlineBlockValue() {
    Value b; b.t = VT::Code; b.setCode(makePayload<Callable>());
    b.code()->isBlock = true;
    return b;
}

// `try { no fatal; … }` — the block turns `use fatal` back off.
bool Interpreter::tryBodySaysNoFatal(const Expr* operand) {
    if (!operand || operand->kind != NK::BlockExpr) return false;
    for (auto& s : static_cast<const BlockExpr*>(operand)->body)
        if (s->kind == NK::UseStmt && static_cast<UseStmt*>(s.get())->isNo &&
            static_cast<UseStmt*>(s.get())->module == "fatal")
            return true;
    return false;
}

// `has IO::Handle $.x = Nil` ASSIGNS Nil, which resets the attribute to its
// default: the `is default` value, else the declared type, else Any.
Value Interpreter::nilAttrDefault(const ClassAttr& at, const std::string& resolvedType) {
    if (at.defaultTrait) return eval(const_cast<Expr*>(at.defaultTrait));
    if (!at.type.empty() && ascii::isupper((unsigned char)at.type[0]) &&
        at.type.find('[') == std::string::npos && at.type.find('(') == std::string::npos)
        return Value::typeObj(resolvedType);
    return Value::any();
}

extern std::atomic<uint64_t> g_symbolGen;   // Interpreter.cpp

// `42 ~~ C` where class C (or a role mixed into the type, `Int but R`)
// declares ACCEPTS: that method decides. A candidate that refuses a type
// object for its invocant (`multi method ACCEPTS(D:D: $)`) leaves the plain
// type check in charge.
bool Interpreter::typeObjectUserAccepts(const Value& l, const Value& r, Value& out) {
    auto it = classes_.find(r.s);
    if (it == classes_.end() || !it->second || it->second->isRole) return false;
    // whether the class declares ACCEPTS, asked once per symbol generation
    // (every `~~ C` asked, walking the MRO)
    ClassInfo& ci = *it->second;
    const uint64_t key = (g_symbolGen.load(std::memory_order_relaxed) + 1) << 1;
    uint64_t k = ci.userAcceptsKey;
    if ((k & ~uint64_t(1)) != key) {
        k = key | (ci.findMethod("ACCEPTS") ? 1 : 0);
        ci.userAcceptsKey = k;
    }
    if (!(k & 1)) return false;
    try { out = methodCall(r, "ACCEPTS", ValueList{l}); }
    catch (RakuError& e) {
        const std::string tn = e.payload.t == VT::Type ? e.payload.s.str()
                             : e.payload.t == VT::Object && e.payload.obj() && e.payload.obj()->cls
                                 ? e.payload.obj()->cls->name : std::string();
        if (tn == "X::Multi::NoMatch" || tn == "X::Parameter::InvalidConcreteness" ||
            e.message.rfind("Cannot resolve caller", 0) == 0 ||
            e.message.find("Invocant of method") != std::string::npos)
            return false;
        throw;
    }
    return true;
}

// What `.minmax` compares: a RANGE element stands for its two ends (`.minmax`
// results combine that way; the empty one, Inf..-Inf, for nothing) and a nested
// LIST for its elements (`(4, [5, 6]).minmax` is 4..6). With `exOf` — the KEYED
// form — every Range counts, and each end carries its own exclusion, so
// `(1^..5, 7).minmax(*.self)` is 1^..7.
void Interpreter::minmaxOperands(const ValueList& items, ValueList& each, std::vector<char>* exOf) {
    std::function<void(const Value&, int)> add = [&](const Value& v, int depth) {
        if (v.t == VT::Array && v.arr() && !v.ext() && v.hashKind.empty() && v.enumName.empty() && depth < 64) {
            for (auto& e : *v.arr()) add(e, depth + 1);
            return;
        }
        if (v.t != VT::Range) { each.push_back(v); if (exOf) exOf->push_back(0); return; }
        Value mn = methodCall(v, "min", ValueList{}), mx = methodCall(v, "max", ValueList{});
        if (!exOf && valueCmp(mn, mx) > 0) return;
        each.push_back(mn); each.push_back(mx);
        if (exOf) { exOf->push_back(v.rExFrom()); exOf->push_back(v.rExTo()); }
    };
    for (auto& v : items) add(v, 0);
}

// `.minpairs` / `.maxpairs` over (key, value) pairs: by `cmp` on the values, or
// through `fn` (see byCallableMinMaxPairs); every value tying for the end is
// kept, an Int key as an Int.
Value Interpreter::minMaxPairsOf(const std::vector<std::pair<Value, Value>>& kvs, const Value& fn, bool wantMax) {
    if (fn.t == VT::Code && fn.code()) return byCallableMinMaxPairs(kvs, fn, wantMax);
    Value out = Value::array(); out.isList = true;
    if (kvs.empty()) return out;
    Value best = kvs[0].second;
    for (auto& kv : kvs) {
        Value c = applyArith("cmp", kv.second, best);
        if (wantMax ? c.toInt() > 0 : c.toInt() < 0) best = kv.second;
    }
    for (auto& kv : kvs)
        if (applyArith("cmp", kv.second, best).toInt() == 0) {
            Value p = Value::pair(kv.first.toStr(), kv.second);
            if (kv.first.t == VT::Int) p.pairKeyM() = std::make_shared<Value>(kv.first);
            out.arr()->push_back(p);
        }
    return out;
}

// `GLOBAL::<Probe> = 43` — a SIGILLESS package symbol springs into being
// (nullptr: the name has a sigil, or is already something).
Value* Interpreter::newGlobalSymbolSlot(const std::string& nm) {
    if (nm.empty() || std::strchr("$@%&", nm[0]) || !global_ || tctx_.cur->find(nm) || classes_.count(nm))
        return nullptr;
    global_->define(nm, Value::any());
    return &global_->vars[nm];
}

// checkElemType for an element assignment that may have GROWN the array
// (ExecContext::lastLvalueGrowBase): a refusal takes the new slots back off,
// so `@a[5] = "s"` into an `Int @a` leaves it as it was.
void Interpreter::checkElemTypeOrShrink(const std::string& want, const Value& v, const std::string& symbol,
                                        Value* growBase, size_t growSize) {
    try { checkElemType(want, v, symbol); }
    catch (...) {
        if (growBase && growBase->arr() && growBase->arr()->size() > growSize) growBase->arr()->resize(growSize);
        throw;
    }
}

// An attribute's .type carries the CONTAINER shape, as in Rakudo:
// `has License @.licenses` answers Positional[License] (and %-attrs
// Associative[T]) — JSON::Unmarshal's array multi dispatches on exactly
// that, and flattening to the element type sent typed-array attributes
// to the Mu fallback (the Test::META chain's last wall).
Value attrTypeValue(const ClassAttr& a) {
    // `has Int @.a is Array` is typed by the container it names: Array[Int]
    // (`is Hash[Int]` as written; `has @.a is List`, List)
    if ((a.sigil == '@' || a.sigil == '%') && !a.containerIs.empty()) {
        const std::string& ci = a.containerIs;
        const size_t br = ci.find('[');
        Value v = Value::typeObj(br == std::string::npos ? ci : ci.substr(0, br));
        if (br != std::string::npos && ci.back() == ']') v.ofTypeM() = ci.substr(br + 1, ci.size() - br - 2);
        else if (!a.type.empty()) v.ofTypeM() = a.type;
        return v;
    }
    if (a.sigil == '@') {
        Value v = Value::typeObj("Positional");
        v.ofTypeM() = a.type;
        return v;
    }
    if (a.sigil == '%') {
        Value v = Value::typeObj("Associative");
        v.ofTypeM() = a.type;
        return v;
    }
    return Value::typeObj(a.type.empty() ? "Mu" : a.type);
}

// `our &infix:<qq> is export = &c` is in its package's EXPORT::DEFAULT and
// EXPORT::ALL as the routine it was given, as an exported `sub` is
// (`M::EXPORT::DEFAULT::{'&infix:<qq>'}`). Only a `&` one: an exported `our $x`
// is imported as the module's own container, which a copy here would shadow.
void Interpreter::publishOurExport(const std::string& name, const Value& v) {
    if (!global_ || tctx_.pkgPrefix.empty() || name.size() < 2 || name[0] != '&') return;
    for (const char* tg : {"DEFAULT", "ALL"})
        global_->define(name.substr(0, 1) + tctx_.pkgPrefix + "EXPORT::" + tg + "::" + name.substr(1), v);
}

// `(HB.new, HB.new)>>.HA::m` — the qualified call, per element.
Value Interpreter::hyperQualifiedCall(const std::string& method, ClassInfo* qual, const Value& inv, ValueList& args) {
    Value out = Value::array(); out.isList = true;
    for (auto& el : (inv.t == VT::Array && inv.arr()) ? *inv.arr() : inv.flatten())
        out.arr()->push_back(invokeMethodChain(method, qual, el, args, nullptr));
    return out;
}

// `self.No::Such::Type::foo` — a qualifier that names NOTHING (no class, no
// package-relative one, no built-in type, no package) is refused when the
// call runs, as Rakudo does.
void Interpreter::refuseUnknownQualifier(const std::string& qual, const std::string& method, const Value& inv) {
    if (isKnownTypeName(qual) || classes_.count(tctx_.pkgPrefix + qual) || pkgKind_.count(qual)) return;
    throwTypedV("X::Method::InvalidQualifier",
        {{"method", Value::str(method)}, {"invocant", inv}, {"qualifier-type", Value::typeObj(qual)}},
        "Cannot dispatch to method " + method + " on " + qual +
        " because it is not inherited or done by " + inv.typeName());
}

// A user ROLE declared with a core role's name (`role Numeric { }`) shadows
// it: the built-in values do not do the new role (`3.5 ~~ Numeric` is False).
bool Interpreter::userShadowsCoreRole(const std::string& type) {
    if (type.empty() || !ascii::isupper((unsigned char)type[0])) return false;
    static const std::set<std::string> kCoreRoles = {
        "Numeric", "Real", "Stringy", "Positional", "Associative", "Callable", "Iterable",
        "Rational", "Dateish", "QuantHash", "Setty", "Baggy", "Mixy"};
    auto shadows = [&](const std::string& t) {
        auto it = classes_.find(t);
        return it != classes_.end() && it->second && it->second->isRole && it->second->decl;
    };
    // Every type check asks, and almost no program declares such a role: so
    // first whether ANY does, decided once per symbol generation.
    const uint64_t key = (g_symbolGen.load(std::memory_order_relaxed) + 1) << 1;
    uint64_t k = coreRoleShadowKey_;
    if ((k & ~uint64_t(1)) != key) {
        bool any = false;
        for (auto& r : kCoreRoles) if (shadows(r)) { any = true; break; }
        k = key | (any ? 1 : 0);
        coreRoleShadowKey_ = k;
    }
    if (!(k & 1)) return false;
    return kCoreRoles.count(type) && shadows(type);
}

// Was this routine declared somewhere ELSE — imported from a module — rather
// than in the current scope chain? Its closure is not one of our scopes.
bool Interpreter::routineFromElsewhere(const Value& code) {
    if (code.t != VT::Code || !code.code() || !code.code()->closure) return false;
    for (Env* e = tctx_.cur.get(); e; e = e->parent.get())
        if (e == code.code()->closure.get()) return false;
    return true;
}

// An attribute's `is default(T)`, evaluated where the role's parameters are
// bound — on the composing class, as construction binds them — so
// `$obj.v = Nil` resets to the INSTANTIATED default (Int), not the name T.
Value Interpreter::evalAttrDefaultIn(const Expr* dflt, ClassInfo* cls) {
    bool any = false;
    for (ClassInfo* c = cls; c && !any; c = c->parent.get()) any = !c->roleParamBindings.empty();
    if (!any) return eval(const_cast<Expr*>(dflt));
    auto env = std::make_shared<Env>(); env->parent = tctx_.cur;
    for (ClassInfo* c = cls; c; c = c->parent.get())
        for (auto& b : c->roleParamBindings)
            if (!b.first.empty() && !env->local(b.first)) env->define(b.first, b.second);
    auto saved = tctx_.cur; tctx_.cur = env;
    try { Value v = eval(const_cast<Expr*>(dflt)); tctx_.cur = saved; return v; }
    catch (...) { tctx_.cur = saved; throw; }
}

// `[+]` / `[min]` / `[max]` over a plain Int Range, from its endpoints (an
// excluded end steps in by one; an empty range is 0 / Inf / -Inf).
bool Interpreter::intRangeReduce(const std::string& op, const Value& v, Value& out) {
    if (v.t != VT::Range || v.itemized || v.rNum() || v.ofType() == "Str" || rangeEnds(v) || v.big()) return false;
    // (an operator DECLARED in scope folds with itself)
    if (tctx_.cur && tctx_.cur->find("&infix:<" + op + ">")) return false;
    const long long lo = v.rFrom() + (v.rExFrom() ? 1 : 0), hi = v.rTo() - (v.rExTo() ? 1 : 0);
    if (v.rTo() == 9223372036854775807LL) return false;   // endless: endlessReduce's
    if (op == "+") { out = methodCall(v, "sum", ValueList{}); return true; }
    if (lo > hi) { out = Value::number(op == "min" ? INFINITY : -INFINITY); return true; }
    out = Value::integer(op == "min" ? lo : hi);
    return true;
}

// `Even(56)` — a subset called as a coercer: a value it already accepts is
// itself; else the value is coerced to the subset's nominal type (`.Int` for
// `subset Even of UInt`) and must then pass the subset, or the coercion is
// impossible.
Value Interpreter::subsetCoerceCall(const std::string& name, const Value& v) {
    auto accepts = [&](const Value& x) {
        try { return boolify(smartmatchValue("~~", x, Value::typeObj(name))); } catch (RakuError&) { return false; }
    };
    if (accepts(v)) return v;
    std::string base = name;
    for (int hop = 0; hop < 32; hop++) {
        auto it = subsets_.find(base);
        if (it == subsets_.end()) break;
        base = it->second.base;
    }
    if (base == "UInt") base = "Int";
    std::string why = "no acceptable coercion method found";
    if (!base.empty() && base != "Any" && base != "Mu") {
        Value cv;
        bool got = false;
        try { cv = methodCall(v, base, ValueList{}); got = true; } catch (RakuError&) {}
        if (got && accepts(cv)) return cv;
        if (got) why = "method " + base + " returned " + (cv.t == VT::Type ? "a type object " : "an instance of ") + cv.typeName();
    }
    throwTypedV("X::Coerce::Impossible",
        {{"target-type", Value::typeObj(name)}, {"from-type", Value::typeObj(v.typeName())}},
        "Impossible coercion from '" + v.typeName() + "' into '" + name + "': " + why);
}

} // namespace rakupp
