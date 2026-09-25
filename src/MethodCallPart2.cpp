#include "CNumeric.h"
#include "AsciiCtype.h"
#include "BuiltinsShared.h" // timerRemainingSecs — Promise.in/.at state is time-derived
#include "RakuAstClasses.h"
#include "MethodCallSegment.h"
#include <chrono> // DateTime.now subsecond stamp (portable — MSVC has no sys/time.h)
#if !defined(_WIN32)
#include <unistd.h> // gethostname — $*KERNEL.hostname
#include <cstdio> // $*DISTRO on macOS (macDistro)
#include <cstring> // $*DISTRO on macOS (macDistro)
#include <fstream> // $*DISTRO on macOS (macDistro)
#include <sstream> // $*DISTRO on macOS (macDistro)
#include <map> // $*DISTRO on macOS (macDistro)
#endif

// Segment 2 of the method-dispatch chain, split out of methodCallInner.
//
// It is a SEGMENT, not a category: the chain is ORDER-SENSITIVE — an earlier arm
// shadows a later one — so these arms run after methodCallInner's own arms and before segment 3. Add a new arm
// where its priority belongs, not where it reads nicely.
//
// std::optional lets every arm keep its original `return X;` verbatim, so a
// `return` inside a nested lambda still means what it always did. nullopt =
// "not handled here".

// A submethod's DISCARDED result is in SINK context: an unhandled FAILURE
// returned from BUILD/TWEAK detonates, throwing its own exception. That is
// how `submethod TWEAK { $!Etype = self.etype($!Etype) }` surfaces
// EType(300)'s X::Enum::NoValue under Rakudo — the Failure sails through the
// assignment and the return typecheck, and dies only here.
static void sinkBuildResult(const rakupp::Value& r) {
    using namespace rakupp;
    if (r.t == VT::Hash && r.hashKind == "Failure") {
        Value ex = Value::typeObj("X::AdHoc");
        std::string msg = "Failed";
        if (r.hash()) {
            auto eit = r.hash()->find("exception");
            if (eit != r.hash()->end()) ex = eit->second;
            auto mit = r.hash()->find("message");
            if (mit != r.hash()->end()) msg = mit->second.toStr();
        }
        throw RakuError{ex, msg};
    }
}

// S-43/S-35/S-36: several Supply combinators take a `&by` that means one thing
// with one parameter and another with two — a KEY EXTRACTOR (`*.chars`) versus a
// COMPARATOR (`-> $a, $b { … }`). This is that count.
static long long supplyByArity(const rakupp::Value& c) {
    using namespace rakupp;
    if (c.t != VT::Code || !c.code()) return 1;
    const Callable* k = c.code();
    if (k->isWhateverCode) return k->whateverArity > 1 ? k->whateverArity : 1;
    long long n = 0;
    if (k->params) { for (auto& pp : *k->params) if (!pp.slurpy && !pp.named && !pp.optional) n++; }
    else n = (long long)k->placeholders.size();
    return n ? n : 1;
}

namespace rakupp {

// The per-class step alone, least-derived first down the primary parent chain
// — what the construction protocol reduces to when no class in the ancestry
// declares a BUILD or a TWEAK. Recursive so the order costs no list.
static void stepDownChain(ClassInfo* c, Interpreter::BuildStep step) {
    if (!c) return;
    stepDownChain(c->parent.get(), step);
    step.fn(step.ctx, c);
}

// Depth-first over the primary and the additional (multiple-inheritance)
// parents, most-derived first, into a stack buffer that spills past eight.
// A plain recursive function and not a recursive std::function: this runs on
// every construction, and the std::function it replaced allocated on each one.
static void collectMroChain(ClassInfo* c, ClassInfo** buf,
                            std::vector<ClassInfo*>& spill, size_t& n) {
    if (!c) return;
    if (n < 8) buf[n] = c; else spill.push_back(c);
    n++;
    collectMroChain(c->parent.get(), buf, spill, n);
    for (auto& p : c->extraParents) collectMroChain(p.get(), buf, spill, n);
}

// A class's OWN hook, or the one it COMPOSED. Role composition FLATTENS a
// role's methods into the composing class: the class's own declaration wins
// and the role's copy is discarded, exactly as it does for any other method.
// So a role's BUILD/TWEAK belongs to the COMPOSER's turn in the walk below,
// never to a turn of the role's own. The first `does` arrives as the parent
// and the rest as extra parents, so both have to be looked through — and only
// through ROLES: a real ancestor class runs its own hook on its own turn.
static Value* composedHook(ClassInfo* c, const char* which) {
    auto it = c->methods.find(which);
    if (it != c->methods.end()) return it->second.t == VT::Code ? &it->second : nullptr;
    if (c->parent && c->parent->isRole)
        if (Value* r = composedHook(c->parent.get(), which)) return r;
    for (auto& p : c->extraParents)
        if (p && p->isRole)
            if (Value* r = composedHook(p.get(), which)) return r;
    return nullptr;
}

// Rakudo's BUILDALL walks the MRO least-derived first and, for EACH class in
// turn, runs that class's BUILD and then that class's TWEAK. `findMethod`
// answers only the MOST-derived one, so a parent that initialises its own
// attributes never ran at all. fez's Fez::Types is the shape that exposes it:
// `class auth-response is api-response` sets $!key in its own BUILD and leaves
// $!success to api-response's, so with the parent BUILD skipped every API
// response read back as unsuccessful and `fez login` reported "unknown error"
// on a login that had in fact succeeded (issue #72).
//
// The two hooks INTERLEAVE per class — an ancestor's TWEAK runs before a
// descendant's BUILD, not after every BUILD in the chain — which is why this
// is one walk and not two. `afterBuild`, when set, runs between a class's own
// BUILD and its own TWEAK: that is where the `is required` check belongs, so
// an attribute a class's own BUILD filled counts as supplied while one only
// its TWEAK would fill still does not.
// The diagnostic an exception object carries: its `message` method if it has
// one, else a plain `has $.message` attribute, else the type's own name. Three
// call sites want it — .throw, .Failure and the .Str/.gist fallback — and one
// of them must NOT go through the object's own Str/gist, which is why it is
// read here rather than left to gistOf.
static std::string excMessageOf(Interpreter& I, const Value& inv) {
    std::string msg;
    if (Value* mm = inv.obj() && inv.obj()->cls ? inv.obj()->cls->findMethod("message") : nullptr)
        { try { ValueList none; msg = I.invokeMethod(*mm, inv, none).toStr(); } catch (...) {} }
    if (msg.empty() && inv.obj()) {
        auto ma = inv.obj()->attrs.find("message");
        if (ma != inv.obj()->attrs.end() && rtIsDefined(ma->second)) msg = ma->second.toStr();
    }
    return msg.empty() ? inv.typeName() : msg;
}

void Interpreter::runBuildChain(ClassInfo* ci, const Value& self, const ValueList& args,
                                BuildStep afterBuild) {
    if (!ci) return;
    // A metaclass-supplied POPULATE runs AROUND the build plan, as Rakudo's
    // does: it sets up whatever the metaclass needs on the fresh instance and
    // then `callsame`s, and the rest of construction is that next candidate.
    // OO::Monitors makes the instance's Lock here, so without this its monitor
    // methods would lock a Lock type object — the half of issue #86 that the
    // declaration hooks alone do not fix. Gated on the flag: an ordinary class
    // has no POPULATE and pays one bool for the question.
    if (ci->hasPopulate) {
        if (Value* pop = ci->findMethod("POPULATE")) {
            // Re-entry guard: POPULATE's own `callsame` lands back here, and the
            // class still says it has one. The flag is per construction, not per
            // class, so a nested `.new` inside POPULATE still gets its own.
            static thread_local const void* inPopulate = nullptr;
            const void* key = (const void*)self.obj();
            if (key && inPopulate != key) {
                const void* saved = inPopulate;
                inPopulate = key;
                RedispatchCtx rc;
                rc.sameArgs = args;
                rc.next = [&](ValueList) { runBuildChain(ci, self, args, afterBuild); return Value::nil(); };
                redispatchStack_.push_back(std::move(rc));
                try { invokeMethod(*pop, self, args, nullptr, /*ownFrame=*/true); }
                catch (...) { redispatchStack_.pop_back(); inPopulate = saved; throw; }
                redispatchStack_.pop_back();
                inPopulate = saved;
                return;
            }
        }
    }
    // Neither hook anywhere in the ancestry: `class K { has $.a; has $.b }`, and
    // nearly every other construction there is. Nothing has to interleave, so
    // the per-class step (the `is required` check) runs straight down the parent
    // chain the way it did before this walk existed, and the MRO collection
    // below — which every single constructed object would otherwise pay for —
    // never happens. Skipping it here is worth ~4% of `K.new`.
    if (!ci->findMethod("BUILD") && !ci->findMethod("TWEAK")) {
        if (afterBuild.fn) stepDownChain(ci, afterBuild);
        return;
    }
    // MRO, most-derived first: depth-first over the primary and the additional
    // (multiple-inheritance) parents, keeping the LAST occurrence of a class
    // reached twice. That is the linearisation `.^mro` itself reports, so the
    // order a diamond builds in cannot drift from the order it reports.
    ClassInfo* linBuf[8];
    std::vector<ClassInfo*> linSpill;
    size_t nLin = 0;
    auto at = [&](size_t i) -> ClassInfo* { return i < 8 ? linBuf[i] : linSpill[i - 8]; };
    collectMroChain(ci, linBuf, linSpill, nLin);
    // One activation of one hook, under a dispatcher frame that has no next
    // candidate — so a `nextsame` inside a BUILD is the benign no-op it is in
    // Rakudo (Fez::Types ends both of its BUILDs with one) instead of
    // "nextsame is not in the dynamic scope of a dispatcher", and so it cannot
    // re-run an ancestor this walk is already running exactly once.
    auto runHook = [&](ClassInfo* c, const char* which) {
        // A composed ROLE is not an ancestor, so it gets no hook of its own: it
        // is in this chain only for the per-class step, and `composedHook` picks
        // its declaration up on the composer's turn instead. Giving it a turn
        // ran BOTH a class's TWEAK and the one it had overridden — Sparrow6's
        // Range context declares a TWEAK that splits stdout into streams and
        // composes a role whose TWEAK builds the flat unsplit one, so every
        // check saw one extra stream holding the whole document (issue #75).
        // A role PUNNED into a class by constructing it is still its own class.
        if (c->isRole && c != ci) return;
        // Only what this class declares or composed: an INHERITED hook belongs
        // to the ancestor that declared it, and runs on that ancestor's turn.
        Value* hook = composedHook(c, which);
        if (!hook) return;
        RedispatchCtx rc;
        rc.sameArgs = args;
        rc.next = [](ValueList) { return Value::nil(); };
        redispatchStack_.push_back(std::move(rc));
        try { sinkBuildResult(invokeMethod(*hook, self, args, nullptr, /*ownFrame=*/true)); }
        catch (...) { redispatchStack_.pop_back(); throw; }
        redispatchStack_.pop_back();
    };
    for (size_t i = nLin; i-- > 0;) {
        ClassInfo* c = at(i);
        bool later = false;              // dedup keeping the LAST occurrence
        for (size_t j = i + 1; j < nLin; j++) if (at(j) == c) { later = true; break; }
        if (later) continue;
        runHook(c, "BUILD");
        if (afterBuild.fn) afterBuild.fn(afterBuild.ctx, c);
        runHook(c, "TWEAK");
    }
}

// An attribute's .type carries the CONTAINER shape, as in Rakudo:
// `has License @.licenses` answers Positional[License] (and %-attrs
// Associative[T]) — JSON::Unmarshal's array multi dispatches on exactly
// that, and flattening to the element type sent typed-array attributes
// to the Mu fallback (the Test::META chain's last wall).
static Value attrTypeValue(const ClassAttr& a) {
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

// The Attribute meta-object for `a`, declared by `ownerName`. ONE builder — the
// `.^attributes` and `.^attribute_table` copies had drifted apart (only one of
// them set `built`). It is cached on the ClassAttr because a user `trait_mod:<is>`
// mixes roles into it at declaration time and `.^attributes` must return the same
// object, not a fresh one that has forgotten the trait ever ran.
// The container interface a class inherits from a built-in parent — the names
// `.^find_method` must answer for even though no `methods` entry holds them.
static bool isContainerMethodName(const std::string& mn) {
    static const std::set<std::string> kContainerMethods = {
        "AT-KEY", "ASSIGN-KEY", "BIND-KEY", "DELETE-KEY", "EXISTS-KEY",
        "AT-POS", "ASSIGN-POS", "BIND-POS", "DELETE-POS", "EXISTS-POS",
        "STORE", "elems", "keys", "values", "pairs", "kv", "iterator"};
    return kContainerMethods.count(mn) != 0;
}

Value attributeMetaObject(ClassAttr& a, const std::string& ownerName) {
    if (a.metaObj.t == VT::Hash && a.metaObj.hash()) return a.metaObj;
    Value at = Value::makeHash(); at.hashKind = "Attribute";
    (*at.hash())["name"] = Value::str(std::string(1, a.sigil) + "!" + a.name);
    (*at.hash())["type"] = attrTypeValue(a);
    (*at.hash())["readonly"] = Value::boolean(!a.rw);
    (*at.hash())["has_accessor"] = Value::boolean(a.pub);
    // public attrs are always built; a private one only via `is built`
    // (what JSON::Marshal's is_built probe asks)
    (*at.hash())["built"] = Value::boolean(a.pub || a.built);
    (*at.hash())["package"] = Value::typeObj(ownerName);
    for (auto& ut : a.userTraits) (*at.hash())["trait:" + ut.first] = ut.second;
    if (!a.pod.empty()) { // declarator pod, answered by .WHY
        (*at.hash())["why"] = Value::str(a.pod);
        (*at.hash())["whyTrail"] = Value::str(a.podTrail);
        (*at.hash())["whyLine"] = Value::integer(a.declLine);
    }
    a.metaObj = at;
    return at;
}

// `.can` on a built-in-backed method: every class news/blesses/gists, and a
// grammar parses (IETF::RFC_Grammar gates on `$g.can('parse')`). The answer is a
// stub callable that dispatches for real if someone actually invokes it.
//
// Both `.can` arms — the one on a class and the one on an instance — needed this
// and each carried a copy, list of universal names included.
static Value builtinCanStub(const std::string& mn, bool isGrammar) {
    static const std::set<std::string> universal = {
        "new", "bless", "gist", "Str", "raku", "perl", "so", "defined",
        "can", "isa", "does", "WHAT", "WHICH", "WHERE", "clone"};
    if (!universal.count(mn) && !(isGrammar && (mn == "parse" || mn == "subparse")))
        return Value::nil();
    Value stub; stub.t = VT::Code; stub.setCode(std::make_shared<Callable>());
    stub.code()->name = mn; stub.code()->isMethod = true;
    std::string mnc = mn;
    stub.code()->builtin = [mnc](Interpreter& I, ValueList& a) -> Value {
        if (a.empty()) return Value::any();
        ValueList rest(a.begin() + 1, a.end());
        return I.methodCall(a[0], mnc, std::move(rest));
    };
    return stub;
}

// The `.can`/`.^lookup` existence probe — one unsafe-names list, one dance.
// Probing CALLS the method with no arguments and watches for
// X::Method::NotFound, so a name with side effects must never be probed: a
// destructive method added to one site's copy of the list stayed probeable
// through the other's (REVIEW-3.7 finding 8).
int Interpreter::probeMethodExists(const Value& inv, const std::string& mn, const char* ioProbePath) {
    static const std::set<std::string> kUnsafeToProbe = {
        "print", "say", "put", "note", "printf", "write", "spurt", "open",
        "mkdir", "rmdir", "symlink", "link", "unlink", "rename", "copy",
        "move", "chdir", "close", "flush", "seek", "run", "shell", "exit",
        "throw", "rethrow", "sink", "emit", "send", "recv", "start",
        "sleep", "kill", "signal", "await", "react", "trans", "subst-mutate" };
    if (mn.empty() || kUnsafeToProbe.count(mn)) return 0;
    Value probe = inv;
    if (probe.hashKind == "IO") probe.s = ioProbePath;
    auto probeWith = [&](ValueList as) {
        bool nf = false;
        try { methodCall(probe, mn, as); }
        catch (RakuError& e) {
            const Value& p = e.payload;
            nf = (p.t == VT::Type && p.s == "X::Method::NotFound") ||
                 (p.t == VT::Object && p.obj() && p.obj()->cls &&
                  p.obj()->cls->name == "X::Method::NotFound");
        }
        catch (...) {}
        return nf;
    };
    // A method that REQUIRES an argument reports NotFound when probed with
    // none — rakupp dispatches on the argument list, so `"".parse-base()` looks
    // exactly like a missing name. One retry with a dummy argument tells the
    // two apart: a name that does not exist still says NotFound, a real one
    // fails some other way (or succeeds).
    if (!probeWith(ValueList{})) return 1;
    return probeWith(ValueList{Value::integer(0)}) ? -1 : 1;
}

// Fill od->attrs for ci's whole inheritance chain, exactly as the default
// constructor does: named args bind DURING the walk (Rakudo's BUILDALL order,
// so a later default reading an earlier attribute sees the constructed value),
// each level's defaults evaluate in the DECLARING class's scope with `self`
// bound to the object under construction, same-named attrs of different
// sigils keep separate slots, native types seed their zeros, `is Set`-style
// containers instantiate, and the trailing pass binds public/`is built`
// nameds. The boxed-builtin constructors (`class D is DateTime`, SubDate.now)
// used stripped copies of this walk with NONE of that — `has $.b = $!a * 2`
// died "no self available" and providedArgs-order was never honoured
// (REVIEW-3.7 finding 6). Box od->boxed BEFORE calling: parents construct
// first, so a default may read the boxed parent through self.
// Assigning Nil RESETS a container to its default — that is what Nil is for,
// and it holds for an attribute initialised by name as much as for `$x = Nil`
// (which rakupp already did). `class C { has $.t }; C.new(t => Nil).t` is Any,
// and a typed attribute takes its own type object. Storing the Nil itself made
// a module's `.new(type => Nil)` sentinel render as `Nil` instead of `(Any)`.
static Value nilResetForAttr(const Value& v, const ClassAttr& a) {
    if (v.t != VT::Nil) return v;
    if (a.sigil == '@') return Value::array();
    if (a.sigil == '%') return Value::makeHash();
    if (!a.type.empty() && ascii::isupper((unsigned char)a.type[0]))
        return Value::typeObj(a.type);
    return Value::any();
}

// Park a tap on a Proc::Async stream Supply ({proc, stream, split?, bin?}).
// The process's output arrives in CHUNKS as it is produced (runProcPromise's
// sink), so a `.lines`-marked stream splits HERE, at the tap: the callback runs
// once per line, as soon as that line's newline has arrived. A chunk boundary
// falls wherever the pipe felt like putting it, so the tail of one is carried
// over to the next and only released when a newline turns up — or, for a last
// line with no terminator, when the stream ends (the feeder calls the wrapper
// one final time with `final` set). The default chomps; a recorded `:!chomp`
// keeps each line's terminator — TAP's parse-stream grammar needs the trailing
// "\n" to close every entry, and chomping regardless made TAP::Harness say
// NOTESTS.
// What lands on the proc is a {emit, done, quit, bin, lines} RECORD: the feeder
// reads the stream flavour and the :done/:quit callbacks from it (TAP relays
// stderr with `.act({…}, :done({…}), :quit({…}))`, which used to lose both).
void Interpreter::registerProcStreamTap(const Value& inv, Value cb, Value done, Value quit) {
    if (!(inv.t == VT::Hash && inv.hash() && inv.hash()->count("proc"))) return;
    Value proc = (*inv.hash())["proc"];
    if (!(proc.t == VT::Hash && proc.hash())) return;
    const char* key = inv.hash()->count("stream") &&
                      (*inv.hash())["stream"].toStr() == "stderr" ? "taps-err" : "taps";
    if (!proc.hash()->count(key)) (*proc.hash())[key] = Value::array();
    if (inv.hash()->count("split") && (*inv.hash())["split"].toStr() == "lines") {
        bool chomp = !inv.hash()->count("split-chomp") || (*inv.hash())["split-chomp"].truthy();
        Value w; w.t = VT::Code; w.setCode(std::make_shared<Callable>());
        Value lineCb = cb;
        auto carry = std::make_shared<std::string>(); // the tail of the last chunk
        w.code()->builtin = [lineCb, chomp, carry](Interpreter& I, ValueList& a) -> Value {
            bool final = a.size() > 1 && a[1].truthy();  // stream ended: release the tail
            *carry += a.empty() ? "" : a[0].toStr();
            std::string data; data.swap(*carry);
            size_t start = 0;
            for (; start < data.size();) {
                size_t nl = data.find('\n', start);
                if (nl == std::string::npos) break;       // incomplete: wait for more
                std::string line;
                if (chomp) {
                    line = data.substr(start, nl - start);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                }
                else line = data.substr(start, nl - start + 1); // "\r\n" rides along whole
                start = nl + 1;
                try { I.callCallable(lineCb, ValueList{Value::str(line)}); }
                catch (NextEx&) {}
                catch (LastEx&) { start = data.size(); break; }
            }
            std::string rest = data.substr(start);
            // A last line with no terminator is still a line — but only once the
            // stream is over; until then it may just be half of one.
            if (final) {
                // As-is, terminator and all — there is none to add, and under
                // :!chomp the consumer is reading exactly what the child wrote.
                if (!rest.empty()) {
                    try { I.callCallable(lineCb, ValueList{Value::str(rest)}); }
                    catch (NextEx&) {} catch (LastEx&) {}
                }
            }
            else *carry = rest;
            return Value::any();
        };
        cb = w;
    }
    Value rec = Value::makeHash();
    (*rec.hash())["emit"] = cb;
    (*rec.hash())["done"] = done;
    (*rec.hash())["quit"] = quit;
    // Only a line-splitting wrapper understands the end-of-stream call; a raw
    // callback would just see a spurious empty chunk.
    if (inv.hash()->count("split") && (*inv.hash())["split"].toStr() == "lines")
        (*rec.hash())["lines"] = Value::boolean(true);
    if (inv.hash()->count("bin") && (*inv.hash())["bin"].truthy())
        (*rec.hash())["bin"] = Value::boolean(true);
    (*proc.hash())[key].arr()->push_back(rec);
    // the MERGED `.Supply` hears stderr as well (its end is announced once)
    if (inv.hash()->count("stream") && (*inv.hash())["stream"].toStr() == "Supply") {
        Value rec2 = Value::makeHash();
        *rec2.hash() = *rec.hash();
        (*rec2.hash())["done"] = Value::any();
        if (!proc.hash()->count("taps-err")) (*proc.hash())["taps-err"] = Value::array();
        (*proc.hash())["taps-err"].arr()->push_back(rec2);
    }
}

void Interpreter::runAttrDefaults(const std::shared_ptr<ObjectData>& od,
                                  const std::shared_ptr<ClassInfo>& ci,
                                  ValueList& args) {
    // attr defaults evaluate with `self` in scope, so a default
    // CLOSURE (`has $.cl = { self.foo }`) captures the new object
    // Built only when a default has to be evaluated — see ensureEnv below. It
    // is a Value wrapping the new object, and the vast majority of
    // constructions never open a scope to put it in.
    Value selfEarly;
    bool selfEarlyMade = false;
    auto savedDenv = tctx_.cur;
    struct EnvRestore {
        Interpreter& I; std::shared_ptr<Env> e;
        ~EnvRestore() { I.tctx_.cur = e; }
    } envRestore{*this, savedDenv};
    // The ancestor chain is rebuilt on EVERY construction, and allocated a
    // vector to hold what is almost always one or two pointers. A stack buffer
    // covers any hierarchy short of eight deep; deeper ones spill to the heap.
    ClassInfo* chainBuf[8];
    std::vector<ClassInfo*> chainSpill;
    size_t nChain = 0;
    for (ClassInfo* c = ci.get(); c; c = c->parent.get()) {
        if (nChain < 8) chainBuf[nChain] = c; else chainSpill.push_back(c);
        nChain++;
    }
    auto chainAt = [&](size_t i) -> ClassInfo* { return i < 8 ? chainBuf[i] : chainSpill[i - 8]; };
    // Named args bind DURING the walk, not after it — Rakudo's
    // BUILDALL takes the caller's value for an attribute when one
    // was passed and only otherwise runs the default, so a LATER
    // default that reads an earlier attribute sees the constructed
    // value (`has Code:D $.converter = get-converter($!type)` in
    // Getopt::Long read the declared Str, never the Int passed).
    // A std::map here allocated a red-black node and COPIED the key string for
    // every named argument, on every construction. The list is tiny — the named
    // arguments of one call — so a flat vector searched linearly is smaller and
    // faster, and it borrows each key instead of copying it. `args` outlives
    // this function, so the pointers stay valid. Duplicate names overwrite, as
    // the map's `operator[]` assignment did.
    // It also carries the attribute it resolved to, so the binding pass at the
    // end of this function does not repeat the findAttr, and a `bound` flag so
    // it does not repeat the BINDING either.
    struct ProvidedArg {
        const std::string* name;
        const Value* val;
        const ClassAttr* at;
        bool bound = false;
    };
    std::vector<ProvidedArg> providedArgs;
    for (auto& arg : args)
        if (arg.t == VT::Pair) {
            const ClassAttr* pat = ci->findAttr(arg.s);
            // a class with its OWN BUILD initialises its attributes itself: the
            // default constructor does not bind them from named args (Rakudo's
            // BUILDPLAN), though their defaults still apply
            if (pat) {
                for (size_t ci2 = 0; ci2 < nChain; ci2++) {
                    ClassInfo* oc = chainAt(ci2);
                    bool owns = false;
                    for (auto& a2 : oc->attrs) if (&a2 == pat) { owns = true; break; }
                    if (!owns) continue;
                    auto bm = oc->methods.find("BUILD");
                    if (bm != oc->methods.end() && bm->second.t == VT::Code && bm->second.code() &&
                        bm->second.code()->isSubmethod && !oc->roleSubmethods.count("BUILD"))
                        pat = nullptr;
                    break;
                }
            }
            if (pat && (pat->pub || pat->built)) {
                const std::string& k = arg.s;
                bool seen = false;
                for (auto& pv : providedArgs)
                    if (*pv.name == k) { pv.val = arg.pairVal(); pv.at = pat; seen = true; break; }
                if (!seen) providedArgs.push_back(ProvidedArg{&k, arg.pairVal(), pat, false});
            }
        }
    // `has Digest $.digest` beside `has &!digest`: same bare name,
    // different sigils. attrs is keyed by bare name, so the twins
    // clobbered each other (Auth::SCRAM's callable ended up holding
    // the enum). A non-$ twin stores under "&name"/"@name"/"%name";
    // the read/write paths try that spelling first.
    // Only a NON-$ attribute can need the sigil-prefixed spelling, and most
    // classes have none — yet this map was built on every construction, one
    // red-black node plus a std::set allocation per attribute per ancestor.
    // Build it on first demand instead; a class of plain `$` attributes, which
    // is the common one, never touches it.
    std::map<std::string, std::set<char>> nameSigils;
    bool sigilsBuilt = false;
    auto sigilCount = [&](const std::string& nm) -> size_t {
        if (!sigilsBuilt) {
            sigilsBuilt = true;
            for (ClassInfo* c = ci.get(); c; c = c->parent.get())
                for (auto& a2 : c->attrs) nameSigils[a2.name].insert(a2.sigil);
        }
        return nameSigils[nm].size();
    };
    // A role's TYPE-CAPTURE parameter is a NAME until the composition binds it:
    // `role R[::TYPE] { has TYPE @!items }` leaves the attribute's declared type
    // reading "TYPE", and the role's ClassInfo is SHARED by every class that
    // composes it, so the name cannot be rewritten there — R[Int] and R[Str]
    // would fight over it. Resolve it per OBJECT instead, against the class's
    // own bindings, which is where the composition recorded what TYPE is.
    // Without this the typed container refused EVERY value put into it, in
    // R[Int] and R[Str] alike, and `.of` answered the literal "TYPE"
    // (Concurrent::PriorityQueue is built on exactly that shape).
    auto resolveRoleType = [&](const std::string& t) -> const std::string& {
        if (t.empty()) return t;
        for (ClassInfo* c = ci.get(); c; c = c->parent.get())
            for (auto& b : c->roleParamBindings)
                if (b.first == t && b.second.t == VT::Type && !b.second.s.empty())
                    return b.second.s;
        return t;
    };
    // A value the CALLER passed for a typed container attribute keeps the
    // attribute's element type — the coercion below builds a fresh Array/Hash
    // that knows nothing of the declaration — and every element it brings has
    // to satisfy it, the same check the attribute's own later writes get.
    auto typedContainer = [&](Value v, const ClassAttr& at) -> Value {
        if ((at.sigil != '@' && at.sigil != '%') || at.type.empty()) return v;
        if (v.t != VT::Array && v.t != VT::Hash) return v;
        if (v.t == VT::Hash && !v.hashKind.empty()) return v; // a Set/Bag keys on ofType
        if (v.ofType().empty()) v.ofTypeM() = resolveRoleType(at.type);
        std::string want = elemTypeOf(v);
        if (!want.empty()) {
            std::string sym = std::string(1, at.sigil) + "!" + at.name;
            if (v.t == VT::Array && v.arr()) for (auto& el : *v.arr()) checkElemType(want, el, sym);
            else if (v.hash()) for (auto& kv : *v.hash()) checkElemType(want, kv.second, sym);
        }
        return v;
    };
    for (size_t lvlIx = nChain; lvlIx-- > 0;) {
        ClassInfo* lvl = chainAt(lvlIx);
        if (lvl->attrs.empty()) continue;   // nothing to seed at this level
        // each level's defaults close over ITS declaration scope
        // (class-body constants/lexicals — `constant %Glyphs` in
        // Font::AFM must resolve from another module's `.new`),
        // not over whatever scope the CALLER happens to be in
        // The scope is built only when a default actually has to be EVALUATED.
        // It costs an Env — a hash and a pad vector — plus a `self` definition,
        // and a class whose attributes all take a seed or a passed value
        // evaluates nothing at all. Reset to the caller's scope first, so a
        // level that builds none does not inherit the previous level's.
        tctx_.cur = savedDenv;
        std::shared_ptr<Env> denv;
        auto ensureEnv = [&] {
            if (denv) return;
            denv = std::make_shared<Env>();
            denv->parent = lvl->declEnv ? lvl->declEnv : savedDenv;
            if (!selfEarlyMade) { selfEarly = Value::object(od); selfEarlyMade = true; }
            denv->define("self", selfEarly);
            // A role's parameters are in scope for its attribute DEFAULTS, not
            // only for its method bodies: `role Instruction[$ins] { has $.instruction = $ins }`
            // is how Docker::File gives each of its dozen instruction classes
            // its own name, and the default evaluates HERE, at construction.
            // The bindings sit on the COMPOSING class (the role's own ClassInfo
            // is shared by every composer), so they are read off the object's
            // class chain rather than off `lvl`.
            for (ClassInfo* c = ci.get(); c; c = c->parent.get())
                for (auto& b : c->roleParamBindings)
                    if (!b.first.empty() && !denv->local(b.first))
                        denv->define(b.first, b.second);
            tctx_.cur = denv;
        };
        for (auto& at : lvl->attrs) {
            // storage slot: bare name, unless a same-named twin of
            // another sigil exists — then the non-$ one keys by
            // "&name"/"@name"/"%name"
            // The slot name is the attribute's own name except for a sigil twin,
            // so point at it rather than copying a std::string per attribute per
            // construction.
            std::string slotBuf;
            const std::string* slotp = &at.name;
            if (at.sigil != '$' && sigilCount(at.name) > 1) {
                slotBuf = std::string(1, at.sigil) + at.name;
                slotp = &slotBuf;
            }
            const std::string& slot = *slotp;
            const bool slotIsName = (slotp == &at.name);
            ProvidedArg* provided = nullptr;
            for (auto& pv : providedArgs)
                if (*pv.name == at.name) { provided = &pv; break; }
            if (provided && slotIsName) {
                od->attrs[slot] = typedContainer(coerceToSigil(
                    nilResetForAttr(provided->val ? *provided->val : Value::any(), at), at.sigil), at);
                provided->bound = true;
                continue;
            }
            // The value the slot holds when it has no explicit default.
            // A native-typed scalar takes its zero (`has atomicint $.n`
            // starts at 0); a named type takes its TYPE OBJECT (not Any),
            // so a `.= new` default reads that type object as its invocant
            // (`has T $.x .= new` == `$!x = T.new`) — Rakudo semantics.
            // …and a TYPED container attribute (`has Str @.data`, `has Int %.h`)
            // is an Array[Str]/Hash[Int]: the element type has to be ON the
            // slot, or `.of` answers (Mu) and nothing checks what enters it —
            // `has Str @.data` silently took Ints (issue #63).
            Value seed = at.sigil == '@' || at.sigil == '%'
                       ? rtTypedDefault(resolveRoleType(at.type).c_str(), at.sigil)
                       : Value::any();
            if (at.objKeyed && seed.t == VT::Hash) seed.objKeyed = true;
            if (at.sigil == '$' && !at.type.empty()) {
                if (at.type == "atomicint" || at.type == "byte" ||
                    at.type.rfind("int", 0) == 0 || at.type.rfind("uint", 0) == 0)
                    seed = Value::integer(0);
                else if (at.type.rfind("num", 0) == 0) seed = Value::number(0);
                else if (at.type == "str") seed = Value::str("");
                else if (ascii::isupper((unsigned char)at.type[0])) seed = Value::typeObj(at.type);
            }
            bool userContainer = false;
            if (!at.containerIs.empty() && at.sigil == '%') {
                static const std::set<std::string> quant = {
                    "Set", "SetHash", "Bag", "BagHash", "Mix", "MixHash"};
                if (quant.count(at.containerIs))
                    seed = makeBaggy({}, at.containerIs); // has %.a is Set — empty Setty
                else if (classes_.count(at.containerIs)) {
                    userContainer = true;
                    // a USER type: the attribute IS an instance of it, so
                    // its methods are reachable. DBIish declares
                    // `has %.Converter is DBDish::TypeConverter` and then
                    // calls `.convert` on it; as a plain Hash there was no
                    // such method and nothing said which line was at fault.
                    ValueList none;
                    ensureEnv();
                    seed = methodCall(Value::typeObj(at.containerIs), "new", none);
                }
            }
            // Pre-seed the slot so a self-referential default (`.= new`,
            // or one reading $!this-attr) sees the seed, not an unset Any.
            // …but only when there IS such a default. With none, the seed is
            // exactly what the final assignment below writes, so the pre-seed
            // was a second hash of the same name storing the same value.
            if (at.def || userContainer) od->attrs[slot] = seed;
            if (at.def && !at.hasDefVal) ensureEnv();
            Value dv = at.hasDefVal ? at.defVal
                     : at.def ? eval(const_cast<Expr*>(at.def))
                     : at.buildFn.t == VT::Code
                              ? (ensureEnv(), callCallable(at.buildFn, ValueList{selfEarly}))
                              : seed;   // `.set_build(&closure)`, added at runtime
            // the SIGIL is a container type: `has @.a = (1,2)` holds an
            // Array and `has %.h = (a=>1)` a Hash, so `.WHAT` answers
            // (Array)/(Hash) and the default renderer shows [1, 2] /
            // {:a(1)} rather than the List and Pair the initialiser
            // happened to produce.
            // …but a USER container type is the value: coercing it to
            // the sigil would turn the object straight back into the
            // plain Hash it was declared not to be.
            if (!userContainer) dv = typedContainer(coerceToSigil(dv, at.sigil), at);
            // `has @.a is default(42)` — on a positional or associative the
            // trait is the ELEMENT default, as `my @a is default(42)` is
            if (at.defaultTrait && (at.sigil == '@' || at.sigil == '%') &&
                (dv.t == VT::Array || dv.t == VT::Hash)) {
                ensureEnv();
                dv.elemDefaultM() = std::make_shared<Value>(eval(const_cast<Expr*>(at.defaultTrait)));
            }
            od->attrs[slot] = dv;
        }
    }
    tctx_.cur = savedDenv;
    // the default constructor binds nameds to declared PUBLIC attributes
    // only; anything else is silently ignored (Rakudo semantics — an
    // unknown name must NOT enter the attr store, or `$.name` inside a
    // method would see it instead of dying with X::Method::NotFound)
    // (`is built` opts a PRIVATE attr into construction-by-name — that is the
    // trait's whole purpose, and JSON::Class binds its $!declarant this way.)
    //
    // An argument the walk above ALREADY bound is not bound again. It wrote the
    // same slot, from the same argument, through the same coercion, so a second
    // pass over every named argument re-hashed the name and re-ran
    // typedContainer/coerceToSigil to store what was already there. What is left
    // for this pass is what the walk cannot reach: a sigil twin, whose slot is
    // "&name" rather than the bare name the caller passed, and an attribute
    // inherited through a SECOND parent, since the walk follows `parent` alone.
    for (auto& pv : providedArgs)
        if (!pv.bound)
            od->attrs[*pv.name] = typedContainer(coerceToSigil(
                nilResetForAttr(pv.val ? *pv.val : Value::any(), *pv.at), pv.at->sigil), *pv.at);
}

#if defined(__APPLE__)
// $*DISTRO on macOS, filled in the way Rakudo's src/core.c/Distro.rakumod does
// it: the `sw_vers` fields (ProductVersion is .version, BuildVersion is
// .release), 'Apple Inc.' for .auth, and for .desc the marketing name — a
// static table up to El Capitan, after that the phrase the system's own
// software-licence page opens with ("SOFTWARE LICENSE AGREEMENT FOR macOS
// Tahoe 26"), "<unknown>" when neither answers. Roast keys TODO guards on that
// string (`todo(…) if $*DISTRO.desc eq 'Sonoma' | 'Sequoia' | 'Tahoe 26'`,
// S32-io/out-buffering.t), and we answered our NAME for it, 0 for the version
// and the KERNEL release for .release — so a guard Rakudo trips on this OS
// never fired here. Read once, lazily, on first use.
struct MacDistro { std::string version, release, desc; bool loaded = false; };
static const MacDistro& macDistro() {
    static MacDistro d;
    if (d.loaded) return d;
    d.loaded = true;
    if (FILE* p = popen("sw_vers 2>/dev/null", "r")) {
        char line[512];
        auto trim = [](std::string& t) {
            size_t a = t.find_first_not_of(" \t\r\n"), b = t.find_last_not_of(" \t\r\n");
            t = a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
        };
        while (fgets(line, sizeof line, p)) {
            std::string s(line);
            size_t c = s.find(':');
            if (c == std::string::npos) continue;
            std::string k = s.substr(0, c), v = s.substr(c + 1);
            trim(k); trim(v);
            if (k == "ProductVersion") d.version = v;
            else if (k == "BuildVersion") d.release = v;
        }
        pclose(p);
    }
    // Rakudo's table, keyed on the WHOLE version string as its own is
    static const std::map<std::string, std::string> kNames = {
        {"10.0", "Cheetah"}, {"10.1", "Puma"}, {"10.2", "Jaguar"}, {"10.3", "Panther"},
        {"10.4", "Tiger"}, {"10.5", "Leopard"}, {"10.6", "Snow Leopard"}, {"10.7", "Lion"},
        {"10.8", "Mountain Lion"}, {"10.9", "Mavericks"}, {"10.10", "Yosemite"}, {"10.11", "El Capitan"},
    };
    auto it = kNames.find(d.version);
    if (it != kNames.end()) { d.desc = it->second; return d; }
    d.desc = "<unknown>";
    std::ifstream in("/System/Library/CoreServices/Setup Assistant.app/Contents/Resources/en.lproj/OSXSoftwareLicense.html",
                     std::ios::binary);
    if (in) {
        std::ostringstream ss; ss << in.rdbuf();
        const std::string html = ss.str();
        static const char* kPhrase = "SOFTWARE LICENSE AGREEMENT FOR macOS ";
        size_t at = html.find(kPhrase);
        if (at != std::string::npos) {
            at += std::strlen(kPhrase);
            size_t end = html.find('<', at);
            d.desc = html.substr(at, end == std::string::npos ? std::string::npos : end - at);
        }
    }
    return d;
}
#endif

// `.^add_multi_method(name, &m)`: one more candidate in the class's group of
// that name (a plain sub installed as a method takes the invocant as its first
// positional; an existing single method becomes the group's first candidate).
void insertRuntimeMulti(ClassInfo* ci, const std::string& mname, Value cand) {
    if (cand.t == VT::Code && cand.code() && !cand.code()->isMethod &&
        !cand.code()->subAsMethod) {
        auto clone = std::make_shared<Callable>(*cand.code());
        clone->subAsMethod = true;
        Value c2; c2.t = VT::Code; c2.setCode(std::move(clone));
        cand = std::move(c2);
    }
    auto it = ci->methods.find(mname);
    if (it != ci->methods.end() && it->second.t == VT::Code && it->second.code() &&
        it->second.code()->isMultiDispatcher)
        it->second.code()->candidates.push_back(cand);
    else {
        Value disp; disp.t = VT::Code; disp.setCode(std::make_shared<Callable>());
        disp.code()->name = mname;
        disp.code()->isMultiDispatcher = true;
        disp.code()->isMethod = true;   // the GROUP is a Method, as a declared one is
        if (it != ci->methods.end() && it->second.t == VT::Code)
            disp.code()->candidates.push_back(it->second);
        disp.code()->candidates.push_back(cand);
        ci->methods[mname] = disp;
    }
}

std::optional<Value> Interpreter::methodCallPart2(const Value& inv, const MName& m, ValueList& args,
                                     const std::vector<ExprPtr>* rwArgs) {
    if (inv.t == VT::Hash && inv.hashKind == "Supply") {
        // This arm REWRITES the invocant (drainSupplyBlock) and then reads it for
        // the rest of the block. The parameter is a const reference — the dispatch
        // path stopped copying the invocant on every call — so take the copy here,
        // where it is paid only by Supply methods. The shadow keeps the rest of the
        // block exactly as it was.
        Value invLocal = inv;
        Value& inv = invLocal;
        // .schedule-on($scheduler) hops emissions onto that scheduler in Rakudo;
        // rakupp's taps already run cooperatively, so the identity is the
        // faithful translation (Cro's HTTP/2 frame tests pipe through it)
        if (m == "schedule-on") return inv;
        // Supply.Promise: a Promise kept with the LAST value the Supply emits when it
        // is done (broken if it quits). Drives the supply via tapSupply. Cro coerces
        // `Promise(supply {…})` here (body parsers).
        if (m == "Promise") {
            Value p = Value::makeHash(); p.hashKind = "Promise";
            auto ps = std::make_shared<PromiseState>();
            p.extM() = ps;
            (*p.hash())["status"] = Value::str("Planned");
            auto ph = p.hashS(); // shared: settle lives on the supplier, the Promise may die first
            auto last = std::make_shared<Value>(Value::any());
            auto settle = [ps, ph](bool broke, Value v) {
                std::vector<std::function<void()>> fire;
                { std::lock_guard<std::mutex> lk(ps->m);
                  if (ps->done) return;
                  ps->done = true; if (broke) { ps->broken = true; ps->cause = v; ps->causeMsg = v.toStr(); } else ps->result = v;
                  fire.swap(ps->thens); ps->cv.notify_all(); }
                (*ph)["status"] = Value::str(broke ? "Broken" : "Kept");
                if (!broke) (*ph)["result"] = v;
                for (auto& f : fire) f();
            };
            Value emitCb; emitCb.t = VT::Code; emitCb.setCode(std::make_shared<Callable>());
            emitCb.code()->builtin = [last](Interpreter&, ValueList& a) -> Value { if (!a.empty()) *last = a[0]; return Value::any(); };
            Value doneCb; doneCb.t = VT::Code; doneCb.setCode(std::make_shared<Callable>());
            doneCb.code()->builtin = [settle, last](Interpreter&, ValueList&) -> Value { settle(false, *last); return Value::any(); };
            Value quitCb; quitCb.t = VT::Code; quitCb.setCode(std::make_shared<Callable>());
            quitCb.code()->builtin = [settle](Interpreter&, ValueList& a) -> Value { settle(true, a.empty() ? Value::str("quit") : a[0]); return Value::any(); };
            tapSupply(inv, emitCb, doneCb, quitCb);
            return p;
        }
        // on-demand (block-holding) supply: .tap wires it for real; everything
        // else drains it eagerly (legacy value semantics) and continues below.
        // Inside a react block the legacy eager tap is kept (its whenever/done
        // bookkeeping predates the tap stack).
        // Kind-based live supplies (async socket read, OS signals, listen) are
        // driven by tapSupply, which spawns the I/O worker — `.tap` on them must
        // route there, not fall through to the from-list/eager path (which would
        // silently return an empty Tap and never start the worker).
        if ((m == "tap" || m == "act") && inv.hash()->count("kind")) {
            std::string k = inv.hash()->at("kind").toStr();
            if (k == "async-read" || k == "async-listen" || k == "signal" ||
                k == "interval" || k == "throttle" || k == "throttle-run" ||
                k == "combine" || k == "flatten" || k == "migrate") {
                Value emit = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
                Value done, quit;
                for (auto& a : args) if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "emit") emit = *a.pairVal();
                    else if (a.s == "done") done = *a.pairVal();
                    else if (a.s == "quit") quit = *a.pairVal();
                }
                return tapSupply(inv, emit, done, quit);
            }
        }
        if (inv.hash()->count("block")) {
            if (m == "live") return Value::boolean(false);
            if (m == "Supply") return inv;
            if ((m == "tap" || m == "act") && (reactStack_.empty() || !tctx_.tapStack.empty())) {
                Value emit = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
                Value done, quit;
                for (auto& a : args) if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "emit") emit = *a.pairVal();
                    else if (a.s == "done") done = *a.pairVal();
                    else if (a.s == "quit") quit = *a.pairVal();
                }
                return tapSupply(inv, emit, done, quit);
            }
            // identity/introspection must NOT run the block: isa-ok $supply, Supply
            // (Cro's composer tests) would otherwise tap the pipeline a second time
            bool introspect = m == "isa" || m == "does" || m == "WHAT" || m == "WHICH"
                || m == "WHERE" || m == "HOW" || m == "^name" || m == "defined"
                || m == "so" || m == "Bool" || m == "gist" || m == "raku" || m == "perl";
            if (m == "Channel") {
                // LIVE conversion: each emit queues as it happens, done closes the
                // channel, quit fails it. Eager-draining here would snapshot-and-
                // close, so a later $supplier.emit into the pipeline would hit
                // "receive on a closed channel" (Cro::Core drives establish().Channel
                // exactly that way in its connection-conditional tests).
                Value c = Value::makeHash(); c.hashKind = "Channel";
                (*c.hash())["queue"] = Value::array();
                (*c.hash())["closed"] = Value::boolean(false);
                auto ps = std::make_shared<PromiseState>();
                c.extM() = ps;
                Value cp = Value::makeHash(); cp.hashKind = "Promise"; cp.extM() = ps;
                (*cp.hash())["status"] = Value::str("Planned");
                (*c.hash())["closedPromise"] = cp;
                auto ch = c.hashS();
                auto settle = [ch, ps](bool failed, Value cause) {
                    std::lock_guard<std::recursive_mutex> lk(Interpreter::atomicStripe(ch.get()));
                    (*ch)["closed"] = Value::boolean(true);
                    if (failed) (*ch)["failCause"] = cause;
                    if ((*ch)["queue"].arr()->empty()) {
                        std::lock_guard<std::mutex> lk(ps->m);
                        if (!ps->done) {
                            if (failed) { ps->broken = true; ps->cause = cause; ps->causeMsg = cause.toStr(); }
                            else ps->result = Value::boolean(true);
                            ps->done = true;
                        }
                        ps->cv.notify_all();
                        (*(*ch)["closedPromise"].hash())["status"] = Value::str(failed ? "Broken" : "Kept");
                    }
                };
                Value emitCb; emitCb.t = VT::Code; emitCb.setCode(std::make_shared<Callable>());
                emitCb.code()->builtin = [ch](Interpreter&, ValueList& a) -> Value {
                    std::lock_guard<std::recursive_mutex> lk(Interpreter::atomicStripe(ch.get()));
                    if (!a.empty()) (*ch)["queue"].arr()->push_back(a[0]);
                    return Value::any();
                };
                Value doneCb; doneCb.t = VT::Code; doneCb.setCode(std::make_shared<Callable>());
                doneCb.code()->builtin = [settle](Interpreter&, ValueList&) -> Value {
                    settle(false, Value::any()); return Value::any();
                };
                Value quitCb; quitCb.t = VT::Code; quitCb.setCode(std::make_shared<Callable>());
                quitCb.code()->builtin = [settle](Interpreter&, ValueList& a) -> Value {
                    settle(true, a.empty() ? Value::str("quit") : a[0]); return Value::any();
                };
                tapSupply(inv, emitCb, doneCb, quitCb);
                return c;
            }
            if (!introspect) inv = drainSupplyBlock(inv);
        }
        bool listy = inv.hash()->count("values");
        auto vals = [&]() -> ValueList { return listy ? *(*inv.hash())["values"].arr() : ValueList{}; };
        auto mkSupply = [&](ValueList v) { Value s = Value::makeHash(); s.hashKind = "Supply"; Value a = Value::array(); *a.arr() = std::move(v); (*s.hash())["values"] = a; return s; };
        if (m == "live") {
            // A live Supply emits whether or not anything taps it — and only `map`
            // and `grep` carry that through. Every other combinator is a `supply {}`
            // block in Rakudo, which is on-demand, so `$s.Supply.lines.live` is
            // False where `$s.Supply.map(…).live` is True (S17-supply/lines.t).
            bool live = inv.hash()->count("supplier") > 0;
            if (live && inv.hash()->count("chain"))
                for (auto& step : *(*inv.hash())["chain"].arrS()) {
                    if (!step.hash()) continue;
                    const std::string sop = (*step.hash())["op"].toStr();
                    if (sop != "map" && sop != "grep") { live = false; break; }
                }
            return Value::boolean(live);
        }
        if (m == "Supply") return inv;
        // S-04: every Supply rakupp hands out delivers its events one at a time
        // to a given tap — that is what `.serial` promises, and it holds for the
        // live, list-backed, block and kind-based shapes alike.
        if (m == "serial") return Value::boolean(true);
        // ---- S-24  start: each value becomes a SUPPLY of its own that runs
        //      code(value) on the thread pool and emits the single result, then
        //      is done; an exception there is that inner supply's quit. Consume
        //      it with `.flat` or `.migrate`. It is `map` to a promise-backed
        //      supply, which is what makes it work on a live source as well as a
        //      list-backed one — and what lets the started blocks overlap.
        // On a LIVE source the started blocks must not be waited for: the test
        // that drives them has one block waiting on a Promise that a LATER value
        // keeps, so tapping the inner supply has to return at once and the value
        // arrive when it arrives. The inner supply is therefore a PRESERVING
        // supplier the settled Promise emits into — hot enough to be async, kept
        // enough that a tap which arrives after the result still sees it.
        if (!listy && m == "start" && !args.empty() && args[0].t == VT::Code) {
            Value code = args[0];
            Value mapper; mapper.t = VT::Code; mapper.setCode(std::make_shared<Callable>());
            mapper.code()->builtin = [code](Interpreter& I, ValueList& a) -> Value {
                Value v = a.empty() ? Value::any() : a[0];
                Value body; body.t = VT::Code; body.setCode(std::make_shared<Callable>());
                body.code()->builtin = [code, v](Interpreter& I2, ValueList&) -> Value {
                    ValueList one{v}; return I2.callCallable(code, one);
                };
                Value pr = I.spawnPromise(body);
                Value sup = Value::makeHash(); sup.hashKind = "Supplier";
                (*sup.hash())["taps"] = Value::array();
                (*sup.hash())["preserving"] = Value::boolean(true);
                (*sup.hash())["buffer"] = Value::array();
                Value inner = Value::makeHash(); inner.hashKind = "Supply";
                (*inner.hash())["supplier"] = sup;
                if (pr.t == VT::Hash && pr.ext()) {
                    auto ps = std::static_pointer_cast<PromiseState>(pr.ext());
                    Interpreter* ip = &I;
                    std::function<void()> settle = [ip, sup, ps]() {
                        Value s2 = sup;
                        bool broke; Value cause; std::string cm; Value res;
                        { std::lock_guard<std::mutex> lk(ps->m);
                          broke = ps->broken; cause = ps->cause; cm = ps->causeMsg; res = ps->result; }
                        try {
                            if (broke) {
                                ValueList one{cause.t == VT::Nil ? Value::str(cm) : cause};
                                ip->methodCall(s2, "quit", one);
                            } else {
                                ValueList one{res};
                                ip->methodCall(s2, "emit", one);
                                ValueList na; ip->methodCall(s2, "done", na);
                            }
                        } catch (...) {}
                    };
                    bool now = false;
                    { std::lock_guard<std::mutex> lk(ps->m); if (ps->done) now = true; else ps->thens.push_back(settle); }
                    if (now) settle();
                }
                return inner;
            };
            ValueList margs{mapper};
            return methodCall(inv, "map", margs, nullptr);
        }
        if (listy && m == "start" && !args.empty() && args[0].t == VT::Code) {
            // (a list-backed source has every value in hand, so the started
            // blocks can be awaited where their results are asked for)
            Value code = args[0];
            Value mapper; mapper.t = VT::Code; mapper.setCode(std::make_shared<Callable>());
            mapper.code()->builtin = [code](Interpreter& I, ValueList& a) -> Value {
                Value v = a.empty() ? Value::any() : a[0];
                Value body; body.t = VT::Code; body.setCode(std::make_shared<Callable>());
                body.code()->builtin = [code, v](Interpreter& I2, ValueList&) -> Value {
                    ValueList one{v}; return I2.callCallable(code, one);
                };
                Value pr = I.spawnPromise(body);
                Value blk; blk.t = VT::Code; blk.setCode(std::make_shared<Callable>());
                blk.code()->builtin = [pr](Interpreter& I2, ValueList&) -> Value {
                    Value p = pr; ValueList na;
                    Value r = I2.methodCall(p, "result", na);   // waits; raises if broken
                    ValueList one{r};
                    return I2.callBuiltin("emit", one);
                };
                Value s2 = Value::makeHash(); s2.hashKind = "Supply";
                (*s2.hash())["block"] = blk;
                return s2;
            };
            ValueList margs{mapper};
            return methodCall(inv, "map", margs, nullptr);
        }

        // S-10: `.serialize` promises no two events are delivered at once and
        // `.sanitize` additionally enforces the emit* [done|quit] grammar. Both
        // return the invocant when it already has the property — which, given
        // `.serial` above and the per-tap `ended` flag on the Supplier, it does.
        if (m == "serialize" || m == "sanitize") return inv;
        if (m == "Tappable") return Value::typeObj("Supply::Sanitize");
        if (m == "on-close") { // callback fires when the tapping supply/react block ends
            // inside a REAL supply activation the callback belongs to that
            // activation's tap: it runs when the tap closes (e.g. via `done`)
            if (!args.empty() && !tctx_.tapStack.empty() && tctx_.tapStack.back()->tap) {
                auto& th = tctx_.tapStack.back()->tap;
                std::lock_guard<std::mutex> lk(th->m);
                if (!th->closed) { th->closePhasers.push_back(args[0]); return inv; }
            }
            // no real tap: the callback belongs to the INNERMOST enclosing
            // context — an eager supply drain (tapStack) or a react block
            if (!args.empty() && !tctx_.tapStack.empty()) { tctx_.tapStack.back()->closers.push_back(args[0]); return inv; }
            if (!args.empty() && !reactStack_.empty()) {
                auto ctx = reactStack_.back();
                std::lock_guard<std::mutex> lk(ctx->m);
                ctx->closers.push_back(args[0]);
                return inv;
            }
            // S-12: outside any activation `.on-close` is a COMBINATOR — it
            // answers a supply whose every tap carries the hook, and the hook
            // runs each time that tap is closed (not once, and not on a natural
            // done). A fresh Supply value, so the invocant keeps its own hooks.
            if (!args.empty() && args[0].t == VT::Code) {
                Value s2 = Value::makeHash(); s2.hashKind = "Supply";
                *s2.hash() = *inv.hash();
                if (inv.ext()) s2.extM() = inv.extM();
                Value hooks = Value::array();
                auto old = inv.hash()->find("closers");
                if (old != inv.hash()->end() && old->second.arr()) *hooks.arr() = *old->second.arr();
                hooks.arr()->push_back(args[0]);
                (*s2.hash())["closers"] = hooks;
                return s2;
            }
            return inv;
        }
        // S-17: a kind-based live supply (an interval ticker, a socket read, an
        // OS-signal stream) has no values until something drives it. Asking for
        // its list taps it and waits for the stream to finish — which, for
        // `Supply.interval(0.02).head(3)`, is when the chain has had its three.
        // S-50: on a LIVE supply the subscription is made HERE, and the list is
        // filled as the events arrive — so values emitted between this call and
        // the moment the list is read are kept, not lost. The Array handed back
        // shares its storage with the tap, which is what makes that work without
        // blocking the thread that is about to do the emitting.
        if ((m == "list" || m == "List" || m == "Seq" || m == "eager") &&
            !listy && inv.hash()->count("supplier") && !inv.hash()->count("kind")) {
            Value o = Value::array(); o.isList = true;
            if (m == "Seq") o.s = "Seq";
            auto cell = o.arrS();
            Value emitCb; emitCb.t = VT::Code; emitCb.setCode(std::make_shared<Callable>());
            emitCb.code()->builtin = [cell](Interpreter&, ValueList& a) -> Value {
                if (!a.empty()) cell->push_back(a[0]);
                return Value::any();
            };
            tapSupply(inv, emitCb, Value::nil(), Value::nil());
            return o;
        }
        if ((m == "list" || m == "List" || m == "Seq" || m == "eager") &&
            !listy && inv.hash()->count("kind")) {
            auto out = makePayload<ValueList>();
            auto fin = std::make_shared<int>(0);          // 0 running, 1 done, 2 quit
            auto err = std::make_shared<Value>();
            Value emitCb; emitCb.t = VT::Code; emitCb.setCode(std::make_shared<Callable>());
            emitCb.code()->builtin = [out](Interpreter&, ValueList& a) -> Value {
                if (!a.empty()) out->push_back(a[0]);
                return Value::any();
            };
            Value doneCb; doneCb.t = VT::Code; doneCb.setCode(std::make_shared<Callable>());
            doneCb.code()->builtin = [fin](Interpreter&, ValueList&) -> Value { if (!*fin) *fin = 1; return Value::any(); };
            Value quitCb; quitCb.t = VT::Code; quitCb.setCode(std::make_shared<Callable>());
            quitCb.code()->builtin = [fin, err](Interpreter&, ValueList& a) -> Value {
                if (!*fin) { *fin = 2; *err = a.empty() ? Value::any() : a[0]; }
                return Value::any();
            };
            Value tapV = tapSupply(inv, emitCb, doneCb, quitCb);
            while (!*fin) sleepYield(0.001);             // releases the GIL: the ticker runs
            if (tapV.t == VT::Hash && tapV.hashKind == "Tap") { ValueList na; methodCall(tapV, "close", na); }
            if (*fin == 2) throw RakuError{*err, err->toStr()};
            Value o = Value::array(); *o.arr() = *out; o.isList = true;
            if (m == "Seq") o.s = "Seq";
            return o;
        }
        if (m == "list" || m == "List" || m == "Seq" || m == "eager") {
            // S-56: a supply that QUIT has no list — the exception that ended it
            // surfaces here, where the values are asked for.
            if (inv.hash()->count("quit-reason"))
                throw RakuError{(*inv.hash())["quit-reason"],
                                inv.hash()->count("quit-message") ? (*inv.hash())["quit-message"].toStr() : "Supply quit"};
            Value o = Value::array(); *o.arr() = vals(); o.isList = true;
            if (m == "Seq") o.s = "Seq";
            return o;
        }
        // .comb/.words/.lines all concatenate the stream FIRST and then run the Str
        // method. Applied per MESSAGE instead, `.words` over "Hello Word!".comb
        // yielded one "word" per character.
        // `.lines` on a PROCESS stream is a stream of lines, not a value to render:
        // it marks the Supply, and the split happens when the tap is fed (below).
        if (m == "lines" && !listy && inv.hash()->count("proc")) {
            Value s = Value::makeHash(); s.hashKind = "Supply";
            *s.hash() = *inv.hash();
            (*s.hash())["split"] = Value::str("lines");
            // `:!chomp` rides along to the tap-time splitter: each line keeps
            // its terminator (TAP's parse-stream reads proc output exactly so)
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "chomp")
                    (*s.hash())["split-chomp"] = Value::boolean(a.pairVal() && a.pairVal()->truthy());
            return s;
        }
        // ---- S-32  comb, per chunk with a carry. The stream is not one string:
        //      a piece that straddles a chunk boundary is completed by the next
        //      chunk, and what follows the last piece is carried forward — so a
        //      regex match never grows across a boundary.
        if (m == "comb" && listy) {
            Value what; bool haveWhat = false, haveLimit = false;
            double limit = 0;
            for (auto& a : args) {
                if (a.t == VT::Pair) continue;                 // :match changes nothing here
                if (!haveWhat) { what = a; haveWhat = true; }
                // `*` (and Inf) is "no limit", not a limit of zero
                else if (a.t == VT::Whatever) continue;
                else if (!haveLimit) {
                    double lv = a.toNum();
                    if (std::isinf(lv)) continue;
                    limit = lv; haveLimit = true;
                }
            }
            const bool byCount = haveWhat && (what.t == VT::Int || what.t == VT::Num);
            const long long n   = byCount ? what.toInt() : 0;
            const bool byRegex  = haveWhat && what.t == VT::Regex;
            const bool byNeedle = haveWhat && what.t == VT::Str && !what.s.empty();
            auto strCall = [&](Value inv2, const char* meth, ValueList a2) {
                return methodCall(inv2, meth, a2);
            };
            auto charsOf = [&](const std::string& t) {
                Value sv = Value::str(t); ValueList na;
                return strCall(sv, "chars", na).toInt();
            };
            auto substrFrom = [&](const std::string& t, long long from) {
                if (from <= 0) return t;
                Value sv = Value::str(t); ValueList a2{Value::integer(from)};
                return strCall(sv, "substr", a2).toStr();
            };
            ValueList out; std::string carry;
            bool full = false;
            auto push = [&](Value v) {
                if (full) return;
                out.push_back(std::move(v));
                if (haveLimit && (double)out.size() >= limit) full = true;
            };
            for (auto& v : vals()) {
                if (full) break;
                std::string text = carry + v.toStr();
                carry.clear();
                if (byRegex) {
                    Value sv = Value::str(text);
                    Value gAdv = Value::pair("g", Value::boolean(true));
                    gAdv.namedArg = true;               // an adverb, not a positional Pair
                    ValueList a2{what, gAdv};
                    Value ms = strCall(sv, "match", a2);
                    ValueList matches;
                    if (ms.t == VT::Array && ms.arr()) matches = *ms.arr();
                    else if (ms.t == VT::Match) matches.push_back(ms);
                    if (matches.empty()) { carry = text; continue; }
                    long long endPos = 0;
                    for (auto& mm : matches) {
                        ValueList na;
                        Value mv = mm;
                        push(Value::str(strCall(mv, "Str", na).toStr()));
                        ValueList na2; Value mv2 = mm;
                        endPos = strCall(mv2, "pos", na2).toInt();
                    }
                    carry = substrFrom(text, endPos);
                    continue;
                }
                if (byNeedle) {
                    Value sv = Value::str(text);
                    ValueList a2{what};
                    Value pieces = strCall(sv, "comb", a2);
                    if (pieces.t == VT::Array && pieces.arr()) for (auto& pc : *pieces.arr()) push(pc);
                    ValueList a3{what};
                    Value sv2 = Value::str(text);
                    Value ri = strCall(sv2, "rindex", a3);
                    long long nlen = charsOf(what.s.str());
                    std::string tail = defined(ri) ? substrFrom(text, ri.toInt() + nlen) : text;
                    long long keep = nlen - 1;
                    long long tlen = charsOf(tail);
                    carry = keep <= 0 ? std::string() : substrFrom(tail, tlen - keep < 0 ? 0 : tlen - keep);
                    continue;
                }
                if (byCount && n > 1) {
                    long long tlen = charsOf(text);
                    long long whole = (tlen / n) * n;
                    std::string head = substrFrom(text, 0);
                    {   // the part that divides evenly; the rest is carried
                        Value sv = Value::str(text);
                        ValueList a2{Value::integer(0), Value::integer(whole)};
                        head = strCall(sv, "substr", a2).toStr();
                    }
                    carry = substrFrom(text, whole);
                    Value hv = Value::str(head); ValueList a4{Value::integer(n)};
                    Value pieces = strCall(hv, "comb", a4);
                    if (pieces.t == VT::Array && pieces.arr()) for (auto& pc : *pieces.arr()) push(pc);
                    continue;
                }
                // no argument, an Int of 1 or less, or the empty string: characters
                Value sv = Value::str(text); ValueList na;
                Value pieces = strCall(sv, "comb", na);
                if (pieces.t == VT::Array && pieces.arr()) for (auto& pc : *pieces.arr()) push(pc);
            }
            // an n-character comb emits the leftover when the stream ends; a
            // regex or needle comb drops what never matched
            if (byCount && n > 1 && !carry.empty()) push(Value::str(carry));
            return mkSupply(std::move(out));
        }
        if ((m == "comb" || m == "words" || m == "lines") && listy) {
            std::string all; for (auto& v : vals()) all += v.toStr();
            Value res = methodCall(Value::str(all), m, args, rwArgs);
            ValueList segs; if (res.t == VT::Array && res.arr()) segs = *res.arr();
            return mkSupply(std::move(segs));
        }
        if (m == "split" && listy) {
            // Supply.split concatenates the stream and splits by the needle; unlike
            // Str.split, a LIMIT keeps the first N CLEAN pieces (not N-1 + the rest),
            // and :skip-empty drops empties (before the limit is applied).
            std::string all; for (auto& v : vals()) all += v.toStr();
            Value needle; bool haveNeedle = false, skipEmpty = false;
            bool haveLimit = false; double limit = 0;
            for (auto& a : args) {
                if (a.t == VT::Pair) { if (a.s == "skip-empty") skipEmpty = !a.pairVal() || a.pairVal()->truthy(); continue; }
                if (!haveNeedle) { needle = a; haveNeedle = true; continue; }
                if (!haveLimit) { haveLimit = true;
                    if (a.t == VT::Whatever) limit = INFINITY;
                    else if (a.t == VT::Code) limit = callCallable(a, {}).toNum();
                    else limit = a.toNum(); // Inf / "Inf" / 3 / "3" / -1 …
                }
            }
            Value res = methodCall(Value::str(all), "split", ValueList{needle});
            ValueList segs; if (res.t == VT::Array && res.arr()) segs = *res.arr();
            if (skipEmpty) { ValueList keep; for (auto& s : segs) if (!s.toStr().empty()) keep.push_back(s); segs.swap(keep); }
            if (haveLimit) {
                if (limit < 0 || (limit == 0)) segs.clear();
                else if (std::isfinite(limit) && (double)segs.size() > limit) segs.resize((size_t)limit);
            }
            return mkSupply(std::move(segs));
        }
        if (m == "Channel" && inv.hash()->count("supplier")) {
            // A live (supplier-backed) Supply → Channel: register a tap on the
            // supplier that pushes each emitted value into the channel queue, so
            // both `.receive`/`.poll` (the queue) and `.Supply` (re-expose) see
            // the live stream. `.Channel` must forward emits, not snapshot.
            Value c = Value::makeHash(); c.hashKind = "Channel";
            Value q = Value::array(); auto qarr = q.arrS(); // shared: the emit lambda outlives a temporary channel
            (*c.hash())["queue"] = q;
            (*c.hash())["closed"] = Value::boolean(false);
            (*c.hash())["supplier"] = (*inv.hash())["supplier"];
            Value tapRec = Value::makeHash();
            Value emitCb; emitCb.t = VT::Code; emitCb.setCode(std::make_shared<Callable>());
            emitCb.code()->builtin = [qarr](Interpreter&, ValueList& a) -> Value {
                if (!a.empty()) qarr->push_back(a[0]);
                return Value::any();
            };
            (*tapRec.hash())["emit"] = emitCb;
            // S-49: the stream's END is part of the coercion — done closes the
            // channel, quit fails it. Without these the reader of `for @$c` hung
            // on a finished supply, and an unhandled quit had no handler to go
            // to and unwound the emitter instead.
            auto ps = std::make_shared<PromiseState>();
            c.extM() = ps;
            Value cp = Value::makeHash(); cp.hashKind = "Promise"; cp.extM() = ps;
            (*cp.hash())["status"] = Value::str("Planned");
            (*c.hash())["closedPromise"] = cp;
            auto ch = c.hashS();
            auto settle = [ch, ps](bool failed, Value cause) {
                std::lock_guard<std::recursive_mutex> lk(Interpreter::atomicStripe(ch.get()));
                (*ch)["closed"] = Value::boolean(true);
                if (failed) (*ch)["failCause"] = cause;
                if ((*ch)["queue"].arr()->empty()) {
                    std::lock_guard<std::mutex> plk(ps->m);
                    if (!ps->done) {
                        if (failed) { ps->broken = true; ps->cause = cause; ps->causeMsg = cause.toStr(); }
                        else ps->result = Value::boolean(true);
                        ps->done = true;
                    }
                    ps->cv.notify_all();
                    (*(*ch)["closedPromise"].hash())["status"] = Value::str(failed ? "Broken" : "Kept");
                }
            };
            Value doneCb; doneCb.t = VT::Code; doneCb.setCode(std::make_shared<Callable>());
            doneCb.code()->builtin = [settle](Interpreter&, ValueList&) -> Value { settle(false, Value::any()); return Value::any(); };
            Value quitCb; quitCb.t = VT::Code; quitCb.setCode(std::make_shared<Callable>());
            quitCb.code()->builtin = [settle](Interpreter&, ValueList& a) -> Value {
                settle(true, a.empty() ? Value::str("quit") : a[0]); return Value::any();
            };
            (*tapRec.hash())["done"] = doneCb;
            (*tapRec.hash())["quit"] = quitCb;
            Value sup = (*inv.hash())["supplier"];
            if (sup.t == VT::Hash && sup.hash()->count("taps")) (*sup.hash())["taps"].arr()->push_back(tapRec);
            return c;
        }
        if (m == "Channel") { // drain a (from-list) Supply into a closed Channel
            Value c = Value::makeHash(); c.hashKind = "Channel";
            Value q = Value::array(); *q.arr() = vals(); (*c.hash())["queue"] = q;
            (*c.hash())["closed"] = Value::boolean(true);
            auto ps = std::make_shared<PromiseState>(); ps->done = true; ps->result = Value::boolean(true); c.extM() = ps;
            Value cp = Value::makeHash(); cp.hashKind = "Promise"; cp.extM() = ps; (*cp.hash())["status"] = Value::str("Kept");
            (*c.hash())["closedPromise"] = cp;
            return c;
        }
        // (`.elems` is S-41's running-count SUPPLY, handled with the other
        // combinators below — not the plain count it used to answer.)
        if (m == "tap" || m == "act") {
            Value emit = args.empty() ? Value::nil() : args[0];
            Value done, quit, tapCb;
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal()) {
                if (a.s == "done") done = *a.pairVal();
                else if (a.s == "quit") quit = *a.pairVal();
                else if (a.s == "emit") emit = *a.pairVal();
                else if (a.s == "tap") tapCb = *a.pairVal();   // S-02: sees the Tap before any value flows
            }
            // (Rakudo also rejects a tap block that cannot ACCEPT a value —
            // `-> { … }` as against `{ … }`, Roast basic.t. rakupp's parser
            // records both as a block with an empty parameter list, so the two
            // are indistinguishable here; telling them apart is a parser change.)
            if (inv.hash()->count("supplier")) {
                // live Supply: register the callbacks with the Supplier; emit/done fan out later
                Value tapRec = Value::makeHash();
                (*tapRec.hash())["emit"] = emit; (*tapRec.hash())["done"] = done; (*tapRec.hash())["quit"] = quit;
                // carry any transform chain, giving each step its own fresh mutable state
                if (inv.hash()->count("chain")) {
                    Value chain = Value::array();
                    for (auto& step : *(*inv.hash())["chain"].arr()) {
                        Value s2 = Value::makeHash(); *s2.hash() = *step.hash();
                        Value st0 = Value::makeHash();
                        // when this subscription began: a bucketed step (`.elems($s)`)
                        // measures its first bucket from here, not from its first value
                        (*st0.hash())["t0"] = Value::number(epochNowSecs());
                        (*s2.hash())["state"] = st0;
                        chain.arr()->push_back(s2);
                    }
                    (*tapRec.hash())["chain"] = chain;
                }
                if (inv.hash()->count("closers")) (*tapRec.hash())["closers"] = (*inv.hash())["closers"];
                Value sup = (*inv.hash())["supplier"];
                bool registered = false;
                if (sup.t == VT::Hash && sup.hash()->count("taps")) {
                    // registration and replay are one step (see tapSupply)
                    std::lock_guard<std::recursive_mutex> regLk(supplierMutex(sup.hash()));
                    (*sup.hash())["taps"].arr()->push_back(tapRec);
                    replayPreserved(sup, tapRec);
                    registered = true;
                }
                Value tapVal = tapRec; tapVal.hashKind = "Tap"; // shares the record's hash so .close can mark it closed
                if (tapCb.t == VT::Code) { ValueList one{tapVal}; callCallable(tapCb, one); }
                (void)registered;   // the replay happened with the registration
                return tapVal;
            }
            // S-02: the :tap callback receives the Tap before any value flows,
            // so it is built here rather than at the bottom of this arm.
            Value eagerTap = Value::makeHash(); eagerTap.hashKind = "Tap";
            if (inv.hash()->count("closers")) (*eagerTap.hash())["closers"] = (*inv.hash())["closers"];
            if (tapCb.t == VT::Code) { ValueList one{eagerTap}; callCallable(tapCb, one); }
            // eager: push every value to the emit callback, then run the done phaser
            // (or, if the supply block died, the quit callback with the reason).
            if (listy) {
                if (emit.t == VT::Code) for (auto& v : vals()) {
                    ValueList one{v};
                    // `next` in a whenever skips this value; `last` stops the stream
                    try { callCallable(emit, one); }
                    catch (NextEx&) {}
                    catch (LastEx&) { break; }
                    // `done` inside the block closes the enclosing react: stop emitting
                    if (!reactStack_.empty() && reactStack_.back()->closed) break;
                }
                if (inv.hash()->count("quit-reason")) {
                    if (quit.t == VT::Code) { ValueList one{(*inv.hash())["quit-reason"]}; callCallable(quit, one); }
                    else // unhandled: the supply's death propagates to the tapper (react dies)
                        throw RakuError{(*inv.hash())["quit-reason"],
                                        inv.hash()->count("quit-message") ? (*inv.hash())["quit-message"].toStr() : "Supply quit"};
                }
                else if (done.t == VT::Code) { ValueList none; callCallable(done, none); }
            } else if (!args.empty() && args[0].t == VT::Code && inv.hash()->count("proc")) {
                // a Proc::Async stream (zef's test/build/fetch backends are all
                // written as `whenever $proc.stdout.lines { … }`): park the
                // {emit, done, quit, bin} record on the proc for runProcPromise
                registerProcStreamTap(inv, args[0], done, quit);
            }
            return eagerTap;
        }
        if (listy && (m == "min" || m == "max")) {
            // Supply.min/max is a *running* extreme: every value that strictly
            // improves on what came before. S-43: a one-parameter `&by` is a KEY
            // EXTRACTOR and the keys are compared with cmp; a two-parameter one
            // is the COMPARATOR itself. An undefined value is skipped.
            bool wantMax = (m == "max");
            Value by = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
            bool cmpFn = by.t == VT::Code && supplyByArity(by) >= 2;
            ValueList out; bool have = false; Value best, bestKey;
            for (auto& v : vals()) {
                if (!defined(v)) continue;
                bool better;
                if (cmpFn) {
                    if (!have) better = true;
                    else { ValueList two{v, best}; long long c = callCallable(by, two).toInt(); better = wantMax ? c > 0 : c < 0; }
                    if (better) { out.push_back(v); best = v; have = true; }
                    continue;
                }
                Value key = v;
                if (by.t == VT::Code) { ValueList one{v}; key = callCallable(by, one); }
                better = !have || (wantMax ? valueCmp(key, bestKey) > 0 : valueCmp(key, bestKey) < 0);
                if (better) { out.push_back(v); best = v; bestKey = key; have = true; }
            }
            return mkSupply(out);
        }
        if (listy && m == "grab") { // hand the whole stream (as $_) to a collector, emit its result
            if (!args.empty() && args[0].t == VT::Code) {
                Value listArg = Value::array(); *listArg.arr() = vals(); listArg.isList = true;
                ValueList one{listArg}; Value r = callCallable(args[0], one);
                return mkSupply(r.t == VT::Array ? *r.arr() : r.flatten());
            }
            return mkSupply(vals());
        }
        if (listy && (m == "produce" || m == "reduce")) { // scan (produce) / fold (reduce) over the stream
            Value op = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
            ValueList out; Value acc; bool first = true;
            for (auto& v : vals()) {
                if (first) { acc = v; first = false; }
                else if (op.t == VT::Code) { ValueList two{acc, v}; acc = callCallable(op, two); }
                if (m == "produce") out.push_back(acc);
            }
            // S-28: an empty source still emits ONE value — Nil, as List.reduce
            // answers for an empty list.
            if (m == "reduce") return mkSupply(first ? ValueList{Value::nil()} : ValueList{acc});
            return mkSupply(out);
        }
        if (listy && m == "minmax") { // emit the running (min..max) Range after each value
            Value by = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
            bool cmpFn = by.t == VT::Code && supplyByArity(by) >= 2;
            ValueList out; Value mn, mx, mnK, mxK; bool first = true;
            auto worse = [&](const Value& a, const Value& b) {   // a sorts before b
                if (cmpFn) { ValueList two{a, b}; return callCallable(by, two).toInt() < 0; }
                return valueCmp(a, b) < 0;
            };
            for (auto& v : vals()) {
                Value key = v;
                if (by.t == VT::Code && !cmpFn) { ValueList one{v}; key = callCallable(by, one); }
                else if (cmpFn) key = v;
                bool changed = first;
                if (first) { mn = mx = v; mnK = mxK = key; first = false; }
                else {
                    if (worse(key, mnK)) { mn = v; mnK = key; changed = true; }
                    if (worse(mxK, key)) { mx = v; mxK = key; changed = true; }
                }
                if (!changed) continue; // only emit when the running min..max actually widens
                // The endpoints are the VALUES, kept as the objects they are —
                // a Str range of more than one character has no integer form to
                // walk, but `.min`, `.max` and `.raku` all read the endpoints.
                if (mn.t == VT::Str || mx.t == VT::Str) {
                    Value rg = Value::range(0, 0, false, false);
                    attachRangeEnds(rg, mn, mx);
                    out.push_back(rg);
                } else {
                    Value rg = Value::range(mn.toInt(), mx.toInt(), false, false);
                    out.push_back(rg);
                }
            }
            return mkSupply(out);
        }
        // ---- S-47  throttle, in its two forms.
        //   throttle($elems, &process, …): at most $elems calls of process(value)
        //     run at a time, and the supply emits the finished PROMISES, not
        //     their results — so the consumer decides when to await them.
        //   throttle($elems, $seconds, $delay = 0, …): at most $elems values pass
        //     per $seconds tick; the rest wait their turn, and the supply is done
        //     only once the waiting ones have gone through.
        if (m == "throttle" && !args.empty()) {
            Value process;
            double seconds = 0, delay = 0;
            long long elems = args[0].toInt();
            size_t pos = 0;
            for (auto& a : args) {
                if (a.t == VT::Pair) continue;
                pos++;
                if (pos == 2) { if (a.t == VT::Code) process = a; else seconds = a.toNum(); }
                else if (pos == 3) delay = a.toNum();
            }
            if (process.t == VT::Code) {
                // The concurrency form: at most `$elems` calls of process(value)
                // run at a time, and the supply emits the PROMISES, in value
                // order, as each is started. A `:control` supply may change the
                // allowance while it runs — a throttle started at 0 does nothing
                // until it is let go — and `:status` receives a report at the end.
                Value s2 = Value::makeHash(); s2.hashKind = "Supply";
                (*s2.hash())["kind"] = Value::str("throttle-run");
                (*s2.hash())["src"] = inv;
                (*s2.hash())["elems"] = Value::integer(elems > 0 ? elems : 0);
                (*s2.hash())["process"] = process;
                (*s2.hash())["delay"] = Value::number(delay);
                for (auto& a : args)
                    if (a.t == VT::Pair && a.pairVal() && (a.s == "control" || a.s == "status"))
                        (*s2.hash())[a.s] = *a.pairVal();
                return s2;
            }
            if (seconds <= 0) return inv;          // no tick: nothing to pace
            Value s2 = Value::makeHash(); s2.hashKind = "Supply";
            (*s2.hash())["kind"] = Value::str("throttle");
            (*s2.hash())["src"] = inv;
            (*s2.hash())["elems"] = Value::integer(elems > 0 ? elems : 0);
            (*s2.hash())["seconds"] = Value::number(seconds);
            (*s2.hash())["delay"] = Value::number(delay);
            // `:control` is a Supply of commands — "limit:N" raises or lowers how
            // many may pass per tick while the stream is running, which is how a
            // throttle started at 0 is let go later.
            for (auto& a : args)
                if (a.t == VT::Pair && a.pairVal() && (a.s == "control" || a.s == "status"))
                    (*s2.hash())[a.s] = *a.pairVal();
            return s2;
        }
        // ---- S-48  share: a HOT supply. It subscribes to the source at once and
        //      hands the events to every tap of the result, so whatever the
        //      source produced before a tap existed — including its done — is
        //      lost to that tap. On a finished list-backed source that means
        //      everything, and `.share.list` never completes; a Supplier-backed
        //      one fans out live to all the taps, which is the point of it.
        if (m == "share") {
            Value sup = Value::makeHash(); sup.hashKind = "Supplier";
            (*sup.hash())["taps"] = Value::array();
            Value out = Value::makeHash(); out.hashKind = "Supply";
            (*out.hash())["supplier"] = sup;
            // subscribe NOW: the events start flowing into a Supplier that so
            // far has no taps, and are dropped
            Value emitCb; emitCb.t = VT::Code; emitCb.setCode(std::make_shared<Callable>());
            Value supCopy = sup;
            emitCb.code()->builtin = [supCopy](Interpreter& I, ValueList& a) -> Value {
                ValueList one{a.empty() ? Value::any() : a[0]};
                Value s2 = supCopy; return I.methodCall(s2, "emit", one);
            };
            Value doneCb; doneCb.t = VT::Code; doneCb.setCode(std::make_shared<Callable>());
            doneCb.code()->builtin = [supCopy](Interpreter& I, ValueList&) -> Value {
                ValueList na; Value s2 = supCopy; return I.methodCall(s2, "done", na);
            };
            Value quitCb; quitCb.t = VT::Code; quitCb.setCode(std::make_shared<Callable>());
            quitCb.code()->builtin = [supCopy](Interpreter& I, ValueList& a) -> Value {
                ValueList one{a.empty() ? Value::any() : a[0]};
                Value s2 = supCopy; try { return I.methodCall(s2, "quit", one); } catch (...) { return Value::any(); }
            };
            tapSupply(inv, emitCb, doneCb, quitCb);
            return out;
        }
        // ---- S-34  encode / decode. `.encode` turns each Str into a Blob of the
        //      named encoding. `.decode` goes the other way and HOLDS BACK the
        //      last character of each decoded chunk until the next chunk or the
        //      end, so a grapheme split across two chunks is never emitted in
        //      halves — which means a one-chunk stream emits nothing until it
        //      completes.
        if (listy && (m == "encode" || m == "decode")) {
            ValueList out;
            if (m == "encode") {
                for (auto& v : vals()) {
                    Value sv = v;
                    out.push_back(methodCall(sv, "encode", args, rwArgs));
                }
                return mkSupply(std::move(out));
            }
            std::string held;
            for (auto& v : vals()) {
                Value bv = v;
                Value dv = methodCall(bv, "decode", args, rwArgs);
                std::string text = held + dv.toStr();
                held.clear();
                Value tv = Value::str(text);
                ValueList na;
                Value chs = methodCall(tv, "comb", na);
                if (!(chs.t == VT::Array && chs.arr()) || chs.arr()->empty()) continue;
                auto& cs = *chs.arr();
                std::string head;
                for (size_t i = 0; i + 1 < cs.size(); i++) head += cs[i].toStr();
                held = cs.back().toStr();               // the last character waits
                if (!head.empty()) out.push_back(Value::str(head));   // the chunk, one value
            }
            if (!held.empty()) out.push_back(Value::str(held));
            return mkSupply(std::move(out));
        }
        // ---- snip: cut the stream into sublists. Each test in turn is the
        //      boundary that ENDS the current piece — the value that matches it
        //      starts the next one, and the test is then spent. What is left
        //      after the last test is one final piece, emitted when the stream
        //      completes.
        if (listy && m == "snip") {
            ValueList tests;
            for (auto& a : args) if (a.t != VT::Pair) tests.push_back(a);
            ValueList out, chunk;
            size_t ti = 0;
            auto hits = [&](const Value& t, const Value& v) {
                ValueList one{v};
                if (t.t == VT::Code) return predAnswerTruthy(*this, callCallable(t, one), v);
                if (t.t == VT::Regex) return regexMatch(v.toStr(), t.s.str()).truthy();
                return applyArith("~~", v, t).truthy();
            };
            for (auto& v : vals()) {
                if (ti < tests.size() && hits(tests[ti], v)) {
                    Value b = Value::array(); b.isList = true; *b.arr() = chunk;
                    out.push_back(b);
                    chunk.clear();
                    ti++;
                }
                chunk.push_back(v);
            }
            if (!chunk.empty()) { Value b = Value::array(); b.isList = true; *b.arr() = chunk; out.push_back(b); }
            return mkSupply(std::move(out));
        }
        // ---- S-26  flat: a supply of supplies. Every inner supply contributes
        //      all of its values, in the order the outer one hands them over.
        if (listy && m == "flat") {
            ValueList out;
            bool anySupply = false;
            for (auto& v : vals()) if (v.t == VT::Hash && v.hashKind == "Supply") { anySupply = true; break; }
            if (!anySupply) {
                Value arr = Value::array(); *arr.arr() = vals(); arr.isList = true;
                Value r = methodCall(arr, "flat", args, rwArgs);
                return mkSupply(r.t == VT::Array && r.arr() ? *r.arr() : ValueList{r});
            }
            for (auto& v : vals()) {
                if (v.t == VT::Hash && v.hashKind == "Supply") {
                    // an inner supply that quit ends the flattened stream there:
                    // its reason becomes this supply's (S-06 again — nothing
                    // follows a quit)
                    if (v.hash() && v.hash()->count("quit-reason")) {
                        Value s3 = mkSupply(std::move(out));
                        (*s3.hash())["quit-reason"] = (*v.hash())["quit-reason"];
                        if (v.hash()->count("quit-message")) (*s3.hash())["quit-message"] = (*v.hash())["quit-message"];
                        return s3;
                    }
                    ValueList na; Value iv = v;
                    Value l;
                    try { l = methodCall(iv, "list", na); }
                    catch (RakuError& e) {          // the inner supply quit as it ran
                        Value s3 = mkSupply(std::move(out));
                        (*s3.hash())["quit-reason"] = exceptionFor(e);
                        (*s3.hash())["quit-message"] = Value::str(e.message);
                        return s3;
                    }
                    if (l.t == VT::Array && l.arr()) for (auto& x : *l.arr()) out.push_back(x);
                } else out.push_back(v);
            }
            return mkSupply(std::move(out));
        }
        // ---- S-20  first: `.first` is `.head`, `.first(test)` is
        //      `.grep(test).head`, and `:end` takes the last match instead. An
        //      empty source yields an EMPTY supply, not one holding Nil.
        if (listy && m == "first") {
            ValueList src = vals();
            bool wantEnd = false; Value test; bool haveTest = false;
            for (auto& a : args) {
                if (a.t == VT::Pair) { if (a.s == "end") wantEnd = !a.pairVal() || a.pairVal()->truthy(); continue; }
                if (!haveTest) { test = a; haveTest = true; }
            }
            ValueList hits;
            for (auto& v : src) {
                if (!haveTest) { hits.push_back(v); continue; }
                ValueList one{v};
                bool ok = test.t == VT::Code  ? predAnswerTruthy(*this, callCallable(test, one), v)
                        : test.t == VT::Regex ? regexMatch(v.toStr(), test.s.str()).truthy()
                                              : applyArith("~~", v, test).truthy();
                if (ok) hits.push_back(v);
            }
            if (hits.empty()) return mkSupply({});
            return mkSupply(ValueList{wantEnd ? hits.back() : hits.front()});
        }
        // ---- S-44  collate: a grab that sorts by the collation order
        if (listy && m == "collate") {
            Value arr = Value::array(); *arr.arr() = vals(); arr.isList = true;
            Value r = methodCall(arr, "collate", args, rwArgs);
            return mkSupply(r.t == VT::Array && r.arr() ? *r.arr() : ValueList{r});
        }
        // ---- S-42  tail on an EMPTY source emits Any (a quirk, but a caller
        //      asking for the last value of nothing is told "nothing", not Nil)
        if (listy && m == "tail" && args.empty() && vals().empty())
            return mkSupply(ValueList{Value::any()});
        // ---- S-30  migrate: the values must be Supplies. Each new one replaces
        //      the previous, whose subscription is dropped; on a list-backed
        //      source the inner supplies have all finished, so every value of
        //      each of them is emitted in turn.
        if (listy && m == "migrate") {
            ValueList out;
            for (auto& v : vals()) {
                if (!(v.t == VT::Hash && v.hashKind == "Supply"))
                    throw RakuError{Value::typeObj("X::Supply::Migrate::Needs"),
                                    "migrate expects Supply values"};
                ValueList na;
                Value l = methodCall(const_cast<Value&>(v), "list", na);
                if (l.t == VT::Array && l.arr()) for (auto& x : *l.arr()) out.push_back(x);
            }
            return mkSupply(out);
        }
        // ---- S-31  classify / categorize emit `key => Supply` PAIRS, one per key
        //      in order of first appearance. The inner supplies replay to a late
        //      tapper, which a list-backed one does by construction. Keys are
        //      compared by identity, so 1 and "1" are different keys.
        if (listy && (m == "classify" || m == "categorize") && !args.empty() && args[0].t == VT::Code) {
            std::vector<std::string> order;
            std::map<std::string, std::pair<Value, ValueList>> groups;  // WHICH -> (key, values)
            for (auto& v : vals()) {
                ValueList one{v};
                Value r = callCallable(args[0], one);
                ValueList keys;
                if (m == "categorize") { if (r.t == VT::Array && r.arr()) keys = *r.arr(); else keys.push_back(r); }
                else keys.push_back(r);
                for (auto& k : keys) {
                    std::string id = whichOf(k);
                    auto it = groups.find(id);
                    if (it == groups.end()) { order.push_back(id); groups.emplace(id, std::make_pair(k, ValueList{v})); }
                    else it->second.second.push_back(v);
                }
            }
            ValueList out;
            for (auto& id : order) {
                auto& g = groups[id];
                Value inner = mkSupply(g.second);
                Value pr = Value::pair(g.first.toStr(), inner);
                // a key that is not a Str keeps its object: `1 => …` and
                // `"1" => …` are different pairs (S-31's identity rule)
                if (g.first.t != VT::Str) pr.pairKeyM() = std::make_shared<Value>(g.first);
                out.push_back(pr);
            }
            return mkSupply(out);
        }
        // ---- S-37  repeated: a value on its second and every later occurrence
        if (listy && m == "repeated") {
            Value asFn, withFn;
            for (auto& a : args) {
                if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "as") asFn = *a.pairVal();
                    else if (a.s == "with") withFn = *a.pairVal();
                } else if (a.t == VT::Code && asFn.t != VT::Code) asFn = a;
            }
            ValueList seen, out;
            for (auto& v : vals()) {
                Value key = v;
                if (asFn.t == VT::Code) { ValueList one{v}; key = callCallable(asFn, one); }
                bool known = false;
                for (auto& k : seen) {
                    if (withFn.t == VT::Code) { ValueList two{k, key}; if (callCallable(withFn, two).truthy()) { known = true; break; } }
                    else if (whichOf(k) == whichOf(key)) { known = true; break; }
                }
                if (known) out.push_back(v); else seen.push_back(key);
            }
            return mkSupply(out);
        }
        // ---- S-36  squish: :with is called with the previously KEPT value first
        if (listy && m == "squish") {
            Value asFn, withFn;
            for (auto& a : args) {
                if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "as") asFn = *a.pairVal();
                    else if (a.s == "with") withFn = *a.pairVal();
                } else if (a.t == VT::Code && asFn.t != VT::Code) asFn = a;
            }
            ValueList out; Value lastKey; bool have = false;
            for (auto& v : vals()) {
                Value key = v;
                if (asFn.t == VT::Code) { ValueList one{v}; key = callCallable(asFn, one); }
                bool same = false;
                if (have) {
                    if (withFn.t == VT::Code) { ValueList two{lastKey, key}; same = callCallable(withFn, two).truthy(); }
                    else same = whichOf(lastKey) == whichOf(key);
                }
                if (!same) { out.push_back(v); lastKey = key; have = true; }
            }
            return mkSupply(out);
        }
        // ---- S-38  rotor: the batches are ARRAYS, and the cycle repeats
        if (listy && m == "rotor") {
            Value arr = Value::array(); *arr.arr() = vals(); arr.isList = true;
            Value r = methodCall(arr, m, args, rwArgs);
            ValueList out;
            if (r.t == VT::Array && r.arr())
                for (auto& b : *r.arr()) {
                    Value a2 = Value::array();
                    if (b.t == VT::Array && b.arr()) *a2.arr() = *b.arr(); else a2.arr()->push_back(b);
                    out.push_back(a2);          // an Array, not a List
                }
            return mkSupply(out);
        }
        // ---- S-39  batch: one-element batches by default; :elems groups
        if (listy && m == "batch") {
            long long n = 0; bool timed = false;
            for (auto& a : args) {
                if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "elems") n = a.pairVal()->toInt();
                    // :seconds and :emit-timed close a batch on the wall clock;
                    // a list-backed source arrives all at once, so one batch.
                    else if (a.s == "seconds" || a.s == "emit-timed") timed = true;
                } else if (a.t == VT::Int) n = a.toInt();
            }
            ValueList src = vals(), out;
            if (timed && n < 1) {               // a timed batch of a finished list: one batch
                if (!src.empty()) { Value b = Value::array(); *b.arr() = src; b.isList = true; out.push_back(b); }
                return mkSupply(out);
            }
            if (n < 1) n = 1;                   // no :elems, or :elems(0) or less
            for (size_t i = 0; i < src.size(); i += (size_t)n) {
                Value b = Value::array(); b.isList = true;
                for (size_t j = i; j < src.size() && j < i + (size_t)n; j++) b.arr()->push_back(src[j]);
                out.push_back(b);
            }
            return mkSupply(out);
        }
        // ---- S-41  elems: the RUNNING count after every value. `elems($seconds)`
        //      reports at most once per bucket and, at done, the final count when
        //      it has not been reported — Rakudo never emits that last one (the
        //      sheet flags it as a bug); a list-backed source finishes inside one
        //      bucket, so the total is exactly what it owes.
        if (listy && m == "elems") {
            ValueList src = vals(), out;
            bool timed = !args.empty() && args[0].t != VT::Pair && args[0].toNum() > 0;
            if (timed) { if (!src.empty()) out.push_back(Value::integer((long long)src.size())); }
            else for (size_t i = 0; i < src.size(); i++) out.push_back(Value::integer((long long)i + 1));
            return mkSupply(out);
        }
        // `produce`/`reduce` fold with an OPERATOR: anything else is a signature
        // error, not a value to fold with (Roast produce.t, reduce.t).
        if ((m == "produce" || m == "reduce") && !args.empty() && args[0].t != VT::Code
            && args[0].t != VT::Pair)
            throw RakuError{Value::typeObj("X::TypeCheck::Binding::Parameter"),
                            "Type check failed in binding to parameter '&with'; "
                            "expected Callable but got " + args[0].typeName()};
        // S-26/S-30 on a source whose values do not exist yet: hand back the spec
        // and let each tap subscribe for itself (tapSupply's flatten/migrate arm).
        if (!listy && (m == "flat" || m == "migrate")) {
            Value s2 = Value::makeHash(); s2.hashKind = "Supply";
            (*s2.hash())["kind"] = Value::str(m == "flat" ? "flatten" : "migrate");
            (*s2.hash())["src"] = inv;
            return s2;
        }
        if (m == "zip" || m == "merge" || m == "zip-latest") {
            // $s.zip($other, …) — the invocant is the first stream; reuse the class-method logic.
            ValueList a2; a2.push_back(inv); for (auto& a : args) a2.push_back(a);
            return methodCall(Value::typeObj("Supply"), m, a2, rwArgs);
        }
        // Live-Supply combinators: build a lazy transform chain that runs per emitted
        // value when the resulting Supply is tapped (see applyTapChain + emit fan-out).
        if (!listy && inv.hash()->count("supplier") &&
            (m == "map" || m == "grep" || m == "head" || m == "skip" ||
             m == "first" || m == "unique" || m == "squish" ||
             m == "lines" || m == "words" ||
             m == "produce" || m == "reduce" || m == "elems" ||
             m == "batch" || m == "classify" || m == "categorize")) {
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash())["supplier"] = (*inv.hash())["supplier"];
            Value chain = Value::array();
            if (inv.hash()->count("chain")) *chain.arr() = *(*inv.hash())["chain"].arrS();
            Value step = Value::makeHash();
            (*step.hash())["op"] = Value::str(m);
            for (auto& a : args) if (a.t != VT::Pair) { (*step.hash())["arg"] = a; break; }
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal() && (a.s == "as" || a.s == "with" || a.s == "chomp" || a.s == "expires" || a.s == "elems" || a.s == "seconds")) (*step.hash())[a.s] = *a.pairVal();
            (*step.hash())["state"] = Value::makeHash();
            chain.arr()->push_back(step);
            (*s.hash())["chain"] = chain;
            return s;
        }
        // Same for a kind-based live supply (Supply.interval, signal(…), the async
        // socket streams): keep the whole spec — every `kind` check downstream must
        // still recognise it — and record the step. wrapSupplyChain applies it when
        // the supply is finally tapped or `whenever`ed. Without this the combinator
        // fell through to the generic Hash path and called the block ONCE, with the
        // spec hash itself as the topic.
        if (!listy && !inv.hash()->count("supplier") && inv.hash()->count("kind") &&
            (m == "map" || m == "grep" || m == "head" || m == "skip" ||
             m == "first" || m == "unique" || m == "squish" ||
             m == "lines" || m == "words" ||
             m == "produce" || m == "reduce" || m == "elems" ||
             m == "batch" || m == "classify" || m == "categorize")) {
            Value s = Value::makeHash(); s.hashKind = "Supply";
            *s.hash() = *inv.hash();
            Value chain = Value::array();
            if (inv.hash()->count("chain")) *chain.arr() = *(*inv.hash())["chain"].arrS();
            Value step = Value::makeHash();
            (*step.hash())["op"] = Value::str(m);
            for (auto& a : args) if (a.t != VT::Pair) { (*step.hash())["arg"] = a; break; }
            for (auto& a : args) if (a.t == VT::Pair && a.pairVal() && (a.s == "as" || a.s == "with" || a.s == "chomp" || a.s == "expires" || a.s == "elems" || a.s == "seconds")) (*step.hash())[a.s] = *a.pairVal();
            (*step.hash())["state"] = Value::makeHash();
            chain.arr()->push_back(step);
            (*s.hash())["chain"] = chain;
            return s;
        }
        // S-14/S-18/S-25: a one-source operator whose user code dies turns that
        // death into the supply's QUIT — and NOTHING follows a quit. Rakudo lets
        // the values after the failing one through (its own grammar says it
        // should not: the sheet flags that as a bug); here the stream stops.
        // The values produced before the death are kept, so a tapper sees
        // `1`, then the quit.
        if (listy && (m == "map" || m == "grep" || m == "do") &&
            !args.empty() && args[0].t == VT::Code &&
            !inv.hash()->count("quit-reason")) {
            ValueList src = vals(), out;
            bool quit = false; Value quitReason; std::string quitMsg;
            for (auto& v : src) {
                ValueList one{v};
                try {
                    if (m == "map") { Value r = callCallable(args[0], one); out.push_back(r); }
                    else if (m == "do") { callCallable(args[0], one); out.push_back(v); }
                    else if (callCallable(args[0], one).truthy()) out.push_back(v);
                } catch (RakuError& e) {
                    quit = true; quitReason = exceptionFor(e); quitMsg = e.message; break;
                }
            }
            Value s2 = mkSupply(std::move(out));
            if (quit) { (*s2.hash())["quit-reason"] = quitReason; (*s2.hash())["quit-message"] = Value::str(quitMsg); }
            return s2;
        }
        if (listy && m == "do") return mkSupply(vals()); // side-effect-free arg: pass through
        if (listy && (m == "map" || m == "grep" || m == "head" || m == "tail" || m == "skip" ||
                      m == "first" ||
                      m == "reverse" || m == "sort" || m == "unique" || m == "squish" || m == "rotor" ||
                      m == "rotate" || m == "sum" ||
                      m == "batch" || m == "lines" || m == "words" || m == "flat" ||
                      m == "classify" || m == "categorize" || m == "start" || m == "schedule-on" ||
                      m == "stable" || m == "delayed" || m == "migrate" || m == "on-demand")) {
            // Delegate list-transform semantics to the Array method dispatcher, then re-wrap.
            Value arr = Value::array(); *arr.arr() = vals(); arr.isList = true;
            if (m == "start" || m == "schedule-on" || m == "stable" || m == "delayed" ||
                m == "migrate" || m == "on-demand" || m == "batch") return inv; // scheduling no-ops
            Value r = methodCall(arr, m, args, rwArgs);
            if (r.t == VT::Array) return mkSupply(*r.arr());
            return mkSupply(ValueList{r});
        }
        if (m == "wait") {
            // Rakudo BLOCKS here until the supply completes. Returning True at
            // once made `$supplier.Supply.wait` a no-op, so every use of it as
            // a barrier silently raced — Log::Async's `done` is exactly that
            // (`start { sleep 0.1; $.source.done }; $.source.Supply.wait`), and
            // its remove-tap test passed or failed on interpreter speed alone.
            // A list-backed Supply is already complete; a live one waits on the
            // Supplier that feeds it.
            if (!listy && inv.hash()->count("supplier")) {
                Value sup = (*inv.hash())["supplier"];
                // subscribe BEFORE waiting, so the values that arrive during the
                // wait are the ones the answer is drawn from
                Value seen = Value::array(); seen.isList = true;
                auto cell = seen.arrS();
                Value emitCb; emitCb.t = VT::Code; emitCb.setCode(std::make_shared<Callable>());
                emitCb.code()->builtin = [cell](Interpreter&, ValueList& a) -> Value {
                    if (!a.empty()) cell->push_back(a[0]);
                    return Value::any();
                };
                tapSupply(inv, emitCb, Value::nil(), Value::nil());
                if (sup.t == VT::Hash && sup.hash()) {
                    for (;;) {
                        bool finished;
                        {   // the supplier's own stripe — done/quit are written under it
                            std::lock_guard<std::recursive_mutex> lk(supplierMutex(sup.hash()));
                            finished = (sup.hash()->count("done_state") && (*sup.hash())["done_state"].truthy())
                                    || (sup.hash()->count("quit_state") && (*sup.hash())["quit_state"].truthy());
                        }
                        if (finished) break;
                        sleepYield(0.001);   // releases the GIL, so the emitter can run
                    }
                    if (sup.hash()->count("quit_state") && (*sup.hash())["quit_state"].truthy())
                        throw RakuError{Value::typeObj("X::AdHoc"), "Supply quit"};
                }
                return cell->empty() ? Value::nil() : cell->back();
            }
            // S-51: the answer is the LAST value, Nil when there was none.
            if (inv.hash()->count("quit-reason"))
                throw RakuError{(*inv.hash())["quit-reason"],
                                inv.hash()->count("quit-message") ? (*inv.hash())["quit-message"].toStr() : "Supply quit"};
            ValueList vs = listy ? vals() : ValueList{};
            if (!listy) { ValueList na; Value l = methodCall(inv, "list", na);
                          if (l.t == VT::Array && l.arr()) vs = *l.arr(); }
            return vs.empty() ? Value::nil() : vs.back();
        }
        if (m == "done" || m == "close" || m == "quit") return Value::boolean(true);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Tap") {
        // a listening socket's tap knows where it listens (Promises, as in Rakudo)
        if ((m == "socket-host" || m == "socket-port") && inv.hash() && inv.hash()->count(m))
            return (*inv.hash())[m];
        // .close removes the tap from its source: mark it closed so emit skips it;
        // a wired tap (on-demand supply / async socket) also tears down its
        // inner taps, CLOSE phasers, and I/O workers via the TapHandle.
        if (m == "close") {
            if (inv.hash()) {
                (*inv.hash())["closed"] = Value::boolean(true);
                (*inv.hash())["ended"] = Value::boolean(true);   // S-06: no event reaches a closed tap
                // S-03/S-12: the close hooks run on EVERY close call. A copy of
                // the list, so a hook that closes again cannot walk it twice.
                auto cit = inv.hash()->find("closers");
                if (cit != inv.hash()->end() && cit->second.arr()) {
                    ValueList hooks = *cit->second.arr();
                    for (auto& h : hooks) if (h.t == VT::Code) { ValueList none; callCallable(h, none); }
                }
            }
            if (inv.ext() && inv.hash() && inv.hash()->count("wired") && (*inv.hash())["wired"].truthy())
                closeTapHandle(std::static_pointer_cast<TapHandle>(inv.ext()));
            return Value::boolean(true);
        }
        if (m == "emit" || m == "done" || m == "quit") return Value::boolean(true);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Attribute") {
        auto& h = *inv.hash();
        // A role MIXED INTO this meta-object brings its methods with it, and in
        // Rakudo a mixin wins over what it is mixed into — so ask it first.
        // `$attr does Red::Attr::Column(%args)` is how every Red column is
        // declared, and `$attr.column` / `.args` are read straight back off the
        // Attribute afterwards. (`~~` already consults the same list.)
        {
            auto rit = h.find(ATTR_ROLES_KEY);
            // newest first: a LATER mixin wins over an earlier one, as it
            // does in Rakudo (`$a does R[&x]` after `$a but R[&y]` answers &x)
            // …and a role another listed role COMPOSED is asked after that
            // one: `$a does R2` where `role R2 does R1` lists both, and R2's
            // own `u` overrides the one it got from R1 (JSON::Unmarshal's
            // CustomUnmarshallerCode over the stub in CustomUnmarshaller)
            std::vector<const Value*> order;
            if (rit != h.end() && rit->second.t == VT::Array && rit->second.arr()) {
                auto& lst = *rit->second.arr();
                auto composedByOther = [&](const std::string& nm) {
                    for (auto& other : lst) {
                        if (other.s == nm) continue;
                        auto oc = classes_.find(other.s);
                        if (oc != classes_.end() && oc->second && oc->second->doneRoles.count(nm)) return true;
                    }
                    return false;
                };
                for (auto ri = lst.rbegin(); ri != lst.rend(); ++ri) if (!composedByOther(ri->s)) order.push_back(&*ri);
                for (auto ri = lst.rbegin(); ri != lst.rend(); ++ri) if (composedByOther(ri->s)) order.push_back(&*ri);
            }
            if (!order.empty())
                for (const Value* rnp : order) {
                    const Value& rn = *rnp;
                    auto cit = classes_.find(rn.s);
                    if (cit == classes_.end() || !cit->second) continue;
                    // A public ATTRIBUTE of the role is served by the slot the
                    // mixin seeded in this very map, not by its generated
                    // accessor: that accessor would read `%!args` off a `self`
                    // that is this Hash, and find nothing.
                    if (args.empty() && cit->second->findAttr(m) && h.count(m)) return h[m];
                    if (Value* rm = cit->second->findMethod(m))
                        return invokeMethod(*rm, inv, args, rwArgs);
                    // …and a `handles` on one of the role's ATTRIBUTES, whose value
                    // lives in this same map: `has COSAttr $.cos is rw handles<tie raku>`
                    // publishes the descriptor's own methods on the Attribute, which is
                    // how PDF::COS::Tie ties an assigned value to its entry (`.tie($lval)`).
                    for (auto& ra : cit->second->attrs)
                        for (size_t hi = 0; hi < ra.handles.size(); hi++)
                            if (ra.handles[hi] == m) {
                                auto tv = h.find(ra.name);
                                Value target = tv != h.end() ? tv->second : Value::any();
                                const std::string& to = hi < ra.handlesTo.size() && !ra.handlesTo[hi].empty()
                                                        ? ra.handlesTo[hi] : (const std::string&)m;
                                return methodCall(target, to, std::move(args), rwArgs);
                            }
                }
        }
        if (m == "name") return h.count("name") ? h["name"] : Value::str("");
        // `Int $!x` — the gist is the type and the name, the Str just the name
        if (m == "gist" && args.empty()) {
            std::string nm = h.count("name") ? h["name"].toStr() : std::string();
            Value t = h.count("type") ? h["type"] : Value::typeObj("Mu");
            return Value::str((t.t == VT::Type ? t.s.str() : t.typeName()) + " " + nm);
        }
        if (m == "Str" && args.empty()) return Value::str(h.count("name") ? h["name"].toStr() : std::string());
        if (m == "type" || m == "of" || m == "returns") return h.count("type") ? h["type"] : Value::typeObj("Mu");
        if (m == "package") return h.count("package") ? h["package"] : Value::any();
        // the JSON::Name / JSON::Unmarshal / JSON::Marshal attribute-role
        // surface: the traits were stored on the ClassAttr at declaration time
        // (see userTraits) instead of mixing roles into this meta-object.
        if (m == "json-name" && h.count("trait:json-name")) return h["trait:json-name"];
        // META6's MetaAttribute::Specification accessors, off the stored
        // `is specification(Optionality, Version?)` trait payload
        if ((m == "optionality" || m == "spec-version") && h.count("trait:specification")) {
            Value& sp = h["trait:specification"];
            bool isList = sp.t == VT::Array && sp.arr();
            if (m == "optionality") return isList ? (sp.arr()->empty() ? Value::any() : (*sp.arr())[0]) : sp;
            if (isList && sp.arr()->size() > 1) return (*sp.arr())[1];
            Value v0 = Value::str("0"); v0.hashKind = "Version"; return v0;
        }
        if ((m == "unmarshal" || m == "marshal") && !args.empty()) {
            std::string tk = m == "unmarshal" ? "trait:unmarshalled-by" : "trait:marshalled-by";
            if (h.count(tk)) {
                Value& by = h[tk];
                // `is (un)marshalled-by(-> $d {…})` calls the code with the
                // value. The METHOD-NAME spellings differ by direction:
                // unmarshal calls it on the attribute's TYPE with the JSON
                // datum ($type."$meth"($json)); marshal calls it ON THE VALUE
                // with no arguments — `$value."$meth"()`, Nil when undefined
                // (JSON::Marshal's CustomMarshallerMethod, 030-trait.t).
                if (by.t == VT::Code) return callCallable(by, ValueList{args[0]});
                if (m == "marshal") {
                    Value d = methodCall(args[0], "defined", ValueList{});
                    if (!d.truthy()) return Value::any();
                    return methodCall(args[0], by.toStr(), ValueList{});
                }
                Value ty = args.size() > 1 ? args[1]
                         : h.count("type") ? h["type"] : Value::typeObj("Mu");
                return methodCall(ty, by.toStr(), ValueList{args[0]});
            }
            return args[0]; // no custom (un)marshaller: identity
        }
        // `.set_build(&closure)` — the code that produces this attribute's initial
        // value, which a metaclass adding an attribute at runtime uses in place
        // of the `= default` a declaration would have written.
        if (m == "set_build" && !args.empty()) { h["build"] = args[0]; return args[0]; }
        // `.build` without a set_build answers what Rakudo's does: Mu for an
        // attribute with no default, the value itself for a literal one, and
        // otherwise a METHOD thunk `(instance, Mu)` that computes it. Red's
        // model constructor skips `$built =:= Mu` and calls `$built.(self, Mu)
        // if $built ~~ Method`; every attribute answering Any made it register
        // an Any id for each fresh row.
        if (m == "build") {
            if (h.count("build")) return h["build"];
            const ClassAttr* ca = nullptr;
            std::shared_ptr<ClassInfo> owner;
            if (h.count("package") && h.count("name")) {
                auto cit = classes_.find(h["package"].s);
                std::string an = h["name"].toStr();
                if (an.size() > 2 && an[1] == '!') an = an.substr(2);
                if (cit != classes_.end() && cit->second) {
                    owner = cit->second;
                    for (auto& a : owner->attrs) if (a.name == an) { ca = &a; break; }
                }
            }
            if (!ca) return Value::any();
            if (ca->hasDefVal) return ca->defVal;
            if (!ca->def) return ca->buildFn.t == VT::Code ? ca->buildFn : Value::typeObj("Mu");
            switch (ca->def->kind) {
                case NK::IntLit: case NK::NumLit: case NK::StrLit: case NK::BoolLit: case NK::AllomorphLit:
                    return eval(const_cast<Expr*>(ca->def));
                default: break;
            }
            const Expr* def = ca->def;
            std::shared_ptr<Env> declEnv = owner->declEnv;
            // `has DateTime $.created .= now` — the default MUTATES the slot a
            // construction seeds with the attribute's type object, so the
            // thunk calls the method on that type (Red's timestamp columns)
            std::string seedType = ca->type.empty() ? std::string("Any") : ca->type;
            Value thunk; thunk.t = VT::Code; thunk.setCode(std::make_shared<Callable>());
            thunk.code()->isMethod = true;
            thunk.code()->name = "build";
            thunk.code()->builtin = [def, declEnv, seedType](Interpreter& I, ValueList& a) -> Value {
                // (the parser desugars `.= m(…)` to `$!x.m(…)` on the attribute itself)
                const MethodCall* mcd = def->kind == NK::MethodCall ? static_cast<const MethodCall*>(def) : nullptr;
                if (mcd && mcd->inv && mcd->inv->kind == NK::VarExpr &&
                    static_cast<const VarExpr*>(mcd->inv.get())->name.compare(0, 2, "$!") == 0) {
                    auto* mc = mcd;
                    auto saved = I.tctx_.cur;
                    auto env = std::make_shared<Env>();
                    env->parent = declEnv ? declEnv : I.tctx_.cur;
                    env->define("self", a.empty() ? Value::any() : a[0]);
                    I.tctx_.cur = env;
                    struct R { Interpreter& i; std::shared_ptr<Env> s; ~R() { i.tctx_.cur = s; } } r{I, saved};
                    ValueList margs;
                    for (auto& ae : mc->args) margs.push_back(I.eval(ae.get()));
                    return I.methodCall(Value::typeObj(seedType), mc->method, std::move(margs));
                }
                auto env = std::make_shared<Env>();
                env->parent = declEnv ? declEnv : I.tctx_.cur;
                env->define("self", a.empty() ? Value::any() : a[0]);
                auto saved = I.tctx_.cur; I.tctx_.cur = env;
                struct R { Interpreter& i; std::shared_ptr<Env> s; ~R() { i.tctx_.cur = s; } } r{I, saved};
                return I.eval(const_cast<Expr*>(def));
            };
            return thunk;
        }
        if (m == "set_rw") { h["readonly"] = Value::boolean(false); return inv; }         // trait_mod helpers
        if (m == "set_readonly") { h["readonly"] = Value::boolean(true); return inv; }
        if (m == "has_build") return Value::boolean(h.count("build") && h["build"].t == VT::Code);
        if (m == "readonly") return h.count("readonly") ? h["readonly"] : Value::boolean(true);
        if (m == "rw") return Value::boolean(h.count("readonly") && !h["readonly"].truthy());
        if (m == "has_accessor") return h.count("has_accessor") ? h["has_accessor"] : Value::boolean(false);
        // builders that predate the `built` key fall back to has_accessor —
        // public means built, which is Rakudo's default too
        if (m == "is_built")
            return h.count("built") ? h["built"]
                 : h.count("has_accessor") ? h["has_accessor"] : Value::boolean(false);
        if (m == "gist" || m == "Str") return h.count("name") ? h["name"] : Value::str("");
        if (m == "defined" || m == "Bool") return Value::boolean(true);
        // read/write the attribute's value on an instance through the meta-object —
        // the core MOP operations marshallers (JSON::Marshal etc.) drive.
        if ((m == "get_value" || m == "set_value") && !args.empty()) {
            std::string an = h.count("name") ? h["name"].toStr() : "";
            char sigil = an.empty() ? '$' : an[0];
            while (!an.empty() && (an[0]=='$'||an[0]=='@'||an[0]=='%'||an[0]=='&'||an[0]=='!'||an[0]=='.')) an = an.substr(1);
            Value obj = args[0];
            if (obj.t == VT::Object && obj.obj()) {
                if (m == "set_value" && args.size() > 1) {
                    Value v = args[1];
                    // `set_value(Mu $obj, Mu \value)` binds the value RAW: a
                    // Proxy handed in stays a Proxy in the slot, so every later
                    // read FETCHes afresh. Red backs each column attribute with
                    // one over its column-data hash (`col.set_value: instance<>,
                    // proxy`); storing the value it happened to FETCH first froze
                    // a row's `.id` at Any before the INSERT assigned it.
                    if (rwArgs && rwArgs->size() > 1 && (*rwArgs)[1] &&
                        ((*rwArgs)[1]->kind == NK::VarExpr || (*rwArgs)[1]->kind == NK::NameTerm)) {
                        const Expr* ve = (*rwArgs)[1].get();
                        std::string vn = ve->kind == NK::VarExpr ? static_cast<const VarExpr*>(ve)->name
                                                                 : static_cast<const NameTerm*>(ve)->name;
                        if (Value* raw = tctx_.cur ? tctx_.cur->find(vn) : nullptr)
                            if (raw->t == VT::Hash && raw->hashKind == "Proxy") v = *raw;
                    }
                    if (sigil == '@' && v.t == VT::Range) { Value a = Value::array(); *a.arr() = v.flatten(); a.isList = true; v = a; }
                    obj.obj()->attrs[an] = v;
                    return v;
                }
                auto it = obj.obj()->attrs.find(an);
                if (it != obj.obj()->attrs.end()) return it->second;
                // uninitialised: the attribute's declared default kind
                return sigil == '@' ? Value::array() : sigil == '%' ? Value::makeHash() : Value::any();
            }
            // a TYPE object has no attribute storage — Rakudo throws, and
            // Data::Dump's `try get_value // … // 'undefined'` chain expects it
            throw RakuError{Value::typeObj("X::Method::NotFound"),
                "Cannot look up attributes in a " + obj.typeName() + " type object"};
        }
        // no `.hash` on an Attribute either (the guts would leak into a dump)
        if (m == "hash")
            throw RakuError{Value::typeObj("X::Method::NotFound"),
                "No such method 'hash' for invocant of type 'Attribute'"};
        if (m == "container_descriptor" || m == "container") return inv; // enough for `.of`/rw queries
        if (m == "package" || m == "declaring_package") return h.count("package") ? h["package"] : Value::typeObj("Mu");
        // An accessor of a role a trait mixed in (`$a does R` put R's attributes
        // into this same map): `$a.where` after META6's `is customary`. Last,
        // so it can never shadow a real Attribute method.
        {
            auto ai = h.find(m);
            if (ai != h.end()) return ai->second;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Failure") {
        Value ex = inv.hash()->count("exception") ? (*inv.hash())["exception"] : Value::typeObj("Exception");
        if (m == "exception") return failureException(inv);
        if (m == "defined" || m == "Bool" || m == "so") {
            (*inv.hash())["handled"] = Value::boolean(true); // testing a Failure marks it handled
            return Value::boolean(false);
        }
        if (m == "not") { (*inv.hash())["handled"] = Value::boolean(true); return Value::boolean(true); }
        if (m == "handled") return inv.hash()->count("handled") ? (*inv.hash())["handled"] : Value::boolean(false);
        if (m == "self" || m == "Failure") return inv;
        // .throw keeps the Failure's own exception TYPE and message — routing an
        // unthrown X::Str::Numeric through X::AdHoc lost both.
        if (m == "throw" || m == "sink") {
            auto mm = inv.hash()->find("message");
            std::string msg = mm != inv.hash()->end() ? mm->second.toStr() : ex.toStr();
            if (ex.t == VT::Object) throw RakuError{ex, msg};
            throw RakuError{ex.t == VT::Type ? ex : Value::typeObj("X::AdHoc"), msg};
        }
        // .message reads the diagnostic without detonating; .Str/.gist USE the
        // value, so an UNHANDLED Failure throws there (Rakudo). The NUMERIC
        // coercions use it just as much — `.Int` on an unhandled Failure used to
        // return the hash's element count, a 2 out of nowhere.
        // (`.raku` is deliberately NOT in this list, though Rakudo detonates
        // there too. Two Roast files reach it on a Failure that Rakudo never
        // makes — a failed `Grammar.parse` under 6.e and a missing symbol in
        // S10-packages/scope.t both answer Nil there and a Failure here — so
        // detonating would report those bugs from the wrong place and abort
        // both files. Worth revisiting once .parse and symbol lookup answer
        // Nil; until then the Range and Int-Num-Rat sheets record the
        // difference.)
        if (m == "Str" || m == "gist" || m == "Int" || m == "Num" || m == "Rat" ||
            m == "Numeric" || m == "Real" || m == "FatRat" || m == "Complex") {
            auto h = inv.hash()->find("handled");
            if (h == inv.hash()->end() || !h->second.truthy()) {
                auto mm = inv.hash()->find("message");
                throw RakuError{ex, mm != inv.hash()->end() ? mm->second.toStr() : ex.toStr()};
            }
        }
        if (m == "message" || m == "Str" || m == "gist") {
            auto mm = inv.hash()->find("message");
            Value out = mm != inv.hash()->end() ? mm->second
                                                : methodCall(ex, m == "gist" ? std::string("message") : std::string(m), args, rwArgs);
            // a HANDLED Failure gists with the marker Rakudo prints, so the two
            // states are told apart at a glance
            if (m == "gist") return Value::str("(HANDLED) " + out.toStr());
            return out;
        }
        // Everything else is Failure.FALLBACK: a method the Failure does not
        // answer itself is a USE of the value, and a use throws the exception
        // it carries — handled or not, as Rakudo has it. Without this the
        // call fell through to the Hash the Failure is stored as, so
        // `"x".Int.elems` answered 2 (the hash's key count) and a symbolic
        // lookup that missed (`$::($lang)`, Date::Names) went on as if it had
        // found something. The names that stay quiet are the Failure's own
        // and Mu's introspection — asking WHAT a thing is does not use it.
        static const std::set<std::string> quiet = {
            "exception", "defined", "Bool", "so", "not", "handled", "self", "Failure",
            "throw", "sink", "rethrow", "message", "raku", "perl", "new", "clone",
            "WHAT", "WHICH", "WHERE", "HOW", "WHO", "DEFINITE", "isa", "does", "can",
            "ACCEPTS", "item", "VAR", "mark-handled", "bless", "BUILDALL", "CREATE" };
        if (!m.empty() && m[0] != '^' && !quiet.count(m)) {
            (*inv.hash())["handled"] = Value::boolean(true);
            auto mm = inv.hash()->find("message");
            std::string msg = mm != inv.hash()->end() ? mm->second.toStr() : ex.toStr();
            if (ex.t == VT::Object) throw RakuError{ex, msg};
            throw RakuError{ex.t == VT::Type ? ex : Value::typeObj("X::AdHoc"), msg};
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Pod") {
        auto& h = *inv.hash();
        if (m == "name")     return h.count("name") ? h["name"] : Value::str("");
        if (m == "type")     return h.count("type") ? h["type"] : Value::str("");
        if (m == "meta")     return h.count("meta") ? h["meta"] : Value::array(); // L<text|url>'s url, X<>'s entries
        if (m == "contents") return h.count("contents") ? h["contents"] : Value::array();
        if (m == "level")    return h.count("level") ? h["level"] : Value::integer(1);
        if (m == "config")   return h.count("config") ? h["config"] : Value::makeHash();
        // A table's own two: the header row (empty when the table has no
        // divider) and the caption from its config.
        if (m == "headers")  return h.count("headers") ? h["headers"] : Value::array();
        if (m == "caption")  return h.count("caption") ? h["caption"] : Value::str("");
        if (m == "term")     return h.count("term") ? h["term"] : Value::str("");       // Pod::Defn
        // a declarator block's own three: what it documents, and its parts
        if (m == "WHEREFORE") return h.count("WHEREFORE") ? h["WHEREFORE"] : Value::any();
        if (m == "leading")   return h.count("leading") ? h["leading"] : Value::any();
        if (m == "trailing")  return h.count("trailing") ? h["trailing"] : Value::any();
        if (m == "WHAT")     return Value::typeObj(h.count("podclass") ? h["podclass"].s.str() : std::string("Pod::Block"));
        if (m == "defined" || m == "Bool") return Value::boolean(true);
        if (m == "Str" || m == "gist" || m == "raku") {
            // stringify to the concatenated text of the contents (paragraphs/children)
            std::function<std::string(const Value&)> flat = [&](const Value& v) -> std::string {
                if (v.t == VT::Str) return v.s;
                if (v.t == VT::Hash && v.hashKind == "Pod" && v.hash()->count("contents")) {
                    std::string o; for (auto& c : *(*v.hash())["contents"].arr()) o += flat(c); return o;
                }
                if (v.t == VT::Array && v.arr()) { std::string o; for (auto& c : *v.arr()) o += flat(c); return o; }
                return v.toStr();
            };
            return Value::str(flat(inv));
        }
    }
    if ((inv.t == VT::Type && inv.s == "Kernel") ||
        (inv.t == VT::Hash && inv.hashKind == "Kernel")) {
        if (m == "endian") { // all supported targets are little-endian
            Value e = Value::enumVal("LittleEndian", 1); e.enumType = "Endian"; return e;
        }
        // $*KERNEL.bits — the process's native pointer width, an Int. (It used to
        // fall through to the name, so `$*KERNEL.bits == 64` compared "darwin".)
        if (m == "bits") return Value::integer((long long)(sizeof(void*) * 8));
        if (m == "hardware" || m == "arch") {
#if !defined(_WIN32)
            struct utsname u;
            if (uname(&u) == 0) return Value::str(u.machine);
#endif
            return Value::str("");
        }
        // Kernel.hostname — the host's name, and callable on the TYPE OBJECT:
        // Sys::Hostname's whole body is `sub hostname() { Kernel.hostname }`.
        // Without an arm of its own it fell to the lenient accessor below and
        // answered the kernel's name ("darwin") for every machine.
        if (m == "hostname") {
#if !defined(_WIN32)
            char buf[256];
            if (gethostname(buf, sizeof(buf)) == 0) { buf[sizeof(buf) - 1] = 0; return Value::str(buf); }
#endif
            return Value::str("localhost");
        }
    }
    if (inv.t == VT::Hash && (inv.hashKind == "Distro" || inv.hashKind == "Kernel" || inv.hashKind == "VM")) {
        std::string name = inv.hash()->count("name") ? (*inv.hash())["name"].toStr() : "";
        // $*VM.config — the VM's BUILD CONFIGURATION, as a Hash.
        //
        // Rakudo answers MoarVM's own build settings here; the ecosystem reads
        // it to drive a C toolchain. LibraryMake — the build helper a long tail
        // of NativeCall dists shells out to — opens with
        // `%vars<O> = $*VM.config<obj>` and fills a Makefile template from a
        // dozen more keys. Without an arm of its own that fell to the lenient
        // accessor at the end of this block and answered the VM's NAME, so
        // `$*VM.config<obj>` was a subscript on the Str "moar": it used to give
        // a quiet Any (and LibraryMake built a Makefile of undefined values),
        // and once associative indexing on a defined scalar started dying as
        // Rakudo's does, it took the dist's whole suite with it.
        //
        // The values describe THIS engine's toolchain, not MoarVM's — the
        // compiler `--exe` would drive, the platform's object/library/exe
        // spellings, and the `%s` templates the readers substitute into. They
        // are answers we can stand behind rather than a copy of Rakudo's.
        if (m == "config" && inv.hashKind == "VM") {
            auto envOr = [](const char* var, const char* dflt) -> std::string {
                const char* e = std::getenv(var);
                return e && *e ? std::string(e) : std::string(dflt);
            };
#if defined(_WIN32)
            const bool msvc = true;
            std::string cc = envOr("CC", "cl");
            const char* objExt = ".obj"; const char* dllPat = "%s.dll";
            const char* exeExt = ".exe"; const char* ldShared = "/DLL";
            const char* makeProg = "nmake";
#else
            const bool msvc = false;
            std::string cc = envOr("CC", "cc");
            const char* objExt = ".o";
            const char* exeExt = "";
#if defined(__APPLE__)
            const char* dllPat = "lib%s.dylib"; const char* ldShared = "-dynamiclib";
#else
            const char* dllPat = "lib%s.so";    const char* ldShared = "-shared";
#endif
            const char* makeProg = "make";
#endif
            Value c = Value::makeHash();
            auto& ch = *c.hash();
            ch["obj"]      = Value::str(objExt);
            ch["dll"]      = Value::str(dllPat);
            ch["exe"]      = Value::str(exeExt);
            ch["cc"]       = Value::str(cc);
            ch["ld"]       = Value::str(envOr("LD", cc.c_str()));
            // A position-independent object is the default on Apple's clang and
            // required everywhere else a shared library is linked from one.
#if defined(__APPLE__) || defined(_WIN32)
            ch["ccshared"] = Value::str("");
#else
            ch["ccshared"] = Value::str("-fPIC");
#endif
            ch["ccout"]    = Value::str(msvc ? "/Fo" : "-o ");
            ch["ldout"]    = Value::str(msvc ? "/OUT:" : "-o ");
            ch["cflags"]   = Value::str(envOr("CFLAGS",  msvc ? "/O2" : "-O2"));
            ch["ldflags"]  = Value::str(envOr("LDFLAGS", ""));
            ch["ldlibs"]   = Value::str(envOr("LDLIBS",  ""));
            ch["ldshared"] = Value::str(ldShared);
            // `%s` templates: the readers strip or substitute the marker —
            // LibraryMake does `$ldusr ~~ s/\%s//` to recover the bare flag.
            ch["ldusr"]    = Value::str(msvc ? "%s.lib" : "-l%s");
            ch["make"]     = Value::str(envOr("MAKE", makeProg));
            // `osname` is the OPERATING SYSTEM, not the VM: Rakudo's
            // `$*VM.config<osname>` is 'darwin'/'linux'/'mswin32', and modules
            // dispatch on it (NativeLibs' cannon-name test does). Answering the
            // VM's own name matched no branch anywhere.
#if defined(_WIN32)
            ch["osname"]   = Value::str("MSWin32");   // MoarVM's own spelling, capitals and all
#else
            ch["osname"]   = Value::str(platKernelName());
#endif
            return c;
        }
        // `$*VM.request-garbage-collection` — the one hook Raku offers to ask
        // for finalization. Runs the pending-DESTROY sweep (see Interpreter.h).
        if (m == "request-garbage-collection") { runPendingDestroys(); return Value::boolean(true); }
#if defined(__APPLE__)
        if (inv.hashKind == "Distro") { // what Rakudo reads off `sw_vers` — see macDistro()
            const MacDistro& md = macDistro();
            const std::string ver = md.version.empty() ? "0" : md.version;
            if (m == "version") { Value v = Value::str(ver); v.hashKind = "Version"; return v; }
            if (m == "release") return Value::str(md.release.empty() ? "unknown" : md.release);
            if (m == "desc")    return Value::str(md.desc);
            if (m == "auth")    return Value::str("Apple Inc.");
            if (m == "gist")    return Value::str(name + " (" + ver + ")"); // Systemic.gist: "$name ($version)"
        }
#endif
        // $*VM describes THIS engine's runtime. Everything below used to fall
        // through to the lenient accessor at the end of this block, so `.auth`,
        // `.desc`, `.precomp-ext`, `.precomp-target` and `.prefix` all answered
        // the literal VM NAME — the same failure `.config` had, and none of
        // them load-bearing enough for anyone to notice.
        //
        // The name is `cpp` (and `js` under --target=js), the shape Rakudo uses
        // for moar/jvm/js; `$*RAKU.VMnames` lists both, which is Roast's only
        // check on it. The ecosystem's build recipes — LibraryMake and its kin —
        // gate on `moar` and have no other branch, so `rakupp install` hands its
        // BUILD HOOKS a $*VM answering `moar` for the duration of the hook
        // (tools/install.raku): a dialect scoped to the hook, not this identity.
        if (inv.hashKind == "VM") {
            if (m == "auth") return Value::str("Andrew Shitov");   // as $*RAKU.compiler.auth
            if (m == "desc")
                return Value::str("Raku++'s own runtime: a C++ tree-walking interpreter, "
                                  "with a native backend (--exe) and a JavaScript one (--target=js).");
            // The VM's version is THIS binary's release. (The COMPILER's version
            // deliberately answers the Rakudo era instead — see rakuIntrospection
            // in Builtins.cpp for why that one cannot be ours.)
            if (m == "version") { Value v = Value::str(RAKUPP_VERSION); v.hashKind = "Version"; return v; }
            if (m == "gist") return Value::str(name + " (" + RAKUPP_VERSION + ")"); // Systemic.gist: "$name ($version)"
            // The precompilation store keeps SERIALISED ASTs, not bytecode:
            // ~/.cache/rakupp/precomp/XX/<hash>.ast (see docs/guide/CACHING.md).
            if (m == "precomp-ext" || m == "precomp-target") return Value::str("ast");
            // Rakudo answers MoarVM's install prefix; ours is where THIS binary
            // lives — $RAKUPP_HOME when set, else the directory above bin/.
            if (m == "prefix") {
                if (const char* home = std::getenv("RAKUPP_HOME")) return Value::str(home);
                size_t sl = execPath_.find_last_of("/\\");
                std::string dir = sl == std::string::npos ? std::string(".") : execPath_.substr(0, sl);
                size_t up = dir.find_last_of("/\\");
                return Value::str(up == std::string::npos ? dir : dir.substr(0, up));
            }
        }
        if (m == "name" || m == "Str" || m == "gist" || m == "auth" || m == "desc") return Value::str(name);
        // Rakudo's rule, verbatim (Distro.rakumod TWEAK): the NAME decides.
        // This was a hard-coded False, so on Windows every `$*DISTRO.is-win`
        // branch took the POSIX arm — a GUI module picked its GTK backend and
        // asked the loader for libgtk-3.so.0, `rakupp install` looked for a
        // native extension under lib*.so, and this engine's own suite and
        // tooling (t/run.raku, tools/install.raku, tools/lib/Gate.rakumod)
        // believed they were on POSIX.
        if (m == "is-win")
            return Value::boolean(name == "mswin32" || name == "mingw" ||
                                  name == "msys"    || name == "cygwin");
        if (m == "version") { // a Version object (it was the Str "0"): the kernel's is uname -r
            std::string ver = "0";
#if !defined(_WIN32)
            struct utsname u;
            if (inv.hashKind == "Kernel" && uname(&u) == 0) ver = u.version; // Rakudo: Version.new(uname -v)
#endif
            Value v = Value::str(ver); v.hashKind = "Version"; return v;
        }
        if (m == "signature") { // a Blob (S02-magicals/VM.t, DISTRO.t assert the type), empty: nothing signs this binary
            return Value::typeObj("Blob");   // Rakudo: the Blob type object
        }
        // The PATH separator, which is ';' on Windows — Rakudo picks it the
        // same way, off the name.
        if (m == "path-sep")
            return Value::str(name == "mswin32" || name == "mingw" ||
                              name == "msys"    || name == "cygwin" ? ";" : ":");
        if (m == "release") { // kernel release string (uname -r)
#if !defined(_WIN32)
            struct utsname u;
            if (uname(&u) == 0) return Value::str(u.release);
#endif
            return Value::str("0");
        }
        if (m == "cpu-cores") { unsigned n = std::thread::hardware_concurrency(); return Value::integer(n ? (long long)n : 1); }
        if (m == "archname" || m == "cpu-arch") { // was a hard-coded "x86_64" on every box
#if !defined(_WIN32)
            struct utsname u;
            if (uname(&u) == 0) {
                std::string sys = u.sysname; for (auto& ch : sys) ch = (char)ascii::tolower((unsigned char)ch);
                return Value::str(m == "archname" ? std::string(u.machine) + "-" + sys : std::string(u.machine));
            }
#endif
            return Value::str("x86_64");
        }
        // $*VM.platform-library-name("…/lib/ssl".IO) → "…/lib/libssl.dylib":
        // prepend `lib` to the basename and append the platform extension. Used
        // by OpenSSL::NativeLib et al. to build the `is native` library name.
        if (m == "platform-library-name" && !args.empty()) {
            // `:version` names a SONAME version, and every DBDish driver probes
            // with it (`try-versions('mysqlclient', $wks, 16..21)` walks
            // libmysqlclient.16.dylib … .21.dylib). Ignoring it answered the bare
            // name for every candidate, so the whole probe collapsed to one
            // attempt and a versioned-only library was never found.
            std::string p, ver;
            for (auto& a : args) {
                if (a.t == VT::Pair) {
                    if (a.s == "version" && a.pairVal() && a.pairVal()->t != VT::Type)
                        ver = a.pairVal()->toStr();
                }
                else if (p.empty()) p = a.toStr();
            }
            size_t slash = p.find_last_of('/');
            std::string dir = slash == std::string::npos ? "" : p.substr(0, slash + 1);
            std::string base = slash == std::string::npos ? p : p.substr(slash + 1);
            // don't double-prefix if it already starts with lib
#if defined(_WIN32)
            std::string libbase = base + ".dll";   // Windows carries no SONAME version
#elif defined(__APPLE__)
            std::string libbase = (base.compare(0, 3, "lib") == 0 ? base : "lib" + base) +
                                  (ver.empty() ? "" : "." + ver) + ".dylib";
#else
            std::string libbase = (base.compare(0, 3, "lib") == 0 ? base : "lib" + base) +
                                  ".so" + (ver.empty() ? "" : "." + ver);
#endif
            Value r = Value::str(dir + libbase); r.hashKind = "IO"; return r;
        }
        return Value::str(name); // lenient: any other Distro/Kernel/VM accessor
    }
    if (inv.t == VT::Hash && inv.hashKind == "Proc") { // standard Proc from run()
        if (m == "exitcode") return (*inv.hash())["exitcode"];
        if (m == "timedout") { // rakupp extension, set when :timeout(N) fired
            auto it = inv.hash()->find("timedout");
            return it != inv.hash()->end() ? it->second : Value::boolean(false);
        }
        if (m == "signal") { auto it = inv.hash()->find("signal"); return it != inv.hash()->end() ? it->second : Value::integer(0); }
        if (m == "so" || m == "Bool") { // a signalled child is not a success either
            auto sg = inv.hash()->find("signal");
            return Value::boolean((*inv.hash())["exitcode"].toInt() == 0 && (sg == inv.hash()->end() || sg->second.toInt() == 0));
        }
        if (m == "command") { // a List, as Proc::Async's is
            auto it = inv.hash()->find("argv");
            Value out = Value::array(); out.isList = true;
            if (it != inv.hash()->end() && it->second.arr()) *out.arr() = *it->second.arrS();
            return out;
        }
        if (m == "in") { Value h = inv; h.hashKind = "ProcIn"; return h; } // writable stdin handle (shares hash)
        // `$proc.out` / `$proc.err` — a read handle over what the child wrote. It
        // keeps the Proc it came from: Rakudo's IO::Pipe.close answers that Proc,
        // and sinking an unsuccessful one is what reports a failed child.
        if (m == "out" || m == "err") { Value h = Value::makeHash(); h.hashKind = "FileHandle"; (*h.hash())["buffer"] = (*inv.hash())[m == "out" ? "out-str" : "err-str"]; (*h.hash())["mode"] = Value::str("r"); (*h.hash())["captured"] = Value::boolean(true); (*h.hash())["proc-owner"] = inv; return h; }
        if (m == "sink" || m == "self") return inv;
        if (m == "pid") { auto it = inv.hash()->find("pid"); return it != inv.hash()->end() ? it->second : Value::integer(0); } // (was a hard-coded 0)
        // `.shell(CMD)` / `.spawn(@cmd)` on a Proc built by `Proc.new` — run it
        // NOW, with the adverbs the constructor recorded, and fill this same
        // object in place: `$proc.out` after the call is the child's output,
        // and the Proc the caller holds is the one that answers `.exitcode`.
        // …and on one that has ALREADY run: Rakudo's Proc is re-runnable, and
        // `.pid` tracking is asserted through exactly that (roast
        // S29-os/system.t runs a Proc, then shells and spawns from it).
        if ((m == "shell" || m == "spawn") && !args.empty()) {
            ValueList ba;
            if (m == "shell") ba.push_back(Value::str(args[0].toStr()));
            else if (args.size() == 1 && args[0].t == VT::Array && args[0].arr())
                for (auto& x : *args[0].arr()) ba.push_back(Value::str(x.toStr()));
            else for (auto& a : args) { if (a.t != VT::Pair) ba.push_back(Value::str(a.toStr())); }
            for (const char* k : {"out", "err", "merge"}) {
                auto w = inv.hash()->find(std::string("want-") + k);
                if (w == inv.hash()->end()) continue;
                Value pr = Value::pair(k, Value::boolean(w->second.truthy()));
                pr.namedArg = true;
                ba.push_back(pr);
            }
            for (auto& a : args) if (a.t == VT::Pair) ba.push_back(a); // :cwd / :env pass through
            Value res = callBuiltin(m == "shell" ? "shell" : "run", std::move(ba));
            inv.hash()->erase("os-error");   // whatever went wrong LAST time is over
            // The new run's state replaces this one's — except `pid`, which the
            // run only reports when a child actually started. A spawn that never
            // got off the ground leaves the previous pid standing, which is what
            // ".pid does not update on failed run()" asks for.
            if (res.t == VT::Hash && res.hash())
                for (auto& kv : *res.hash()) (*inv.hash())[kv.first] = kv.second;
            inv.hash()->erase("unspawned");
            auto ec = inv.hash()->find("exitcode");
            return Value::boolean(ec != inv.hash()->end() && ec->second.toInt() == 0);
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "ProcIn") { // $proc.in — feed stdin, which runs a deferred proc
        // Closing stdin without ever writing to it still runs the child — with no
        // input. `run(cmd, :in, :out); $p.in.close` was a pair of no-ops, so the
        // child never started and `.out` came back empty.
        if (m == "print" || m == "spurt" || m == "write" || m == "say" ||
            (m == "close" && !inv.hash()->count("ran"))) {
            std::string input = (m == "close" || args.empty()) ? "" : args[0].toStr();
            if (m == "say") input += "\n";
            std::vector<std::string> argv;
            auto it = inv.hash()->find("argv");
            if (it != inv.hash()->end() && it->second.arr()) for (auto& x : *it->second.arr()) argv.push_back(x.toStr());
            // run(..., :in, :env(...), :cwd(...)) stashed these on the Proc
            std::vector<std::string> envKV; bool haveEnv = false;
            auto ei = inv.hash()->find("env-kv");
            if (ei != inv.hash()->end() && ei->second.arr()) {
                haveEnv = true;
                for (auto& x : *ei->second.arr()) envKV.push_back(x.toStr());
            }
            std::string cwd;
            auto ci = inv.hash()->find("cwd");
            if (ci != inv.hash()->end()) cwd = ci->second.toStr();
            // …and which streams the run() adverbs asked for. -1 (no adverb)
            // means the child writes to ours, which is what Rakudo does; only
            // `:out`/`:err` capture and only `:!out`/`:!err` discard.
            auto mode = [&](const char* k) {
                auto it = inv.hash()->find(k);
                return it != inv.hash()->end() ? (int)it->second.toInt() : 1;
            };
            int outMode = mode("out-mode"), errMode = mode("err-mode");
            std::string out, err; int code;
            spawnWithInput(argv, input, out, code, this, haveEnv ? &envKV : nullptr, cwd,
                           errMode == 1 ? &err : nullptr, errMode == -1, outMode);
            (*inv.hash())["out-str"] = Value::str(out);      // shared hash: $proc.out.slurp sees this
            (*inv.hash())["err-str"] = Value::str(err);
            storeProcStatus(inv, code); // exitcode + signal
            (*inv.hash())["ran"] = Value::boolean(true);
            if (m == "close") { Value pr = inv; pr.hashKind = "Proc"; return pr; } // as above
            return Value::boolean(true);
        }
        // …and `.close` answers the PROC, as every other pipe's close does
        // (Rakudo's IO::Pipe.close; roast S29-os/system.t compares it with `===`).
        // `.in` shares the Proc's own hash, so this is that same hash wearing
        // its Proc identity again — the identity test sees one object.
        if (m == "close") { Value pr = inv; pr.hashKind = "Proc"; return pr; }
    }
    if (inv.t == VT::Hash && (inv.hashKind == "Promise" || inv.hashKind == "Vow")) {
        auto ps = inv.ext() ? std::static_pointer_cast<PromiseState>(inv.ext()) : nullptr;
        std::string kind = inv.hash()->count("kind") ? (*inv.hash())["kind"].toStr() : "";

        // keep / break — settle a manual promise (or the vow that controls it).
        // Settling takes the promise's vow; once vowed (explicitly via .vow or
        // implicitly by a prior keep/break), only the Vow object may settle it.
        auto takeVow = [&]() {
            if (inv.hashKind == "Vow") {
                // a vow settles its promise ONCE
                std::string st = inv.hash()->count("status") ? (*inv.hash())["status"].toStr() : "";
                if (st == "Kept" || st == "Broken") {
                    Value pr = inv; pr.hashKind = "Promise";
                    throwTypedV("X::Promise::Resolved", {{"promise", pr}},
                                "Cannot keep/break a Promise more than once (status: " + st + ")");
                }
                return;
            }
            bool vowed = inv.hash()->count("vowed");
            std::string st = inv.hash()->count("status") ? (*inv.hash())["status"].toStr() : "";
            if (vowed || st == "Kept" || st == "Broken")
                throw RakuError{Value::typeObj("X::Promise::Vowed"),
                                "Access denied to keep/break this Promise; already vowed"};
            (*inv.hash())["vowed"] = Value::boolean(true);
        };
        if (m == "keep") {
            takeVow();
            Value v = args.empty() ? Value::boolean(true) : args[0];
            std::vector<std::function<void()>> fire;
            if (ps) { std::lock_guard<std::mutex> lk(ps->m); if (!ps->done) { ps->result = v; ps->done = true; } fire.swap(ps->thens); ps->cv.notify_all(); }
            (*inv.hash())["status"] = Value::str("Kept"); (*inv.hash())["result"] = v;
            for (auto& f : fire) f(); // run `.then` continuations now that it's settled
            return inv;
        }
        if (m == "break") {
            takeVow();
            Value c = args.empty() ? Value::str("Died") : args[0];
            // A non-exception cause (e.g. break("msg")) is wrapped in X::AdHoc so
            // that `$p.cause.message` works, mirroring `die "msg"`.
            if (c.t != VT::Object) {
                auto xit = classes_.find("X::AdHoc");
                if (xit != classes_.end()) {
                    Value ex; ex.t = VT::Object; ex.setObj(makePayload<ObjectData>());
                    ex.obj()->cls = xit->second; ex.obj()->attrs["message"] = Value::str(c.toStr());
                    ex.obj()->attrs["payload"] = c;   // `.payload` is the value itself
                    c = ex;
                }
            }
            std::vector<std::function<void()>> fire;
            if (ps) { std::lock_guard<std::mutex> lk(ps->m); if (!ps->done) { ps->broken = true; ps->cause = c; ps->causeMsg = c.toStr(); ps->done = true; } fire.swap(ps->thens); ps->cv.notify_all(); }
            (*inv.hash())["status"] = Value::str("Broken"); (*inv.hash())["cause"] = c;
            for (auto& f : fire) f();
            return inv;
        }
        if (m == "vow") { takeVow(); Value v = inv; v.hashKind = "Vow"; return v; }

        // Fold the state of an anyof/allof combinator lazily from its children.
        auto childState = [&](Value& c, bool& done, bool& broken) {
            done = broken = false;
            if (c.ext()) { auto s = std::static_pointer_cast<PromiseState>(c.ext()); done = s->done; broken = s->broken; }
            else if (c.hash() && c.hash()->count("status")) {
                auto s = (*c.hash())["status"].toStr(); broken = (s == "Broken"); done = (s == "Kept" || s == "Broken");
                // an UNSETTLED timer child settles when its moment passes (nobody
                // flips the hash) — but an explicit keep/break above wins
                if (!done && c.hash()->count("kind") && (*c.hash())["kind"].toStr() == "timer")
                    done = timerRemainingSecs(c) <= 0;
            }
        };
        auto comboStatus = [&]() -> std::string {
            if (!inv.hash()->count("promises")) return "Kept";
            auto& kids = *(*inv.hash())["promises"].arr();
            if (kids.empty()) return "Kept";
            if (kind == "anyof") { for (auto& c : kids) { bool d, b; childState(c, d, b); if (d) return "Kept"; } return "Planned"; }
            bool all = true; // allof: Kept once every child has settled (a broken child doesn't fail it)
            for (auto& c : kids) { bool d, b; childState(c, d, b); if (!d) { all = false; break; } }
            return all ? "Kept" : "Planned";
        };

        std::string st;
        if (kind == "anyof" || kind == "allof") st = comboStatus();
        else if (ps) st = ps->done ? (ps->broken ? "Broken" : "Kept") : "Planned";
        else if (kind == "timer") {
            // time-derived: nobody flips the hash when the delay elapses — but an
            // explicit keep/break (stored status) wins over the clock
            std::string hs = inv.hash()->count("status") ? (*inv.hash())["status"].toStr() : "";
            st = (hs == "Kept" || hs == "Broken") ? hs : (timerRemainingSecs(inv) <= 0 ? "Kept" : "Planned");
        }
        else st = inv.hash()->count("status") ? (*inv.hash())["status"].toStr() : "Kept";

        // Return the PromiseStatus enum value (matches the Planned/Kept/Broken
        // barewords), so both `is $p.status, Kept` and `~$p.status eq 'Kept'`
        // hold. It has to come from the SAME list the barewords resolve
        // through: built here from a second copy of the ordinals, it carried
        // its own (Kept and Broken the wrong way round) and no enum type, and
        // `$p.status ~~ Broken` was then false against the bareword.
        if (m == "status") { Value sv; if (coreEnumValue(st, sv)) return sv; return Value::enumVal(st, 0); }
        if (m == "Bool" || m == "so") return Value::boolean(st != "Planned");
        if (m == "cause") {
            if (ps && ps->broken) return ps->cause;
            auto it = inv.hash()->find("cause");
            if (it != inv.hash()->end()) return it->second;
            // only a BROKEN promise has a cause (a combinator is asked below)
            if (kind.empty()) {
                std::string st = inv.hash()->count("status") ? (*inv.hash())["status"].toStr() : "Planned";
                if (st != "Broken" && !(ps && ps->broken) && (!ps || ps->done || st == "Planned"))
                    throwTypedV("X::Promise::CauseOnlyValidOnBroken",
                                {{"promise", inv}, {"status", Value::str(ps && ps->done && !ps->broken ? "Kept" : st)}},
                                "Can only call cause on a broken promise (status: " +
                                (ps && ps->done && !ps->broken ? std::string("Kept") : st) + ")");
            }
            return Value::nil();
        }
        if (m == "result") {
            if (kind == "anyof" || kind == "allof") return Value::boolean(true);
            if (ps) { awaitPromise(ps); if (ps->broken) throw RakuError{ ps->cause, ps->causeMsg.empty() ? std::string("Promise broken") : ps->causeMsg }; return ps->result; }
            if (kind == "timer" && st != "Broken" && !inv.hash()->count("result")) { // .result blocks until the timer fires, like await (a keep-settled timer falls through to its stored result)
                double left = timerRemainingSecs(inv);
                if (left > 0) sleepYield(left);
                (*inv.hash())["status"] = Value::str("Kept");
                (*inv.hash())["result"] = Value::boolean(true);
                return Value::boolean(true);
            }
            // a lazily-realized process promise: `.result` is a wait — run the
            // process now, so the answer is the FINISHED proc (exitcode set)
            if (kind == "proc") { Value pv = inv; runProcPromise(pv, 0); }
            auto it = inv.hash()->find("result"); if (it != inv.hash()->end()) return it->second;
            auto pr = inv.hash()->find("proc");
            if (pr != inv.hash()->end()) {
                // a Proc, as await answers (shared hash, re-kinded): TAP's
                // Status multi takes `Proc $proc`, and the Async kind missed it
                Value pv = pr->second; pv.hashKind = "Proc";
                return pv;
            }
            return Value::nil();
        }
        if (m == "then") {
            // Deferred: the block runs only once the promise settles, receiving the
            // (identical) promise; its return keeps the new Promise, a throw breaks it.
            Value cb = args.empty() ? Value::nil() : args[0];
            Value parent = inv; // shares hash/ext with the promise → `$res === $orig` holds
            auto childPs = std::make_shared<PromiseState>();
            Value np = Value::makeHash(); np.hashKind = "Promise"; np.extM() = childPs;
            (*np.hash())["status"] = Value::str("Planned");
            Interpreter* self = this;
            std::function<void()> run = [self, cb, parent, childPs]() mutable {
                Value res; bool broke = false; Value cause; std::string cmsg;
                try { if (cb.t == VT::Code) { ValueList one{ parent }; res = self->callCallable(cb, one); } }
                catch (const RakuError& e) { broke = true; cause = e.payload; cmsg = e.message; }
                catch (...) { broke = true; }
                std::vector<std::function<void()>> chain;
                { std::lock_guard<std::mutex> lk(childPs->m);
                  if (broke) { childPs->broken = true; childPs->cause = cause; childPs->causeMsg = cmsg; }
                  else childPs->result = res;
                  childPs->done = true; chain.swap(childPs->thens); childPs->cv.notify_all(); }
                for (auto& f : chain) f();
            };
            bool now = false;
            if (ps) { std::lock_guard<std::mutex> lk(ps->m); if (ps->done) now = true; else ps->thens.push_back(run); }
            else if (kind == "timer") spawnDelayedNative(timerRemainingSecs(inv), run); // fire when the timer does, not at t=0
            else if (kind == "proc") {
                // a lazily-realized process promise: nobody else will run it, so
                // `.then` is a realization point (as await is) — the callback
                // must see the SETTLED parent. Firing at registration handed
                // TAP's `$start.then({ Status.new($start.result) })` a
                // still-Planned start, whose taps had seen no output yet.
                Value pv = inv; runProcPromise(pv, 0);
                now = true;
            }
            else now = true;
            if (now) run();
            return np;
        }
    }
    if (inv.t == VT::Type && inv.s == "IO::Special") {
        if (m == "gist") return Value::str("(Special)");   // as every type object gists
        if (m == "Str" || m == "path") return Value::str("");
    }
    if (inv.t == VT::Type && inv.s == "Stash") {
        if (m == "new") { Value h = Value::makeHash(); h.hashKind = "Stash"; return h; }
    }
    // `ValueObjAt.new("Foo|1")` — how a class that is a VALUE type writes its
    // own `method WHICH` (Red::Column, Red::AST); ObjAt.new the same for an
    // object identity. Both are the tagged Str that `.WHICH` itself answers.
    if (inv.t == VT::Type && (inv.s == "ObjAt" || inv.s == "ValueObjAt") && m == "new") {
        const Value* str = nullptr;
        for (auto& a : args) if (!(a.t == VT::Pair && a.namedArg)) { str = &a; break; }
        // the one positional is required: `ObjAt.new(:val("x"))` is Rakudo's
        // "Too few positionals" (S02-types/built-in.t)
        if (!str)
            throw RakuError{Value::typeObj("X::TypeCheck::Argument"),
                "Too few positionals passed; expected 2 arguments but got 1"};
        Value w = Value::str(str->toStr());
        w.hashKind = inv.s;
        return w;
    }
    if (inv.t == VT::Type && (inv.s == "Uni" || inv.s == "NFC" || inv.s == "NFD" || inv.s == "NFKC" || inv.s == "NFKD")) {
        if (m == "new") {
            std::vector<uint32_t> in;
            for (auto& a : args) {
                if (a.t == VT::Pair) continue;
                if (a.t == VT::Array || a.t == VT::Range) { // a codepoint LIST flattens: Uni.new(@cps)
                    for (auto& x : a.flatten()) in.push_back((uint32_t)x.toInt());
                } else in.push_back((uint32_t)a.toInt());
            }
            if (inv.s != "Uni") in = uniNormalize(in, inv.s == "NFD" ? 0 : inv.s == "NFC" ? 1 : inv.s == "NFKD" ? 2 : 3);
            Value out = Value::array(); out.s = (inv.s == "Uni" ? std::string("Uni") : inv.s.str()); for (uint32_t c : in) out.arr()->push_back(Value::integer((long long)c));
            return out;
        }
    }
    // a Uni / NFC / NFD / NFKC / NFKD value is an array of codepoints tagged in `s`
    if (inv.t == VT::Array && (inv.s == "Uni" || inv.s == "NFC" || inv.s == "NFD" || inv.s == "NFKC" || inv.s == "NFKD")) {
        if (m == "NFC" || m == "NFD" || m == "NFKC" || m == "NFKD") {
            std::vector<uint32_t> in; if (inv.arr()) for (auto& x : *inv.arr()) in.push_back((uint32_t)x.toInt());
            auto norm = uniNormalize(in, m == "NFD" ? 0 : m == "NFC" ? 1 : m == "NFKD" ? 2 : 3);
            Value out = Value::array(); out.s = m; for (uint32_t c : norm) out.arr()->push_back(Value::integer((long long)c));
            return out;
        }
        if (m == "list" || m == "List" || m == "values" || m == "Seq" || m == "cache") { Value out = Value::array(); out.isList = true; if (inv.arr()) out.setArr(inv.arrS()); return out; }
        if (m == "codes" || m == "elems") return Value::integer(inv.arr() ? (long long)inv.arr()->size() : 0);
        // .gist lives in Value::gist now, so `say $u` and an interpolated $u agree
        // with it. Only .raku is here — it genuinely differs.
        if (m == "raku") {
            char buf[16];
            std::string body;
            if (inv.arr()) for (size_t i = 0; i < inv.arr()->size(); i++) {
                if (i) body += ", ";
                snprintf(buf, sizeof buf, "0x%04x", (unsigned)(*inv.arr())[i].toInt()); // lowercase, as Rakudo
                body += buf;
            }
            // Rakudo reprs the CONSTRUCTOR plus the normalisation: Uni.new(…).NFD
            return Value::str("Uni.new(" + body + ")" + (inv.s == "Uni" ? "" : "." + inv.s));
        }
        if (m == "Str" || m == "Stringy") {
            // Raku Strs are NFG (NFC-normalized under the hood): canonically
            // equivalent codepoint orders must yield the SAME Str, so normalize
            // on the way from Uni to Str (mass-equality.t).
            std::vector<uint32_t> in; if (inv.arr()) for (auto& x : *inv.arr()) in.push_back((uint32_t)x.toInt());
            auto norm = uniNormalize(in, 1 /*NFC*/);
            std::string s; for (uint32_t c : norm) s += cpToUtf8(c);
            return Value::str(s);
        }
    }
    if (inv.t == VT::Type && inv.s == "Complex") {
        if (m == "new") return Value::complex(args.size() > 0 ? args[0].toNum() : 0.0,
                                              args.size() > 1 ? args[1].toNum() : 0.0);
    }
    // `*` is a singleton, so `Whatever.new` hands back the one that exists;
    // HyperWhatever has no instance to hand back and says so by name.
    // `Range.new($min, $max, :excludes-min, :excludes-max)` — the same range the
    // operators build, with the exclusions as nameds instead of carets. It was
    // missing entirely, so every constructed range was X::Method::NotFound
    // (RG-04; the `..` family and this share rtRangeVal, so the endpoint
    // refusals and the coercions are the same rules).
    if (inv.t == VT::Type && m == "new" && inv.s == "Range") {
        ValueList pos;
        bool exMin = false, exMax = false;
        for (auto& a : args) {
            if (a.t == VT::Pair && a.pairVal()) {
                if (a.s == "excludes-min") exMin = a.pairVal()->truthy();
                else if (a.s == "excludes-max") exMax = a.pairVal()->truthy();
                continue;
            }
            pos.push_back(a);
        }
        Value lo = pos.size() > 0 ? pos[0] : Value::integer(0);
        Value hi = pos.size() > 1 ? pos[1] : Value::integer(0);
        return rtRangeVal(lo, hi, exMin, exMax);
    }
    if (inv.t == VT::Type && m == "new" && inv.s == "Whatever") return Value::whatever();
    if (inv.t == VT::Type && m == "new" && inv.s == "HyperWhatever")
        throwTypedV("X::Cannot::New", {{"type", Value::typeObj("HyperWhatever")}},
                    "Cannot make a HyperWhatever object using .new");
    // Num had no constructor, so `Num.new(⅓)` fell through to the generic
    // type-object `.new` and answered 0 for every argument.
    if (inv.t == VT::Type && inv.s == "Num" && m == "new")
        return Value::number(args.empty() ? 0.0 : args[0].toNum());
    // A NATIVE integer type's `.Range` is its exact two's-complement span, and a
    // native float's is -Inf..Inf. S02-types/int-uint.t opens by asking each of
    // them for `.Range.int-bounds` and could not get past line 24 without this;
    // `Bool.Range` is Int's and `Rational.Range` is Rat's (RG-32).
    if (inv.t == VT::Type && m == "Range") {
        static const std::map<std::string, std::pair<const char*, const char*>> kNativeSpan = {
            {"int8",   {"-128", "127"}},
            {"uint8",  {"0", "255"}},
            {"byte",   {"0", "255"}},
            {"int16",  {"-32768", "32767"}},
            {"uint16", {"0", "65535"}},
            {"int32",  {"-2147483648", "2147483647"}},
            {"uint32", {"0", "4294967295"}},
            {"int64",  {"-9223372036854775808", "9223372036854775807"}},
            {"uint64", {"0", "18446744073709551615"}},
            {"int",    {"-9223372036854775808", "9223372036854775807"}},
            {"uint",   {"0", "18446744073709551615"}},
            {"atomicint", {"-9223372036854775808", "9223372036854775807"}},
        };
        auto it = kNativeSpan.find(inv.s.str());
        if (it != kNativeSpan.end()) {
            Value lo = numifyStrOrThrow(it->second.first);
            Value hi = numifyStrOrThrow(it->second.second);
            Value r = rtRangeVal(lo, hi, false, false);
            // The 64-bit spans END on the int64 limits, which are also the
            // sentinels for "no end at all" — so the endpoints have to be
            // CARRIED or `int.Range` renders as -Inf..Inf and matches everything.
            attachRangeEnds(r, lo, hi);
            return r;
        }
        if (inv.s == "num" || inv.s == "num32" || inv.s == "num64")
            return rtRangeVal(Value::number(-INFINITY), Value::number(INFINITY), false, false);
    }
    if (inv.t == VT::Type && m == "Range" &&
        (inv.s == "Int" || inv.s == "Rat" || inv.s == "FatRat" || inv.s == "Num" ||
         inv.s == "UInt" || inv.s == "Bool" || inv.s == "Rational")) {
        // The endpoints are ±Inf. The range stays int-backed and saturated for
        // arithmetic, but it must also CARRY them: without RangeEnds it rendered
        // as -9223372036854775808..9223372036854775807, and `Rat.Range eqv
        // -Inf..Inf` only passed because the old comparison expanded both sides.
        // Int is exclusive at both ends (no Int is infinite), UInt starts at 0.
        bool uint = inv.s == "UInt";
        // `Bool.Range` is Int's, exclusive at both ends; `Rational.Range` is
        // Rat's, inclusive.
        bool intLike = inv.s == "Int" || inv.s == "Bool";
        bool exFrom = intLike, exTo = intLike || uint;
        Value r = Value::range(uint ? 0 : LLONG_MIN, LLONG_MAX, exFrom, exTo);
        attachRangeEnds(r, uint ? Value::integer(0) : Value::number(-INFINITY),
                        Value::number(INFINITY));
        return r;
    }
    // `Rational[Int,Int].new(3,10)` — the parameterized role constructs a Rat
    // (JSON::Fast's roundtrip test builds one directly)
    if (inv.t == VT::Type && (inv.s == "Rat" || inv.s == "FatRat" || inv.s == "Rational") && m == "new") {
        BigInt n = args.size() > 0 ? args[0].toBig() : BigInt(0);
        BigInt d = args.size() > 1 ? args[1].toBig() : BigInt(1);
        Value v = Value::ratZ(std::move(n), std::move(d));
        if (inv.s == "FatRat") v.fatRatM() = true;
        // Rat denominators are capped at uint64 (FatRat is arbitrary): a wider one
        // degrades to Num at construction too — Rat.new(10**400, 9**999).Str is "0"
        // (the value underflows a double), matching the arithmetic spill rule.
        else if (v.ratD() && !v.ratD()->fitsU64()) return Value::number(v.toNum());
        return v;
    }
    // A stand-in for the ecosystem's in-memory read handle — and only while the
    // real thing is absent. Text::CSV ships Text::IO::String as a Raku class of
    // its own; once that class is loaded, its `new` must win, or every handle
    // the module makes comes back as a plain FileHandle with none of the class's
    // state (its whole eol test file reads and writes through one).
    if (inv.t == VT::Type && (inv.s == "IO::String" || inv.s == "Text::IO::String") &&
        [&]{ auto it = classes_.find(inv.s);
             return it == classes_.end() || !it->second || !it->second->findMethod("new"); }()) {
        if (m == "new") {
            std::string data = args.empty() ? "" : args[0].toStr();
            Value h = Value::makeHash(); h.hashKind = "FileHandle";
            (*h.hash())["path"] = Value::str(""); (*h.hash())["mode"] = Value::str("r");
            Value lines = Value::array();
            std::istringstream is(data); std::string line;
            while (std::getline(is, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.arr()->push_back(Value::str(line));
            }
            (*h.hash())["lines"] = lines; (*h.hash())["pos"] = Value::integer(0);
            return h;
        }
    }
    // Lock::Async is a queue of Promises, not a mutex: `.lock` answers a Promise
    // kept once the lock is this caller's, `.unlock` hands it to the next one
    // waiting (or frees it). Nothing blocks — an await is where the waiting is.
    if (inv.t == VT::Hash && inv.hashKind == "Lock::Async" && inv.hash() &&
        (m == "lock" || m == "unlock" || m == "protect")) {
        static std::mutex laM;
        auto kept = [&]() {
            Value p = methodCall(Value::typeObj("Promise"), "new", ValueList{});
            methodCall(p, "keep", ValueList{Value::boolean(true)});
            return p;
        };
        if (m == "lock") {
            // the waiter's Promise is made FIRST, so that seeing the lock held
            // and joining its queue are one step: split, an unlock in between
            // found the queue empty, freed the lock, and this Promise was then
            // queued behind nobody — kept by no one, ever
            Value p = methodCall(Value::typeObj("Promise"), "new", ValueList{});
            {
                std::lock_guard<std::mutex> lk(laM);
                auto& h = *inv.hash();
                if (h.count("__held") && h["__held"].truthy()) {
                    if (!h.count("__queue")) h["__queue"] = Value::array();
                    h["__queue"].arr()->push_back(p);
                    return p;
                }
                h["__held"] = Value::boolean(true);
            }
            methodCall(p, "keep", ValueList{Value::boolean(true)});
            return p;
        }
        if (m == "unlock") {
            Value next;
            {
                std::lock_guard<std::mutex> lk(laM);
                auto& h = *inv.hash();
                if (!h.count("__held") || !h["__held"].truthy())
                    throwTyped("X::Lock::Async::NotLocked", {},
                               "Cannot unlock a Lock::Async that is not currently locked");
                auto q = h.find("__queue");
                if (q != h.end() && q->second.arr() && !q->second.arr()->empty()) {
                    next = q->second.arr()->front();
                    q->second.arr()->erase(q->second.arr()->begin());
                }
                else h["__held"] = Value::boolean(false);
            }
            if (next.t == VT::Hash) methodCall(next, "keep", ValueList{Value::boolean(true)});
            return Value::nil();
        }
        // protect: await the lock, run the block, release it however the block ends
        Value p = methodCall(inv, "lock", ValueList{});
        callBuiltin("await", ValueList{p});
        struct Rel { Interpreter& I; Value l; ~Rel() { try { I.methodCall(l, "unlock", ValueList{}); } catch (...) {} } } rel{*this, inv};
        if (args.empty() || args[0].t != VT::Code) return Value::nil();
        return callCallable(args[0], {});
    }
    if (inv.t == VT::Hash && (inv.hashKind == "Lock" || inv.hashKind == "Lock::Async")) {
        auto st = inv.ext() ? std::static_pointer_cast<LockState>(inv.ext()) : nullptr;
        if (m == "protect" || m == "protect-or-queue-on-recursion") {
            if (args.empty() || args[0].t != VT::Code) return args.empty() ? Value::any() : args[0];
            if (st) { // real mutual exclusion, released even if the block throws
                // Acquire with the GIL RELEASED when contended: a worker that holds
                // the GIL and blocks here would otherwise deadlock the lock's holder
                // (which needs the GIL to run its protected block and release).
                if (!st->m.try_lock()) { bool parked = gilPark(); st->m.lock(); gilUnpark(parked); }
                std::lock_guard<std::recursive_mutex> lk(st->m, std::adopt_lock);
                return callCallable(args[0], {});
            }
            return callCallable(args[0], {});
        }
        if (m == "lock" || m == "acquire") { if (st) { if (!st->m.try_lock()) { bool parked = gilPark(); st->m.lock(); gilUnpark(parked); } } return Value::boolean(true); }
        if (m == "unlock" || m == "release") { if (st) st->m.unlock(); return Value::boolean(true); }
        if (m == "condition") {
            Value v = Value::makeHash(); v.hashKind = "LockCondition";
            if (st) { auto cs = std::make_shared<LockCondState>(); cs->lock = st; v.extM() = cs; }
            return v;
        }
    }
    // A Lock's condition variable: `.wait` releases the lock while it waits
    // (and holds it again after), `.wait(&cond)` until the condition is true,
    // `.signal` / `.signal_all` wake one / every waiter.
    if (inv.t == VT::Hash && inv.hashKind == "LockCondition") {
        auto cs = inv.ext() ? std::static_pointer_cast<LockCondState>(inv.ext()) : nullptr;
        if (m == "signal")     { if (cs) cs->cv.notify_one(); return Value::nil(); }
        if (m == "signal_all") { if (cs) cs->cv.notify_all(); return Value::nil(); }
        if (m == "wait") {
            if (!cs) return Value::nil();
            Value pred = !args.empty() && args[0].t == VT::Code ? args[0] : Value::any();
            auto ready = [&]() { return pred.t == VT::Code && boolify(callCallable(pred, {})); };
            while (!ready()) {
                // the lock is HELD (the caller is inside protect): wait releases
                // it, so the signalling thread can take it, and re-takes it
                std::unique_lock<std::recursive_mutex> lk(cs->lock->m, std::adopt_lock);
                bool parked = gilPark();
                cs->cv.wait(lk);
                gilUnpark(parked);
                lk.release();   // still held — protect releases it
                if (pred.t != VT::Code) break;
            }
            return Value::nil();
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Semaphore") {
        auto st = inv.ext() ? std::static_pointer_cast<SemaphoreState>(inv.ext()) : nullptr;
        if (m == "acquire") {
            if (st) { std::unique_lock<std::mutex> lk(st->m); st->cv.wait(lk, [&]{ return st->count > 0; }); st->count--; }
            return Value::boolean(true);
        }
        if (m == "release") {
            if (st) { std::lock_guard<std::mutex> lk(st->m); st->count++; st->cv.notify_one(); }
            return Value::boolean(true);
        }
        if (m == "try_acquire" || m == "try-acquire") {
            if (!st) return Value::boolean(true);
            std::lock_guard<std::mutex> lk(st->m);
            if (st->count > 0) { st->count--; return Value::boolean(true); }
            return Value::boolean(false);
        }
    }

    // user-defined class: type-object methods (.new and custom constructors)
    // DateTime / Date constructors
    if (inv.t == VT::Type && (inv.s == "DateTime" || inv.s == "Date")) {
        // `Date.new(Int, 1, 1)` — a type object is no year
        if (m == "new")
            for (auto& a : args)
                if (!(a.t == VT::Pair && a.namedArg) && (a.t == VT::Type || a.t == VT::Any || a.t == VT::Nil))
                    throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                        "Cannot resolve caller new(" + inv.s.str() + ", " + a.typeName() + ", …); none of these signatures matches"};
        // a `:formatter(&code)` is stored and applied by .Str (Rakudo's stringifier hook)
        Value formatter; bool haveFmt = false;
        // A formatter need not be a bare Code: `does Callable` + `method CALL-ME`
        // is how DateTime::Format ships one, and callCallable already invokes
        // that. Requiring VT::Code here dropped the argument on the floor and the
        // DateTime silently stringified as ISO-8601 instead.
        for (auto& a : args) if (a.t == VT::Pair && a.s == "formatter" && a.pairVal() &&
                                 (a.pairVal()->t == VT::Code || a.pairVal()->t == VT::Object))
            { formatter = *a.pairVal(); haveFmt = true; }
        auto mk = [&](long long y, long long mo, long long d, long long h, long long mi, Value sec, long long posix, long long tz) {
            // reject out-of-range fields (Rakudo dies): month 1..12, day 1..days-in-month,
            // and for DateTime hour 0..23, minute 0..59 (seconds are leap-checked separately).
            {
                if (mo < 1 || mo > 12)
                    throwTyped("X::Temporal::OutOfRange",
                        {{"what", "Month"}, {"got", std::to_string(mo)}, {"range", "1..12"}},
                        "Month out of range. Is: " + std::to_string(mo) + ", should be in 1..12");
                static const int mlen[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                long long dim = mlen[mo - 1];
                if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) dim = 29;
                if (d < 1 || d > dim)
                    throwTyped("X::Temporal::OutOfRange",
                        {{"what", "Day"}, {"got", std::to_string(d)}, {"range", "1.." + std::to_string(dim)}},
                        "Day out of range. Is: " + std::to_string(d) + ", should be in 1.." + std::to_string(dim));
                if (inv.s == "DateTime") {
                    if (h < 0 || h > 23)
                        throwTyped("X::Temporal::OutOfRange",
                            {{"what", "Hour"}, {"got", std::to_string(h)}, {"range", "0..23"}},
                            "Hour out of range. Is: " + std::to_string(h) + ", should be in 0..23");
                    if (mi < 0 || mi > 59)
                        throwTyped("X::Temporal::OutOfRange",
                            {{"what", "Minute"}, {"got", std::to_string(mi)}, {"range", "0..59"}},
                            "Minute out of range. Is: " + std::to_string(mi) + ", should be in 0..59");
                }
            }
            Value v = Value::makeHash(); v.hashKind = inv.s;
            (*v.hash())["year"] = Value::integer(y); (*v.hash())["month"] = Value::integer(mo); (*v.hash())["day"] = Value::integer(d);
            // A Date is a civil DAY: like Rakudo's (year/month/day/daycount/formatter
            // attributes only), it stores no time-of-day. `Date.today` used to keep
            // the clock reading it was built from, so two Dates of the same day
            // compared unequal and eqv-distinct depending on when they were made.
            if (inv.s == "DateTime") {
                (*v.hash())["hour"] = Value::integer(h); (*v.hash())["minute"] = Value::integer(mi);
                (*v.hash())["second"] = sec; // exact: Int, or Rat/Num for fractional seconds
                (*v.hash())["posix"] = Value::integer(posix);
                (*v.hash())["timezone"] = Value::integer(tz);
            }
            if (haveFmt) (*v.hash())["formatter"] = formatter;
            return v;
        };
        // leap seconds: second 60 must land at 23:59:60 UTC on a real historical
        // leap-second day; 61+ is always out of range.
        auto checkLeap = [&](long long y, long long mo, long long d, long long h, long long mi, long long s, long long tz) {
            if (s < 0)
                throw RakuError{Value::typeObj("X::OutOfRange"),
                    "Second out of range. Is: " + std::to_string(s) + ", should be in 0..^62"};
            if (s < 60) return;
            if (s >= 61)
                throw RakuError{Value::typeObj("X::OutOfRange"),
                    "Second out of range. Is: " + std::to_string(s) + ", should be in 0..^61"};
            long long ep = civilToDays(y, mo, d) * 86400 + h * 3600 + mi * 60 + 59 - tz;
            long long uDays = ep >= 0 ? ep / 86400 : -((-ep + 86399) / 86400);
            long long uy, umo, ud; daysToCivil(uDays, uy, umo, ud);
            long long ymd = uy * 10000 + umo * 100 + ud;
            static const std::set<long long> leapDays = {
                19720630, 19721231, 19731231, 19741231, 19751231, 19761231, 19771231,
                19781231, 19791231, 19810630, 19820630, 19830630, 19850630, 19871231,
                19891231, 19901231, 19920630, 19930630, 19940630, 19951231, 19970630,
                19981231, 20051231, 20081231, 20120630, 20150630, 20161231};
            if (ep % 86400 != 86399 || !leapDays.count(ymd))
                throwTyped("X::OutOfRange",
                    {{"what", "Second"}, {"got", "60"}, {"range", "0..^60"},
                     {"comment", "or leap second not allowed here"}},
                    "Second out of range. Is: 60, should be in 0..^60"
                    " (or leap second not allowed here)");
        };
        if (m == "now" || m == "today") {
            // DateTime.now carries fractional seconds (Rakudo prints six digits;
            // Log::Async's default-format test matches `'.' \d+` in the stamp).
            // Date.today ignores the second slot entirely. Both fields derive
            // from ONE microsecond reading so the fraction can't straddle a
            // second boundary. std::chrono, not gettimeofday: MSVC has no
            // sys/time.h.
            long long us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            // `:timezone(N)` asks for the reading in THAT offset, not the host's
            // — `DateTime.now(timezone => 0)` is how a program says "give me UTC"
            // (DBIish's Pg datetime test stores exactly that). Ignoring it made
            // every such stamp local, so a round-trip through the DB never matched.
            long long tzOff = tzOffsetDyn();
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "timezone" && a.pairVal())
                    tzOff = a.pairVal()->toInt();
            time_t t = (time_t)(us / 1000000);
            struct tm tmbuf; struct tm* lt;
            if (tzOff == tzOffsetDyn()) lt = localtime(&t);
            else {
                time_t shifted = (time_t)(t + tzOff);
#if defined(_WIN32)
                gmtime_s(&tmbuf, &shifted); lt = &tmbuf;
#else
                lt = gmtime_r(&shifted, &tmbuf);
#endif
            }
            Value sec = inv.s == "Date" ? Value::integer(lt->tm_sec)
                      : Value::number((double)lt->tm_sec + (double)(us % 1000000) / 1e6);
            return mk(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, sec, (long long)t, tzOff);
        }
        if (m == "new") {
            if (args.empty()) // `DateTime.new()` / `Date.new()` — must provide arguments
                throw RakuError{Value::typeObj("X::Temporal"),
                    "Cannot call " + inv.s + ".new with no arguments"};
            long long y = 0, mo = 1, d = 1, h = 0, mi = 0, tz = 0;
            Value secV = Value::integer(0);
            // `*` in the DAY slot means "the last day of that month" (Date.new(2042, 2, *)
            // is 2042-02-28). It reaches here as a Whatever/WhateverCode, which numifies
            // to 0 and used to trip the 1..days-in-month check. Year and month may be
            // parsed after the day, so record it and resolve once both are known.
            bool lastDay = false;
            auto isWhatever = [](const Value& v) {
                return v.t == VT::Whatever || (v.t == VT::Code && v.code() && v.code()->isWhateverCode);
            };

            ValueList pos;
            bool isoStr = false;
            bool haveNamedField = false;
            size_t posN = 0;
            for (auto& a : args) if (a.t != VT::Pair) posN++;
            for (auto& a : args) {
                if (a.t == VT::Pair) {
                    long long val = a.pairVal() ? a.pairVal()->toInt() : 0;
                    if (a.s == "year") { y = val; haveNamedField = true; } else if (a.s == "month") { mo = val; haveNamedField = true; } else if (a.s == "day") { d = val; haveNamedField = true; if (a.pairVal() && isWhatever(*a.pairVal())) lastDay = true; }
                    else if (a.s == "hour") { h = val; haveNamedField = true; } else if (a.s == "minute") { mi = val; haveNamedField = true; }
                    else if (a.s == "second") { secV = a.pairVal() ? *a.pairVal() : Value::integer(0); haveNamedField = true; } // exact (frac OK)
                    else if (a.s == "timezone") tz = val;
                    // `DateTime.new(date => Date.new(…), hour => 1)` — the date
                    // argument supplies year/month/day, the rest default to 0
                    else if (a.s == "date" && a.pairVal() && a.pairVal()->t == VT::Hash && a.pairVal()->hash()) {
                        auto& dh = *a.pairVal()->hash();
                        auto get = [&](const char* k, long long dflt) {
                            auto it = dh.find(k); return it == dh.end() ? dflt : it->second.toInt();
                        };
                        y = get("year", y); mo = get("month", mo); d = get("day", d);
                        haveNamedField = true;
                    }
                } else if ((a.t == VT::Str && a.s.find('-', 1) == std::string::npos &&
                            !a.s.empty() && posN == 1) ||
                           // …and one carrying a synthetic or any other non-ASCII
                           // character: `20\x[308]16-07-05` is no ISO timestamp
                           (a.t == VT::Str && posN == 1 &&
                            std::any_of(a.s.str().begin(), a.s.str().end(),
                                        [](char ch) { return (unsigned char)ch >= 0x80; }))) {
                    // the SINGLE string positional that is NOT ISO-shaped (no
                    // dashes): "2012/04" etc. — invalid temporal format
                    // (multiple positionals are the y,m,d form, digits legal)
                    throwTypedV("X::Temporal::InvalidFormat",
                        {{"invalid-str", Value::str(a.s)}, {"target", Value::str(inv.s)},
                         {"format", Value::str(inv.s == "Date" ? "yyyy-mm-dd" : "an ISO 8601 timestamp")}},
                        "Invalid " + inv.s + " string '" + a.s +
                        "'; use " + (inv.s == "Date" ? "yyyy-mm-dd" : "an ISO 8601 timestamp") + " instead");
                } else if (a.t == VT::Str && a.s.find('-', 1) != std::string::npos) {
                    // ISO 8601: YYYY-MM-DD[THH:MM:SS[.frac]][Z|±HH:MM|±HHMM]
                    isoStr = true;
                    const std::string& is = a.s;
                    double fs = 0;
                    (void)sscanf(is.c_str(), "%lld-%lld-%lld", &y, &mo, &d);
                    size_t tp = is.find_first_of("Tt"); // ISO 8601 allows a lowercase 't'
                    if (tp != std::string::npos) {
                        std::string tstr = is.substr(tp + 1);
                        for (auto& c : tstr) if (c == ',') c = '.'; // comma decimal separator for seconds
                        (void)sscanf(tstr.c_str(), "%lld:%lld", &h, &mi);
                        // seconds via cnum, not sscanf %lf: the host's LC_NUMERIC
                        // must not decide whether ".43" parses
                        if (size_t sc1 = tstr.find(':'); sc1 != std::string::npos) {
                            if (size_t sc2 = tstr.find(':', sc1 + 1); sc2 != std::string::npos)
                                fs = cnum::strtod(tstr.c_str() + sc2 + 1, nullptr);
                        }
                        // fractional seconds are EXACT — `:00.43` is the Rat 43/100,
                        // not a double, so .day-fraction and friends stay rational
                        secV = (fs == (long long)fs) ? Value::integer((long long)fs) : Value::number(fs);
                        {
                            size_t c1 = tstr.find(':');
                            size_t c2 = c1 == std::string::npos ? c1 : tstr.find(':', c1 + 1);
                            if (c2 != std::string::npos) {
                                std::string st;
                                for (size_t k = c2 + 1; k < tstr.size() &&
                                     (ascii::isdigit((unsigned char)tstr[k]) || tstr[k] == '.'); k++) st += tstr[k];
                                size_t dot = st.find('.');
                                if (dot != std::string::npos && dot + 1 < st.size()) {
                                    std::string digits = st.substr(0, dot) + st.substr(dot + 1);
                                    BigInt den(1); for (size_t k = dot + 1; k < st.size(); k++) den = den * BigInt(10LL);
                                    secV = Value::ratZ(BigInt::fromString(digits), den);
                                }
                            }
                        }
                        size_t zp = is.find_first_of("Zz+-", tp + 1);
                        // a timestamp that carries its own offset takes no :timezone
                        if (zp != std::string::npos)
                            for (auto& na : args)
                                if (na.t == VT::Pair && na.namedArg && na.s == "timezone")
                                    throwTypedV("X::DateTime::TimezoneClash", {},
                                        "DateTime.new(Str): :timezone argument not allowed with a timestamp offset");
                        if (zp != std::string::npos) {
                            if (is[zp] == 'Z' || is[zp] == 'z') tz = 0;
                            else {
                                // the offset must be exactly ±HH or ±HH:MM (2-digit fields)
                                std::string off = is.substr(zp + 1);
                                auto dig = [](char c) { return c >= '0' && c <= '9'; };
                                bool ok = (off.size() == 2 && dig(off[0]) && dig(off[1])) ||              // ±HH
                                          (off.size() == 4 && dig(off[0]) && dig(off[1]) && dig(off[2]) && dig(off[3])) || // ±HHMM
                                          (off.size() == 5 && off[2] == ':' && dig(off[0]) && dig(off[1]) && dig(off[3]) && dig(off[4])); // ±HH:MM
                                if (!ok)
                                    // the same fault the non-ISO branch above
                                    // reports, and it gets the same class and
                                    // wording: X::DateTime::InvalidFormat is not
                                    // a Raku type, and this was the only site
                                    // that used it
                                    throwTypedV("X::Temporal::InvalidFormat",
                                        {{"invalid-str", Value::str(is)}, {"target", Value::str("DateTime")},
                                         {"format", Value::str("an ISO 8601 timestamp")}},
                                        "Invalid DateTime string '" + is +
                                        "'; use an ISO 8601 timestamp instead");
                                long long oh = (off[0] - '0') * 10 + (off[1] - '0');
                                long long om = off.size() == 4 ? (off[2] - '0') * 10 + (off[3] - '0')
                                             : off.size() == 5 ? (off[3] - '0') * 10 + (off[4] - '0') : 0;
                                if (om >= 60)
                                    throwTyped("X::OutOfRange",
                                        {{"what", "minute"}, {"got", std::to_string(om)}, {"range", "0..59"}},
                                        "Minute out of range. Is: " + std::to_string(om) + ", should be in 0..59");
                                tz = (is[zp] == '-' ? -1 : 1) * (oh * 3600 + om * 60);
                            }
                        }
                    }
                } else pos.push_back(a);
            }
            if (haveNamedField && !pos.empty()) // `DateTime.new(:2016year, 42)` — no mixing
                throw RakuError{Value::typeObj("X::Temporal"),
                    "Cannot mix a named date component with a positional argument to " + inv.s + ".new"};
            // DateTime.new($dt) / Date.new($dt) — a Dateish (or Instant) argument
            // is the INSTANT it names, not a year. It fell through to the
            // positional-fields arm below, where `.toInt()` made the whole
            // DateTime a year and `DateTime.new($x + $min)` answered 0008-01-01.
            // (Reached constantly here: this engine's DateTime + Num is a
            // DateTime, where Rakudo's is an Instant, so a module adding a
            // random offset to a bound hands .new exactly this.)
            //
            // Date has two shapes of its own (#88: `Date.new(now)` printed
            // +1789452789-01-01 — the posix reading taken as a year, because
            // only the DateTime arm below consumed the seconds):
            //  - Date.new(Dateish) copies the CIVIL day, as Rakudo's
            //    `self.new($d.year, $d.month, $d.day)` does. A DateTime at
            //    01:00 in +02:00 is that local date, not the UTC one its posix
            //    names, so it must not go through the seconds at all;
            //  - Date.new(Instant) is Rakudo's `self.new(DateTime.new($i))`,
            //    which is UTC — the seconds arm, with no zone applied.
            // An Instant is a NUMBER tagged "Instant" (that is how `now` and
            // Instant.from-posix arrive), so the tag is read before the
            // Dateish-hash test, which it does not pass.
            bool fromInstant = pos.size() == 1 && pos[0].hashKind == "Instant";
            if (!isoStr && pos.size() == 1 && pos[0].t == VT::Hash &&
                (pos[0].hashKind == "DateTime" || pos[0].hashKind == "Date" ||
                 pos[0].hashKind == "Instant")) {
                if (inv.s == "Date" && !fromInstant) {
                    auto& dh = *pos[0].hash();
                    auto civil = [&](const char* k) {
                        auto it = dh.find(k); return it == dh.end() ? 0LL : it->second.toInt();
                    };
                    long long cy = civil("year"), cmo = civil("month"), cd = civil("day");
                    return mk(cy, cmo, cd, 0, 0, Value::integer(0), civilToDays(cy, cmo, cd) * 86400, 0);
                }
                Value posix = methodCall(pos[0], fromInstant ? "Num" : "posix",
                                         ValueList{Value::pair("real", Value::boolean(true))});
                pos[0] = posix;
            }
            if (!isoStr && pos.size() == 1 && pos[0].isNumeric() && (inv.s == "DateTime" || fromInstant)) {
                // DateTime.new($posix) — seconds since the epoch, EXACT: a Rat keeps
                // its fraction and an Int of any size its day (`DateTime.new(1273…129)`
                // is the year +4034522497029953, far past what a double counts in
                // whole seconds). A :timezone shifts the civil reading only.
                // An INSTANT is TAI: Instant.to-posix takes the leap seconds back
                // off, and one that falls INSIDE a leap second reads as :60 —
                // Rakudo's `self.new(floor($p - $leap)); .second + $p % 1 + $leap`,
                // then `.in-timezone`.
                Value P = pos[0]; P.hashKind = "";
                long long leap = 0;
                bool instant = fromInstant || pos[0].hashKind == "Instant";
                if (instant) {
                    bool inLeap = false;
                    long long off = posixOffsetForTai(floorSecsLL(P.toNum()), inLeap);
                    P = applyArith("-", P, Value::integer(off));
                    leap = inLeap ? 1 : 0;
                }
                Value ipV = P.t == VT::Int ? P : methodCall(P, "floor", ValueList{});
                Value fracV = applyArith("-", P, ipV);
                bool hasFrac = fracV.toNum() != 0.0;
                if (leap) ipV = applyArith("-", ipV, Value::integer(1));
                long long tzUse = inv.s == "DateTime" ? tz : 0;   // a Date has no zone: the UTC day
                bool laterZone = instant && tzUse != 0;         // shift after, so a :60 survives
                Value ltV = applyArith("+", ipV, Value::integer(laterZone ? 0 : tzUse));
                long long days = applyArith("div", ltV, Value::integer(86400)).toInt();
                long long rem = applyArith("mod", ltV, Value::integer(86400)).toInt();
                daysToCivil(days, y, mo, d);
                h = rem / 3600; mi = (rem % 3600) / 60;
                long long si = rem % 60 + leap;
                secV = hasFrac ? applyArith("+", Value::integer(si), fracV) : Value::integer(si);
                long long ip = ipV.toInt() + leap;
                if (laterZone)
                    return methodCall(mk(y, mo, d, h, mi, secV, ip, 0), "in-timezone",
                                      ValueList{Value::integer(tzUse)});
                return mk(y, mo, d, h, mi, secV, ip, tz);
            }
            if (!isoStr) {
                if (pos.size() >= 1) y = pos[0].toInt();
                if (pos.size() >= 2) mo = pos[1].toInt();
                if (pos.size() >= 3) { if (isWhatever(pos[2])) lastDay = true; else d = pos[2].toInt(); }
                if (pos.size() >= 4) h = pos[3].toInt();   // DateTime.new(y, m, d, H, M, S)
                if (pos.size() >= 5) mi = pos[4].toInt();
                if (pos.size() >= 6) secV = pos[5];        // exact (frac OK)
            }
            if (lastDay) { // resolve `*` now that the year and month are known
                static const int mlen[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                d = (mo >= 1 && mo <= 12) ? mlen[mo - 1] : 31;
                if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) d = 29;
            }
            long long sInt = secV.toInt(); // floor for epoch/leap math
            if (secV.toNum() < 0) // a fractional negative second (-1/2) truncates to 0, so check the value
                throw RakuError{Value::typeObj("X::OutOfRange"),
                    "Second out of range. Is: " + secV.toStr() + ", should be in 0..^62"};
            if (inv.s == "DateTime") checkLeap(y, mo, d, h, mi, sInt, tz);
            long long ep = civilToDays(y, mo, d) * 86400 + h * 3600 + mi * 60 + (sInt >= 60 ? 59 : sInt) - tz;
            return mk(y, mo, d, h, mi, secV, ep, tz);
        }
    }
    if (inv.t == VT::Hash && (inv.hashKind == "DateTime" || inv.hashKind == "Date")) {
        auto fld = [&](const char* k) { auto it = inv.hash()->find(k); return it != inv.hash()->end() ? it->second.toInt() : 0; };
        // Dates enumerate day by day: .succ/.pred step a whole day (Range
        // iteration and `for $d1..$d2` rely on this)
        if ((m == "succ" || m == "pred") && inv.hashKind == "Date")
            return makeDate(civilToDays(fld("year"), fld("month"), fld("day")) + (m == "succ" ? 1 : -1), &inv);
        // with no formatter of its own a Date/DateTime answers the Callable TYPE
        // object — the attribute's declared type, not a bare Any
        if (m == "formatter") return inv.hash()->count("formatter") ? (*inv.hash())["formatter"]
                                                                  : Value::typeObj("Callable");
        if (m == "second" || m == "whole-second") {
            auto it = inv.hash()->find("second");
            Value sv = it != inv.hash()->end() ? it->second : Value::integer(0);
            return m == "whole-second" ? Value::integer(sv.toInt()) : sv; // .second keeps the fraction
        }
        if (m == "posix") { // `:real` keeps the fractional seconds
            // a Date stores no clock fields — its posix reading is midnight UTC
            long long px = inv.hash()->count("posix")
                ? fld("posix")
                : civilToDays(fld("year"), fld("month"), fld("day")) * 86400;
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "real" && (!a.pairVal() || a.pairVal()->truthy())) {
                    Value sec = inv.hash()->count("second") ? (*inv.hash())["second"] : Value::integer(0);
                    Value frac = applyArith("-", sec, Value::integer(sec.toInt()));
                    return applyArith("+", Value::integer(px), frac);
                }
            return Value::integer(px);
        }
        if (m == "year" || m == "month" || m == "day" || m == "hour" || m == "minute")
            return Value::integer(fld(m.c_str()));
        if (m == "hh-mm-ss") { char b[16]; snprintf(b, sizeof b, "%02lld:%02lld:%02lld", fld("hour"), fld("minute"), fld("second")); return Value::str(b); }
        if (m == "day-of-month") return Value::integer(fld("day")); // alias for .day
        if (m == "weekday-of-month") return Value::integer((fld("day") - 1) / 7 + 1);
        if (m == "DateTime") { // Date → DateTime (midnight); DateTime → self
            if (inv.hashKind == "DateTime") return inv;
            ValueList mk{Value::integer(fld("year")), Value::integer(fld("month")),
                         Value::integer(fld("day"))};
            // 6.e added `:timezone`, and its DEFAULT is $*TZ rather than UTC —
            // so a bare .DateTime moves too, not just one given an argument.
            // The 6.c candidate takes nothing, so before 6.e the argument is
            // silently dropped and the answer is always UTC.
            if (sixE()) {
                bool given = false;
                for (auto& a2 : args)
                    if (a2.t == VT::Pair && a2.s == "timezone" && a2.pairVal()) {
                        mk.push_back(Value::pair("timezone", *a2.pairVal()));
                        given = true;
                    }
                if (!given) mk.push_back(Value::pair("timezone", Value::integer(tzOffsetDyn())));
            }
            return methodCall(Value::typeObj("DateTime"), "new", mk);
        }
        if (m == "Instant") {
            // TAI, exactly: POSIX (the leap second 23:59:60 reads as the midnight
            // after it, as Rakudo's `.posix` does) plus TAI − UTC at that moment,
            // plus the fraction of the second. An inexact double made
            // `$b.Instant - $a.Instant` 8704.900000095367 instead of 8704.9.
            auto sit = inv.hash()->find("second");
            Value sec = sit != inv.hash()->end() ? sit->second : Value::integer(0);
            sec.hashKind = "";
            long long whole = (long long)std::floor(sec.toNum());
            long long ep = civilToDays(fld("year"), fld("month"), fld("day")) * 86400 +
                           fld("hour") * 3600 + fld("minute") * 60 + whole - fld("timezone");
            Value frac = applyArith("-", sec, Value::integer(whole));
            Value v = applyArith("+", Value::integer(ep + taiOffsetForPosix(ep, whole >= 60)), frac);
            v.hashKind = "Instant"; return identify(v);
        }
        if ((m == "timezone" || m == "offset") && inv.hashKind == "DateTime") return Value::integer(fld("timezone"));
        if ((m == "in-timezone" || m == "utc" || m == "local") && inv.hashKind == "DateTime") {
            long long newTz = m == "utc" ? 0 : m == "local" ? tzOffsetDyn()
                            : (args.empty() ? 0 : args[0].toInt());
            auto sit = inv.hash()->find("second");
            Value secV = sit != inv.hash()->end() ? sit->second : Value::integer(0);
            long long sInt = (long long)std::floor(secV.toNum());
            // fractional seconds survive the shift, EXACTLY: a Rat second stays one
            Value fracV = secV; fracV.hashKind = "";
            fracV = applyArith("-", fracV, Value::integer(sInt));
            double frac = fracV.toNum();
            long long leap = sInt >= 60 ? 1 : 0;
            long long ep = civilToDays(fld("year"), fld("month"), fld("day")) * 86400 +
                           fld("hour") * 3600 + fld("minute") * 60 +
                           (leap ? 59 : sInt) - fld("timezone");
            long long lt = ep + newTz;
            long long days = lt >= 0 ? lt / 86400 : -((-lt + 86399) / 86400);
            long long rem = lt - days * 86400;
            long long y, mo, d; daysToCivil(days, y, mo, d);
            long long outSec = rem % 60 + leap;
            Value v = Value::makeHash(); v.hashKind = "DateTime";
            // a conversion keeps the formatter: `$dt.utc`, `.local`, `.clone`,
            // `.in-timezone`, `.later`, `.earlier` all stay formatted, as in Rakudo
            if (inv.hash()->count("formatter")) (*v.hash())["formatter"] = (*inv.hash())["formatter"];
            (*v.hash())["year"] = Value::integer(y); (*v.hash())["month"] = Value::integer(mo); (*v.hash())["day"] = Value::integer(d);
            (*v.hash())["hour"] = Value::integer(rem / 3600); (*v.hash())["minute"] = Value::integer((rem % 3600) / 60);
            (*v.hash())["second"] = frac != 0.0 ? applyArith("+", Value::integer(outSec), fracV) : Value::integer(outSec);
            (*v.hash())["posix"] = Value::integer(ep); (*v.hash())["timezone"] = Value::integer(newTz);
            return v;
        }
        if (m == "raku" && inv.hashKind == "Date") { // Rakudo's Date.raku form
            char buf[48];
            snprintf(buf, sizeof buf, "Date.new(%lld,%lld,%lld)", fld("year"), fld("month"), fld("day"));
            return Value::str(buf);
        }
        if (m == "raku" && inv.hashKind == "DateTime") {
            // Rakudo's form is POSITIONAL — `DateTime.new(2022,1,1,2,9,0)` — the
            // second is the stored value's own Str (0.5, 0.333333, 59.999: not
            // truncated to a whole second) and `:timezone(N)` trails only when N
            // is not 0. The named form printed here before was ours alone;
            // Mathematica::Serializer rewrites this string into a WL
            // DateObject[{…}] by substitution and pins the Rakudo text exactly.
            auto sit = inv.hash()->find("second");
            std::string sec = sit != inv.hash()->end() ? sit->second.toStr() : "0";
            char buf[160];
            snprintf(buf, sizeof buf, "DateTime.new(%lld,%lld,%lld,%lld,%lld,%s",
                     fld("year"), fld("month"), fld("day"), fld("hour"), fld("minute"), sec.c_str());
            std::string out = buf;
            if (long long tz = fld("timezone")) out += ",:timezone(" + std::to_string(tz) + ")";
            return Value::str(out + ")");
        }
        if (m == "mm-dd-yyyy" || m == "dd-mm-yyyy") { // US / European date strings
            char buf[48];
            if (m == "mm-dd-yyyy") snprintf(buf, sizeof buf, "%02lld-%02lld-%04lld", fld("month"), fld("day"), fld("year"));
            else                   snprintf(buf, sizeof buf, "%02lld-%02lld-%04lld", fld("day"), fld("month"), fld("year"));
            return Value::str(buf);
        }
        if (m == "Str" || m == "gist" || m == "yyyy-mm-dd" || m == "Date") {
            // `$d.Date` on a Date is Rakudo's `self` and keeps the formatter;
            // `$dt.Date` on a DateTime builds a fresh Date and does NOT
            if (m == "Date")
                return makeDate(civilToDays(fld("year"), fld("month"), fld("day")),
                                inv.hashKind == "Date" ? &inv : nullptr);
            // .Str/.gist go through the value model, which applies a stored
            // `:formatter` (one renderer — `$d eq "$d"` has to hold).
            // `.yyyy-mm-dd` is the ISO form by name and never formats.
            if (m == "yyyy-mm-dd") return Value::str(dateGist(*inv.hash(), true));
            return Value::str(inv.toStr());
        }
        if (m == "day-of-week" || m == "dow") { // 1=Monday .. 7=Sunday (Sakamoto's algorithm)
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
            long long yy = (mo < 3) ? y - 1 : y;
            int sak = (int)(((yy + yy / 4 - yy / 100 + yy / 400 + t[(mo - 1) % 12] + d) % 7 + 7) % 7); // 0=Sun
            return Value::integer((sak + 6) % 7 + 1);
        }
        if ((m == "later" || m == "earlier") && inv.hashKind == "Date") {
            long long sign = (m == "later") ? 1 : -1;
            long long days = 0, months = 0, years = 0;
            ValueList units;
            for (auto& a : args) { if (a.t == VT::Array && a.arr()) for (auto& x : *a.arr()) units.push_back(x); else units.push_back(a); }
            for (auto& a : units) if (a.t == VT::Pair && a.pairVal()) {
                long long v = a.pairVal()->toInt();
                if (a.s == "day" || a.s == "days") days += v;
                else if (a.s == "week" || a.s == "weeks") days += 7 * v;
                else if (a.s == "month" || a.s == "months") months += v;
                else if (a.s == "year" || a.s == "years") years += v;
            }
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            if (months || years) {
                long long total = (y * 12 + (mo - 1)) + sign * (years * 12 + months);
                y = total >= 0 ? total / 12 : -((-total + 11) / 12);
                mo = total - y * 12 + 1;
                // clamp to the target month's length (2026-01-31 +1 month -> 2026-02-28)
                static const int mlen[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                long long lim = mlen[mo - 1];
                if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) lim = 29;
                if (d > lim) d = lim;
            }
            return makeDate(civilToDays(y, mo, d) + sign * days, &inv);
        }
        if ((m == "later" || m == "earlier") && inv.hashKind == "DateTime") {
            long long sign = (m == "later") ? 1 : -1;
            long long secs = 0, days = 0, months = 0, years = 0;
            ValueList units; // `.later((:2hours, :30minutes))` passes the units in a list
            for (auto& a : args) { if (a.t == VT::Array && a.arr()) for (auto& x : *a.arr()) units.push_back(x); else units.push_back(a); }
            // Seconds alone move the INSTANT, as Rakudo's move-by-unit does: TAI
            // counts the leap seconds, so 23:59:59 + 1s on a leap day is 23:59:60
            // and 00:00:00 − 1s after one lands on it. A fractional amount stays exact.
            {
                bool onlySecs = !units.empty();
                Value amt = Value::integer(0);
                for (auto& a : units) {
                    if (!(a.t == VT::Pair && a.pairVal() && (a.s == "second" || a.s == "seconds"))) { onlySecs = false; break; }
                    amt = applyArith("+", amt, *a.pairVal());
                }
                if (onlySecs) {
                    Value inst = methodCall(inv, "Instant", ValueList{});
                    Value moved = applyArith(sign > 0 ? "+" : "-", inst, amt);
                    moved.hashKind = "Instant";
                    Value out = methodCall(Value::typeObj("DateTime"), "new",
                        ValueList{moved, Value::pair("timezone", Value::integer(fld("timezone")))});
                    if (inv.hash()->count("formatter") && out.t == VT::Hash && out.hash())
                        (*out.hash())["formatter"] = (*inv.hash())["formatter"];
                    return out;
                }
            }
            for (auto& a : units) if (a.t == VT::Pair && a.pairVal()) {
                long long v = a.pairVal()->toInt();
                if      (a.s == "second" || a.s == "seconds") secs   += v;
                else if (a.s == "minute" || a.s == "minutes") secs   += 60 * v;
                else if (a.s == "hour"   || a.s == "hours")   secs   += 3600 * v;
                else if (a.s == "day"    || a.s == "days")    days   += v;
                else if (a.s == "week"   || a.s == "weeks")   days   += 7 * v;
                else if (a.s == "month"  || a.s == "months")  months += v;
                else if (a.s == "year"   || a.s == "years")   years  += v;
            }
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            long long h = fld("hour"), mi = fld("minute"), tz = fld("timezone");
            double secF = inv.hash()->count("second") ? (*inv.hash())["second"].toNum() : 0.0;
            long long sInt = (long long)std::floor(secF); double frac = secF - (double)sInt;
            // calendar units first: shift year/month, clamp the day into the month
            if (months || years) {
                long long total = (y * 12 + (mo - 1)) + sign * (years * 12 + months);
                y = total >= 0 ? total / 12 : -((-total + 11) / 12);
                mo = total - y * 12 + 1;
                static const int mlen[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                long long lim = mlen[mo - 1];
                if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) lim = 29;
                if (d > lim) d = lim;
            }
            // A LEAP second (:60) only exists on the day it was inserted — moving
            // off that day clamps it to :59 rather than rolling into midnight,
            // unless the move lands on another leap second (checked below).
            bool wasLeap = sInt == 60 && (days || months || years || secs);
            if (wasLeap) sInt = 59;
            // fixed duration: fold days + time into an absolute count, then re-split
            long long dayNum = civilToDays(y, mo, d) + sign * days;
            long long totSec = h * 3600 + mi * 60 + sInt + sign * secs;
            long long carry = totSec >= 0 ? totSec / 86400 : -((-totSec + 86399) / 86400);
            dayNum += carry; totSec -= carry * 86400;
            daysToCivil(dayNum, y, mo, d);
            long long nh = totSec / 3600, nmi = (totSec % 3600) / 60, nsec = totSec % 60;
            if (wasLeap && nsec == 59) {   // 1972-12-31T23:59:60 + 1 year is 1973-12-31T23:59:60
                long long ut = dayNum * 86400 + totSec - tz;
                long long ud = ut >= 0 ? ut / 86400 : -((-ut + 86399) / 86400);
                long long us = ut - ud * 86400;
                long long uy, um, udd; daysToCivil(ud, uy, um, udd);
                if (us == 86399 && isLeapSecondDate(uy, um, udd)) nsec = 60;
            }
            Value v = Value::makeHash(); v.hashKind = "DateTime";
            // a conversion keeps the formatter: `$dt.utc`, `.local`, `.clone`,
            // `.in-timezone`, `.later`, `.earlier` all stay formatted, as in Rakudo
            if (inv.hash()->count("formatter")) (*v.hash())["formatter"] = (*inv.hash())["formatter"];
            (*v.hash())["year"] = Value::integer(y); (*v.hash())["month"] = Value::integer(mo); (*v.hash())["day"] = Value::integer(d);
            (*v.hash())["hour"] = Value::integer(nh); (*v.hash())["minute"] = Value::integer(nmi);
            (*v.hash())["second"] = frac != 0.0 ? Value::number((double)nsec + frac) : Value::integer(nsec);
            (*v.hash())["timezone"] = Value::integer(tz);
            (*v.hash())["posix"] = Value::integer(dayNum * 86400 + totSec - tz);
            return v;
        }
        if ((m == "truncated-to" || m == "truncate-to") &&
            (inv.hashKind == "DateTime" || inv.hashKind == "Date") && !args.empty()) {
            std::string u = args[0].toStr();
            long long y = fld("year"), mo = fld("month"), d = fld("day"), h = fld("hour"), mi = fld("minute");
            double sec = inv.hash()->count("second") ? (*inv.hash())["second"].toNum() : 0.0;
            long long si = (long long)std::floor(sec);
            if (u == "second")      sec = (double)si;
            else if (u == "minute") { sec = 0; }
            else if (u == "hour")   { sec = 0; mi = 0; }
            else if (u == "day")    { sec = 0; mi = 0; h = 0; }
            else if (u == "week")   { sec = 0; mi = 0; h = 0;
                long long dn = civilToDays(y, mo, d); long long wd = ((dn % 7) + 3 + 7) % 7; // 0=Mon
                dn -= wd; daysToCivil(dn, y, mo, d); }
            else if (u == "month")  { sec = 0; mi = 0; h = 0; d = 1; }
            else if (u == "year")   { sec = 0; mi = 0; h = 0; d = 1; mo = 1; }
            if (inv.hashKind == "Date") return makeDate(civilToDays(y, mo, d), &inv);
            long long tz = fld("timezone");
            long long ep = civilToDays(y, mo, d) * 86400 + h * 3600 + mi * 60 + si - tz;
            Value v = Value::makeHash(); v.hashKind = "DateTime";
            // a conversion keeps the formatter: `$dt.utc`, `.local`, `.clone`,
            // `.in-timezone`, `.later`, `.earlier` all stay formatted, as in Rakudo
            if (inv.hash()->count("formatter")) (*v.hash())["formatter"] = (*inv.hash())["formatter"];
            (*v.hash())["year"] = Value::integer(y); (*v.hash())["month"] = Value::integer(mo); (*v.hash())["day"] = Value::integer(d);
            (*v.hash())["hour"] = Value::integer(h); (*v.hash())["minute"] = Value::integer(mi);
            (*v.hash())["second"] = (u == "second" && sec != std::floor(sec)) ? Value::number(sec) : Value::integer((long long)sec);
            (*v.hash())["timezone"] = Value::integer(tz); (*v.hash())["posix"] = Value::integer(ep);
            return v;
        }
        if (m == "is-leap-year" && (inv.hashKind == "Date" || inv.hashKind == "DateTime")) {
            long long y = fld("year");
            return Value::boolean((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
        }
        if (m == "days-in-month" || m == "last-date-in-month") {
            long long y = fld("year"), mo = fld("month");
            static const int mlen[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            long long dim = (mo >= 1 && mo <= 12) ? mlen[mo - 1] : 30;
            if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) dim = 29;
            if (m == "days-in-month") return Value::integer(dim);
            return makeDate(civilToDays(y, mo, dim), &inv); // last-date-in-month → a Date
        }
        if (m == "first-date-in-month")
            return makeDate(civilToDays(fld("year"), fld("month"), 1), &inv);
        if (m == "day-of-year") {
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            return Value::integer(civilToDays(y, mo, d) - civilToDays(y, 1, 1) + 1);
        }
        if (m == "days-in-year" && (inv.hashKind == "Date" || inv.hashKind == "DateTime")) {
            long long y = fld("year");
            return Value::integer(((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365);
        }
        if (m == "week-number" || m == "week-year" || m == "week") {
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            long long ordinal = civilToDays(y, mo, d) - civilToDays(y, 1, 1) + 1;
            static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
            auto dow = [&](long long yy, long long mm, long long dd) -> int { // 1=Mon..7=Sun
                long long yr = (mm < 3) ? yy - 1 : yy;
                int sak = (int)(((yr + yr / 4 - yr / 100 + yr / 400 + t[(mm - 1) % 12] + dd) % 7 + 7) % 7);
                return (sak + 6) % 7 + 1;
            };
            auto weeksInYear = [](long long yy) -> long long { // ISO 8601: 52 or 53
                auto p = [](long long a) { return (int)(((a + a / 4 - a / 100 + a / 400) % 7 + 7) % 7); };
                return (p(yy) == 4 || p(yy - 1) == 3) ? 53 : 52;
            };
            long long wd = dow(y, mo, d);
            long long week = (10 + ordinal - wd) / 7, wyear = y;
            if (week < 1) { wyear = y - 1; week = weeksInYear(y - 1); }
            else if (week > weeksInYear(y)) { wyear = y + 1; week = 1; }
            if (m == "week-number") return Value::integer(week);
            if (m == "week-year")   return Value::integer(wyear);
            Value o = Value::array({Value::integer(wyear), Value::integer(week)}); o.isList = true; return o;
        }
        if (m == "daycount") { // days since the MJD epoch (1858-11-17)
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            return Value::integer(civilToDays(y, mo, d) + 40587);
        }
        // Dateish numifies: a Date IS its daycount (an Int), a DateTime its
        // Instant — so `$date == $other` and `+$date` answer by the day.
        if (m == "Numeric" || m == "Real" || (m == "Int" && inv.hashKind == "Date")) {
            if (inv.hashKind == "Date")
                return Value::integer(civilToDays(fld("year"), fld("month"), fld("day")) + 40587);
            ValueList none; return methodCall(inv, "Instant", none);
        }
        if (m == "day-fraction") {
            // a UTC day with a leap second is 86401 seconds long (finite frozen list)
            static const std::set<long long> leapDays = {
                19720630, 19721231, 19731231, 19741231, 19751231, 19761231, 19771231,
                19781231, 19791231, 19810630, 19820630, 19830630, 19850630, 19871231,
                19891231, 19901231, 19920630, 19930630, 19940630, 19951231, 19970630,
                19981231, 20051231, 20081231, 20120630, 20150630, 20161231};
            long long ymd = fld("year") * 10000 + fld("month") * 100 + fld("day");
            long long dayLen = 86400 + (leapDays.count(ymd) ? 1 : 0);
            // stay in Value arithmetic so an exact (Rat) second keeps the answer
            // exact: 12:23:00.43 of an ordinary day is 4458043/8640000
            Value sec = inv.hash()->count("second") ? (*inv.hash())["second"] : Value::integer(0);
            Value total = applyArith("+", applyArith("+",
                              Value::integer(fld("hour") * 3600),
                              Value::integer(fld("minute") * 60)), sec);
            return applyArith("/", total, Value::integer(dayLen));
        }
        if (m == "julian-date" || m == "modified-julian-date") {
            long long y = fld("year"), mo = fld("month"), d = fld("day");
            // exact, like .day-fraction: the day count is an Int and the time of
            // day a Rat, so the sum stays rational
            Value sec = inv.hash()->count("second") ? (*inv.hash())["second"] : Value::integer(0);
            Value frac = applyArith("/", applyArith("+", applyArith("+",
                              Value::integer(fld("hour") * 3600),
                              Value::integer(fld("minute") * 60)), sec),
                          Value::integer(86400));
            // 2440587.5 = the Julian date of the civil epoch 1970-01-01T00:00
            Value jd = applyArith("+", applyArith("+",
                           Value::integer(civilToDays(y, mo, d)),
                           Value::ratZ(BigInt(4881175LL), BigInt(2LL))), frac);
            return m == "julian-date" ? jd
                 : applyArith("-", jd, Value::ratZ(BigInt(4800001LL), BigInt(2LL)));
        }
        // the timezone offset in bigger units (the raw one is seconds)
        if (m == "offset-in-minutes" && inv.hashKind == "DateTime")
            return applyArith("/", Value::integer(fld("timezone")), Value::integer(60));
        if (m == "offset-in-hours" && inv.hashKind == "DateTime")
            return applyArith("/", Value::integer(fld("timezone")), Value::integer(3600));
        if (m == "truncated-to" || m == "earlier" || m == "later") return inv; // best-effort (weeks etc.)
    }

    // `.new` on a scalar built-in type object → that type's default value. This is
    // what real Raku does (Str.new → "", Int.new → 0) and lets `augment class Str {…}`
    // methods be reached via `Str.new.themethod`.
    // A BacktraceFrame (from Exception.backtrace): file/line/code plus the
    // predicates Backtrace consumers grep by (Log::Async::Context)
    // A Backtrace: the list of frames, tagged so its STRING forms are the frame
    // lines rather than a joined list of hashes. `.list`/`.elems`/`.grep` and
    // every other list method fall through to the Array surface untouched.
    if (inv.t == VT::Array && inv.s == "Backtrace" && inv.arr()) {
        if (m == "Str" || m == "gist" || m == "full" || m == "nice" || m == "concise") {
            BtStyle st; st.excerpt = st.typeLine = st.colour = false;
            if (m == "full") { st.full = true; st.collapse = false; }
            return Value::str(renderBacktraceValue(inv, st));
        }
        if (m == "summary") {  // Rakudo: the frames a reader cares about
            BtStyle st; st.excerpt = st.typeLine = st.colour = false;
            return Value::str(renderBacktraceValue(inv, st));
        }
        if (m == "next-interesting-index") {
            // ours records only user frames, so the next one is simply the next
            long long from = args.empty() ? 0 : args[0].toInt();
            long long n = (long long)inv.arr()->size();
            return from + 1 < n ? Value::integer(from + 1) : Value::any();
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "BacktraceFrame" && inv.hash()) {
        if (m == "file" || m == "line" || m == "code") {
            auto it = inv.hash()->find(m.s);
            return it != inv.hash()->end() ? it->second : Value::any();
        }
        if (m == "is-hidden" || m == "is-setting" || m == "is-routine")
            return Value::boolean(false);
        if (m == "subname") {
            auto it = inv.hash()->find("code");
            // the mainline has no declaring routine: Rakudo names it <unit>,
            // and a frame whose subname is "" is indistinguishable from a
            // bare block to anything walking the list
            if (it == inv.hash()->end() || !it->second.code()) return Value::str("<unit>");
            return Value::str(it->second.code()->name);
        }
        if (m == "gist" || m == "Str") {
            Value one = Value::array(); one.isList = true; one.arr()->push_back(inv);
            BtStyle plain; plain.excerpt = plain.typeLine = plain.colour = false;
            std::string r = renderBacktraceValue(one, plain);
            if (!r.empty() && r.back() == '\n') r.pop_back();
            return Value::str(r);
        }
    }
    // A CallFrame (from `callframe`): .file / .line / .code, and `<unit>` as the
    // code's name at mainline, where there is no enclosing routine.
    if (inv.t == VT::Hash && inv.hashKind == "CallFrame" && inv.hash()) {
        if (m == "file" || m == "line") {
            auto it = inv.hash()->find(m.s);
            return it != inv.hash()->end() ? it->second : Value::any();
        }
        if (m == "my") {
            auto it = inv.hash()->find("my");
            if (it != inv.hash()->end()) return it->second;
        }
        if (m == "code" || m == "callframe" || m == "my" || m == "annotations") {
            auto it = inv.hash()->find("code");
            if (m == "code") {
                if (it != inv.hash()->end()) return it->second;
                Value u; u.t = VT::Code; u.setCode(std::make_shared<Callable>());
                u.code()->name = "<unit>";        // the mainline is not a routine
                return u;
            }
            return Value::makeHash();
        }
        if (m == "raku") {
            auto fl = inv.hash()->find("file"); auto ln = inv.hash()->find("line");
            return Value::str("CallFrame.new(file => " + std::string(fl != inv.hash()->end() ? fl->second.toStr() : "") +
                              ", line => " + (ln != inv.hash()->end() ? ln->second.toStr() : "0") + ")");
        }
        if (m == "gist" || m == "Str") {
            auto fl = inv.hash()->find("prog");
            if (fl == inv.hash()->end()) fl = inv.hash()->find("file");
            auto ln = inv.hash()->find("line");
            return Value::str((fl != inv.hash()->end() ? fl->second.toStr() : "") + " at line " +
                              (ln != inv.hash()->end() ? ln->second.toStr() : "0"));
        }
    }
    // $?DISTRIBUTION — the compiling module's distribution. `.meta` is the parsed
    // META6.json (zef reads <version>/<ver>/<api>/<auth> from it), `.prefix` the
    // checkout root; `.content` reads a file listed in the meta.
    if (inv.t == VT::Hash && inv.hashKind == "Distribution" && inv.hash()) {
        if (m == "meta") { auto it = inv.hash()->find("meta"); return it != inv.hash()->end() ? it->second : Value::makeHash(); }
        if (m == "prefix") { auto it = inv.hash()->find("prefix"); return it != inv.hash()->end() ? it->second : Value::any(); }
        if (m == "name" || m == "Str" || m == "gist") {
            auto it = inv.hash()->find("meta");
            if (it != inv.hash()->end() && it->second.hash()) {
                auto n = it->second.hash()->find("name");
                if (n != it->second.hash()->end()) return Value::str(n->second.toStr());
            }
            return Value::str("Distribution");
        }
    }
    // `Match.new(:orig(…), :from(…), :pos(…), :list(…), :hash(…))` — the inverse of
    // Match.raku, so a match round-trips through EVAL (S05-match/raku.t does exactly
    // that). Every part is optional; what is absent is simply empty.
    // The Pod::* classes are SYNTHESISED — a hash carrying a `podclass` — so
    // they had no `.new` candidate at all and every program that BUILDS pod
    // rather than reading it died on the first constructor. Pod::Utils' whole
    // Build module is such constructors (`Pod::Block::Para.new(:@contents)`,
    // `Pod::FormattingCode.new(type => 'L', …)`). Each named argument is an
    // attribute — contents, name, level, type, meta, config — and the class
    // name is the podclass.
    if (inv.t == VT::Type && m == "new" && inv.s.rfind("Pod::", 0) == 0) {
        Value v = Value::makeHash(); v.hashKind = "Pod";
        (*v.hash())["podclass"] = Value::str(inv.s);
        (*v.hash())["contents"] = Value::array();
        for (auto& a : args)
            if (a.t == VT::Pair && a.pairVal()) (*v.hash())[a.s] = *a.pairVal();
        // A FormattingCode always carries a `meta`, empty when the code has no
        // `|` half — which is what the PARSER produces, so a constructed one
        // without it is not `is-deeply` the same block.
        if (inv.s == "Pod::FormattingCode" && !v.hash()->count("meta"))
            (*v.hash())["meta"] = Value::array();
        return v;
    }
    if (inv.t == VT::Type && inv.s == "Match" && m == "new") {
        std::string orig; long long from = 0, pos = 0;
        Value list, hash;
        for (auto& a : args) {
            if (a.t != VT::Pair || !a.pairVal()) continue;
            if (a.s == "orig") orig = a.pairVal()->toStr();
            else if (a.s == "from") from = a.pairVal()->toInt();
            else if (a.s == "pos" || a.s == "to") pos = a.pairVal()->toInt();
            else if (a.s == "list") list = *a.pairVal();
            else if (a.s == "hash") hash = *a.pairVal();
        }
        if (from < 0) from = 0;
        if (pos < from) pos = from;
        std::string text = (size_t)from <= orig.size()
                         ? orig.substr((size_t)from, (size_t)(pos - from)) : std::string();
        Value mv = Value::matchVal(text, (long)from, (long)pos);
        mv.extM() = std::make_shared<std::string>(orig);
        if (list.t == VT::Array && list.arr()) *mv.arr() = *list.arr();
        if (hash.t == VT::Hash && hash.hash()) *mv.hash() = *hash.hash();
        return mv;
    }
    // `Backtrace.new` — the call chain at the point of call, the same
    // innermost-first BacktraceFrame list `Exception.backtrace` answers. The
    // optional Int drops that many innermost frames (Rakudo's $offset), which
    // is how a routine reports its CALLER's position rather than its own.
    // A program that declares its OWN `class Backtrace` keeps it: the built-in
    // stands aside whenever the name is in classes_.
    if (inv.t == VT::Type && inv.s == "Backtrace" && m == "new" && !classes_.count("Backtrace")) {
        Value bt = captureBacktrace();
        long long off = 0;
        for (auto& a : args) if (a.t == VT::Int) { off = a.toInt(); break; }
        if (off > 0 && bt.arr()) {
            auto& fr = *bt.arr();
            fr.erase(fr.begin(), fr.begin() + std::min((size_t)off, fr.size()));
        }
        return bt;
    }
    // `Deprecation.report` — the deprecated calls seen so far, as Rakudo words
    // them, and forgotten once reported (Nil when there are none)
    if (inv.t == VT::Type && inv.s == "Deprecation" && m == "report") return deprecationReport();
    if (inv.t == VT::Type && inv.s == "Collation" && m == "new") return makeCollation();
    // a Collation's levels: read them, `.set` them (in place, answering itself),
    // and the gist Rakudo prints, whose `collation-level` packs level i as bit
    // 2i when it is on and bit 2i+1 when it is reversed
    if (inv.t == VT::Hash && inv.hashKind == "Collation" && inv.hash()) {
        static const char* kLv[4] = {"primary", "secondary", "tertiary", "quaternary"};
        for (const char* k : kLv) if (m == k) return (*inv.hash())[k];
        if (m == "set") {
            for (auto& a : args)
                if (a.t == VT::Pair && a.pairVal())
                    for (const char* k : kLv)
                        if (a.s == k) {
                            const Value& pv = *a.pairVal();
                            (*inv.hash())[k] = Value::integer(pv.t == VT::Bool ? (pv.b ? 1 : 0) : pv.toInt());
                        }
            return inv;
        }
        if (m == "collation-level") {
            long long bits = 0;
            for (int i = 0; i < 4; i++) {
                long long v = (*inv.hash())[kLv[i]].toInt();
                if (v > 0) bits |= 1LL << (2 * i);
                else if (v < 0) bits |= 1LL << (2 * i + 1);
            }
            return Value::integer(bits);
        }
        if (m == "Country") return Value::str("International");
        if (m == "Language") return Value::str("None");
        if (m == "gist" || m == "Str" || m == "raku") {
            std::string g = "collation-level => " + methodCall(inv, "collation-level", ValueList{}).toStr() +
                            ", Country => International, Language => None";
            for (const char* k : kLv) g += std::string(", ") + k + " => " + (*inv.hash())[k].toStr();
            return Value::str(g);
        }
    }
    if (inv.t == VT::Type && inv.s == "Failure" && m == "new") {
        // Failure.new (no args) picks up the current $! as its exception.
        Value ex; bool haveEx = false; std::string msg;
        for (auto& a : args) if (a.t == VT::Object) { ex = a; haveEx = true; } // Failure.new($ex) / :exception
        // `Failure.new("oh noes!")` — a plain STRING is the message, wrapped in an
        // X::AdHoc exactly as Rakudo does. Ignored, the Failure had nothing to say
        // and its gist came back "(Any)".
        if (!haveEx)
            for (auto& a : args)
                if (a.t == VT::Str && !a.namedArg) { msg = a.toStr(); haveEx = true;
                    ex = makeTypedEx("X::AdHoc", {{"message", Value::str(msg)}}, msg); break; }
        if (!haveEx) { Value* be = tctx_.cur->find("$!"); if (be && be->t != VT::Nil && be->t != VT::Type && be->t != VT::Any) { ex = *be; haveEx = true; } }
        // …and with no `$!` either it still carries an exception: "Failed"
        if (!haveEx) { msg = "Failed"; ex = makeTypedEx("X::AdHoc", {{"message", Value::str(msg)}}, msg); }
        Value f = rakuppNewFailure();
        (*f.hash())["exception"] = ex;
        if (!msg.empty()) (*f.hash())["message"] = Value::str(msg);
        return f;
    }
    if (inv.t == VT::Type && inv.s == "Proxy" && m == "new") {
        // Proxy.new(:FETCH(method(){…}), :STORE(method($v){…})) — a container whose
        // reads call FETCH and whose writes call STORE (see VarExpr eval / evalAssign).
        Value p = Value::makeHash(); p.hashKind = "Proxy";
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal())
            { if (a.s == "FETCH" || a.s == "STORE") (*p.hash())[a.s] = *a.pairVal(); }
        return p;
    }
    // `IO::Handle.new` — an UNOPENED handle. Rakudo gives one for `.new` with no
    // arguments and for `.new(:$path)`; every read or write on it then fails the
    // way an unopened handle should. IO::MiddleMan's error suite opens with
    // `my $fh = IO::Handle.new`, and there was no constructor at all.
    if (inv.t == VT::Type && inv.s == "IO::Handle" && m == "new") {
        Value h = Value::makeHash(); h.hashKind = "FileHandle";
        (*h.hash())["mode"] = Value::str("r");
        (*h.hash())["buffer"] = Value::str("");
        // constructed, never opened: `.opened` is False and the gist says
        // (closed) from the start — `.open` on it is what makes it live.
        (*h.hash())["closed"] = Value::boolean(true);
        bool bin = false; const Value* enc = nullptr;
        for (auto& a : args) {
            if (!(a.t == VT::Pair && a.pairVal())) continue;
            if (a.s == "path" && a.pairVal()->t != VT::Any)
                (*h.hash())["path"] = Value::str(a.pairVal()->toStr());
            else if (a.s == "bin") bin = a.pairVal()->truthy();
            else if ((a.s == "encoding" || a.s == "enc") && a.pairVal()->t != VT::Any)
                enc = a.pairVal();
        }
        // the two are contradictory: bytes have no encoding to be read in
        if (bin && enc)
            throw RakuError{Value::typeObj("X::IO::BinaryAndEncoding"),
                "Cannot open a handle in binary mode with an encoding"};
        if (bin) (*h.hash())["bin"] = Value::boolean(true);
        else (*h.hash())["encoding"] = Value::str(enc ? enc->toStr() : "utf8");
        // The constructed handle carries the attribute's own default, which is
        // a LIST — `$("\n", "\r\n")`. An opened handle answers the same two
        // separators as an Array; the two spellings are what each shows.
        Value nl = Value::array(); nl.isList = true; nl.itemized = true;
        nl.arr()->push_back(Value::str("\n"));
        nl.arr()->push_back(Value::str("\r\n"));
        (*h.hash())["nl-in"] = nl;
        return h;
    }
    if (inv.t == VT::Type && m == "new") {
        const std::string& t = inv.s;
        // `Int.new(5)` / `Str.new(value => 'x')` — the constructors Rakudo gives
        // these two. They were reached with the arguments already in hand and
        // answered 0 / "" for every one of them, which is what a `class Int64 is
        // Int` inherits when it constructs.
        const Value* given = nullptr;
        for (auto& a : args)
            if (a.t == VT::Pair) { if (a.s == "value" && a.pairVal()) given = a.pairVal(); }
            else if (!given) given = &a;
        if (t == "Str" || t == "Cool") return Value::str(given ? given->toStr() : "");
        if (t == "Int") {
            // `Int.new($x)` is `$x.Int`, so it takes anything that has one —
            // a Str parses, a Rat or Num truncates, a Range counts. What it
            // does NOT take is a TYPE OBJECT, which has no value to convert:
            // Rakudo dies "Cannot create an Int from a 'Str' type object"
            // where this answered 0 for every refusal it should have made
            // (Int-Num-Rat sheet N-06; S32-num/int.t).
            if (!given) return Value::integer(0);
            if (given->t == VT::Type || given->t == VT::Any || given->t == VT::Nil)
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Cannot create an Int from a '" +
                    (given->t == VT::Type ? given->s.str() : given->typeName()) +
                    "' type object"};
            if (given->t == VT::Str && !given->isAllomorph())
                return numifyStrOrThrow(given->s.str());   // `Int.new("abc")` is X::Str::Numeric
            if (given->t == VT::Num && !std::isfinite(given->n))
                return armedFailure("X::Numeric::CannotConvert",
                    "Cannot convert " + given->toStr() + " to Int");
            // a Range, a List or an Array converts through its own .Int
            if (given->t == VT::Range || given->t == VT::Array)
                return methodCall(*given, "Int", ValueList{});
            return Value::integer(given->toInt());
        }
        if (t == "Real" || t == "Numeric") return Value::number(0.0); // (`Num.new` has its own arm above)
        if (t == "Bool") return Value::boolean(false);
        // `Mu.new` / `Any.new` — an instance of the bare root type: defined (so
        // truthy), gisting as `Mu.new`. It carries no attributes of its own.
        if (t == "Mu" || t == "Any") {
            Value v = Value::makeHash(); v.hashKind = t; return v;
        }
        // `Compiler.new` — the same object `$*RAKU.compiler` answers. META::
        // constants builds one in its EXPORT sub to read the compiler's name and
        // version, and got X::Method::NotFound for a type that already exists.
        if (t == "Compiler") return rakuIntrospection(true);
        // ANY exception type is constructible, not just the handful the
        // interpreter registers by hand. Rakudo ships every X:: name as a real
        // class, so `X::Hash::Store::OddNumber.new(...)` is ordinary code there
        // — and library code writes it: Hash::Agnostic's STORE constructs
        // exactly that one to report an odd element count, so building a
        // Hash::Ordered died with "No such method 'new'" instead of storing.
        // The name is registered on first use with whatever named arguments it
        // was given (plus `message`), and `Exception` as the native parent so
        // the generated ancestry still answers `.^mro` and `~~ Exception`.
        // A standard X:: exception built by hand carries its message in its
        // ATTRIBUTES, not in a `message` argument: Rakudo gives each of these
        // classes a `method message` that renders the attributes it was given.
        // Without that, `X::OutOfRange.new(what => 'Year', got => 0, range =>
        // '1..Inf')` answered an undefined `.message` and an empty `.gist` —
        // and four of the Date::Calendar distributions throw exactly that one
        // to report a bad month or day, so their errors arrived blank. The
        // message is synthesised HERE, before construction, so it lands in the
        // ordinary `message` attribute and `.message`, `.Str`, `.gist` and a
        // `.throw`n message all read the same string. An explicit `message`
        // argument still wins, and this runs whether or not the class has been
        // registered already.
        if (t.rfind("X::", 0) == 0) {
            std::map<std::string, std::string> at;
            for (auto& a : args)
                if (a.t == VT::Pair && a.pairVal() && rtIsDefined(*a.pairVal()))
                    at[a.s] = a.pairVal()->toStr();
            std::string syn;
            if (t == "X::OutOfRange" && at.count("what")) {
                syn = at["what"] + " out of range. Is: " +
                      (at.count("got") ? at["got"] : std::string()) +
                      ", should be in " + (at.count("range") ? at["range"] : std::string());
                if (at.count("comment")) syn += "; " + at["comment"];
            }
            else if (t == "X::NYI" && at.count("feature"))
                syn = at["feature"] + " not yet implemented. Sorry.";
            // The rendered message WINS over an explicit `message` argument, as
            // Rakudo's `method message` does — it is a method there, so an
            // attribute of the same name never gets a say.
            if (!syn.empty()) {
                for (size_t i = 0; i < args.size(); )
                    if (args[i].t == VT::Pair && args[i].s == "message") args.erase(args.begin() + i);
                    else ++i;
                args.push_back(Value::pair("message", Value::str(syn)));
            }
        }
        if ((t.rfind("X::", 0) == 0 || t.rfind("CX::", 0) == 0) && !classes_.count(t)) {
            auto ci = std::make_shared<ClassInfo>();
            ci->name = t;
            ci->nativeParent = "Exception";
            std::set<std::string> seen;
            for (auto& a : args)
                if (a.t == VT::Pair && seen.insert(a.s).second) {
                    ClassAttr ca; ca.name = a.s; ca.sigil = '$'; ca.pub = true;
                    ci->attrs.push_back(ca);
                }
            if (!seen.count("message")) {
                ClassAttr ca; ca.name = "message"; ca.sigil = '$'; ca.pub = true;
                ci->attrs.push_back(ca);
            }
            classes_[t] = ci;
            // fall through to the ordinary construction path below, which now
            // finds the class and binds the named arguments to its attributes
        }
        // …and `VM.new`, the same object `$*VM` answers, for the same reason:
        // META::constants reads both in its EXPORT sub.
        if (t == "VM") { Value h = Value::makeHash(); h.hashKind = "VM"; (*h.hash())["name"] = Value::str("cpp"); return h; }
    }
    // `List.from-iterator($it)` — and the Array/Seq/Slip spellings. Drains the
    // iterator into the named container, whether it is a user object doing
    // Iterator or one of ours: both answer `pull-one` until IterationEnd. The
    // Agnostic role family builds `.list`/`.List`/`.Array`/`.Slip` on this one
    // method, so without it a Hash::Agnostic class had none of the four.
    if (m == "from-iterator" && args.size() == 1 &&
        (inv.s == "List" || inv.s == "Array" || inv.s == "Seq" || inv.s == "Slip")) {
        Value out = Value::array();
        out.isList = inv.s != "Array";
        // …and the container KEEPS the type it was asked for. A `Slip` came back
        // as a plain List, so `is-deeply @a.Slip, (1,2,3).Slip` failed with both
        // sides rendering identically — Array::Agnostic builds its `.Slip` as
        // `Slip.from-iterator(self.iterator)`, and List::Agnostic and
        // Array::Sparse inherit the same assertion.
        if (inv.s == "Seq" || inv.s == "Slip") out.s = inv.s;
        for (;;) {
            ValueList none;
            Value x = methodCall(args[0], "pull-one", none);
            if (x.t == VT::Type && x.s == "IterationEnd") break;
            if (x.t == VT::Any || x.t == VT::Nil) break; // a drained builtin iterator
            out.arr()->push_back(std::move(x));
        }
        return out;
    }
    if (inv.t == VT::Type && (inv.s == "List" || inv.s == "Array" || inv.s == "Seq" || inv.s == "array") && m == "new") {
        if (inv.s == "array" && inv.ofType().empty()) // native arrays need a type parameter
            throw RakuError{Value::typeObj("X::MustBeParametric"),
                            "Must first parameterize the vector type, e.g.: array[int32]"};
        // Seq.new(one of OUR iterators) — `Seq.new(Rakudo::Iterator.OneValue($path))`
        // is paths' answer for a lone file — drains it by pull-one as well; it
        // used to be read as a plain hash of its `items`/`pos` slots
        if (inv.s == "Seq" && args.size() == 1 && args[0].t == VT::Hash &&
            args[0].hashKind == "Iterator" && args[0].hash()) {
            Value v = Value::array(); v.isList = true; v.s = "Seq";
            for (;;) {
                ValueList none;
                Value x = methodCall(args[0], "pull-one", none);
                if (x.t == VT::Type && x.s == "IterationEnd") break;
                v.arr()->push_back(std::move(x));
            }
            return v;
        }
        // Seq.new(iterator-object): a user object doing Iterator drains by pull-one
        if (inv.s == "Seq" && args.size() == 1 && args[0].t == VT::Object && args[0].obj() &&
            args[0].obj()->cls && args[0].obj()->cls->findMethod("pull-one")) {
            Value* po = args[0].obj()->cls->findMethod("pull-one");
            Value v = Value::array(); v.isList = true; v.s = "Seq";
            for (;;) {
                ValueList none;
                Value x = invokeMethod(*po, args[0], none);
                if (x.t == VT::Type && x.s == "IterationEnd") break;
                v.arr()->push_back(x);
            }
            return v;
        }
        Value v = Value::array(); v.isList = (inv.s == "List" || inv.s == "Seq"); v.ofTypeM() = inv.ofType();
        std::vector<long long> dims;
        ValueList seed;
        bool lazySeed = false; Value lazyFrom;   // `Array.new(1..*)` stays lazy
        size_t posCount = 0;   // positional arguments — `:shape` is not one
        for (auto& a : args)
            if (!(a.t == VT::Pair && a.s == "shape")) posCount++;
        for (auto& a : args) {
            if (a.t == VT::Pair && a.s == "shape") {
                // Only a definite integer (or a list of them) is a shape. A
                // Range is refused outright, and a TYPE OBJECT warns and is
                // ignored — the array comes back unshaped (sheet LA-20).
                if (a.pairVal() && a.pairVal()->t == VT::Range)
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        "Setting a shape with a Range not allowed: " + a.pairVal()->gist()};
                if (a.pairVal() && a.pairVal()->t == VT::Type) {
                    { const std::string wm = "Useless use of :shape(" + a.pairVal()->gist() + ")";
                      if (!runControlWarn(wm)) std::cerr << wm << "\n"; }
                    continue;
                }
                if (a.pairVal()) for (auto& d : a.pairVal()->flatten()) dims.push_back(d.toInt());
                continue;
            }
            // `List.new` takes its arguments AS ELEMENTS: `List.new([1,2])` is a
            // ONE-element list holding the Array and `List.new({:a(1)})` one
            // holding the Hash. Only `Array.new` flattens — Rakudo agrees there,
            // `Array.new([1,2])` has two. Crane's list leaf is
            // `List.new({:path(…), :value(…)})`, which came back as two loose
            // Pairs; the caller then sorted a flat pair soup and every path in
            // the answer was separated from its value (issue #69).
            if (inv.s == "List") seed.push_back(a);
            // `Array.new` follows the SINGLE-ARGUMENT RULE, not a blanket
            // flatten: one Iterable argument is the list of elements, several
            // are each one element — `Array.new((1,2),(3,4))` has two, and an
            // ITEMIZED one is a single element whatever its company (LA-04).
            else if (a.itemized || posCount > 1 ||
                     (a.t == VT::Pair && a.namedArg)) seed.push_back(a);
            // …and an ENDLESS source is not walked at all: `Array.new(1..*)`
            // is a LAZY array, and toList would try to reify it (sheet LA-04).
            else if (a.t == VT::Range && !a.rNum() && a.rTo() >= 9223372036854775807LL) {
                Value lz = a; lazySeed = true; lazyFrom = lz;
            }
            else for (auto& x : toList(a)) seed.push_back(x);
        }
        // A PARAMETERIZED constructor type-checks what it is given, exactly as
        // an assignment into the container would: `Array[Int].new("a")` is
        // X::TypeCheck::Assignment, not an Array holding a Str (sheet LA-21).
        if (!v.ofType().empty() && v.ofType() != "Mu" && v.ofType() != "Any" &&
            v.ofType().find(',') == std::string::npos &&
            !ascii::islower((unsigned char)v.ofType()[0]))
            for (auto& x : seed) {
                if (x.t == VT::Nil || typeOrSubsetMatches(x, v.ofType())) continue;
                throwTypedV("X::TypeCheck::Assignment",
                    {{"got", x}, {"expected", Value::typeObj(v.ofType())}},
                    "Type check failed in assignment; expected " + v.ofType() +
                        " but got " + x.typeName() + " (" + typeCheckRepr(x) + ")");
            }
        if (!dims.empty()) { // shaped array — pre-sized, row-major, tagged with .shape()
            std::string et = v.ofType() == "Any" || v.ofType() == "Mu" ? "" : v.ofType();
            Value s = makeShapedContainer(dims, et, seed.empty() ? nullptr : &seed);
            s.isList = v.isList;
            return s;
        }
        if (lazySeed) {
            ValueList none;
            Value lz = methodCall(lazyFrom, "list", none);
            lz.isList = v.isList;   // Array.new keeps the Array face
            return lz;
        }
        *v.arr() = seed;
        return v;
    }
    if (inv.t == VT::Type && (inv.s == "Hash" || inv.s == "Map") && m == "Map")
        return Value::typeObj("Map"); // Hash.Map on the type object is the Map type
    if (inv.t == VT::Type && (inv.s == "Hash" || inv.s == "Map") && m == "new") {
        Value v = Value::makeHash(); v.ofTypeM() = inv.ofType();
        // `Hash[V,K]` is an OBJECT hash however it is built, so `.new` has to key
        // by IDENTITY like the `my %h{K}` declarator does — the Int 13 and the Str
        // "13" are two keys, and `.keys` gives back the Int. Stringifying here made
        // `"13" (elem) Hash[Any,Any].new(13 => "x")` True.
        if (inv.ofType().find(',') != std::string::npos) v.objKeyed = true;
        auto put = [&](const Value& key, const Value& val) {
            if (!v.objKeyed) { (*v.hash())[key.toStr()] = val; return; }
            Value stored = val;
            stored.pairKeyM() = std::make_shared<Value>(key);
            (*v.hash())[objHashIndex(key)] = std::move(stored);
        };
        if (inv.s == "Map") v.hashKind = "Map"; // a Map is a distinct (immutable) type
        // The arguments FLATTEN before they are paired up, and they flatten all the
        // way down — `Hash.new((("a","1"),("b","2")))` is {a => 1, b => 2}, same as
        // the flat form. Spreading only ONE level left each inner list whole, so the
        // first became a stringified key and the second its value:
        // `{"a 1" => $("b","2")}`. That is what scrambled a Hash built from a zip
        // (`Hash.new: @keys Z @values`) in rakupp#12.
        //
        // An ITEMIZED list does not flatten — `Hash.new(($("a","1"),$("b","2")))`
        // really is `{"a 1" => $("b","2")}` in Rakudo — and a Pair is not a list, so
        // neither is descended into.
        ValueList items;
        std::function<void(const Value&)> spread = [&](const Value& x) {
            if ((x.t == VT::Array || x.t == VT::Range) && !x.itemized && x.t != VT::Pair) {
                for (auto& e : toList(x)) spread(e);
            } else items.push_back(x);
        };
        for (auto& a : args) spread(a);
        for (size_t k = 0; k < items.size(); k++) {
            // a HASH argument contributes its own pairs (`Hash.new({ :$curi, :@dists })`
            // and `Hash.new(%other)` — zef's list-installed builds its rows that way);
            // it used to be paired up as a KEY, so the hash came back empty
            if (items[k].t == VT::Hash && items[k].hash() && !items[k].itemized) {
                for (auto& kv : *items[k].hash()) (*v.hash())[kv.first] = kv.second;
                continue;
            }
            // …and so does an OBJECT doing Associative: `Hash.new(self)` is how the
            // Hash::Agnostic family builds its `.Hash`, and the object was paired up
            // as a KEY, so the hash came back empty.
            if (items[k].t == VT::Object && items[k].obj() && items[k].obj()->cls &&
                items[k].obj()->cls->findMethod("pairs") &&
                (items[k].obj()->cls->findMethod("AT-KEY") || items[k].obj()->cls->doesRole("Associative"))) {
                for (auto& p : toList(methodCall(items[k], "pairs", {})))
                    if (p.t == VT::Pair) {
                        Value pv = p.pairVal() ? *p.pairVal() : Value::any();
                        if (pv.t == VT::Hash && pv.hashKind == "Proxy" && pv.hash()) pv = deproxy(pv);
                        (*v.hash())[p.s] = pv;
                    }
                continue;
            }
            if (items[k].t == VT::Pair)
                put(items[k].pairKey() ? *items[k].pairKey() : Value::str(items[k].s),
                    items[k].pairVal() ? *items[k].pairVal() : Value::any());
            else if (k + 1 < items.size()) { put(items[k], items[k + 1]); k++; }
            else throwHashOddNumber((long long)items.size(), items[k]); // (it dropped the key)
        }
        return v;
    }
    // …and `.CREATE` builds the same empty buffer: the generic CREATE below
    // allocates a bare user object, which has no `.push` — Backtrace::Files
    // gathers its context lines into `IterationBuffer.CREATE`.
    if (inv.t == VT::Type && inv.s == "IterationBuffer" && (m == "new" || m == "CREATE")) {
        Value v = Value::makeHash(); v.hashKind = "IterationBuffer";
        Value items = Value::array();
        for (auto& a : args) for (auto& x : toList(a)) items.arr()->push_back(x);
        (*v.hash())["items"] = items;
        return v;
    }
    if (inv.t == VT::Type) {
        if (inv.s.rfind("IO::Spec", 0) == 0) { Value r; if (ioSpecMethod(*this, inv.s, m, args, r)) return r; }
        // `.CREATE` is the low-level allocator: an instance whose attributes hold
        // their empty CONTAINERS, with no BUILD/TWEAK and no initializer run.
        // Delegating to .new/.bless would pass the one documented example
        // (`Mu.CREATE.defined` is True) while being semantically wrong, so it
        // builds the ObjectData directly.
        //
        // The container is what matters, and it was missing: every attribute got
        // Any regardless of sigil, so `has @!array` on a CREATE'd object was not
        // an Array and `has int $!elems` was not 0. BinaryHeap allocates exactly
        // this way (`multi method new(--> BinaryHeap:D) { self.CREATE }`), so
        // every heap it handed out was unusable — which is what left Graph's
        // Dijkstra and A* walking an unordered queue. rtTypedDefault is the same
        // rule the `my` declarators use: `@` → [], `%` → {}, native → 0/"",
        // `Int` → the type object, untyped `$` → Any.
        if (m == "CREATE") {
            auto od = makePayload<ObjectData>();
            auto it = classes_.find(resolveClassAlias(inv.s));
            if (it != classes_.end()) {
                od->cls = it->second;
                for (auto* c = it->second.get(); c; c = c->parent.get())
                    for (auto& at : c->attrs)
                        if (!od->attrs.count(at.name))
                            od->attrs[at.name] = rtTypedDefault(at.type.c_str(), at.sigil);
            }
            return Value::object(od);
        }
        // .^attributes on the built-in Date/DateTime: the introspection
        // JSON::Unmarshal walks to rebuild a value from its parts (its tests
        // mirror Rakudo's internals, daycount and formatter included). Names,
        // order and flags match Rakudo's; every type is Any — the rebuild
        // treats the parts opaquely and .new ignores unknown nameds exactly
        // as Rakudo's does. Other built-ins keep answering the empty list.
        if (m == "attributes" && !classes_.count(inv.s) &&
            (inv.s == "DateTime" || inv.s == "Date")) {
            static const char* dtA[] = {"$!hour", "$!minute", "$!second", "$!timezone",
                                        "$!year", "$!month", "$!day", "$!daycount", "&!formatter"};
            static const char* dA[]  = {"$!year", "$!month", "$!day", "$!daycount", "&!formatter"};
            Value out = Value::array(); out.isList = true;
            auto one = [&](const char* nm) {
                Value at = Value::makeHash(); at.hashKind = "Attribute";
                (*at.hash())["name"] = Value::str(nm);
                (*at.hash())["type"] = Value::typeObj("Any");
                (*at.hash())["readonly"] = Value::boolean(true);
                (*at.hash())["has_accessor"] = Value::boolean(true);
                (*at.hash())["is_built"] = Value::boolean(true);
                (*at.hash())["package"] = Value::typeObj(inv.s);
                out.arr()->push_back(at);
            };
            if (inv.s == "DateTime") for (auto* n : dtA) one(n);
            else                     for (auto* n : dA)  one(n);
            return out;
        }
        // `.^pun` — a role's punned class. Roles are types here already, so the
        // pun is the type itself (`nqp::create(buf8.^pun)` is how path-utils
        // allocates its sniffing buffer).
        if (m == "pun") return inv;
        // `Pair.ACCEPTS($x)` / `Regex.ACCEPTS($needle)` — a built-in TYPE
        // OBJECT accepts by type check, exactly what `$x ~~ Type` asks
        // (highlighter sorts its needles this way). A user class's own
        // ACCEPTS was dispatched before this point.
        // …and so does a user ROLE or class with no ACCEPTS of its own: highlighter
        // asks `Type.ACCEPTS($needle)` of its role to see whether the needle was
        // tagged with it (Rakudo checks the type there too, never the role's method).
        if (m == "ACCEPTS" && !args.empty()) {
            auto uc = classes_.find(inv.s);
            if (uc == classes_.end() || !uc->second || uc->second->isRole || !uc->second->findMethod("ACCEPTS"))
                return smartmatchValue("~~", args[0], inv);
        }
        // .^mro / .mro on a built-in type → the class-only linearisation (roles like
        // Real/Numeric are excluded, matching Rakudo's Int.^mro == (Int Cool Any Mu)).
        if (m == "mro" && !classes_.count(inv.s)) {
            // `.^mro(:roles)` asks for the linearisation WITH the roles in it —
            // `Int.^mro(:roles)` is (Int Real Numeric Cool Any Mu). typeAncestry
            // already carries them; the plain form is that list with the roles
            // filtered out, so the adverb simply stops filtering. `are` walks
            // this list to find the common type of a list of values, and without
            // the roles it answered Cool where Real was due.
            bool withRoles = false;
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "roles" && (!a.pairVal() || a.pairVal()->truthy()))
                    withRoles = true;
            Value out = Value::array(); out.isList = true;
            for (auto& a : typeAncestry(inv.s)) if (withRoles || !isBuiltinRole(a)) out.arr()->push_back(Value::typeObj(a));
            if (out.arr()->empty()) { out.arr()->push_back(Value::typeObj(inv.s)); out.arr()->push_back(Value::typeObj("Any")); out.arr()->push_back(Value::typeObj("Mu")); }
            return out;
        }
        if (m == "parents" && !classes_.count(inv.s)) { // built-in type: () by default, full chain with :all
            Value out = Value::array(); out.isList = true;
            // …but a RakuAST:: class answers its whole chain unadorned, the way
            // Rakudo does — `RakuAST::IntLiteral.^parents` is ten names, and the
            // `.^mro` arm above is that list with the class in front. A class
            // with no ancestors (RakuAST::Node, the punned roots) answers the
            // empty list there too, so nothing special is needed for it.
            if (isRakuAstName(inv.s)) {
                const auto& anc = rakuAstAncestry(inv.s);
                for (size_t i = 1; i + 2 < anc.size(); i++)   // minus self, minus Any/Mu
                    out.arr()->push_back(Value::typeObj(anc[i]));
                return out;
            }
            bool all = false;
            for (auto& a : args) if (a.t == VT::Pair && a.s == "all" && (!a.pairVal() || a.pairVal()->truthy())) all = true;
            if (all) { bool self = true;
                for (auto& a : typeAncestry(inv.s)) { if (self) { self = false; continue; } if (!isBuiltinRole(a)) out.arr()->push_back(Value::typeObj(a)); } }
            return out;
        }
        // The whole MOP-mutator surface below is gated on the invocant being a
        // user-declared type, so `Int.^add_method` died. The storage and the reader
        // already exist: builtinExt_ is what `augment class Int {…}` writes to and
        // what method dispatch consults for every non-Object invocant. Only the
        // writer was missing. Kept to CORE types — isKnownTypeName blanket-accepts
        // any X::/Metamodel::/IO:: prefix, which would let a typo mint a method.
        if (!classes_.count(inv.s) && isCoreTypeName(inv.s)) {
            if (m == "add_method" && args.size() >= 2) {
                noteSymbolMutation("runtime .^add_method on a built-in type");
                builtinExt_[inv.s][args[0].toStr()] = args[1];
                return args[1];
            }
            if (m == "compose" || m == "publish_method_cache" || m == "invalidate_method_caches")
                return inv;
        }
        // `.^language-revision` — the revision the type was declared under
        if (m == "language-revision" && args.empty()) {
            auto cit = classes_.find(inv.s);
            int r = cit != classes_.end() && cit->second && cit->second->langRev >= 0 ? cit->second->langRev : langRev_;
            return Value::str(r == 0 ? "c" : r == 1 ? "d" : "e");
        }
        // `.^ver` / `.^auth` / `.^api` on a PACKAGE (`module Zef:ver(…):auth(…)`) —
        // a package has no ClassInfo, so its adverbs come from pkgMeta_. Everything
        // else answers Rakudo's defaults for a type that declares none: "" for auth,
        // the language version for ver. zef's plugin loader reads all three.
        if (m == "ver" || m == "auth" || m == "api") {
            // A subset answers with the language revision it was declared under
            // — `subset Even of Int …; Even.^ver` is 6.d in a 6.d unit and 6.e
            // in a 6.e one. Types that declare their own :ver fall through.
            if (m == "ver") {
                auto sit = subsets_.find(inv.s);
                if (sit != subsets_.end())
                    // A plain Str, as Rakudo's is: `E.^ver.^name` is Str there
                    // and `E.^ver ~~ Version` is False. Tagging it Version would
                    // print v6.d and answer that type check the other way.
                    return Value::str(sit->second.langRev == 0 ? "6.c"
                                    : sit->second.langRev == 1 ? "6.d" : "6.e");
            }
            auto pit = pkgMeta_.find(inv.s);
            std::string v = pit == pkgMeta_.end() ? std::string()
                          : (m == "ver" ? pit->second.ver : m == "auth" ? pit->second.auth : pit->second.api);
            if (!classes_.count(inv.s) || pit != pkgMeta_.end()) {
                if (m == "auth") return Value::str(v);
                if (v.empty() && m == "ver") { Value ver = Value::str("6.c"); ver.hashKind = "Version"; return ver; }
                if (v.empty()) return Value::any();
                if (m == "ver") { if (v[0] == 'v') v.erase(0, 1); Value ver = Value::str(v); ver.hashKind = "Version"; return ver; }
                return Value::str(v);
            }
        }
        auto cit = classes_.find(inv.s);
        // …and, only once `classes_` has missed, the RakuAST:: registry: its
        // ClassInfos are ordinary classes, so `.new`, `.^mro`, `.^parents` and
        // method dispatch all come out of this same arm rather than needing a
        // parallel one. (A user's own `class RakuAST::Mine` was found above.)
        // (A POINTER into the registry, not a shared_ptr local: this arm is on
        // the ordinary method-call path, and a local shared_ptr here — built and
        // torn down whether or not it is ever used — measured as ~4% on object
        // construction.)
        const std::shared_ptr<ClassInfo>* astCi =
            cit == classes_.end() && isRakuAstName(inv.s) ? rakuAstClass(inv.s) : nullptr;
        if (cit != classes_.end() || astCi) {
            auto ci = cit != classes_.end() ? cit->second : *astCi;
            // A user-declared META-METHOD — `method ^parameterize(Mu:U \obj, **@pos)`
            // — answers the `.^name(…)` call on its type. Rakudo hands the type in
            // as the first positional (the invocant is the HOW), which is the
            // signature every such method is written to (Parameterizable).
            if (Value* umm = ci->findMethod("^" + m.s)) {
                ValueList a2; a2.reserve(args.size() + 1);
                a2.push_back(inv);
                for (auto& x : args) a2.push_back(x);
                return invokeMethod(*umm, inv, a2, nullptr);
            }
            // grammar entry points
            if ((m == "parse" || m == "subparse" || m == "parsefile") && (ci->isGrammar || ci->findRule("TOP"))) {
                bool sub = (m == "subparse");
                // the built-in parse behavior (also the `next` candidate for a user override)
                auto builtinParse = [this, ci, sub](ValueList a) -> Value {
                    std::string startRule = "TOP"; Value actions;
                    ValueList ruleArgs; // `args => (…)` — the start rule's parameters
                    for (auto& arg : a) {
                        if (arg.t == VT::Pair && arg.s == "rule") startRule = arg.pairVal() ? arg.pairVal()->toStr() : "TOP";
                        if (arg.t == VT::Pair && arg.s == "actions" && arg.pairVal()) actions = *arg.pairVal();
                        if (arg.t == VT::Pair && arg.s == "args" && arg.pairVal()) {
                            const Value& av = *arg.pairVal();
                            if (av.t == VT::Array && av.arr()) ruleArgs = *av.arr();
                            else if (av.t != VT::Any) ruleArgs.push_back(av);
                        }
                    }
                    // an undefined parse target dies (Rakudo: warns on Any-to-Str
                    // coercion, then dies calling .chars on it) instead of
                    // silently parsing "" and returning Nil
                    if (!a.empty() && (a[0].t == VT::Any || a[0].t == VT::Nil))
                        throw RakuError{Value::typeObj("X::Method::NotFound"),
                            "No such method 'chars' for invocant of type '" +
                            a[0].typeName() + "'"};
                    std::string input = a.empty() ? "" : a[0].toStr();
                    Value r = grammarParse(ci.get(), input, sub, startRule, actions,
                                           ruleArgs.empty() ? nullptr : &ruleArgs);
                    if (r.t == VT::Match && r.md() && actions.t != VT::Any && actions.t != VT::Nil)
                        r.mdW().actions = std::make_shared<Value>(actions);   // `$match.actions`
                    // From 6.e a FAILED .parse is a Failure carrying
                    // X::Syntax::Confused, not a bare Nil — 6.e gives grammars a
                    // base class whose parse reports where it stopped. .subparse
                    // keeps answering with what it managed to match.
                    if (sixE() && !sub && (r.t == VT::Nil || r.t == VT::Any || r.t == VT::Type)) {
                        Value f = rakuppNewFailure();
                        (*f.hash())["exception"] = Value::typeObj("X::Syntax::Confused");
                        (*f.hash())["message"]   = Value::str("Confused");
                        return f;
                    }
                    return r;
                };
                if (m == "parsefile") { // slurp the file, then parse its contents
                    std::string input = args.empty() ? "" : args[0].toStr();
                    std::ifstream in(input); std::ostringstream ss; ss << in.rdbuf(); input = ss.str();
                    // Rakudo's parsefile matches the file contents verbatim,
                    // trailing newline included (rule sigspace absorbs it)
                    ValueList a2 = args; if (!a2.empty()) a2[0] = Value::str(input); else a2.push_back(Value::str(input));
                    return builtinParse(a2);
                }
                // A user-defined `method parse`/`subparse` (e.g. YAMLish wires :actions via nextwith)
                // runs first; the built-in is its redispatch target.
                if (Value* um = ci->findMethod(m)) {
                    RedispatchCtx prc; prc.next = builtinParse; prc.sameArgs = args;
                    redispatchStack_.push_back(std::move(prc));
                    Value r;
                    // ownFrame: the frame just pushed belongs to the invoked
                    // method — its redispatch floor must sit BELOW it, or
                    // nextwith/callsame can't reach the built-in parse
                    try { r = invokeMethod(*um, inv, args, rwArgs, /*ownFrame=*/true); }
                    catch (...) { redispatchStack_.pop_back(); throw; }
                    redispatchStack_.pop_back();
                    return r;
                }
                return builtinParse(args);
            }
            // `.^candidates` / `R.HOW.candidates(R)` — every declaration of a
            // parametric role group, earliest first (a lone role is its own one)
            if (m == "candidates" && ci->isRole) {
                Value out = Value::array(); out.isList = true;
                for (size_t k = 0; k <= ci->roleVariants.size(); k++)
                    out.arr()->push_back(Value::typeObj(ci->name));
                return out;
            }
            // metamodel (.^find_method / .^add_method / .^methods / .^lookup / .^can)
            if (m == "find_method" || m == "lookup") {
                std::string mn = args.empty() ? "" : args[0].toStr();
                // A ROLE asking for a CONTAINER method wants the ORIGINAL, not the
                // override it is in the middle of declaring: Rakudo runs a role
                // body per composition, before the role's own methods reach the
                // class, so `my &AT-KEY := ::?CLASS.^find_method('AT-KEY')` there
                // is the built-in one. Our role bodies run once, with the role's
                // methods already registered, so the lookup found the override and
                // WriteOnceHash called itself until the stack ran out.
                const bool roleWantsOriginal = ci->isRole && isContainerMethodName(mn);
                Value* um = roleWantsOriginal ? nullptr : ci->findMethod(mn);
                if (um) return *um;
                // a grammar's token/rule/regex is a method too: a Regex that
                // matches its rule against what it is given, and knows its doc
                for (ClassInfo* c = &*ci; c; c = c->parent.get()) {
                    auto rit = c->rules.find(mn);
                    if (rit == c->rules.end()) continue;
                    std::string pat = rit->second;
                    std::string kind = c->ruleKind.count(mn) ? c->ruleKind[mn] : "regex";
                    Value code; code.t = VT::Code; code.setCode(std::make_shared<Callable>());
                    code.code()->name = mn;
                    code.code()->isMethod = true;
                    code.code()->isRegexRoutine = true;
                    auto pit = c->rulePod.find(mn);
                    if (pit != c->rulePod.end()) {
                        code.code()->pod = std::get<0>(pit->second);
                        code.code()->podTrail = std::get<1>(pit->second);
                        code.code()->declLine = std::get<2>(pit->second);
                    }
                    code.code()->builtin = [pat, kind](Interpreter& I, ValueList& a) -> Value {
                        return a.empty() ? Value::nil()
                                         : I.regexMatch(I.rxSubject(a[0]), pat, nullptr, kind);
                    };
                    return code;
                }
                // An ATTRIBUTE ACCESSOR is a method too: `has $.type` publishes
                // `.type`, and `.^find_method('type')` must find it. The
                // accessors are generated at dispatch rather than stored in
                // `methods`, so the meta-lookup answered Nil and a module
                // walking `while $obj.^find_method('type')` stopped at the first
                // node (Data::TypeSystem's is-full-array).
                for (ClassInfo* c = &*ci; c; c = c->parent.get())
                    for (auto& a : c->attrs)
                        if (a.pub && a.name == mn) {
                            Value code; code.t = VT::Code;
                            code.setCode(std::make_shared<Callable>());
                            code.code()->name = mn; code.code()->isMethod = true;
                            code.code()->builtin = [mn](Interpreter& I, ValueList& av) -> Value {
                                if (av.empty()) return Value::any();
                                Value in2 = av[0]; ValueList rest(av.begin() + 1, av.end());
                                return I.methodCall(in2, mn, rest);
                            };
                            return code;
                        }
                // …and a method the class inherits from a BUILT-IN parent. A
                // `class WOH is Hash` answers AT-KEY/ASSIGN-KEY/… from the
                // engine, not from `methods`, so the lookup found nothing and
                // WriteOnceHash — which captures the original with
                // `::?CLASS.^find_method('AT-KEY')` and calls it from its own
                // override — got back something that answered the hash itself.
                // The stub dispatches for real when it is invoked.
                {
                    bool fromBuiltin = ci->isRole;   // a ROLE's ::?CLASS is not composed yet
                    for (ClassInfo* c = &*ci; c && !fromBuiltin; c = c->parent.get())
                        if (!c->nativeParent.empty()) fromBuiltin = true;
                    std::string nativeBase;   // the built-in this class derives
                    for (ClassInfo* c = &*ci; c; c = c->parent.get())
                        if (!c->nativeParent.empty()) { nativeBase = c->nativeParent; break; }
                    if (fromBuiltin && isContainerMethodName(mn)) {
                        Value code; code.t = VT::Code;
                        code.setCode(std::make_shared<Callable>());
                        code.code()->name = mn; code.code()->isMethod = true;
                        code.code()->builtin = [mn, nativeBase](Interpreter& I, ValueList& av) -> Value {
                            if (av.empty()) return Value::any();
                            Value in2 = av[0]; ValueList rest(av.begin() + 1, av.end());
                            // The ORIGINAL is what the caller asked for, so reach
                            // the BUILT-IN behind the class rather than dispatching
                            // again — dispatch would find the override that is
                            // asking, and WriteOnceHash (whose AT-KEY calls the
                            // captured original) recursed 32,000 frames deep.
                            // The storage is shared, so a write through the
                            // stripped view still lands in the object.
                            if (in2.t == VT::Object && in2.obj() && in2.obj()->hasBoxed)
                                in2 = in2.obj()->boxed;
                            if (in2.t == VT::Hash || in2.t == VT::Array) {
                                Value plain = in2;
                                plain.hashKind = nativeBase;   // "" = the plain built-in
                                return I.methodCall(plain, mn, rest);
                            }
                            return I.methodCall(in2, mn, rest, nullptr, /*skipOwn=*/true);
                        };
                        return code;
                    }
                }
                return Value::nil();
            }
            if (m == "declares_method") { // locally declared (not inherited)?
                std::string mn = args.empty() ? "" : args[0].toStr();
                auto it = ci->methods.find(mn);
                return it != ci->methods.end() ? it->second : Value::boolean(false);
            }
            if (m == "find_method_qualified" && args.size() >= 2) { // ($type, $name)
                std::string tn = args[0].t == VT::Type ? args[0].s : args[0].typeName();
                std::string mn = args[1].toStr();
                auto tit = classes_.find(tn);
                if (tit != classes_.end()) { auto mit = tit->second->methods.find(mn); if (mit != tit->second->methods.end()) return mit->second; }
                Value* um = ci->findMethod(mn);
                return um ? *um : Value::nil();
            }
            if (m == "add_method") { // .^add_method($name, $code)
                if (args.size() >= 2) {
                    noteSymbolMutation("runtime .^add_method");
                    Value add = args[1];
                    // Adding a bare PROTO under a second name (Method::Also aliases a
                    // `proto method … is also<…>`) must alias the whole multi GROUP,
                    // or the alias runs the proto body — `{ * }` — instead of
                    // dispatching. Rakudo re-adds each candidate with
                    // ^add_multi_method; here the dispatcher already holds them, so
                    // install that.
                    if (add.t == VT::Code && add.code() && add.code()->isProto)
                        for (auto& kv : ci->methods) {
                            if (!(kv.second.t == VT::Code && kv.second.code() &&
                                  kv.second.code()->isMultiDispatcher)) continue;
                            bool holdsIt = false;
                            for (auto& cand : kv.second.code()->candidates)
                                if (cand.code() == add.code()) { holdsIt = true; break; }
                            if (holdsIt) { add = kv.second; break; }
                        }
                    // A plain SUB takes the invocant as its first POSITIONAL when it
                    // is installed as a method — that is Rakudo's rule, and the
                    // reason PDF::COS::Tie generates every PDF entry accessor as
                    // `sub (\obj) is rw { obj.rw-accessor(…) }`. Stored as it came,
                    // the sub ran with no arguments at all and `obj` was Any. The
                    // Callable is shared, so the flag rides on a CLONE: the same
                    // `&sub` may still be called as a sub elsewhere.
                    if (add.t == VT::Code && add.code() && !add.code()->isMethod &&
                        !add.code()->subAsMethod) {
                        auto clone = std::make_shared<Callable>(*add.code());
                        clone->subAsMethod = true;
                        Value m2; m2.t = VT::Code; m2.setCode(std::move(clone));
                        add = std::move(m2);
                    }
                    const std::string addName = args[0].toStr();
                    ci->methods[addName] = add;
                    // …and if it is the build-plan routine, say so on the class:
                    // the construction path runs it only for a class that has one.
                    if (addName == "POPULATE") ci->hasPopulate = true;
                }
                return args.size() >= 2 ? args[1] : Value::nil();
            }
            // `.^add_multi_method($name, $meth)` — the same, as a multi CANDIDATE:
            // it joins whatever group that name already holds instead of replacing
            // it. Red generates one comparison method per column this way
            // (`type.^add_multi_method: $attr.name.substr(2), method (Mu:U:) {…}`),
            // so without it a model's columns stopped at the first one (issue #77).
            if (m == "add_multi_method" && args.size() >= 2) {
                noteSymbolMutation("runtime .^add_multi_method");
                const std::string mname = args[0].toStr();
                Value cand = args[1];
                if (ci->awaitingCompose) {   // queued until the base compose (see ClassInfo)
                    ci->pendingMultis.emplace_back(mname, cand);
                    return args[1];
                }
                insertRuntimeMulti(ci.get(), mname, std::move(cand));
                return args[1];
            }
            if (m == "add_parent" && !args.empty()) { // .^add_parent(Type) — runtime inheritance
                // Test::Mock builds a mock type with `Metamodel::ClassHOW.new_type`
                // then `.^add_parent($mocked)` so the mock is-a the mocked type;
                // Method::Protected reparents a class the same way. A user parent
                // becomes the class's parent (or an extra parent if it already has
                // one); a built-in becomes its native parent (`is Str`-style).
                std::string pn = args[0].t == VT::Type ? args[0].s : args[0].typeName();
                if (pn.empty() || pn == "Any" || pn == "Mu") return inv;
                noteSymbolMutation("runtime .^add_parent");
                auto pit = classes_.find(pn);
                if (pit == classes_.end()) pit = classes_.find(resolveClassAlias(pn));
                if (pit != classes_.end()) {
                    if (!ci->parent) ci->parent = pit->second;
                    else if (ci->parent.get() != pit->second.get()) {
                        bool have = ci->parent.get() == pit->second.get();
                        for (auto& ep : ci->extraParents) if (ep.get() == pit->second.get()) have = true;
                        if (!have) ci->extraParents.push_back(pit->second);
                    }
                }
                else if (isKnownTypeName(pn)) ci->nativeParent = pn;
                return inv;
            }
            if (m == "add_role" && !args.empty()) { // .^add_role(Role) — runtime composition
                // The compile-time compose loop also merges multis and detects
                // conflicts; a runtime add takes the simple child-wins merge, which
                // is what the trait-time uses in the wild need (JSON::Class tags its
                // exception wrappers with Rakudo's X::Wrapper this way).
                std::string rn = args[0].t == VT::Type ? args[0].s : args[0].typeName();
                auto rit = classes_.find(rn);
                if (rit == classes_.end()) rit = classes_.find(resolveClassAlias(rn));
                // A BUILT-IN role — Iterable, Positional, Associative — has no
                // ClassInfo to merge methods from: what composing it means here
                // is that the type ANSWERS to it, which is what a `~~ Iterable`
                // check asks. Red's per-model ResultSeq class is built at
                // runtime and composes Iterable exactly this way.
                if ((rit == classes_.end() || !rit->second->isRole) && isBuiltinRole(rn)) {
                    noteSymbolMutation("runtime .^add_role (built-in)");
                    ci->doneRoles.insert(rn);
                    return inv;
                }
                if (rit == classes_.end() || !rit->second->isRole)
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        rn + " is not a known role, so " + ci->name + " cannot .^add_role it"};
                noteSymbolMutation("runtime .^add_role");
                for (auto& kv : rit->second->methods)
                    if (!ci->methods.count(kv.first)) ci->methods[kv.first] = kv.second;
                for (auto& at : rit->second->attrs) {
                    bool have = false;
                    for (auto& ex : ci->attrs) if (ex.name == at.name) { have = true; break; }
                    if (!have) ci->attrs.push_back(at);
                }
                ci->doneRoles.insert(rn);
                for (auto& dr : rit->second->doneRoles) ci->doneRoles.insert(dr);
                // a built-in role the role composed first sits in its NATIVE-parent
                // slot (`also does Sequence` in Red::ResultSeq); the class does it too
                for (ClassInfo* r = rit->second.get(); r; r = r->parent.get())
                    if (!r->nativeParent.empty() && isBuiltinRole(r->nativeParent))
                        ci->doneRoles.insert(r->nativeParent);
                // …and a PARAMETERIZED role's bindings, as a runtime mixin
                // brings them: Red builds each model's ResultSeq class with
                // `.^add_role: Red::ResultSeq[type]`, whose `method of { $of }`
                // reads the parameter through the invocant's class. The pun
                // carries its arguments first; the defaults fill the rest.
                for (auto& b : rit->second->roleParamBindings) ci->roleParamBindings.push_back(b);
                {
                    ValueList noArgs;
                    bindRoleParamsInto(ci.get(), rit->second.get(), noArgs, rit->second->declEnv);
                }
                return inv;
            }
            // rakupp composes types eagerly, so .^compose is a no-op returning the
            // type (modules call it after add_method/add_attribute to finalize).
            // These bare metamodel names must NOT shadow a user method of the same
            // name — `Cro.compose(...)` is a real method, not a MOP finalize call.
            // A type whose METACLASS writes its own `compose` (a runtime type from
            // `MyHOW.new_type`) composes through it — Red builds a model's
            // columns there — unless this IS that compose deferring to the base.
            if (m == "compose" && ci && !ci->findMethod(m) && !tctx_.metaForwarding.count("compose") &&
                ci->howObj.t == VT::Object && ci->howObj.obj() && ci->howObj.obj()->cls) {
                bool userCompose = false;
                for (ClassInfo* c = ci->howObj.obj()->cls.get(); c && !userCompose; c = c->parent.get())
                    if (c->methods.count("compose")) userCompose = true;
                if (userCompose) {
                    Value how = ci->howObj;
                    try { methodCall(how, "compose", ValueList{inv}); }
                    catch (RakuError& ce) {
                        if (ce.message.find("to redispatch to") == std::string::npos &&
                            ce.message.find("not in the dynamic scope of a dispatcher") == std::string::npos)
                            throw;
                    }
                    if (ci->awaitingCompose) {   // its `nextsame` never reached the base
                        ci->awaitingCompose = false;
                        auto pend = std::move(ci->pendingMultis);
                        ci->pendingMultis.clear();
                        for (auto& pm : pend) insertRuntimeMulti(ci.get(), pm.first, pm.second);
                    }
                    return inv;
                }
            }
            if (m == "set_rw" && ci && !ci->findMethod(m)) ci->classRw = true;
            if ((m == "compose" || m == "compose_repr" || m == "publish_method_cache" ||
                 m == "publish_type_cache" || m == "compose_attributes" || m == "set_rw" ||
                 m == "invalidate_method_caches" || m == "publish_boolification_spec") &&
                !(ci && ci->findMethod(m))) {
                // the base compose INCORPORATES the multis queued since new_type
                if (m == "compose" && ci && ci->awaitingCompose) {
                    ci->awaitingCompose = false;
                    auto pend = std::move(ci->pendingMultis);
                    ci->pendingMultis.clear();
                    for (auto& pm : pend) insertRuntimeMulti(ci.get(), pm.first, pm.second);
                }
                return inv;
            }
            if (m == "set_name" || m == "set_shortname") {
                // Rename the CLASS and publish it under the new name, then answer
                // a type object carrying it: the caller holds a Value whose `s` is
                // the old name, and `.^name` reads that — so without re-registering
                // and handing the renamed type back, `.^set_name` looked like a
                // no-op (Parameterizable names each parameterization this way).
                if (!args.empty()) {
                    std::string nn = args[0].toStr();
                    if (!nn.empty() && nn != ci->name) {
                        ci->name = nn;
                        classes_[nn] = ci;      // reachable under its new name
                        noteSymbolMutation("set_name");
                        Value rn = Value::typeObj(nn);
                        rn.ofTypeM() = inv.ofType();
                        return rn;
                    }
                }
                return inv;
            }
            // the Versioning SETTERS: only the getters existed, while the howOps
            // forwarding table already rewrote `$t.HOW.set_ver($t, v)` into
            // `$t.^set_ver(v)` — faithfully forwarding to a method that was not there.
            // `:ver<0.0.1>` stores a BARE "0.0.1" and the getter re-adds the v by
            // tagging hashKind "Version", so a `v0.0.1` literal must be stripped or
            // it comes back as vv0.0.1.
            if (m == "set_ver" && !args.empty()) {
                std::string vs = args[0].toStr();
                if (!vs.empty() && vs[0] == 'v') vs.erase(0, 1);
                ci->ver = vs; return inv;
            }
            // `.^rw` — was the class declared `is rw` (Red makes every column
            // accessor of such a model writable); `.^set_rw` sets it
            if (m == "rw" && args.empty()) return Value::boolean(ci->classRw);
            if (m == "set_auth" && !args.empty()) { ci->auth = args[0].toStr(); return inv; }
            if (m == "set_api"  && !args.empty()) { ci->api  = args[0].toStr(); return inv; }
            if (m == "ver")  return ci->ver.empty()  ? Value::any() : ([&]{ Value v = Value::str(ci->ver);  v.hashKind = "Version"; return v; }());
            if (m == "auth") return Value::str(ci->auth);
            if (m == "api")  return ci->api.empty() ? Value::any() : Value::str(ci->api);
            if (m == "attribute_table") { // Hash: '$!name' => Attribute
                Value out = Value::makeHash();
                for (auto& a : ci->attrs)
                    (*out.hash())[std::string(1, a.sigil) + "!" + a.name] =
                        attributeMetaObject(a, ci->name);
                return out;
            }
            if (m == "add_attribute" && !args.empty()) { // .^add_attribute(Attribute.new(...))
                Value av = args[0];
                if (av.t == VT::Hash && av.hashKind == "Attribute" && av.hash()) {
                    ClassAttr a;
                    std::string an = av.hash()->count("name") ? (*av.hash())["name"].toStr() : "";
                    a.sigil = an.empty() ? '$' : an[0];
                    while (!an.empty() && (an[0]=='$'||an[0]=='@'||an[0]=='%'||an[0]=='&'||an[0]=='!'||an[0]=='.')) an = an.substr(1);
                    a.name = an;
                    a.type = av.hash()->count("type") && (*av.hash())["type"].t == VT::Type ? (*av.hash())["type"].s.str() : std::string();
                    // For `@`/`%` the Attribute's :type is the CONTAINER's type,
                    // where a declaration's `has Int %h` names the element's:
                    // Red adds `Attribute.new(:name<%!___ID_VALUES___>,
                    // :type(Hash))` and then stores Ints in it. Only a
                    // parameterization (`Hash[Int]`) constrains the elements.
                    if (a.sigil == '@' || a.sigil == '%') {
                        const Value& tv = (*av.hash())["type"];
                        a.type = av.hash()->count("type") && tv.t == VT::Type ? tv.ofType() : std::string();
                    }
                    a.rw = av.hash()->count("readonly") ? !(*av.hash())["readonly"].truthy() : false;
                    a.pub = av.hash()->count("has_accessor") ? (*av.hash())["has_accessor"].truthy() : false;
                    if (av.hash()->count("build") && (*av.hash())["build"].t == VT::Code)
                        a.buildFn = (*av.hash())["build"];
                    // `.^attributes` hands back THIS object, roles mixed in and all:
                    // Red adds `$attr does Red::Attr::Column(%data)` and then finds
                    // its columns with `.^attributes.grep(Red::Attr::Column)`
                    a.metaObj = av;
                    noteSymbolMutation("runtime .^add_attribute");
                    ci->attrs.push_back(a);
                }
                return av;
            }
            if (m == "can") {
                std::string mn = args.empty() ? "" : args[0].toStr();
                Value* um = ci->findMethod(mn);
                Value out = Value::array(); out.isList = true;
                if (um) out.arr()->push_back(*um);
                // a public attribute's generated accessor is a method too, as on
                // the instance `.can` arm: `class Q { has $.x }; Q.^can('x')`
                if (out.arr()->empty())
                    for (ClassInfo* c2 = ci.get(); c2; c2 = c2->parent.get()) {
                        const ClassAttr* at = c2->findAttr(mn);
                        if (at && at->pub) {
                            Value stub; stub.t = VT::Code; stub.setCode(std::make_shared<Callable>());
                            stub.code()->name = mn; stub.code()->isMethod = true;
                            std::string mnc = mn;
                            stub.code()->builtin = [mnc](Interpreter& I, ValueList& a) -> Value {
                                if (a.empty()) return Value::any();
                                ValueList rest(a.begin() + 1, a.end());
                                return I.methodCall(a[0], mnc, std::move(rest));
                            };
                            out.arr()->push_back(stub);
                            break;
                        }
                    }
                // BUILT-IN methods answer .can too: every class news/blesses/gists,
                // and a grammar parses (IETF::RFC_Grammar gates on `.can('parse')`)
                if (out.arr()->empty()) {
                    Value stub = builtinCanStub(mn, ci->isGrammar);
                    if (stub.t == VT::Code) out.arr()->push_back(stub);
                }
                return out;
            }
            // .^private_method_table: the class's OWN private methods, name =>
            // Method (without the `!`). Red scans it for row phasers
            // (`method !before-create is before-create { … }`).
            if (m == "private_method_table") {
                Value out = Value::makeHash();
                for (auto& kv : ci->methods)
                    if (kv.first.size() > 1 && kv.first[0] == '!')
                        (*out.hash())[kv.first.substr(1)] = kv.second;
                return out;
            }
            // `.^submethod_table` — name => Submethod for the type's own submethods
            // (and those its roles composed in, which 6.e no longer does)
            if (m == "submethod_table" && ci) {
                Value out = Value::makeHash();
                for (auto& kv : ci->methods)
                    if (kv.second.t == VT::Code && kv.second.code() && kv.second.code()->isSubmethod &&
                        !ci->roleSubmethodsHidden.count(kv.first) && !(kv.first.size() && kv.first[0] == '!'))
                        (*out.hash())[kv.first] = kv.second;
                return out;
            }
            if (m == "methods" || m == "method_names" || m == "method_table") {
                // `:local` → only this class's own methods; otherwise walk the user
                // inheritance chain (parents + roles), stopping before Any/Mu.
                // .^method_names is the method TABLE's names, so it is local by
                // definition — `class B is A` answers B's own methods only, and
                // an inherited `foo` is not among them (Rakudo agrees).
                // .^method_table is that same local table AS the Hash it is —
                // name => Method (DBIish gates its API on `{$_}:exists` over it).
                bool names = (m == "method_names");
                bool table = (m == "method_table");
                bool local = names || table;
                std::vector<std::string> tblNames;
                for (auto& a : args) if (a.t == VT::Pair && a.s == "local")
                    local = a.pairVal() ? a.pairVal()->truthy() : true;
                Value out = Value::array(); out.isList = true;
                std::set<ClassInfo*> visited; // dedup by class (MRO), not by method name
                std::set<std::string> seen;   // ...except inside one flattened table
                std::function<void(ClassInfo*)> walk = [&](ClassInfo* c) {
                    if (!c || !visited.insert(c).second) return;
                    for (auto& kv : c->methods) {
                        // private (!p) methods stay out, as in Rakudo
                        if (!kv.first.empty() && kv.first[0] == '!') continue;
                        if (local && !seen.insert(kv.first).second) continue;
                        out.arr()->push_back(names ? Value::str(kv.first) : kv.second);
                        if (table) tblNames.push_back(kv.first);
                    }
                    // a PUBLIC attribute's auto-generated accessor is a method too
                    // (Rakudo lists it; Data::Dump renders `method public () …`)
                    for (auto& at : c->attrs) {
                        if (!at.pub || c->methods.count(at.name)) continue;
                        if (local && !seen.insert(at.name).second) continue;
                        if (names) { out.arr()->push_back(Value::str(at.name)); continue; }
                        Value stub; stub.t = VT::Code; stub.setCode(std::make_shared<Callable>());
                        stub.code()->name = at.name; stub.code()->isMethod = true;
                        std::string an = at.name;
                        stub.code()->retType = at.type.empty() ? "" : at.type;
                        stub.code()->builtin = [an](Interpreter& I, ValueList& a) -> Value {
                            if (a.empty()) return Value::any();
                            ValueList rest(a.begin() + 1, a.end());
                            return I.methodCall(a[0], an, std::move(rest));
                        };
                        out.arr()->push_back(stub);
                        if (table) tblNames.push_back(at.name);
                    }
                    if (local) {
                        // a composed role's methods are FLATTENED into the class, so they
                        // belong to its own table: `class C does R` answers `rm` to both
                        // .^method_names and .^methods(:local), as Rakudo does. Class
                        // parents stay out — those are inherited, not local.
                        if (c->parent && c->parent->isRole) walk(c->parent.get());
                        for (auto& p : c->extraParents) if (p && p->isRole) walk(p.get());
                        return;
                    }
                    walk(c->parent.get());
                    for (auto& p : c->extraParents) walk(p.get());
                };
                walk(ci.get());
                if (table) {
                    Value h = Value::makeHash();
                    for (size_t i = 0; i < tblNames.size() && i < out.arr()->size(); i++)
                        (*h.hash())[tblNames[i]] = (*out.arr())[i];
                    return h;
                }
                return out;
            }
            // `roles_to_compose` is Rakudo's "queued for composition" list; by the
            // time a user metaclass's `compose` asks, those roles are exactly the
            // ones the declaration named — which is what `roles` answers here.
            if (m == "roles" || m == "role_typecheck_list" || m == "roles_to_compose") { // composed roles
                Value out = Value::array(); out.isList = true;
                for (auto& rn : ci->doneRoles) out.arr()->push_back(Value::typeObj(rn));
                return out;
            }
            // `.^is_pun` / `.^pun_source` — a role used AS A CLASS is punned into
            // one, and Rakudo lets a method ask whether it is running on that pun
            // and which role it came from. PDF::COS::Tie's `induce` opens with
            // `$obj.mixin: self.^pun_source if self.^is_pun`, so an ordinary class
            // needs the False answer before anything else can happen.
            if (m == "is_pun" || m == "pun_source") {
                std::string roleOf;
                auto pm = ci->name.find("\x01pun");
                if (pm != std::string::npos) roleOf = ci->name.substr(0, pm);
                else if (ci->isRole) roleOf = ci->name;   // the role itself puns to a class
                // Rakudo answers the nqp-level 0/1 here, not a Bool — match it.
                if (m == "is_pun") return Value::integer(roleOf.empty() ? 0 : 1);
                return roleOf.empty() ? Value::any() : Value::typeObj(roleOf);
            }
            // `.^parents` — every ancestor in MRO order, less Any and Mu; `:all`
            // keeps those two, `:local` is the immediate parents only, `:tree`
            // nests each immediate parent with its own tree. Composed roles are
            // not parents.
            if (m == "parents") {
                bool all = false, local = false, tree = false;
                for (auto& av : args)
                    if (av.t == VT::Pair && (!av.pairVal() || av.pairVal()->truthy())) {
                        if (av.s == "all") all = true;
                        else if (av.s == "local") local = true;
                        else if (av.s == "tree") tree = true;
                    }
                if (!local) {
                    auto immediate = [](ClassInfo* c) {
                        std::vector<ClassInfo*> r;
                        if (c->parent && !c->parent->isRole) r.push_back(c->parent.get());
                        for (auto& p : c->extraParents) if (p && !p->isRole) r.push_back(p.get());
                        return r;
                    };
                    if (tree) {
                        std::function<Value(ClassInfo*)> sub = [&](ClassInfo* c) -> Value {
                            Value lst = Value::array();
                            auto ps = immediate(c);
                            if (ps.empty()) {   // the universal tail: [Any, [Mu]]
                                Value mu = Value::array(); mu.arr()->push_back(Value::typeObj("Mu"));
                                Value any = Value::array(); any.arr()->push_back(Value::typeObj("Any")); any.arr()->push_back(mu);
                                lst.arr()->push_back(any);
                                return lst;
                            }
                            for (ClassInfo* p : ps) {
                                Value node = Value::array();
                                node.arr()->push_back(Value::typeObj(p->name));
                                Value rest = sub(p);
                                for (auto& x : *rest.arr()) node.arr()->push_back(x);
                                lst.arr()->push_back(node);
                            }
                            return lst;
                        };
                        Value out = sub(ci.get()); out.isList = true;
                        return out;
                    }
                    std::vector<std::string> lin;
                    std::function<void(ClassInfo*)> visit = [&](ClassInfo* c) {
                        for (ClassInfo* p : immediate(c)) { lin.push_back(p->name); visit(p); }
                    };
                    visit(ci.get());
                    Value out = Value::array(); out.isList = true;
                    for (size_t i = 0; i < lin.size(); i++) {   // C3 for the simple diamond: keep the LAST
                        bool later = false;
                        for (size_t j = i + 1; j < lin.size(); j++) if (lin[j] == lin[i]) { later = true; break; }
                        if (!later) out.arr()->push_back(Value::typeObj(lin[i]));
                    }
                    if (!ci->nativeParent.empty()) {
                        const auto& anc = typeAncestry(ci->nativeParent);
                        if (anc.empty() || anc[0] != ci->nativeParent)
                            out.arr()->push_back(Value::typeObj(ci->nativeParent));
                        else for (auto& a : anc)
                            if (a != "Any" && a != "Mu" && (all || a != "Cool") && !isBuiltinRole(a))
                                out.arr()->push_back(Value::typeObj(a));
                    }
                    if (all && !ci->isRole) {
                        out.arr()->push_back(Value::typeObj("Any"));
                        out.arr()->push_back(Value::typeObj("Mu"));
                    }
                    return out;
                }
                Value out = Value::array(); out.isList = true;
                if (ci->parent && !ci->parent->isRole) out.arr()->push_back(Value::typeObj(ci->parent->name));
                for (auto& p : ci->extraParents) if (p && !p->isRole) out.arr()->push_back(Value::typeObj(p->name));
                // a built-in parent answers with its ancestry minus the hidden/
                // universal tail, matching Rakudo: G.^parents is (Grammar Match
                // Capture) — no Cool (hidden), no Any/Mu
                if (out.arr()->empty() && !ci->nativeParent.empty()) {
                    const auto& anc = typeAncestry(ci->nativeParent);
                    if (anc.empty() || anc[0] != ci->nativeParent)
                        out.arr()->push_back(Value::typeObj(ci->nativeParent));
                    else for (auto& a : anc)
                        if (a != "Any" && a != "Mu" && a != "Cool" && !isBuiltinRole(a))
                            out.arr()->push_back(Value::typeObj(a));
                }
                if (out.arr()->empty() && !ci->isRole) out.arr()->push_back(Value::typeObj("Any"));
                return out;
            }
            if (m == "mro") { // method resolution order: self, ancestors, then Any, Mu
                Value out = Value::array(); out.isList = true;
                // Depth-first over the primary + additional (multiple-inheritance) parents,
                // then dedup keeping the LAST occurrence — the C3 order for simple diamonds
                // (D is B is C, B/C is A → D, B, C, A).
                std::vector<std::string> lin;
                // A composed ROLE is not an ancestor: Rakudo answers `A,Any,Mu`
                // for `class A does R`, and a module walking the MRO to find
                // ancestors must not meet R there. (`does` records the role in
                // the parent slot here, which is why it showed up at all.)
                std::function<void(ClassInfo*)> visit = [&](ClassInfo* c) {
                    if (!c) return;
                    if (!c->isRole) lin.push_back(c->name);
                    if (c->parent) visit(c->parent.get());
                    for (auto& p : c->extraParents) visit(p.get());
                };
                visit(ci.get());
                for (size_t i = 0; i < lin.size(); i++) {
                    bool later = false;
                    for (size_t j = i + 1; j < lin.size(); j++) if (lin[j] == lin[i]) { later = true; break; }
                    if (!later) out.arr()->push_back(Value::typeObj(lin[i]));
                }
                // a built-in parent anywhere up the primary chain contributes
                // its class-only ancestry: G,Grammar,Match,Capture,Cool,Any,Mu
                // for a grammar, F,DateTime,Any,Mu for `class F is DateTime`
                for (ClassInfo* c = ci.get(); c; c = c->parent.get())
                    if (!c->nativeParent.empty()) {
                        const auto& anc = typeAncestry(c->nativeParent);
                        if (anc.empty() || anc[0] != c->nativeParent) // no table entry: the parent alone
                            out.arr()->push_back(Value::typeObj(c->nativeParent));
                        else for (auto& a : anc)
                            if (a != "Any" && a != "Mu" && !isBuiltinRole(a))
                                out.arr()->push_back(Value::typeObj(a));
                        break;
                    }
                // A BUILT-IN parent can arrive twice — once as the ClassInfo the
                // `is` named, once from the nativeParent ancestry — and
                // `class A is Exception` reported `A,Exception,Exception,Any,Mu`.
                {
                    std::set<std::string> seen;
                    ValueList uniq;
                    for (auto& t : *out.arr())
                        if (seen.insert(t.t == VT::Type ? t.s : t.typeName()).second) uniq.push_back(t);
                    *out.arr() = std::move(uniq);
                }
                out.arr()->push_back(Value::typeObj("Any"));
                out.arr()->push_back(Value::typeObj("Mu"));
                return out;
            }
            // `.^declares_method('name')` — does THIS class define it, rather
            // than inherit or compose it? Red asks before installing its own
            // TWEAK, so as not to shadow a model's.
            if (m == "declares_method" && !args.empty()) {
                std::string mn = args[0].t == VT::Code && args[0].code()
                               ? args[0].code()->name : args[0].toStr();
                auto dm = ci->methods.find(mn);   // Rakudo hands back the METHOD
                return dm != ci->methods.end() ? dm->second : Value::boolean(false);
            }
            if (m == "attributes") { // Attribute objects: .name ($!x), .type, .readonly
                bool local = false;
                for (auto& a : args) if (a.t == VT::Pair && a.s == "local") local = a.pairVal() ? a.pairVal()->truthy() : true;
                Value out = Value::array(); out.isList = true;
                std::set<ClassInfo*> visited;
                std::function<void(ClassInfo*)> walk = [&](ClassInfo* c) {
                    if (!c || !visited.insert(c).second) return;
                    for (auto& a : c->attrs) out.arr()->push_back(attributeMetaObject(a, c->name));
                    if (local) return;
                    walk(c->parent.get());
                    for (auto& p : c->extraParents) walk(p.get());
                };
                walk(ci.get());
                return out;
            }
            // `.^get_attribute_for_usage('$!x')` — ONE attribute's meta-object,
            // by name. AttrX::Mooish reaches every lazy attribute through it
            // (`self.^get_attribute_for_usage('$!' ~ $name).get_value(self)`),
            // and it works on an instance as well as on the type. Rakudo looks
            // only at the class's OWN attributes — a parent's `$!b` answers
            // "No $!b attribute in Kid" there — so neither does this.
            if (m == "get_attribute_for_usage" && !args.empty()) {
                std::string want = args[0].t == VT::Hash && args[0].hashKind == "Attribute" &&
                                   args[0].hash() && args[0].hash()->count("name")
                                 ? args[0].hash()->at("name").toStr() : args[0].toStr();
                for (auto& a : ci->attrs) {
                    if (std::string(1, a.sigil) + "!" + a.name == want || a.name == want)
                        return attributeMetaObject(a, ci->name);
                }
                throw RakuError{Value::typeObj("X::Attribute::Undeclared"),
                                "No " + want + " attribute in " + ci->name};
            }
            // `new`: a user-defined `new` (often a multi) coexists with the default
            // Mu.new. Use a custom candidate only if one matches the args; otherwise
            // fall back to default construction (named args / no args).
            //
            // …unless the caller asked to reach PAST the invocant's own methods.
            // `self.Pair::new($k, $v)` inside `class ValuePair is Pair` means the
            // built-in Pair's constructor; finding the user's `new` here called
            // it again, forever. Every other method already honoured skipOwn;
            // `new` had its own arm and did not.
            if (m == "new" && !m.skipOwn) {
                Value* um = ci->findMethod("new");
                bool useCustom = um != nullptr;
                if (um && um->code() && um->code()->isMultiDispatcher) {
                    useCustom = false;
                    bool hasProto = false;
                    for (auto& cand : um->code()->candidates) {
                        if (cand.code() && cand.code()->isProto) { hasProto = true; continue; }
                        if (scoreCandidate(cand, args) >= 0) { useCustom = true; break; }
                    }
                    // A class that writes its own `proto method new(|)` REPLACES the
                    // default constructor: with no candidate matching, the call is an
                    // error, not a default construction. (Without a proto the multis
                    // only ADD to Mu.new, which still takes named attributes.)
                    if (!useCustom && hasProto) return invokeMethod(*um, inv, args, rwArgs);
                }
                // through the CHAIN, so a callwith/callsame inside the custom new
                // has a dispatcher (AttrProxy.new's callwith → the builtin Proxy.new)
                if (useCustom) return invokeMethodChain(m, ci.get(), inv, args, rwArgs);
            } else if (!m.skipOwn && ci->findMethodForCall(m, langRev_ < 2)) {
                return invokeMethodChain(m, ci.get(), inv, args, rwArgs);
            }
            // `method loader handles <load-delegate>` on a TYPE OBJECT invocant:
            // PDF::COS is a role whose loader API is reached as `PDF::COS.load-dict`,
            // and its body forwards with `$.load-delegate: …`. The instance path has
            // the same arm; without this one the delegated name was never found.
            for (ClassInfo* c = ci.get(); c; c = c->parent.get()) {
                auto hit = c->methodHandles.find(m);
                if (hit == c->methodHandles.end()) continue;
                Value target = methodCall(inv, hit->second, ValueList{});
                return methodCall(target, m, std::move(args), rwArgs);
            }
            // accessing an attribute (public accessor) on a type object is illegal
            if (const ClassAttr* at = ci->findAttr(m)) {
                if (at->pub) throw RakuError{Value::typeObj("X::Method::NotFound"),
                    "Cannot look up attributes in a " + inv.s + " type object"};
            }
            if (m == "new" || m == "bless") {
                // An X::IO exception composes its .message from its attributes,
                // as Rakudo does — the class carries no method of its own, so the
                // text is built here, once, at construction.
                // X::NYI composes its message from `feature`, the way Rakudo's
                // does — an exception whose entire purpose is that sentence is
                // useless without it.
                if (ci->name == "X::NYI") {
                    std::string feature;
                    for (auto& g : args)
                        if (g.t == VT::Pair && g.s == "feature")
                            feature = g.pairVal() ? g.pairVal()->toStr() : "";
                    // Rakudo spells this as a `method message`, not an attribute,
                    // so a `message` the caller passes is IGNORED — the sentence
                    // is always composed from `feature`. Pushed last, because a
                    // duplicate named argument binds the last one.
                    args.push_back(Value::pair("message",
                        Value::str(feature + " not yet implemented. Sorry.")));
                }
                if (ci->name.compare(0, 6, "X::IO:") == 0) {
                    bool haveMsg = false;
                    std::map<std::string, std::string> a;
                    for (auto& g : args)
                        if (g.t == VT::Pair) {
                            if (g.s == "message") haveMsg = true;
                            a[g.s] = g.pairVal() ? g.pairVal()->toStr() : "";
                            if (g.s == "mode" && g.pairVal()) { // '0o755', octal, 3 digits
                                char buf[32]; snprintf(buf, sizeof buf, "%03llo",
                                                       (unsigned long long)g.pairVal()->toInt());
                                a["mode"] = buf;
                            }
                        }
                    const std::string& n = ci->name;
                    std::string msg;
                    auto oserr = [&] { return ": " + a["os-error"]; };
                    if      (n == "X::IO::Dir")     msg = "Failed to get the directory contents of '" + a["path"] + "'" + oserr();
                    else if (n == "X::IO::Rmdir")   msg = "Failed to remove the directory '" + a["path"] + "'" + oserr();
                    else if (n == "X::IO::Unlink")  msg = "Failed to remove the file '" + a["path"] + "'" + oserr();
                    else if (n == "X::IO::Chdir")   msg = "Failed to change the working directory to '" + a["path"] + "'" + oserr();
                    else if (n == "X::IO::Cwd")     msg = "Failed to get the working directory" + oserr();
                    else if (n == "X::IO::Symlink") msg = "Failed to create symlink called '" + a["name"] + "' on target '" + a["target"] + "'" + oserr();
                    else if (n == "X::IO::Link")    msg = "Failed to create link called '" + a["name"] + "' on target '" + a["target"] + "'" + oserr();
                    else if (n == "X::IO::Rename")  msg = "Failed to rename '" + a["from"] + "' to '" + a["to"] + "'" + oserr();
                    else if (n == "X::IO::Copy")    msg = "Failed to copy '" + a["from"] + "' to '" + a["to"] + "'" + oserr();
                    else if (n == "X::IO::Move")    msg = "Failed to move '" + a["from"] + "' to '" + a["to"] + "'" + oserr();
                    else if (n == "X::IO::Mkdir")   msg = "Failed to create directory '" + a["path"] + "' with mode '0o" + a["mode"] + "'" + oserr();
                    else if (n == "X::IO::Chmod")   msg = "Failed to set the mode of '" + a["path"] + "' to '0o" + a["mode"] + "'" + oserr();
                    else if (n == "X::IO::DoesNotExist") msg = "Failed to find '" + a["path"] + "' while trying to do '." + a["trying"] + "'";
                    else if (n == "X::IO::Directory") msg = "'" + a["path"] + "' is a directory, cannot do '." + a["trying"] + "' on a directory";
                    if (!haveMsg && !msg.empty()) args.push_back(Value::pair("message", Value::str(msg)));
                }
                // NativeCall CStruct: allocate zeroed native memory and set fields
                // from named args, so the instance can be passed to / read from C.
                // `repr` is empty on every ordinary class, and an empty std::string
                // still costs three strlen'd comparisons here without the guard.
                if (!ci->repr.empty() &&
                    (ci->repr == "CStruct" || ci->repr == "CPPStruct" || ci->repr == "CUnion")) {
                    long long size = Interpreter::ncStructSize(ci.get());
                    void* mem = calloc(1, size ? (size_t)size : 1);
                    auto od = makePayload<ObjectData>();
                    od->cls = ci;
                    od->attrs["__native_ptr"] = Value::integer((long long)(intptr_t)mem);
                    od->attrs["__cstruct_owned"] = Value::boolean(true);
                    Value self = Value::object(od);
                    for (auto& arg : args) if (arg.t == VT::Pair && arg.pairVal()) {
                        std::string type; long long off = Interpreter::ncFieldOffset(ci.get(), arg.s, type);
                        if (off >= 0) Interpreter::ncWriteElem((long long)(intptr_t)mem + off, type, 0, *arg.pairVal());
                    }
                    runBuildChain(ci.get(), self, args);
                    maybeRegisterDestroy(self);
                    return self;
                }
                // A class subclassing a native container (`class A is Array`): the
                // instance is an object backed by a native Array/Hash (via ObjectData.boxed),
                // so it indexes/pushes natively while .WHAT answers the user type.
                std::string nb;
                for (ClassInfo* c = ci.get(); c && nb.empty(); c = c->parent.get()) nb = c->nativeParent;
                // Every arm below tests `nb` — the nearest BUILT-IN ancestor — against
                // a name, and a plain user class has no built-in ancestor at all: `nb`
                // is empty, and all twenty-three comparisons are a std::string against
                // a literal that cannot match. They are not free. The literal arrives
                // as a `const char*`, so its length is not a constant and each
                // comparison calls strlen — which put strlen at the TOP of the leaf
                // table when profiling `class K { }` construction, a class with
                // nothing whatever to build. One emptiness test skips the lot, and
                // every arm inside still sees exactly what it saw before.
                if (!nb.empty()) {
                    if (nb == "Set" || nb == "SetHash" || nb == "Bag" || nb == "BagHash" ||
                        nb == "Mix" || nb == "MixHash" ||
                        // …and the BYTE-BUFFER family, which a class reaches by
                        // COMPOSING it: `unit class PDF::IO::Blob does Blob[uint8]`
                        // is how PDF carries every encoded stream, and with nothing
                        // backing the instance none of `.bytes`, `.decode` or
                        // `.subbuf` existed on it.
                        nb == "Blob" || nb == "Buf" ||
                        nb == "blob8" || nb == "blob16" || nb == "blob32" || nb == "blob64" ||
                        nb == "buf8" || nb == "buf16" || nb == "buf32" || nb == "buf64") {
                        // `class MySet is Set`: back the instance with a real quanthash
                        // built from the args, so .elems/.keys/{k} dispatch to it
                        auto od = makePayload<ObjectData>();
                        od->cls = ci; od->hasBoxed = true;
                        od->boxed = methodCall(Value::typeObj(nb), "new", args);
                        Value self = Value::object(od);
                        runBuildChain(ci.get(), self, args);
                        maybeRegisterDestroy(self);
                        return self;
                    }
                    // `my class CosOfAttr is Attribute does COSAttrHOW {}` — a user
                    // class built on a META-OBJECT. PDF::COS::Tie makes one per
                    // array/hash element type and reads `.name`/`.type` straight back
                    // off it; with nothing backing the instance both answered Nil.
                    // Box a real meta-object built from the named arguments, which is
                    // what the built-in constructor does with them.
                    if (nb == "Attribute" || nb == "Parameter") {
                        auto od = makePayload<ObjectData>();
                        od->cls = ci; od->hasBoxed = true;
                        Value meta = Value::makeHash(); meta.hashKind = nb;
                        // A named argument that names one of the SUBCLASS's OWN
                        // attributes initialises that attribute; only the rest
                        // describe the meta-object. Without the filter every pair
                        // went to the box and the class's attributes were left at
                        // their type defaults, so `class C is Parameter { has $.x }`
                        // answered Nil from `C.new(x => 'V').x` — the value was in
                        // the box, reachable as `self<x>` and by the meta-object's
                        // catch-all accessor, but never in the slot `$!x` and the
                        // generated accessor read. A subclass declaring NO attribute
                        // (which is what PDF::COS::Tie's `is Attribute` classes are)
                        // filters nothing and is byte-for-byte unchanged.
                        for (auto& arg : args)
                            if (arg.t == VT::Pair && !ci->findAttr(arg.s))
                                (*meta.hash())[arg.s] = arg.pairVal() ? *arg.pairVal() : Value::boolean(true);
                        od->boxed = std::move(meta);
                        // …and the attributes are bound from the args, the way every
                        // other built-in-backed construction does it (see the
                        // IO::Path and DateTime arms below).
                        runAttrDefaults(od, ci, args);
                        for (ClassInfo* c = ci.get(); c; c = c->parent.get())
                            for (auto& at : c->attrs)
                                if (!od->attrs.count(at.name))
                                    od->attrs[at.name] = rtTypedDefault(at.type.c_str(), at.sigil);
                        Value self = Value::object(od);
                        runBuildChain(ci.get(), self, args);
                        maybeRegisterDestroy(self);
                        return self;
                    }
                    if (nb == "Array" || nb == "List" || nb == "Hash" || nb == "Map") {
                        auto od = makePayload<ObjectData>();
                        od->cls = ci; od->hasBoxed = true;
                        if (nb == "Hash" || nb == "Map") od->boxed = Value::makeHash();
                        else { od->boxed = Value::array(); od->boxed.isList = (nb == "List"); }
                        od->boxed.ofTypeM() = inv.ofType(); // A[Int] -> element type on the box
                        // An attribute with no value is its DECLARED TYPE OBJECT
                        // (`has Int $.obj-num` answers Int, not Any) and a typed
                        // container is that container (`has Str @.data` is an
                        // Array[Str]). The plain-object path seeds every slot that
                        // way; this one seeded none, so a role's typed attribute
                        // composed into a Hash-backed class came back Any — and
                        // `my Int $obj-num = $object.obj-num` in PDF::IO::Serializer
                        // failed its own type check. (A `= default` EXPRESSION is
                        // deliberately not run: Rakudo does not run it here either.)
                        for (ClassInfo* c = ci.get(); c; c = c->parent.get())
                            for (auto& at : c->attrs)
                                if (!od->attrs.count(at.name))
                                    od->attrs[at.name] = rtTypedDefault(at.type.c_str(), at.sigil);
                        for (auto& arg : args)
                            if (arg.t == VT::Pair) {
                                const ClassAttr* at = ci->findAttr(arg.s);
                                if (at && at->pub)
                                    od->attrs[arg.s] = arg.pairVal() ? *arg.pairVal() : Value::any();
                            }
                        Value self = Value::object(od);
                        runBuildChain(ci.get(), self, args);
                        maybeRegisterDestroy(self);
                        return self;
                    }
                    // A class subclassing a scalar built-in with its own `.new` (DateTime,
                    // Date): box the built-in and keep the user object's identity/attrs.
                    // `class IO::Path::AutoDecompress is IO::Path`: back the
                    // instance with a real IO::Path built from the non-attribute
                    // args, so `.slurp`/`.lines`/`.e`/`.Str` reach the built-in
                    // while `.WHAT` keeps the user type. A mixin over it (`self
                    // but Proccer`) copies the box along, and a qualified
                    // `self.IO::Path::slurp` goes straight to it (the skipOwn
                    // forward in methodCallInner). Without the box the instance
                    // was a bare object: "No such method 'slurp'".
                    if (nb == "IO::Path") {
                        auto od = makePayload<ObjectData>(); od->cls = ci; od->hasBoxed = true;
                        ValueList builtinArgs;
                        for (auto& a : args)
                            if (!(a.t == VT::Pair && a.namedArg && ci->findAttr(a.s))) builtinArgs.push_back(a);
                        od->boxed = methodCall(Value::typeObj("IO::Path"), "new", builtinArgs);
                        runAttrDefaults(od, ci, args);
                        Value self = Value::object(od);
                        runBuildChain(ci.get(), self, args);
                        maybeRegisterDestroy(self);
                        return self;
                    }
                    if (nb == "DateTime" || nb == "Date") {
                        auto od = makePayload<ObjectData>(); od->cls = ci; od->hasBoxed = true;
                        // args that are not attribute pairs feed the BUILT-IN's own
                        // constructor (`D.new(:2000year, a => 5)`: :year boxes the
                        // DateTime, a => 5 binds the attribute)
                        ValueList builtinArgs;
                        for (auto& a : args)
                            if (!(a.t == VT::Pair && ci->findAttr(a.s))) builtinArgs.push_back(a);
                        // box FIRST (the parent constructs before subclass defaults,
                        // as in Rakudo's BUILDPLAN), then the ONE attribute walk —
                        // the stripped copy here had no `self` in scope and no
                        // provided-args-during-walk, so `has $.b = $!a * 2` died
                        od->boxed = methodCall(Value::typeObj(nb), "new", builtinArgs);
                        runAttrDefaults(od, ci, args);
                        Value self = Value::object(od);
                        runBuildChain(ci.get(), self, args);
                        maybeRegisterDestroy(self);
                        return self;
                    }
                    // `class ValuePair is Pair`: back the instance with a real Pair,
                    // so `.key`/`.value` and everything the type answers dispatch to
                    // it while `.WHAT` keeps saying the user type. Both spellings the
                    // built-in takes reach it — two positionals, or :key/:value — and
                    // a POSITIONAL Pair argument (`ValuePair.new( (a => 42) )`) is
                    // not mistaken for a named one.
                    if (nb == "Pair" || nb == "Enum") {
                        auto od = makePayload<ObjectData>(); od->cls = ci; od->hasBoxed = true;
                        ValueList builtinArgs;   // attribute pairs stay with the object
                        for (auto& a : args)
                            if (!(a.t == VT::Pair && a.namedArg && ci->findAttr(a.s)))
                                builtinArgs.push_back(a);
                        od->boxed = methodCall(Value::typeObj("Pair"), "new", builtinArgs);
                        runAttrDefaults(od, ci, args);
                        Value self = Value::object(od);
                        runBuildChain(ci.get(), self, args);
                        maybeRegisterDestroy(self);
                        return self;
                    }
                    // A class subclassing a SCALAR built-in (`class Int64 is Int`,
                    // `class Symbol is Str`): box the value its parent's constructor
                    // makes, so the instance numifies, stringifies and compares as
                    // that value while .WHAT keeps answering the user type. Without
                    // the box `Int64.new(-42)` was an attribute-less object whose
                    // numeric value was its address.
                    // …but only for a class that adds NOTHING of its own — no
                    // attributes and no methods, just a name for the built-in's
                    // values (`class Int64 is Int does Special {}`). A class that adds
                    // either is an ordinary object that merely INHERITS the built-in's
                    // type: roast's `class NotComplex is Cool { method Numeric {…} }`
                    // decides its own numification, and `class DifferentReal is Real {
                    // has $.value }` keeps state the box would throw away.
                    bool addsOwn = false;
                    for (ClassInfo* c2 = ci.get(); c2 && !addsOwn; c2 = c2->parent.get())
                        if (!c2->attrs.empty() || !c2->methods.empty()) addsOwn = true;
                    if (!addsOwn &&
                        (nb == "Int" || nb == "Num" || nb == "Rat" || nb == "FatRat" ||
                         nb == "Str" || nb == "Cool" || nb == "Real" || nb == "Numeric" ||
                         nb == "Complex" || nb == "Bool")) {
                        auto od = makePayload<ObjectData>(); od->cls = ci; od->hasBoxed = true;
                        ValueList builtinArgs;   // attribute pairs stay with the object
                        for (auto& a : args)
                            if (!(a.t == VT::Pair && ci->findAttr(a.s))) builtinArgs.push_back(a);
                        od->boxed = methodCall(Value::typeObj(nb), "new", builtinArgs);
                        runAttrDefaults(od, ci, args);
                        Value self = Value::object(od);
                        runBuildChain(ci.get(), self, args);
                        maybeRegisterDestroy(self);
                        return self;
                    }
                }
                auto od = makePayload<ObjectData>();
                od->cls = ci;
                runAttrDefaults(od, ci, args);
                // A class deriving a built-in SCALAR inherits that type's storage:
                // Rakudo's Str carries a `value` attribute, so `class S is Str`
                // constructed with `value => …` answers it from `~$s` and from a
                // qualified `self.Str::Str()`. The branch above boxes such a class
                // only when it adds NOTHING of its own; one that also declares
                // attributes or methods is an ordinary object here, and the
                // `value` argument — naming no attribute it declares — was
                // silently dropped, leaving it to stringify as its own gist.
                // Terminal::Table's `String is Str` is exactly that shape.
                //
                // A class declaring its OWN `value` keeps it (roast's
                // `class DifferentReal is Real { has $.value }`), and with no
                // `value` argument nothing changes.
                //
                // …with one exception, measured against Rakudo: deriving `Str`,
                // the argument fills BOTH — the subclass's attribute and the
                // string the instance IS. (`Real` is a role with no storage, so
                // the roast case above still keeps the box empty.) PDF's
                // TextString is `class … is Str { has $.value }` and passes
                // itself to a `Str $str` routine to encode; without the string
                // behind it every author, title and date in a document was
                // written out as the object's own gist.
                if (!nb.empty() && (!ci->findAttr("value") || nb == "Str") &&
                    (nb == "Int" || nb == "Num" || nb == "Rat" || nb == "FatRat" ||
                     nb == "Str" || nb == "Cool" || nb == "Real" || nb == "Numeric" ||
                     nb == "Complex" || nb == "Bool"))
                    for (auto& a : args)
                        if (a.t == VT::Pair && a.s == "value" && a.pairVal()) {
                            od->hasBoxed = true;
                            od->boxed = *a.pairVal();
                            break;
                        }
                // the checks below walk it too — on the stack, since it is one or
                // two pointers on any ordinary hierarchy and this ran per construction
                ClassInfo* chainBuf[8];
                std::vector<ClassInfo*> chainSpill;
                size_t nChain = 0;
                for (ClassInfo* c = ci.get(); c; c = c->parent.get()) {
                    if (nChain < 8) chainBuf[nChain] = c; else chainSpill.push_back(c);
                    nChain++;
                }
                auto chainAt = [&](size_t i) -> ClassInfo* { return i < 8 ? chainBuf[i] : chainSpill[i - 8]; };
                // enforce an attribute type smiley (`has Int:D $.a` / `has Int:U $.a`)
                // on the FINAL slot value, matching Rakudo's X::TypeCheck::Attribute::Default.
                // Only when the attr actually received a value (an explicit default or a
                // construction arg); a bare `has T:D $.x` with neither is a compile-time
                // concern (X::Syntax::Variable::MissingInitializer) we don't model here.
                // the DEFAULT constructor takes named arguments only — a class
                // that wants positionals writes its own .new or a BUILD
                // …but a class deriving a BUILT-IN (`is Num`, `is Str`) inherits
                // that type's constructor, which does take a positional
                // `nb` empty means NO ancestor carries a nativeParent at all, so the
                // walk below cannot find one either — skip it rather than re-walking
                // the whole chain to prove what the search above already established.
                bool nativeBased = false;
                if (!nb.empty())
                    for (ClassInfo* c2 = ci.get(); c2; c2 = c2->parent.get())
                        // the implicit Grammar ancestor brings no positional
                        // constructor — Rakudo's G.new(42) refuses like any class
                        if (!c2->nativeParent.empty() && c2->nativeParent != "Grammar") { nativeBased = true; break; }
                // The two lookups are ordered LAST on purpose: they hash a name and
                // walk the inheritance chain, while the thing they guard only
                // matters when a positional argument is actually present — which,
                // for the default constructor, is the error case and not the
                // common one.
                bool anyPositional = false;
                for (auto& arg : args)
                    if (arg.t != VT::Pair) { anyPositional = true; break; }
                // (a BUILD does not change that: Mu.new passes it NAMED arguments only)
                if (anyPositional && !nativeBased && !ci->findMethod("new"))
                    for (auto& arg : args)
                        if (arg.t != VT::Pair)
                            throwTypedV("X::Constructor::Positional",
                                        {{"type", Value::typeObj(ci->name)}},
                                        "Default constructor for '" + ci->name +
                                        "' only takes named arguments");
                // `is required` is checked DURING the construction walk below, after
                // a class's own BUILD and before its own TWEAK — Rakudo raises it
                // from BUILDALL, so a custom `submethod BUILD` that fills the
                // attribute itself satisfies it (DBDish::Pg's DBError::Pg reads
                // every one of its `is required` fields off the PGresult) while a
                // TWEAK that would fill it comes too late and still throws.
                auto checkRequiredFor = [&](ClassInfo* rc) {
                    for (auto& at : rc->attrs) {
                        if (!at.required) continue;
                        // `is required` means SUPPLIED AT CONSTRUCTION — a default
                        // of its own does not excuse it
                        bool gotArg = false;
                        for (auto& arg : args)
                            if (arg.t == VT::Pair && arg.s == at.name) { gotArg = true; break; }
                        if (gotArg) continue;
                        // …or filled by a custom BUILD. A default of the
                        // attribute's OWN does not excuse it (Rakudo: `has $.d
                        // is required = 7` still demands the argument), so only
                        // an attribute with no default can be satisfied this way.
                        if (!at.def && !at.hasDefVal) {
                            auto ait = od->attrs.find(at.name);
                            if (ait != od->attrs.end() && defined(ait->second)) continue;
                        }
                        throwTypedV("X::Attribute::Required",
                                    {{"name", Value::str("$!" + at.name)},
                                     {"why", Value::str(at.requiredWhy)}},
                                    "The attribute '$!" + at.name + "' is required" +
                                    (at.requiredWhy.empty()
                                         ? std::string(", ")
                                         : " because " + at.requiredWhy + ",\n") +
                                    "but you did not provide a value for it.");
                    }
                };
                for (size_t ci2 = nChain; ci2-- > 0;)
                    for (auto& at : chainAt(ci2)->attrs) {
                        if (!at.defConstraint) continue;
                        bool gotArg = false;
                        for (auto& arg : args)
                            if (arg.t == VT::Pair && arg.s == at.name) { gotArg = true; break; }
                        if (!(at.def || at.hasDefVal || gotArg)) continue;
                        Value cur = od->attrs.count(at.name) ? od->attrs[at.name] : Value::any();
                        bool defd = defined(cur);
                        if ((at.defConstraint == 1 && !defd) || (at.defConstraint == 2 && defd))
                            throwTypedV("X::TypeCheck::Attribute::Default",
                                        {{"name", Value::str("$!" + at.name)}},
                                        "Type check failed on attribute '$!" + at.name +
                                        "'; expected " + at.type +
                                        (at.defConstraint == 1 ? ":D but got " : ":U but got ") +
                                        cur.typeName());
                    }
                // `where {…}` attribute constraints hold at construction too:
                // a DEFINED slot value (arg-provided or defaulted) must satisfy
                // its attr's constraint (Date::Event's lat/lon bounds)
                for (size_t ci2 = nChain; ci2-- > 0;)
                    for (auto& at : chainAt(ci2)->attrs) {
                        if (!at.where) continue;
                        auto wit = od->attrs.find(at.name);
                        if (wit == od->attrs.end() || !defined(wit->second)) continue;
                        if (!attrWhereOk(at.where, wit->second))
                            throwTypedV("X::TypeCheck::Assignment", {{"got", wit->second}},
                                "Type check failed on attribute '$!" + at.name +
                                "'; the value does not satisfy its where constraint");
                    }
                // …and the attribute's declared TYPE holds for what `new` was
                // handed: `has Small $.small` refuses `small => 20` as Rakudo
                // does, naming the attribute and the subset
                for (size_t ci2 = nChain; ci2-- > 0;)
                    for (auto& at : chainAt(ci2)->attrs) {
                        if (at.type.empty() || at.sigil != '$') continue;
                        static const std::set<std::string> kCore = {
                            "Int", "UInt", "Num", "Rat", "Str", "Bool", "Complex"};
                        if (!kCore.count(at.type) && !subsets_.count(at.type)) continue;
                        bool gotArg = false;
                        for (auto& arg : args)
                            if (arg.t == VT::Pair && arg.s == at.name) { gotArg = true; break; }
                        if (!gotArg) continue;
                        auto vit = od->attrs.find(at.name);
                        if (vit == od->attrs.end() || !defined(vit->second)) continue;
                        if (at.coerce) { // `has Int() $.x`: convert what `new` got
                            if (!typeOrSubsetMatches(vit->second, at.type))
                                vit->second = coerceToType(vit->second, at.type);
                            continue;
                        }
                        const Value& v = vit->second;
                        if (v.t == VT::Hash && v.hashKind == "Proxy") continue;
                        if (typeOrSubsetMatches(v, at.type)) continue;
                        throwTypedV("X::TypeCheck::Assignment",
                                    {{"got", v}, {"expected", Value::typeObj(at.type)},
                                     {"symbol", Value::str("$!" + at.name)}},
                                    "Type check failed in assignment to $!" + at.name + "; expected " +
                                    at.type + " but got " + v.typeName() + " (" + typeCheckRepr(v) + ")");
                    }
                Value self = Value::object(od);
                // bless does not re-run BUILD-from-new args the same way, but running
                // BUILD here matches the common `self.bless(:attr(...))` usage.
                // the lambda is passed as a pointer pair, not wrapped in a
                // std::function — see BuildStep
                BuildStep reqStep{[](void* p, ClassInfo* rc) {
                                      (*static_cast<decltype(checkRequiredFor)*>(p))(rc);
                                  },
                                  &checkRequiredFor};
                runBuildChain(ci.get(), self, args, reqStep);
                maybeRegisterDestroy(self);
                return self;
            }
            // `SubDateTime.now` / `.today` — a type-level method not on the user class
            // dispatches to its built-in parent; box the result to keep the subclass.
            {
                std::string nb;
                for (ClassInfo* c = ci.get(); c && nb.empty(); c = c->parent.get()) nb = c->nativeParent;
                if ((nb == "DateTime" || nb == "Date") && !ci->findMethod(m) &&
                    m != "raku" && m != "gist" && m != "Str") {
                    Value r = methodCall(Value::typeObj(nb), m, args, rwArgs);
                    if (r.t == VT::Hash && (r.hashKind == "DateTime" || r.hashKind == "Date")) {
                        auto od = makePayload<ObjectData>(); od->cls = ci; od->hasBoxed = true; od->boxed = r;
                        // the ONE attribute walk (the stripped copy here had no
                        // `self` in scope); `.now`'s args are the built-in's, so
                        // none feed attributes
                        ValueList noAttrArgs;
                        runAttrDefaults(od, ci, noAttrArgs);
                        return Value::object(od);
                    }
                    return r;
                }
            }
            if (m == "raku") { // type-object .raku is the bare name (a pun: its display one)
                std::string d = g_typeDispName ? g_typeDispName(inv.s) : std::string();
                return Value::str(d.empty() ? inv.s.str() : d);
            }
            if (m == "gist") return Value::str(inv.gist()); // `(ShortName)` — see Value::gist
            if (m == "Str") return Value::str(""); // type objects stringify empty
        }
    }
    // exception object .throw / .fail: raise it (message from its .message method).
    // A class that defines a method of that name WINS — otherwise `$obj.throw(…)`
    // never reached the user's code and threw the invocant itself, so whatever
    // the method meant to raise was lost and CATCH received the object. Same
    // guard the `.backtrace` fallback below already uses.
    // …unless the caller asked to reach PAST the invocant's own methods, which
    // is what a `nextsame`/`nextwith` inside that very method means: Red's
    // `method throw is hidden-from-backtrace { nextwith $!orig-backtrace }`
    // redispatched onto ITSELF, 22,456 frames deep, and took every Red
    // transaction with it. (`new` learned the same lesson above.)
    if ((m == "throw" || m == "rethrow" || m == "fail") && inv.t == VT::Object && inv.obj() &&
        !(!m.skipOwn && inv.obj()->cls && inv.obj()->cls->findMethod(m))) {
        // A CONTROL exception (`class CX::Red::Bool is X::Control`) is offered
        // to the innermost CONTROL block first; `.resume` there carries on
        // right after this throw, which is how Red's what-does-it-do explores
        // both answers of a `not`/`so` over a column expression.
        if (m == "throw" && inv.obj()->cls) {
            bool control = false;
            for (ClassInfo* c = inv.obj()->cls.get(); c && !control; c = c->parent.get())
                if (c->name == "X::Control" || c->nativeParent == "X::Control" ||
                    c->doneRoles.count("X::Control")) control = true;
            if (control && runControlException(inv)) return Value::nil();
        }
        // record the backtrace at THROW time on the object itself — the thrown
        // value is shared, so a caught `$exception.backtrace` reads it back
        // (Log::Async::Context throws a fresh Exception exactly for the walk)
        throw RakuError{inv, excMessageOf(*this, inv)};
    }
    // `$exception.Failure` — the exception wrapped in a Failure, unthrown, which
    // is how a routine hands one back instead of raising it (Concurrent::Stack's
    // `X::Concurrent::Stack::Empty.new.Failure`, under the whole DB constellation).
    if (m == "Failure" && inv.t == VT::Object && inv.obj() &&
        !(inv.obj()->cls && inv.obj()->cls->findMethod("Failure"))) {
        Value f = rakuppNewFailure();
        (*f.hash())["exception"] = inv;
        (*f.hash())["message"] = Value::str(excMessageOf(*this, inv));
        return f;
    }
    // `.Str` / `.gist` on an exception object is its MESSAGE (Raku:
    // Exception.Str is .message) — strOf and gistOf already know how to read
    // one, whether it is a method or a plain attribute, and gistOf adds the
    // frames. A class with its own Str/gist still wins. CONTROL blocks read
    // `$_.Str` off a CX::Warn to collect warnings, and got the default gist.
    if ((m == "Str" || m == "gist") && args.empty() &&
        inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
        !(!m.skipOwn && inv.obj()->cls->findMethod(m))) {
        bool exc = inv.obj()->cls->name.rfind("X::", 0) == 0 ||
                   inv.obj()->cls->name.rfind("CX::", 0) == 0;
        for (ClassInfo* c = inv.obj()->cls.get(); c && !exc; c = c->parent.get())
            if (c->name == "Exception") exc = true;
        // a bare `Exception.new` / `X::AdHoc.new` has its message attribute
        // but nothing in it: those two answer Rakudo's defaults (below)
        const std::string& cn0 = inv.obj()->cls->name;
        auto mAttr = inv.obj()->attrs.find("message");
        const bool bareBuiltin = (cn0 == "Exception" || cn0 == "X::AdHoc") &&
            (mAttr == inv.obj()->attrs.end() || !rtIsDefined(mAttr->second));
        if (exc && !bareBuiltin && (inv.obj()->attrs.count("message") || inv.obj()->cls->findMethod("message"))) {
            // A REDISPATCH (`method gist { nextsame }`) must not go back through
            // the object's own Str/gist, which is exactly what gistOf/strOf
            // would do — read the message directly instead.
            if (m.skipOwn) return Value::str(excMessageOf(*this, inv));
            return Value::str(m == "Str" ? strOf(inv) : gistOf(inv));
        }
        // …and one with no message at all says so, as Rakudo's defaults do
        if (exc && bareBuiltin) {
            const std::string tn = inv.obj()->cls->name;
            if (tn == "X::AdHoc") {
                auto pl = inv.obj()->attrs.find("payload");
                return Value::str(pl != inv.obj()->attrs.end() && rtIsDefined(pl->second)
                                  ? pl->second.toStr() : std::string("Unexplained error"));
            }
            return Value::str("Something went wrong in (" + tn + ")");
        }
    }
    // `.backtrace` on an exception object: the frames its .throw recorded
    // (captured NOW for a never-thrown one). A class defining its own
    // backtrace method still wins — this only fills the built-in gap.
    if (m == "backtrace" && inv.t == VT::Object && inv.obj() &&
        !(!m.skipOwn && inv.obj()->cls && inv.obj()->cls->findMethod("backtrace"))) {
        return backtraceOf(inv);   // materializes the throw-time chain, once
    }
    // user object: dispatch to class methods / public accessors first
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) {
        auto ci = inv.obj()->cls;
        // a name the class delegates with `handles` (and does not declare itself) is
        // answered by the delegation, never by a method a composed role supplied
        Value* um0 = (m.skipOwn || (!ci->delegatedNames.empty() && ci->delegatedNames.count(m)))
                     ? nullptr : ci->findMethodForCall(m, langRev_ < 2);
        if (um0) {
            // a role's STUB method (`method body-serializer-selector() { ... }`)
            // is satisfied by the class's public attribute of the same name —
            // the accessor must win over executing the stub
            bool stubOverAttr = um0->t == VT::Code && um0->code() && um0->code()->isStub;
            if (stubOverAttr) {
                const ClassAttr* at0 = ci->findAttr(m);
                stubOverAttr = at0 && at0->pub;
            }
            // `self.new(:$name)` from inside a method, where the class writes only
            // `multi method new(Str $xml)`: with no proto those multis ADD to the
            // default constructor rather than replacing it, so a call none of them
            // takes falls through to it. (The type-object path already knows this;
            // the instance path was throwing before it got there.)
            bool passToDefault = false;
            if (m == "new" && um0->t == VT::Code && um0->code() && um0->code()->isMultiDispatcher) {
                passToDefault = true;
                for (auto& cand : um0->code()->candidates) {
                    if (cand.code() && cand.code()->isProto) { passToDefault = false; break; }
                    if (scoreCandidate(cand, args) >= 0) { passToDefault = false; break; }
                }
            }
            if (!stubOverAttr && !passToDefault)
                return invokeMethodChain(m, ci.get(), inv, args, rwArgs);
        }
        if (m == "clone") { // shallow copy, with :name(val) attribute overrides
            // `clone(*%twiddles)` takes NAMED arguments only. A positional is a
            // dispatch failure in Rakudo, not a silently ignored extra — and a
            // silently ignored one is worse than useless: `.clone($replacement)`
            // looks like it did something.
            for (auto& a : args)
                if (a.t != VT::Pair)
                    throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                        "Cannot resolve caller clone(" + inv.typeName() + ":D: " + a.typeName() +
                        (rtIsDefined(a) ? ":D" : ":U") + "); none of these signatures matches:\n"
                        "    (Mu:U $:: *%_)\n    (Mu:D $:: *%twiddles)"};
            Value nv = inv; auto ni = makePayload<ObjectData>();
            ni->cls = inv.obj()->cls; ni->attrs = inv.obj()->attrs;
            // A twiddle names a PUBLIC attribute — one with an accessor. Rakudo
            // walks `self.^attributes` and twiddles only those, so `$!private`
            // keeps the value it was cloned with (even under `is built`, which
            // opens CONSTRUCTION by name and nothing else), and an unknown name
            // is ignored rather than entering the attr store, where `$.name`
            // would find it instead of dying with X::Method::NotFound.
            // The value ASSIGNS into the attribute's container rather than
            // replacing it: `has @.a` holds an Array whatever shape the twiddle
            // had, exactly as construction does — binding the raw value left
            // `.clone(a => (1,2))` with a List that `.push` could not touch.
            for (auto& a : args) {
                if (a.t != VT::Pair) continue;
                const ClassAttr* at = ci->findAttr(a.s);
                if (!at || !at->pub) continue;
                ni->attrs[a.s] = coerceToSigil(
                    nilResetForAttr(a.pairVal() ? *a.pairVal() : Value::any(), *at), at->sigil);
            }
            nv.setObj(ni); return nv;
        }
        // a grammar INSTANCE (`Grammar.new`) parses just like the type object
        if ((m == "parse" || m == "subparse" || m == "parsefile") && (ci->isGrammar || ci->findRule("TOP")))
            return methodCall(Value::typeObj(ci->name), m, args, rwArgs);
        // `self.bless(...)` / `$obj.new(...)` on an INSTANCE builds a fresh object
        // of its class (Cro::Uri's add(): `(self ?? $!create !! Cro::Uri).bless(|%parts)`)
        if (m == "bless" || m == "new")
            return methodCall(Value::typeObj(ci->name), m, std::move(args), rwArgs);
        // A RakuAST:: node answers its OWN attributes by name, which is how
        // Rakudo's classes are written — `$stmt.expression`, `$call.name`,
        // `$block.body` — and it is how a module walks a tree it was handed
        // (`.AST.statements.head.expression` is Needle::Compile's regex needle).
        // The registry carries no ClassAttr list, so the lookup is the node's
        // own attribute map, which only ever holds syntax-bearing keys.
        if (args.empty() && isRakuAstName(ci->name)) {
            auto it = inv.obj()->attrs.find(m);
            if (it != inv.obj()->attrs.end()) return it->second;
        }
        const ClassAttr* at = ci->findAttr(m);
        if (at && at->pub) {
            auto it = inv.obj()->attrs.find(m);
            // X::AdHoc.message IS its payload stringified (Rakudo defines the
            // method that way), so `X::AdHoc.new(payload => "boom").message`
            // answers "boom" and not an undefined attribute. Only the
            // hand-built form needs it — `die "boom"` sets both.
            if ((it == inv.obj()->attrs.end() || !rtIsDefined(it->second)) &&
                m == "message" && ci->name == "X::AdHoc") {
                auto pl = inv.obj()->attrs.find("payload");
                if (pl != inv.obj()->attrs.end() && rtIsDefined(pl->second)) return Value::str(pl->second.toStr());
                return Value::str("Unexplained error");   // a payload-less X::AdHoc (Rakudo's default)
            }
            if ((it == inv.obj()->attrs.end() || !rtIsDefined(it->second)) &&
                m == "payload" && ci->name == "X::AdHoc")
                return Value::str("Unexplained error");
            return it != inv.obj()->attrs.end() ? it->second : Value::any();
        }
        // `has %.h handles <iterator list …>` — a NAMED delegation is a real method
        // on the class in Rakudo, so it outranks every universal fallback below.
        // It has to be decided here and not in the tail (where `handles *` still
        // lives): names like .iterator/.list/.Str exist for every object, so a
        // delegation of one would never be reached if the built-in answered first.
        // That is what left zef's config wrapper — `class :: { has %.hash handles
        // <… iterator list …> }` — iterating as a single opaque object.
        // A TYPE in the handles list (`has $.backend handles Backend2`) delegates
        // every method that type has.
        auto handlesType = [&](const std::string& h) {
            // …and a regex in it delegates every method whose name it matches
            if (h.size() > 4 && h.compare(0, 4, "\x01rx:") == 0)
            {   // the caller's `$/` is left as it was
                Value* sl = tctx_.cur ? tctx_.cur->find("$/") : nullptr;
                Value saved = sl ? *sl : Value::nil(); bool had = sl != nullptr;
                bool hit = regexMatch(m, h.substr(4)).t == VT::Match;
                if (had) if (Value* sl2 = tctx_.cur->find("$/")) *sl2 = saved;
                return hit;
            }
            if (h.empty() || !ascii::isupper((unsigned char)h[0])) return false;
            auto cit = classes_.find(h);
            return cit != classes_.end() && cit->second && cit->second->findMethod(m) != nullptr;
        };
        for (ClassInfo* c = ci.get(); c; c = c->parent.get())
            for (auto& a : c->attrs)
                for (size_t hi = 0; hi < a.handles.size(); hi++)
                    if (a.handles[hi] == m || handlesType(a.handles[hi])) {
                        auto ait = inv.obj()->attrs.find(a.name);
                        Value target = ait != inv.obj()->attrs.end() ? ait->second : Value::any();
                        if ((target.t == VT::Any || target.t == VT::Nil) && !a.type.empty())
                            target = Value::typeObj(a.type); // an unset typed attr delegates to its type object
                        // a RENAMING delegation calls a DIFFERENT name on the attribute:
                        // `handles(:terminal<t>)` exposes .terminal, asks the handle for .t
                        const std::string& to = hi < a.handlesTo.size() && !a.handlesTo[hi].empty()
                                                ? a.handlesTo[hi] : m;
                        return methodCall(target, to, std::move(args), rwArgs);
                    }
        // …and the same delegation written on a METHOD: `method loader is rw
        // handles <load-delegate>` answers `.load-delegate` by asking `self.loader`
        // for the object to forward to. PDF::COS publishes its whole loader API
        // that way.
        for (ClassInfo* c = ci.get(); c; c = c->parent.get()) {
            auto hit = c->methodHandles.find(m);
            if (hit == c->methodHandles.end()) continue;
            Value target = methodCall(inv, hit->second, ValueList{});
            return methodCall(target, m, std::move(args), rwArgs);
        }
        // Real-role bridge: numeric coercions/methods the class doesn't define
        // dispatch through .Bridge BEFORE the generic Cool handlers (else `.Int`
        // would numify the object itself to 0)
        static const std::set<std::string> bridgeable = {
            "Int", "Num", "Rat", "FatRat", "Numeric", "Real", "Complex", "Str", "gist",
            "abs", "floor", "ceiling", "round", "truncate", "sign", "sqrt", "succ", "pred",
            "exp", "log", "log10", "log2", "sin", "cos", "tan", "asin", "acos", "atan",
            "atan2", "sec", "cosec", "cotan", "sinh", "cosh", "tanh", "isNaN", "narrow",
            "base", "chr", "fmt"};
        if (bridgeable.count(m)) {
            if (Value* br = ci->findMethod("Bridge")) {
                Value bv = invokeMethod(*br, inv, {});
                return methodCall(bv, m, std::move(args), rwArgs);
            }
        }
        // else fall through to universal methods (.defined/.WHAT/.gist/...)
    }

    // `*.method` -> a WhateverCode that applies the method to its argument
    if (inv.t == VT::Whatever) {
        // introspection metamethods do NOT autocurry: *.WHAT is (Whatever)
        if (m == "WHAT") return Value::typeObj("Whatever");
        if (m == "HOW" || m == "WHO" || m == "VAR" || m == "WHICH" || m == "raku")
            { /* fall through to the generic paths below with the Whatever value */ }
        else {
        Value code; code.t = VT::Code; code.setCode(std::make_shared<Callable>());
        std::string mc = m; ValueList ar = args;
        code.code()->builtin = [mc, ar](Interpreter& I, ValueList& a) -> Value {
            Value arg = a.empty() ? Value::any() : a[0];
            ValueList aa = ar;
            return I.methodCall(arg, mc, aa);
        };
        return code;
        }
    }

    // Code introspection / currying
    if (inv.t == VT::Code && inv.code()) {
        // A role mixed into a routine keeps the routine's OWN identity (see
        // mixinValue), so nothing ever renamed its type and `.WHAT` still
        // answered the bare `Sub`. Rakudo answers the mixin type, and
        // Getopt::Long's `is getopt` trait tests exactly that: `&main1.WHAT
        // !=== Sub`, and a `.^name` that contains the role's name.
        if (m == "WHAT" && inv.code()->mixins.p && !inv.code()->mixins.p->roles.empty()) {
            std::string suffix;
            for (auto& rn : inv.code()->mixins.p->roles)
                suffix += (suffix.empty() ? "" : ",") + rn;
            return Value::typeObj(inv.typeName() + "+{" + suffix + "}");
        }
        // an accessor of a role mixed into this routine in place (see mixinValue):
        // `$method.precedence` after `$method does Constraint($p)`
        if (inv.code()->mixins.p && !inv.code()->mixins.p->attrs.empty() && args.empty()) {
            auto ma = inv.code()->mixins.p->attrs.find(m.s);
            if (ma != inv.code()->mixins.p->attrs.end()) return ma->second;
        }
        // `&foo.file` / `&foo.line` — where the routine was DECLARED. Rakudo
        // answers these on every Code object, and a module's EXPORT routine is
        // the common caller: Identity::Utils decides what to export by asking
        // each candidate whether its `.file` ends with the module's own path,
        // so without them the module exported nothing at all.
        if ((m == "file" || m == "line") && args.empty()) {
            if (m == "line") return Value::integer(inv.code()->declLine);
            const std::string& f = inv.code()->declFile;
            // a routine the runtime made up has no declaration site of its own;
            // Rakudo names the setting for those, and so do we
            return Value::str(f.empty() ? std::string("SETTING::src/core.c/") : f);
        }
        if (m == "assuming") { // partial application: &f.assuming(a,b)(c) == f(a,b,c)
            Value orig = inv; ValueList pre = args;
            Value code; code.t = VT::Code; code.setCode(std::make_shared<Callable>());
            code.code()->builtin = [orig, pre](Interpreter& I, ValueList& a) -> Value {
                // a `*` in the priming is a hole: the call's next positional
                // fills it, so `f.assuming(*, 2)(1)` calls `f(1, 2)`
                ValueList pos, nam;
                for (auto& x : a) (x.t == VT::Pair ? nam : pos).push_back(x);
                ValueList all; size_t pi = 0;
                for (auto& p : pre) {
                    if (p.t == VT::Whatever) { if (pi < pos.size()) all.push_back(pos[pi++]); }
                    else all.push_back(p);
                }
                for (; pi < pos.size(); pi++) all.push_back(pos[pi]);
                for (auto& n : nam) all.push_back(n); // the caller's nameds override the primed ones
                return I.callCallable(orig, all);
            };
            // residual signature: params the priming bound disappear; the rest
            // (unbound positional tail, unbound nameds, slurpies) remain
            if (inv.code()->params || inv.code()->hasPrimed) {
                ValueList posArgs; std::map<std::string, Value> namedBound;
                for (auto& a : pre) {
                    if (a.t == VT::Pair) namedBound[a.s] = a.pairVal() ? *a.pairVal() : Value::any();
                    else posArgs.push_back(a); // a `*` here is a hole, not a value
                }
                // `::T $a` primed with an Int makes every later `T` an Int
                std::map<std::string, std::string> typeBind;
                // priming args that can't bind throw right here, as Rakudo's do —
                // but only for a sub whose signature is the whole story (a body
                // using @_ / $^a placeholders takes whatever it's given)
                Value bindErr = Value::nil();
                bool checkBind = !inv.code()->usesArgs && inv.code()->placeholders.empty();
                std::set<std::string> namedSeen; bool posSlurpy = false, namedSlurpy = false;
                // priming an already-primed sub re-primes its residual signature
                std::vector<const Param*> src;
                if (inv.code()->hasPrimed) for (auto& sp : inv.code()->primedParams) src.push_back(sp.get());
                else if (inv.code()->params) for (auto& p : *inv.code()->params) src.push_back(&p);
                size_t pos = 0;
                code.code()->hasPrimed = true;
                for (const Param* pp : src) {
                    const Param& p = *pp;
                    if (p.invocant) continue;
                    auto keep = [&]() -> Param& {
                        code.code()->primedParams.push_back(signatureParamCopy(p));
                        Param& q = *code.code()->primedParams.back();
                        auto tb = typeBind.find(q.type);
                        if (tb != typeBind.end()) { q.type = tb->second; q.typeCapture = false; }
                        return q;
                    };
                    if (p.named) {
                        // any alias layer of `:b(:c($a))` can be the one primed
                        std::vector<std::string> keys = p.aliasKeys;
                        keys.push_back(!p.namedKey.empty() ? p.namedKey
                                     : p.name.size() > 1 ? p.name.substr(1) : p.name);
                        if (p.aliasBoth && p.name.size() > 1) keys.push_back(p.name.substr(1));
                        auto it = namedBound.end();
                        for (auto& k : keys) {
                            namedSeen.insert(k);
                            if (it == namedBound.end()) it = namedBound.find(k);
                        }
                        if (it == namedBound.end()) { keep(); continue; }
                        // a primed named param stays bindable — the caller may
                        // override it — but is now optional, defaulting to the
                        // primed value
                        Param& q = keep();
                        q.required = false; q.optional = true;
                        q.defaultRaku = rakuRepr(it->second);
                    }
                    else if (p.slurpy) { // a `|c` capture takes both kinds
                        if (p.sigil != '%') posSlurpy = true;
                        if (p.sigil == '%' || p.sigil == '\\' || p.sigil == '|') namedSlurpy = true;
                        keep();
                    }
                    else if (pos < posArgs.size()) {
                        const Value& av = posArgs[pos++];
                        if (av.t == VT::Whatever) { keep(); continue; } // hole: still unbound
                        if (p.typeCapture && !p.type.empty()) typeBind[p.type] = av.typeName();
                        // `Int @a` constrains the ELEMENTS, not the Array — only a
                        // scalar param's type checks against the argument itself
                        else if (!p.type.empty() && !p.coerce && p.sigil == '$' &&
                                 checkBind && bindErr.t == VT::Nil) {
                            auto tb = typeBind.find(p.type); // a bound `::T` checks as its type
                            const std::string& want = tb != typeBind.end() ? tb->second : p.type;
                            if (!typeOrSubsetMatches(av, want))
                                bindErr = makeTypedEx("X::TypeCheck::Binding::Parameter",
                                    {{"expected", Value::typeObj(want)}, {"got", Value::typeObj(av.typeName())},
                                     {"symbol", Value::str(p.name)}},
                                    "Type check failed in binding " + p.name + "; expected " + want +
                                    " but got " + av.typeName());
                        }
                    }
                    else keep();
                }
                // Rakudo doesn't throw when a priming can't bind — it mixes a
                // Failure into the returned Code, so the call site sees it
                if (checkBind && bindErr.t == VT::Nil && pos < posArgs.size() && !posSlurpy)
                    bindErr = makeTypedEx("X::AdHoc", {},
                        "Too many positionals passed; expected " + std::to_string(pos) +
                        " arguments but got " + std::to_string(posArgs.size()));
                if (checkBind && bindErr.t == VT::Nil && !namedSlurpy)
                    for (auto& kv : namedBound)
                        if (!namedSeen.count(kv.first)) {
                            bindErr = makeTypedEx("X::AdHoc", {},
                                "Unexpected named argument '" + kv.first + "' passed");
                            break;
                        }
                if (bindErr.t != VT::Nil)
                    throw RakuError{bindErr, bindErr.obj() && bindErr.obj()->attrs.count("message")
                                             ? bindErr.obj()->attrs["message"].toStr() : "binding failed"};
            }
            return code;
        }
        // `.assuming` leaves the RESIDUAL signature in primedParams and `params`
        // null, so both counters below fell through to `placeholders` (empty) and
        // answered 0 for every primed routine — where Rakudo answers the real
        // number. `.signature` was right the whole time, because makeSignature
        // already prefers primedParams (Builtins.cpp, "a .assuming wrapper carries
        // its residual params"), so the engine disagreed with ITSELF: `&k.arity`
        // said 0 while `&k.signature.arity` said 1. Same source for both now.
        auto countedParams = [&](std::vector<const Param*>& out) {
            const Callable* c = inv.code();
            if (c->hasPrimed) { for (auto& sp : c->primedParams) out.push_back(sp.get()); }
            else if (c->params) for (auto& p : *c->params) out.push_back(&p);
        };
        // a multi's arity/count are its PROTO's, which `.signature` renders
        if ((m == "arity" || m == "count") && inv.code()->isMultiDispatcher) {
            Value sig = methodCall(inv, "signature", ValueList{});
            if (sig.t == VT::Hash && sig.hash() && sig.hash()->count(m)) return (*sig.hash())[m];
        }
        if (m == "arity") {
            if (inv.code()->isWhateverCode) return Value::integer(std::max(1LL, inv.code()->whateverArity));
            std::vector<const Param*> ps; countedParams(ps);
            long long n = 0;
            // `hasPrimed || params` is "this routine has a parameter list at all".
            // A primed routine whose residual list is EMPTY must answer 0, not fall
            // through to the placeholder count.
            // (a placeholder block carries an EMPTY param list — its arity is
            // the placeholders' count)
            const bool byPlaceholders = !inv.code()->hasPrimed && !inv.code()->placeholders.empty() &&
                                        (!inv.code()->params || inv.code()->params->empty());
            if (!byPlaceholders && (inv.code()->hasPrimed || inv.code()->params)) {
                for (const Param* p : ps) if (!p->slurpy && !p->named && !p->optional) n++;
            }
            else n = (long long)inv.code()->placeholders.size();
            return Value::integer(n);
        }
        if (m == "count") { // required + optional positionals; a slurpy makes it Inf
            if (inv.code()->isWhateverCode) return Value::integer(std::max(1LL, inv.code()->whateverArity));
            std::vector<const Param*> ps; countedParams(ps);
            long long n = 0; bool slurpy = false;
            const bool byPlaceholders = !inv.code()->hasPrimed && !inv.code()->placeholders.empty() &&
                                        (!inv.code()->params || inv.code()->params->empty());
            if (!byPlaceholders && (inv.code()->hasPrimed || inv.code()->params)) for (const Param* pp : ps) {
                const Param& p = *pp;
                if (p.named) continue;
                // `*%opts` slurps NAMED arguments and accepts no positional at
                // all, so it does not make the count Inf — only *@ / **@ / +@ do.
                // (Every method carries an implicit one, so this reached far past
                // the signatures that write it: Path::Finder decides how to build
                // a filter's Capture from `signature.count`, and Inf sent every
                // one-argument filter down the many-arguments branch.)
                if (p.slurpy && p.sigil == '%') continue;
                if (p.slurpy) slurpy = true; else n++;
            } else n = (long long)inv.code()->placeholders.size();
            return slurpy ? Value::number(std::numeric_limits<double>::infinity()) : Value::integer(n);
        }
        // `&f.callwith(…)` calls it; `&f.nextwith(…)` calls it and RETURNS that
        // from the routine we are in, as a tail call (roast S04-statements/goto.t)
        if (m == "callwith" || m == "nextwith") {
            ValueList ca;
            for (auto& x : args) ca.push_back(x);
            Value r = callCallable(inv, ca);
            if (m == "callwith") return r;
            throw ReturnEx{r};
        }
        if (m == "name") {
            // An operator's name quotes its op the way Rakudo spells it: `<op>`
            // normally, `«op»` when the op holds `<`/`>` — unless it also holds
            // something «» would treat specially (a guillemet, a sigil), and
            // then `<op>` again with the brackets backslashed: infix:«>=»,
            // infix:<~~\>»>, infix:<$\>>.
            const std::string nm = inv.code()->name;
            size_t colon = nm.find(":<");
            if (colon != std::string::npos && colon > 0 && nm.size() > colon + 3 && nm.back() == '>' &&
                nm.find(':') == colon) {
                std::string op = nm.substr(colon + 2, nm.size() - colon - 3);
                if (op.find_first_of("<>") != std::string::npos) {
                    bool special = op.find("\xC2\xAB") != std::string::npos ||
                                   op.find("\xC2\xBB") != std::string::npos ||
                                   op.find_first_of("$@%&") != std::string::npos;
                    std::string out = nm.substr(0, colon + 1);
                    if (!special) out += "\xC2\xAB" + op + "\xC2\xBB";
                    else {
                        out += "<";
                        for (char ch : op) { if (ch == '<' || ch == '>') out += '\\'; out += ch; }
                        out += ">";
                    }
                    return Value::str(out);
                }
            }
            return Value::str(nm);
        }
        // `&code.has-loop-phasers` / `&code.callable_for_phaser('FIRST')` — rak
        // runs a pattern's FIRST/NEXT/LAST phasers itself, around its own loop:
        // the phaser block becomes a Callable closing over the pattern's scope
        if (m == "has-loop-phasers" || m == "callable_for_phaser") {
            const std::vector<StmtPtr>* body = inv.code()->body;
            const std::string want = (m == "callable_for_phaser" && !args.empty()) ? args[0].toStr() : "";
            Block* found = nullptr; bool any = false;
            if (body)
                for (auto& s : *body)
                    if (s && s->kind == NK::Block) {
                        auto* b = static_cast<Block*>(s.get());
                        if (b->phaser == "FIRST" || b->phaser == "NEXT" || b->phaser == "LAST") {
                            any = true;
                            if (!found && b->phaser == want) found = b;
                        }
                    }
            if (m == "has-loop-phasers") return Value::boolean(any);
            if (!found) return Value::nil();
            static std::vector<Param> noParams;
            Value code; code.t = VT::Code; code.setCode(std::make_shared<Callable>());
            code.code()->params = &noParams;
            code.code()->body = &found->stmts;
            code.code()->closure = inv.code()->closure;
            code.code()->langRev = inv.code()->langRev;
            code.code()->declFile = inv.code()->declFile;
            code.code()->isBlock = true;
            return code;
        }
        if (m == "returns" || m == "of")
            return inv.code()->retType.empty() ? Value::typeObj("Mu")
                                              : Value::typeObj(retTypeName(inv.code()->retType));
        if (m == "signature") return makeSignature(inv.code());
        if (m == "yada") return Value::boolean(inv.code()->isStub);   // a `{ ... }` / `{ !!! }` body
        if (m == "multi" || m == "is_dispatcher") return Value::boolean(inv.code()->isMultiDispatcher);
        // `$block.ACCEPTS($x)` is `$block($x)` — a Callable used as a matcher
        // (paths' `$!dir-matcher.ACCEPTS($name)` is how every directory is
        // filtered, and the default matcher is a pointy block)
        if (m == "ACCEPTS" && !args.empty()) {
            ValueList one{args[0]};
            return callCallable(inv, std::move(one));
        }
        // `&proto.add_dispatchee(&candidate)` — hang one more candidate on a
        // dispatch group at run time. The Callable is shared with every name
        // that holds it, so a `MAIN` proto in the caller's scope sees the
        // candidate at once; CLI::Version adds its `--version` handler this way.
        // A one-candidate group (what `my multi sub MAIN(…) {…}` in expression
        // position evaluates to) contributes its candidates.
        if (m == "add_dispatchee" && !args.empty() && args[0].t == VT::Code && args[0].code()) {
            auto disp = inv.code();
            disp->isMultiDispatcher = true;
            auto add = [&](const Value& c) {
                if (!c.code()) return;
                // already in the group — a multi that attached itself at its
                // declaration, possibly reaching here as a CLONE of that
                // Callable, so the declaration (body + signature) is the identity
                for (auto& have : disp->candidates)
                    if (have.code() && (have.code() == c.code() ||
                                        (have.code()->body && have.code()->body == c.code()->body &&
                                         have.code()->params == c.code()->params)))
                        return;
                c.code()->isMultiCandidate = true;
                disp->candidates.push_back(c);
            };
            if (args[0].code()->isMultiDispatcher) for (auto& c : args[0].code()->candidates) add(c);
            else add(args[0]);
            return inv;
        }
        if (m == "candidates") {
            Value out = Value::array(); out.isList = true;
            if (inv.code()->isMultiDispatcher) {
                for (auto& c : inv.code()->candidates)
                    // the group's own `proto … {*}` is the dispatcher, not a
                    // candidate: Rakudo lists the multis only
                    // (with or without a body of its own)
                    if (!(c.code() && (c.code()->isProto || c.code()->isProtoBody)))
                        out.arr()->push_back(c);
            }
            else out.arr()->push_back(inv);
            return out;
        }
        // &candidate.dispatcher — the proto its dispatch group hangs off.
        // Carried by synthesized groups (&trait_mod:<is>); a dispatcher
        // answers itself, and a plain sub answers Mu, as in Rakudo.
        if (m == "dispatcher") {
            if (inv.code()->dispatcherC) {
                Value d; d.t = VT::Code; d.setCode(inv.code()->dispatcherC);
                return d;
            }
            if (inv.code()->isMultiDispatcher) return inv;
            return Value::typeObj("Mu");
        }
        // &routine.wrap(&wrapper): push a wrapper in front of the routine. Because
        // the Callable is shared (shared_ptr), every reference — including calls
        // through the routine's name — sees the wrap. Returns a handle for .unwrap.
        if (m == "wrap" && !args.empty()) {
            inv.code()->wrappers.push_back(args[0]);
            noteSymbolMutation("routine .wrap");
            Value h = Value::makeHash(); h.hashKind = "WrapHandle";
            (*h.hash())["routine"] = inv;               // keep the Callable alive
            (*h.hash())["wrapper"] = args[0];           // identity for targeted .unwrap
            return h;
        }
        // &routine.unwrap($handle) / .unwrap — remove a wrapper. With a handle, remove
        // that specific wrapper; otherwise pop the most-recent one (LIFO).
        if (m == "unwrap") {
            auto& ws = inv.code()->wrappers;
            // the argument must be the handle `.wrap` returned
            if (!args.empty() && !(args[0].t == VT::Hash && args[0].hashKind == "WrapHandle"))
                throwTypedV("X::Routine::Unwrap", {},
                            "Cannot unwrap routine: invalid wrap handle");
            if (!args.empty() && args[0].t == VT::Hash && args[0].hashKind == "WrapHandle" &&
                args[0].hash()->count("wrapper")) {
                const Value& target = (*args[0].hash())["wrapper"];
                for (size_t k = ws.size(); k-- > 0; )
                    if (ws[k].code() == target.code()) { ws.erase(ws.begin() + k); break; }
            }
            else if (!ws.empty()) ws.pop_back();
            noteSymbolMutation("routine .unwrap");
            return inv;
        }
    }

    // $handle.restore — undo the wrap this WrapHandle came from (sugar for
    // &routine.unwrap($handle))
    if (inv.t == VT::Hash && inv.hashKind == "WrapHandle" && m == "restore") {
        Value routine = inv.hash()->count("routine") ? (*inv.hash())["routine"] : Value();
        if (routine.t == VT::Code && routine.code()) {
            ValueList one{inv};
            return methodCall(routine, "unwrap", one);
        }
        return Value::boolean(false);
    }
    // CompUnit::DependencySpecification accessors.
    if (inv.t == VT::Hash && inv.hashKind == "DependencySpec") {
        if (m == "short-name" || m == "version-matcher" || m == "auth-matcher" || m == "api-matcher")
            return inv.hash()->count(m) ? (*inv.hash())[m] : Value::any();
    }
    // IO::Socket::INET connection/listener methods.
    if (inv.t == VT::Hash && inv.hashKind == "Socket") {
        int fd = (int)(*inv.hash())["fd"].toInt();
        if (m == "accept") {
            bool p = gilPark(); int cfd = ::accept(fd, nullptr, nullptr); gilUnpark(p);
            if (cfd < 0) return Value::nil();
            Value s = Value::makeHash(); s.hashKind = "Socket"; (*s.hash())["fd"] = Value::integer(cfd);
            return s;
        }
        // The port/address this socket is actually bound to. Asked of a listener
        // opened on `:localport(0)`, which is how a test gets a free port without
        // guessing one — the answer is only knowable after bind, from the OS.
        if (m == "localport" || m == "localhost" || m == "peerport" || m == "peerhost") {
            sockaddr_in sa{};
            socklen_t sl = sizeof(sa);
            bool peer = m[0] == 'p';
            int rc = peer ? ::getpeername(fd, (sockaddr*)&sa, &sl)
                          : ::getsockname(fd, (sockaddr*)&sa, &sl);
            if (rc < 0) return Value::nil();
            if (m == "localport" || m == "peerport") return Value::integer(ntohs(sa.sin_port));
            auto given = inv.hash()->find(m);   // the name as the caller wrote it
            if (given != inv.hash()->end()) return given->second;
            char buf[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
            return Value::str(buf);
        }
        if (m == "recv" || m == "read") {
            size_t want = 65536;
            if (!args.empty() && args[0].isNumeric()) {
                // `Inf` is a real argument here, not a mistake: it means "whatever
                // has arrived". OpenSSL's bio-read calls `$.net-read.()`, whose
                // closure defaults to `-> $n = Inf { $s.recv($n, :bin) }`, so the
                // very first read of an SSL handshake asked for infinity — and
                // sizing the buffer from it threw std::bad_alloc before the socket
                // was touched. Anything absurd is capped for the same reason; recv
                // is "up to" that many bytes, so a smaller buffer is still correct.
                static const size_t kCap = 64u * 1024 * 1024;
                double d = args[0].toNum();
                if (std::isfinite(d) && d >= 0) want = (size_t)std::min<double>(d, (double)kCap);
            }
            // `:bin` asks for BYTES, and `.read` is always binary — both must answer a
            // Buf, not a Str. Returning a Str made `Buf.new.append($chunk)` append
            // the string instead: it compiles, runs, and simply never matches, which
            // is how an HTTP header loop can spin forever with no error anywhere.
            bool bin = (m == "read");
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "bin") bin = !a.pairVal() || a.pairVal()->truthy();
            std::vector<char> buf(want ? want : 1);
            size_t got = 0;
            bool p = gilPark();
            // `.read($n)` answers EXACTLY $n bytes, blocking until it has them or
            // the peer closes; `.recv($n)` answers at most $n and returns what has
            // arrived. One recv() served both, so a reader that asked for a fixed
            // number got however much happened to be in the first packet — which is
            // silent until a message straddles a packet boundary, and then a chunked
            // HTTP body fails to parse its own chunk header.
            while (got < buf.size()) {
                ssize_t n = ::recv(fd, buf.data() + got, buf.size() - got, 0);
                if (n <= 0) { if (n < 0 && got == 0) { gilUnpark(p); return Value::nil(); } break; }
                got += (size_t)n;
                if (m != "read") break;   // recv is "up to", not "exactly"
            }
            gilUnpark(p);
            Value r = Value::str(std::string(buf.data(), got)); // got==0 => "" (peer closed)
            if (bin) { r.hashKind = "Buf"; identify(r); }
            return r;
        }
        // `.get` — one line, buffered: bytes are pulled as they arrive, and the
        // carry-over between calls lives in the handle ("linebuf"). A socket's
        // nl-in is ["\n", "\r\n"], so the terminator is '\n' with an optional
        // '\r' before it, both consumed and neither returned; Nil at EOF with
        // nothing buffered. LWP::Simple's local test servers read the request
        // this way — `Nil while $client.get.chars` — and without a socket .get
        // the call fell through to the FILE handle's reader and blocked both
        // ends of the conversation. (A later .recv does not see linebuf: the
        // suites read lines on one side of a socket and bytes on the other,
        // never both on one side.)
        if (m == "get" || m == "lines") {
            Value& lbv = (*inv.hash())["linebuf"];
            if (lbv.t != VT::Str) lbv = Value::str("");
            auto getOne = [&](bool& eof) -> Value {
                for (;;) {
                    std::string& lb = lbv.s.mut();
                    size_t nl = lb.find('\n');
                    if (nl != std::string::npos) {
                        std::string line = lb.substr(0, nl);
                        lb.erase(0, nl + 1);
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        return Value::str(line);
                    }
                    char buf[8192];
                    bool p = gilPark();
                    ssize_t n = ::recv(fd, buf, sizeof buf, 0);
                    gilUnpark(p);
                    if (n <= 0) {
                        eof = true;
                        if (lb.empty()) return Value::nil();
                        std::string line = std::move(lb);
                        lb.clear();
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        return Value::str(line);
                    }
                    lb.append(buf, (size_t)n);
                }
            };
            bool eof = false;
            if (m == "get") return getOne(eof);
            Value out = Value::array(); out.isList = true;
            while (!eof) {
                Value l = getOne(eof);
                if (l.t == VT::Nil) break;
                out.arr()->push_back(l);
            }
            return out;
        }
        if (m == "print" || m == "write" || m == "send" || m == "put" || m == "say") {
            std::string data = args.empty() ? "" : (m == "say" ? gistOf(args[0]) : args[0].toStr()); // Blob is a byte-Str
            if (m == "put" || m == "say") data += "\n"; // `.say` used to fall to the universal arm and print the socket's gist to stdout
            size_t off = 0;
            bool p = gilPark();
            while (off < data.size()) { ssize_t n = ::send(fd, data.data() + off, data.size() - off, 0); if (n <= 0) break; off += (size_t)n; }
            gilUnpark(p);
            return Value::boolean(true);
        }
        if (m == "close") { if (fd >= 0) ::close(fd); (*inv.hash())["fd"] = Value::integer(-1); return Value::boolean(true); }
    }

    if (inv.t == VT::Range && (m == "pick" || m == "roll") && inv.big()) {
        // a Range with a BIG upper endpoint (`^(2**100)`): uniform BigInt draws
        // in [rFrom, bound) by limb-wise rejection sampling
        BigInt bound = *inv.big();
        if (!inv.rExTo()) bound = bound + BigInt(1);
        BigInt span = bound - BigInt(inv.rFrom());
        if (span.sign > 0) {
            auto draw = [&]() -> Value {
                const auto& sm = span.mag;
                BigInt c;
                for (;;) {
                    c.mag.assign(sm.size(), 0);
                    for (size_t k = 0; k + 1 < sm.size(); k++) c.mag[k] = (uint32_t)(randDouble() * 1e9);
                    c.mag.back() = (uint32_t)(randDouble() * ((double)sm.back() + 1)); // top limb ≤ span's top
                    c.sign = 1; c.trim();
                    if (BigInt::cmpMag(c, span) < 0) break;
                }
                return Value::bigint(c + BigInt(inv.rFrom()));
            };
            if (args.empty()) return draw();
            bool all = args[0].t == VT::Whatever || (args[0].isNumeric() && std::isinf(args[0].toNum()));
            long long n = all ? 0 : args[0].toInt(); // pick(*) over an astronomic range is degenerate
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            if (m == "pick") {
                std::set<std::string> seen; // distinct draws, keyed by decimal form
                while ((long long)out.arr()->size() < n) {
                    Value v = draw();
                    if (seen.insert(v.toStr()).second) out.arr()->push_back(v);
                }
            }
            else for (long long i = 0; i < n; i++) out.arr()->push_back(draw());
            return out;
        }
    }
    if (inv.t == VT::Range && (m == "pick" || m == "roll")) {
        long long lo = inv.rFrom(), hi = inv.rTo();
        // integer spans sample directly — flattening ^2**40 (or ^2**20, 200 times) hangs
        if (hi >= lo && (unsigned long long)(hi - lo) >= 1024) {
            unsigned long long span = (unsigned long long)(hi - lo) + 1; // 0 == full 64-bit width
            auto draw = [&]() -> long long {
                unsigned long long r = ((unsigned long long)(randDouble() * 4294967296.0) << 32)
                                     | (unsigned long long)(randDouble() * 4294967296.0);
                return lo + (long long)(span ? r % span : r);
            };
            if (args.empty()) return Value::integer(draw());
            bool all = args[0].t == VT::Whatever ||
                       (args[0].isNumeric() && std::isinf(args[0].toNum()));
            // pick(*) shuffles the whole range when that is sane; a 2**64 request is degenerate
            long long n = all ? (span && span <= (1ULL << 22) ? (long long)span : 0) : args[0].toInt();
            if (m == "pick" && span && (unsigned long long)n > span) n = (long long)span;
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            if (m == "pick") {
                std::set<long long> seen;
                while ((long long)out.arr()->size() < n) {
                    long long v = draw();
                    if (seen.insert(v).second) out.arr()->push_back(Value::integer(v));
                }
            }
            else for (long long i = 0; i < n; i++) out.arr()->push_back(Value::integer(draw()));
            return out;
        }
    }
    // universal
    bool isFH = (inv.t == VT::Hash && inv.hashKind == "FileHandle");
    // An object deriving the BUILTIN IO::Handle is a handle, not a Mu that
    // prints itself: $cap.say("x") writes "x" through its WRITE sink, not
    // the object's own gist. Fall through to the handle-protocol shim.
    bool isUserHandle = false;
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) {
        std::string nb;
        for (ClassInfo* c = inv.obj()->cls.get(); c && nb.empty(); c = c->parent.get())
            nb = c->nativeParent;
        isUserHandle = nb == "IO::Handle";
    }
    // An IO::Handle-derived class inherits the line-ending accessors as real,
    // WRITABLE state: `$io.nl-out = "\t\t"` then `self.nl-out` is what its own
    // `.say` appends (IO::Blob, which 14 dists name, tests exactly that). The
    // Any fallback answers the constant "\n", which no assignment can move.
    if (isUserHandle && (m == "nl-out" || m == "nl-in") && inv.obj()) {
        auto it = inv.obj()->attrs.find(m);
        if (!args.empty()) { inv.obj()->attrs[m] = args[0]; return args[0]; }
        if (it != inv.obj()->attrs.end()) return it->second;
        if (m == "nl-out") return Value::str("\n");
        Value d = Value::array();                 // nl-in: Rakudo's default pair
        d.arr()->push_back(Value::str("\n"));
        d.arr()->push_back(Value::str("\r\n"));
        d.itemized = true;
        return d;
    }
    // …and `.chomp` is handle state too, defaulting to True. Without this it
    // reached Str.chomp, which stringifies the HANDLE and trims a newline off
    // its gist — Text::CSV's `my Bool $chomped = $io.chomp` then type-checked
    // a Str against Bool.
    if (isUserHandle && m == "chomp" && inv.obj()) {
        auto it = inv.obj()->attrs.find(m);
        if (!args.empty()) { inv.obj()->attrs[m] = args[0]; return args[0]; }
        return it != inv.obj()->attrs.end() ? it->second : Value::boolean(true);
    }
    if (m == "say" && !isFH && !isUserHandle) return ioEmit(gistOf(inv) + "\n", "$*OUT", false);
    if (m == "print" && !isFH && !isUserHandle) return ioEmit(strOf(inv), "$*OUT", false);
    if (m == "put" && !isFH && !isUserHandle) return ioEmit(strOf(inv) + "\n", "$*OUT", false); // a FileHandle's put is Part3's (this arm printed the HANDLE)
    if (m == "note") return ioEmit(gistOf(inv) + "\n", "$*ERR", true);
    if (m == "Str" || (inv.t == VT::Type && m == "Stringy")) {
        // type objects stringify empty (with a warning in Rakudo) — but
        // IterationEnd is a SENTINEL, and stringifies to its own name
        if (inv.t == VT::Type) return Value::str(inv.s == "IterationEnd" ? inv.s.str() : std::string());
        // An ENDLESS lazy list stringifies its REIFIED PREFIX and marks the
        // rest: `my @a = 1..*; @a[2]; ~@a` is "1 2 3 ...", and one nothing has
        // pulled from is just "..." (sheet LA-02). The generic path below
        // passed the prefix off as the whole list.
        if (inv.t == VT::Array && inv.arr() && inv.ext() &&
            std::static_pointer_cast<LazySeqState>(inv.ext())->infinite) {
            std::string out;
            for (auto& e : *inv.arr()) { out += e.toStr(); out += ' '; }
            return Value::str(out + "...");
        }
        // `Int.Str(:superscript)` / `(:subscript)` render the digits (and a leading
        // minus) in the Unicode super/subscript forms. Note ¹²³ are NOT in the
        // U+2070 run — a `0x2070 + d` table is wrong for exactly those three.
        if (inv.t == VT::Int) {
            static const char* sup[] = {"⁰","¹","²","³","⁴",
                                        "⁵","⁶","⁷","⁸","⁹"};
            static const char* sub[] = {"₀","₁","₂","₃","₄",
                                        "₅","₆","₇","₈","₉"};
            bool up = false, down = false;
            for (auto& a : args)
                if (a.t == VT::Pair && a.pairVal() && a.pairVal()->truthy()) {
                    if (a.s == "superscript") up = true;
                    else if (a.s == "subscript") down = true;
                }
            if (up || down) {
                std::string out;
                for (char c : inv.toStr()) {
                    if (c == '-') out += up ? "⁻" : "₋";
                    else if (c >= '0' && c <= '9') out += (up ? sup : sub)[c - '0'];
                    else out += c;
                }
                return Value::str(out);
            }
        }
        return Value::str(inv.toStr());
    }
    if ((m == "Int" || m == "Num" || m == "Real" || m == "Rat" || m == "FatRat") && inv.t == VT::Complex) {
        // Complex → Real conversions need |im| within $*TOLERANCE (default 1e-15),
        // so Num(exp i*π) works but a tightened tolerance throws (X::Numeric::Real)
        double tol = toleranceDyn();
        // relative to the real part (see the twin in evalBinary's `<=>` arm); a
        // zero real part has nothing to scale by and uses the bare tolerance
        if (std::fabs(inv.im()) > tol * (inv.n == 0.0 ? 1.0 : std::fabs(inv.n)))
            throwTypedV("X::Numeric::Real",
                        {{"target", Value::typeObj(m)}, {"source", inv},
                         {"reason", Value::str("imaginary part not zero")}},
                        "Cannot convert " + cnum::to_string(inv.n) + (inv.im() < 0 ? "" : "+") +
                        cnum::to_string(inv.im()) + "i to " + m + ": imaginary part not zero");
        Value re = Value::number(inv.n);
        if (m == "Int") return Value::integer((long long)inv.n);
        if (m == "Rat" || m == "FatRat") return methodCall(re, m, {});
        return re; // Num / Real
    }
    if (inv.t == VT::Complex && (m == "floor" || m == "ceiling" || m == "round" || m == "truncate")) {
        // A Complex rounds PER COMPONENT, and `.round($scale)` rounds to a multiple
        // of the scale — `(1.256-3.875i).round(0.1)` is 1.3-3.9i. The scale used to
        // be dropped here. Each component goes through the scalar path rather than
        // repeating the arithmetic: that one is exact (Rat), and doing it again in
        // doubles gave -3.9000000000000004 for the imaginary part.
        auto f = [&](double x) {
            ValueList a2 = args;
            return methodCall(numifyStr(Value::number(x).toStr()), m, a2, nullptr).toNum();
        };
        return Value::complex(f(inv.n), f(inv.im()));
    }
    if (m == "Int") {
        // ±Inf / NaN cannot convert to Int — a FAILURE, not a throw, exactly
        // like the zero-denominator Rat below it. Rakudo hands back an
        // undefined X::Numeric::CannotConvert that only detonates when someone
        // uses it, so `my $n = NaN.Int` is a live statement and `$n.defined` is
        // False. Throwing killed the program instead: Text::CSV's `method
        // Numeric` coerces `.unival` (NaN for anything but a numeral) and
        // reports the Failure as the field's numeric value.
        if (inv.t == VT::Num && !std::isfinite(inv.n)) {
            Value f = rakuppNewFailure();
            (*f.hash())["exception"] = Value::typeObj("X::Numeric::CannotConvert");
            (*f.hash())["message"]   = Value::str("Cannot convert " + inv.toStr() + " to Int");
            return f;
        }
        // a zero-denominator Rat FAILS on Int coercion (a Failure, not a throw —
        // fails-like requires the returned unhandled Failure)
        if (inv.t == VT::Rat && inv.ratD() && inv.ratD()->isZero()) {
            Value f = rakuppNewFailure();
            (*f.hash())["exception"] = Value::typeObj("X::Numeric::DivideByZero");
            return f;
        }
        // Converting a string that carries an Nl/No numeral (Roman, circled,
        // Tamil ௰, …) is not supported — only Nd digits are numeric. (X::Str::Numeric)
        if (inv.t == VT::Str)
            for (size_t bi = 0; bi < inv.s.size(); ) {
                unsigned char b0 = inv.s[bi];
                int len = b0 < 0x80 ? 1 : b0 >= 0xF0 ? 4 : b0 >= 0xE0 ? 3 : 2;
                uint32_t cp = b0 < 0x80 ? b0 : (b0 & (0xFF >> (len + 1)));
                for (int k = 1; k < len && bi + k < inv.s.size(); k++) cp = (cp << 6) | ((unsigned char)inv.s[bi + k] & 0x3F);
                bi += len;
                if (cp >= 0x80) { std::string gc = uniGeneralCategory(cp);
                    if (gc == "Nl" || gc == "No")
                        throw RakuError{Value::typeObj("X::Str::Numeric"), "Cannot convert string to number: a numeral in category '" + gc + "' is not a digit"}; }
            }
        // A string / match text wider than int64 must stay EXACT — route through
        // the BigInt-aware parse, not the lossy long-long toInt() (which returns 0
        // on overflow). Int stays Int; Rat/Num truncate toward zero.
        if (inv.t == VT::Str || inv.t == VT::Match) {
            Value nv = numifyStrFailure(inv.toStr()); // a non-number is a Failure, like `+$str`
            if (nv.t == VT::Hash && nv.hashKind == "Failure") return nv;
            if (nv.t == VT::Int) return nv;
            if (nv.t == VT::Rat || nv.t == VT::Num) return methodCall(nv, "Int", ValueList{});
        }
        // A Range is a Cool, and a Cool container numifies to its ELEMENT COUNT:
        // `(1..5).Int` is 5, not 0. Value::toInt has no way to count one (a Str
        // range and an endless one both need the interpreter), so the coercion
        // methods ask .elems.
        if (inv.t == VT::Range) {
            double sp;
            if (rangeNumericSpecial(inv, sp))
                return std::isfinite(sp) ? Value::integer((long long)sp) : Value::number(sp);
            ValueList none; return methodCall(inv, "elems", none);
        }
        // …and an Int that outgrew long long stays exact: toInt() saturates, so
        // `(2**64).Int` answered 9223372036854775807.
        if (inv.t == VT::Int && inv.big()) return Value::bigint(*inv.big());
        if (inv.t == VT::Num) return numToIntExact(std::trunc(inv.n)); // exact past 2**63 (`1e19.Int` saturated) — L2 F13
        return Value::integer(inv.toInt());
    }
    if (m == "isNaN") {
        if (inv.t == VT::Num) return Value::boolean(std::isnan(inv.n));
        if (inv.t == VT::Rat) return Value::boolean(inv.ratD() && inv.ratD()->isZero() && inv.ratN() && inv.ratN()->isZero()); // 0/0
        if (inv.t == VT::Int || inv.t == VT::Bool) return Value::boolean(false);
    }
    if (m == "Num") {
        if ((inv.t == VT::Str && inv.hashKind.empty() && !inv.isAllomorph()) || inv.t == VT::Match) {
            Value nv = numifyStrFailure(inv.toStr());
            if (nv.t == VT::Hash && nv.hashKind == "Failure") return nv;
        }
        if (inv.t == VT::Range) {
            double sp;
            if (rangeNumericSpecial(inv, sp))
                return std::isfinite(sp) ? Value::integer((long long)sp) : Value::number(sp);
            ValueList none; return Value::number(methodCall(inv, "elems", none).toNum());
        }
        return Value::number(inv.toNum());
    }
    if (m == "Numeric" && inv.t == VT::Complex) { // a Complex is Numeric already (this arm numified it to Num 0)
        if (inv.isAllomorph()) { Value n = inv; n.hashKind.clear(); n.s.clear(); return n; } // ComplexStr sheds — see below
        return inv;
    }
    if (m == "Numeric" || m == "Real") {
        // a string numifies via the type-preserving ladder ("1"->Int, "1.5"->Rat,
        // "1e0"->Num), like `+$str` — and a non-number is that same Failure.
        if (inv.t == VT::Str) return numifyStrFailure(inv.s);
        if (inv.t == VT::Match) return numifyStrFailure(inv.toStr());
        // an already-numeric value is ITSELF: `3.Numeric` is an Int and `(-4/3).Real`
        // a Rat. Going through toNum() forced everything to Num.
        if (inv.t == VT::Int || inv.t == VT::Rat || inv.t == VT::Num) {
            // …but an ENUM VALUE numifies to a PLAIN Int. Returning it unchanged
            // kept its enumName, so `b.Numeric` and `+b` rendered as `b` rather
            // than 1, while `.Int` and `.value` (which build a fresh Int) were
            // right — the same value answering three ways.
            if (!inv.enumName.empty()) {
                Value n = inv; n.enumName.clear(); n.enumType.clear();
                return n;
            }
            // …and an ALLOMORPH numifies to its NUMERIC half, exactly as `.Rat`
            // on a RatStr already sheds the Str side. Returning it whole kept
            // that side, and the VALUE was right, so arithmetic hid it
            // completely — it showed only in string context, where the result
            // still carried its original text: `~ <0o755>.Numeric` answered
            // "0o755" where Rakudo answers 493. Same shape as the enum case
            // directly above, which this arm already knew to strip.
            if (inv.isAllomorph()) {
                Value n = inv; n.hashKind.clear(); n.s.clear();
                return n;
            }
            return inv;
        }
        if (inv.t == VT::Bool) return Value::integer(inv.b ? 1 : 0);
        // a Cool CONTAINER numifies to its element count, and as an Int:
        // `(1,2).Numeric` is 2, not 2e0. …but a Range with an infinite or NaN
        // endpoint has no count and still numifies — see rangeNumericSpecial.
        if (inv.t == VT::Range) {
            double sp;
            if (rangeNumericSpecial(inv, sp))
                return std::isfinite(sp) ? Value::integer((long long)sp) : Value::number(sp);
        }
        if (inv.t == VT::Range || inv.t == VT::Array ||
            (inv.t == VT::Hash && (inv.hashKind.empty() || inv.hashKind == "Hash" || inv.hashKind == "Map")))
            { ValueList none; return methodCall(inv, "elems", none); }
        return Value::number(inv.toNum());
    }
    if (m == "Bool" || m == "so") {
        if (inv.t == VT::Object) return Value::boolean(boolify(inv)); // honours user Bool / Real Bridge
        if (inv.t == VT::Range) return Value::boolean(boolify(inv));  // 6.e: emptiness, not "has endpoints"
        return Value::boolean(inv.truthy());
    }
    if (m == "not") {
        if (inv.t == VT::Object) return Value::boolean(!boolify(inv));
        if (inv.t == VT::Range) return Value::boolean(!boolify(inv));
        return Value::boolean(!inv.truthy());
    }
    if (m == "defined") return Value::boolean(defined(inv));
    if (m == "DEFINITE") return Value::boolean(defined(inv)); // defined instance vs type/undef
    // Mu.return: return the invocant from the enclosing routine
    // (Cro's serializer selectors: `.return if .is-applicable(...)`)
    if (m == "return") throw ReturnEx{inv};
    if (m == "return-rw") throw ReturnEx{inv};
    // On a HOW — the persistent metaobject or a bare Metamodel::* type:
    // .archetypes answers the standard booleans and ^can/can admits the
    // meta-methods this HOW actually dispatches. JSON::Unmarshal's ClassLike
    // subset gates on `.HOW.archetypes.nominal && .HOW.^can('attributes')`;
    // without these it fell to the plain-Hash multi and unmarshal returned
    // Hash+{JSON::Class} instead of the typed object (the License::SPDX /
    // Test::META chain).
    {
        bool howInv = (inv.t == VT::Type && inv.s.rfind("Metamodel::", 0) == 0) ||
                      (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
                       (inv.obj()->cls->name == "Metamodel::ClassHOW" ||
                        inv.obj()->cls->name == "Metamodel::ParametricRoleGroupHOW"));
        if (howInv) {
            // the meta-object protocol's two-argument forms: `T.HOW.isa(T, U)` is `T.isa(U)`
            if ((m == "isa" || m == "does" || m == "can") && args.size() == 2)
                return methodCall(args[0], m, ValueList{args[1]});
            // `R.HOW.candidates(R)` — the declarations of R's role group
            if (m == "candidates" && !args.empty() && args[0].t == VT::Type) {
                auto ci = classes_.find(args[0].s);
                Value out = Value::array(); out.isList = true;
                if (ci != classes_.end() && ci->second)
                    for (size_t k = 0; k <= ci->second->roleVariants.size(); k++)
                        out.arr()->push_back(Value::typeObj(args[0].s));
                return out;
            }
            if (m == "archetypes") {
                Value a = Value::makeHash(); a.hashKind = "Archetypes";
                bool role = inv.t == VT::Type && inv.s.find("Role") != std::string::npos;
                (*a.hash())["nominal"]       = Value::boolean(!role);
                (*a.hash())["nominalizable"] = Value::boolean(false);
                (*a.hash())["parametric"]    = Value::boolean(role);
                (*a.hash())["generic"]       = Value::boolean(false);
                (*a.hash())["coercive"]      = Value::boolean(false);
                (*a.hash())["definite"]      = Value::boolean(false);
                (*a.hash())["augmentable"]   = Value::boolean(!role);
                return a;
            }
            if ((m == "can" || m == "^can") && !args.empty()) {
                static const std::set<std::string> howCan = {
                    "attributes", "methods", "method_names", "method_table", "name", "archetypes", "add_method",
                    "add_attribute", "compose", "roles", "parents", "mro"};
                return Value::boolean(howCan.count(args[0].toStr()) > 0);
            }
        }
    }
    // the Archetypes value itself: every query is a stored boolean (absent = False)
    if (inv.t == VT::Hash && inv.hashKind == "Archetypes") {
        auto it = inv.hash()->find(m);
        return it != inv.hash()->end() ? it->second : Value::boolean(false);
    }
    if (m == "can") { // Mu.can($name): list of matching methods ([] if none)
        std::string mn = args.empty() ? "" : args[0].toStr();
        Value out = Value::array(); out.isList = true;
        // A BUILT-IN value answers .can too. Everything below this was gated on a
        // user ClassInfo, so `Date.new(…).can('day-of-week')` was False even
        // though the method plainly works — and a module that GATES on .can, as
        // Date::Calendar::Strftime does for %u and %V, silently emitted the
        // format specifier instead of the value. The Dateish set is enumerated
        // against Rakudo's own answers rather than guessed.
        if (inv.t == VT::Hash && (inv.hashKind == "Date" || inv.hashKind == "DateTime")) {
            static const std::set<std::string> dateish = {
                "year", "month", "day", "day-of-week", "day-of-month", "day-of-year",
                "week", "week-number", "week-year", "days-in-month", "is-leap-year",
                "daycount", "yyyy-mm-dd", "dd-mm-yyyy", "mm-dd-yyyy",
                "later", "earlier", "truncated-to", "Str", "gist", "raku", "clone",
                "DateTime", "Date", "defined", "new" };
            static const std::set<std::string> timeOnly = {
                "hour", "minute", "second", "whole-second", "timezone",
                "utc", "local", "in-timezone", "posix" };
            static const std::set<std::string> dateOnly = { "succ", "pred" };
            bool isDT = inv.hashKind == "DateTime";
            if (dateish.count(mn) || (isDT ? timeOnly.count(mn) : dateOnly.count(mn))) {
                Value stub; stub.t = VT::Code; stub.setCode(std::make_shared<Callable>());
                stub.code()->name = mn; stub.code()->isMethod = true;
                std::string mnc = mn;
                stub.code()->builtin = [mnc](Interpreter& I, ValueList& a) -> Value {
                    if (a.empty()) return Value::any();
                    ValueList rest(a.begin() + 1, a.end());
                    return I.methodCall(a[0], mnc, std::move(rest));
                };
                out.arr()->push_back(stub);
                return out;
            }
        }
        ClassInfo* ci = nullptr;
        if (inv.t == VT::Object && inv.obj()) ci = inv.obj()->cls.get();
        else if (inv.t == VT::Type) { auto it = classes_.find(resolveClassAlias(inv.s)); if (it != classes_.end()) ci = it->second.get(); }
        if (ci) if (Value* um = ci->findMethod(mn)) out.arr()->push_back(*um);
        // a public attribute's auto-generated accessor answers .can too
        // (Cro's router gates on `$handler.can('method')` for `has $.method`)
        if (ci && out.arr()->empty()) {
            for (ClassInfo* c2 = ci; c2; c2 = c2->parent.get()) {
                const ClassAttr* at = c2->findAttr(mn);
                if (at && at->pub) {
                    Value stub; stub.t = VT::Code; stub.setCode(std::make_shared<Callable>());
                    stub.code()->name = mn; stub.code()->isMethod = true;
                    std::string mnc = mn;
                    stub.code()->builtin = [mnc](Interpreter& I, ValueList& a) -> Value {
                        if (a.empty()) return Value::any();
                        ValueList rest(a.begin() + 1, a.end());
                        return I.methodCall(a[0], mnc, std::move(rest));
                    };
                    out.arr()->push_back(stub);
                    break;
                }
            }
        }
        // BUILT-IN methods answer .can too: every class news/blesses/gists, and a
        // grammar parses (IETF::RFC_Grammar gates on `$g.can('parse')`). A stub
        // callable that dispatches for real if someone actually invokes it.
        if (ci && out.arr()->empty()) {
            Value stub = builtinCanStub(mn, ci->isGrammar);
            if (stub.t == VT::Code) out.arr()->push_back(stub);
        }
        // a BUILTIN value (Match, IO::Path, …) answers .can by PROBING, the same
        // oracle .^lookup uses: dispatch the name on a sentinel and read the
        // answer off X::Method::NotFound. Data::Dump renders a Match through
        // `qw<made pos hash from list orig>.grep({ $obj.^can($_) })`.
        // …and a bare BUILTIN TYPE object probes on a sentinel VALUE of the
        // type: `Str.can('Int')` is True (the .Int conversion method), which is
        // how DBDish::TypeConverter picks `$type($datum)` over `$type.new(...)`
        Value probeInv = inv;
        if (!ci && inv.t == VT::Type && !classes_.count(inv.s)) {
            if (inv.s == "Str") probeInv = Value::str("");
            else if (inv.s == "Int") probeInv = Value::integer(0);
            else if (inv.s == "Num") probeInv = Value::number(0);
            else if (inv.s == "Bool") probeInv = Value::boolean(false);
            // …and the rest of the built-in types a program asks about. Only
            // four had a sentinel, so `Rat.can('precise')` answered [] for a
            // method `(0.5).precise` plainly runs — an `augment class Rat` is
            // invisible to .can without one, which is the single assertion
            // Rat::Precise's suite fails on (can-ok is Test's .^can).
            else if (inv.s == "Rat" || inv.s == "FatRat") probeInv = Value::rat(BigInt(0), BigInt(1));
            else if (inv.s == "Complex") probeInv = Value::complex(0, 0);
            else if (inv.s == "Array") { probeInv = Value::array(); }
            else if (inv.s == "List" || inv.s == "Seq") probeInv = Value::list({});
            else if (inv.s == "Hash" || inv.s == "Map") probeInv = Value::makeHash();
            else if (inv.s == "Pair") probeInv = Value::pair("k", Value::integer(0));
        }
        if (!ci && out.arr()->empty() && !mn.empty() &&
            probeInv.t != VT::Object && probeInv.t != VT::Type && probeInv.t != VT::Any && probeInv.t != VT::Nil &&
            // Date/DateTime share one dispatch surface here, so a probe cannot
            // tell them apart — the curated Dateish list above is authoritative
            probeInv.hashKind != "Date" && probeInv.hashKind != "DateTime") {
            {
                // one probe (shared with .^lookup): 1 = found, -1 = not found,
                // 0 = a side-effectful name that must not be probed (no stub —
                // the permissive answer this fallback has always given)
                if (probeMethodExists(probeInv, mn, "/nonexistent/rakupp-can-probe") == 1) {
                    Value stub; stub.t = VT::Code; stub.setCode(std::make_shared<Callable>());
                    stub.code()->name = mn; stub.code()->isMethod = true;
                    std::string mnc = mn;
                    stub.code()->builtin = [mnc](Interpreter& I, ValueList& a) -> Value {
                        if (a.empty()) return Value::any();
                        ValueList rest(a.begin() + 1, a.end());
                        return I.methodCall(a[0], mnc, std::move(rest));
                    };
                    out.arr()->push_back(stub);
                }
            }
        }
        // A MIXIN over a built-in value — `Date.new(…) does Role` — is an
        // Object whose class chain ends at the built-in, and nothing above
        // walks past that boundary: the role's own methods answered, the
        // Date's did not. Ask the boxed value itself, which is where the
        // curated Dateish list and the probe live. Date::Calendar::Strftime
        // is used exactly this way (`Date.new(…) does Date::Calendar::Strftime`),
        // gates %u and %V on `.can('day-of-week')`, and emitted the specifier.
        if (out.arr()->empty() && inv.t == VT::Object && inv.obj() && inv.obj()->hasBoxed &&
            inv.obj()->boxed.t != VT::Object)
            return methodCall(inv.obj()->boxed, "can", ValueList{args});
        return out;
    }
    if (inv.t == VT::Type && m == "raku") { // Int.raku -> "Int" (no parens)
        // …and a role pun by the name it was written with, `Foo[Int]`
        std::string d = g_typeDispName ? g_typeDispName(inv.s) : std::string();
        return Value::str(d.empty() ? inv.s.str() : d);
    }
    // An OBJECT's gist is the interpreter's — Class.new(attr => …), a user .gist
    // method, an exception's message. Value::gist() has no access to any of that
    // and falls back to `Class<obj>`, so `say $x` and `say $x.gist` disagreed.
    // A LAZY list goes through gistOf too: an endless one must answer "(...)"
    // (Rakudo), not pass its cached prefix off as the whole list.
    if (m == "gist") return Value::str(inv.t == VT::Object ? gistOf(inv, m.skipOwn)
        : inv.t == VT::Array && inv.arr() && inv.ext() ? gistOf(inv)
        : inv.gist());
    if (m == "raku" && inv.t == VT::Array && inv.arr() && inv.ext() &&
        std::static_pointer_cast<LazySeqState>(inv.ext())->infinite) {
        // .raku of an endless sequence: Rakudo shows the first 100 elements,
        // then marks the rest (the string still must not claim to be complete)
        // …and a lazy ARRAY shows nothing at all — just `[...]` (sheet LA-02)
        if (!inv.isList) return Value::str("[...]");
        materializeLazy(inv, 100);
        std::string out = "(";
        for (size_t i = 0; i < inv.arr()->size() && i < 100; i++) {
            if (i) out += ", ";
            out += rakuRepr((*inv.arr())[i]);
        }
        out += "...).lazy";
        if (inv.s == "Seq") out += ".Seq";   // only a Seq names one
        return Value::str(out);
    }
    if (m == "raku") {
        // `.raku(:arglist)` asks for the ARGUMENT-LIST spelling of a Pair — the
        // arrow form, whatever the key looks like, so `:a(1)` reads back as a
        // positional pair and not as a named argument (sheet HM-16).
        if (inv.t == VT::Pair)
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "arglist" && (!a.pairVal() || a.pairVal()->truthy()))
                    return Value::str((inv.pairKey() ? rakuRepr(*inv.pairKey()) : rakuRepr(Value::str(inv.s))) +
                                      " => " + rakuRepr(inv.pairVal() ? *inv.pairVal() : Value::nil()));
        return Value::str(rakuRepr(inv));
    }
    // A Match is Iterable over its POSITIONAL CAPTURES, so its list coercions answer
    // `$0, $1, …` — not the Match itself. `$/.Slip` slips those captures into the
    // surrounding list, which is how Sparrow6 reads a check's captures
    // (`$matched>>.Slip>>.Str`); the generic `.Slip` below sees a non-Array and
    // handed back the Match, so the whole match came out where `$0` belonged.
    if (inv.t == VT::Match && (m == "Slip" || m == "List")) {
        Value o = Value::array(); o.isList = true;
        if (inv.arr()) *o.arr() = *inv.arr();
        if (m == "Slip") o.s = "Slip";
        return o;
    }
    if (inv.t == VT::Match && m == "Capture") return inv; // a Match already IS one
    if (m == "Slip") { // a Slip flattens into any list-building context (from-list, list literals)
        if (inv.t == VT::Array) { Value r = inv; r.isList = true; r.s = "Slip"; return r; }
        // Everything else slips the list it STANDS for, which for a non-Iterable
        // is the one-element list: `42.Slip` is `slip(42,)`, `Nil.Slip` is
        // `slip(Nil,)`, a Hash slips its pairs (Nil-Any sheet NA-04, NA-16).
        // It used to hand the invocant back unslipped, so `42.Slip` was `42`
        // and spliced nothing.
        Value r = Value::array(); r.isList = true; r.s = "Slip";
        *r.arr() = toList(inv);
        return r;
    }
    // IO::Special: the .path of the standard streams ("<STDOUT>" etc.)
    if (inv.t == VT::Str && inv.hashKind == "IO::Special") {
        if (m == "Str" || m == "what" || m == "gist") return Value::str(inv.s);
        if (m == "IO") return inv;
        if (m == "e") return Value::boolean(true);
        if (m == "d" || m == "f" || m == "l" || m == "x" || m == "z") return Value::boolean(false);
        if (m == "s") return Value::integer(0);
        if (m == "r") return Value::boolean(inv.s == "<STDIN>");
        if (m == "w") return Value::boolean(inv.s != "<STDIN>");
        if (m == "modified" || m == "accessed" || m == "changed") return Value::typeObj("Instant");
        if (m == "mode") return Value::nil();
        if (m == "raku") return Value::str("IO::Special.new(\"" + inv.s + "\")");
        if (m == "WHICH") { Value w = Value::str("IO::Special|" + inv.s); w.hashKind = "ObjAt"; return w; }
    }
    // a Blob/Buf (Str-tagged internally) is Positional over its BYTES, not a scalar
    if (inv.t == VT::Str && (inv.hashKind == "Blob" || inv.hashKind == "Buf")) {
        long long bn = inv.blobElems();
        if (m == "list" || m == "List" || m == "Array" || m == "values" ||
            m == "Seq" || m == "flat" || m == "eager" || m == "cache") {
            Value out = Value::array(); out.isList = (m != "Array");
            *out.arr() = inv.blobList();
            return out;
        }
        if (m == "elems") return Value::integer(bn);
        if (m == "head") return bn == 0 ? Value::any() : inv.blobElemAt(0);
        if (m == "tail") return bn == 0 ? Value::any() : inv.blobElemAt(bn - 1);
        if (m == "AT-POS" && !args.empty()) {
            long long i = args[0].toInt();
            if (i < 0) i += bn;
            return (i >= 0 && i < bn) ? inv.blobElemAt(i) : Value::any();
        }
    }
    // `.elems` on a type object is 1 — a type object is a one-element list of
    // itself, exactly like any other scalar. `.serial` answers the invocant
    // (only a Supply has anything to decide there).
    if (m == "elems" && (inv.t == VT::Type || inv.t == VT::Whatever)) return Value::integer(1);
    if (m == "serial" && inv.t != VT::Object) return inv;
    // .list/.List/.flat/.eager on a *scalar* (Int/Str/Num/Rat/Bool/Complex/Pair/type object)
    // yields a one-element list. Restricted to scalar types so list/array/range/seq values —
    // which carry their own list semantics upstream — are never re-wrapped.
    if ((m == "list" || m == "List" || m == "Seq" || m == "flat" || m == "eager" || m == "cache" || m == "lazy") &&
        (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Str ||
         inv.t == VT::Bool || inv.t == VT::Complex || inv.t == VT::Pair || inv.t == VT::Type ||
         inv.t == VT::Any || inv.t == VT::Nil)) {
        Value o = Value::array(); o.isList = true; o.arr()->push_back(inv);
        // `.flat` of a non-Iterable is a Seq over the one element, not a List:
        // `42.flat.raku` is `(42,).Seq` while `42.list` is `(42,)` (NA-16).
        if (m == "Seq" || m == "flat") o.s = "Seq";
        return o;
    }
    // .deepmap/.duckmap/.nodemap on a non-Iterable map the one element it stands
    // for — including a TYPE OBJECT, which is how `deepmap *.self, Array` reaches
    // here (it must not be a "no such method").
    if ((m == "deepmap" || m == "duckmap" || m == "nodemap") && !args.empty() &&
        (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Str ||
         inv.t == VT::Bool || inv.t == VT::Complex || inv.t == VT::Pair ||
         inv.t == VT::Type || inv.t == VT::Any || inv.t == VT::Nil)) {
        Value o = Value::array(); o.isList = true; o.arr()->push_back(inv);
        return methodCall(o, m, args, rwArgs);
    }
    if (m == "toggle" &&
        (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat || inv.t == VT::Str ||
         inv.t == VT::Bool || inv.t == VT::Complex || inv.t == VT::Pair)) {
        // Any.toggle: a non-iterable is a one-element list
        Value o = Value::array(); o.isList = true; o.arr()->push_back(inv);
        return methodCall(o, "toggle", args, rwArgs);
    }
    if (m == "sink") return Value::nil(); // Mu.sink: evaluate for side effects, yield Nil (user `sink` dispatched earlier)
    if (m == "VAR" || m == "self") return inv; // container introspection: value is its own container
    if (m == "item") { // .item: decontainerize to a single item (itemize a container)
        Value v = inv;
        if (v.t == VT::Array || v.t == VT::Hash) v.itemized = true;
        return v;
    }
    // Bool is an enum (False => 0, True => 1): .key is the name, .value the ordinal.
    // `True.ACCEPTS($x)` is True and `False.ACCEPTS($x)` False, whatever $x —
    // a Bool matcher answers itself (Rakudo: `multi method ACCEPTS(Bool:D:
    // Mu \topic) { self }`). paths' default file matcher is `True`, and its
    // `$!file-matcher.ACCEPTS($entry)` used to die with no such method.
    if (inv.t == VT::Bool && m == "ACCEPTS") return inv;
    // `Numeric.ACCEPTS(Any:D \a) { self == a }` — the method `~~` runs when the
    // MATCHER is a number, and the one an enum value answers with (an enum value
    // is a Numeric). It was missing entirely, so `$level.ACCEPTS($message)` —
    // what Lumberjack's smartmatch comes down to — was a missing method.
    if ((inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat) && m == "ACCEPTS") {
        if (args.empty()) return Value::boolean(false);
        Value topic = args[0];
        // `==` numifies, and an OBJECT numifies through its own `method Numeric`
        // (Lumberjack's Message answers the level it carries)
        if (topic.t == VT::Object && topic.obj() && topic.obj()->cls) {
            ValueList none;
            try { topic = methodCall(topic, "Numeric", none); } catch (RakuError&) {}
        }
        return Value::boolean(boolify(applyBinOp("==", inv, topic)));
    }
    if (inv.t == VT::Bool && m == "key")   return Value::str(inv.b ? "True" : "False");
    if (inv.t == VT::Bool && m == "value") return Value::integer(inv.b ? 1 : 0);
    // .VAR.name on an anonymous container is "element" in Rakudo; some code (Text::CSV)
    // uses `@x.VAR.name ne "element"` to detect an explicitly-passed array.
    if (m == "name" && (inv.t == VT::Array || inv.t == VT::Hash)) return Value::str("element");
    // enum value introspection (VT::Int carrying an enumName, e.g. `medium` of `enum Size <...>`)
    if (inv.t == VT::Int && !inv.enumName.empty()) {
        if (m == "key") return Value::str(inv.enumName);
        // a non-Int enum value (`One => "Eins"`) rides in pairVal beside the ordinal
        // …and a member whose Int exceeds int64 carries it as its own BigInt
        // payload (see the enum declaration): strip the tag rather than clamp
        auto plain = [&]() -> Value {
            if (inv.pairVal()) return *inv.pairVal();
            if (inv.big()) { Value n = inv; n.enumName.clear(); n.enumType.clear(); return n; }
            return Value::integer(inv.toInt());
        };
        if (m == "value") return plain();
        if (m == "pair") return Value::pair(inv.enumName, plain());
        // An enum VALUE's `.kv` is its OWN name and number — `foo.kv` is
        // `("foo", 0)`, a two-element List, not the index/value pair the
        // one-element-list view would give (Nil-Any sheet NA-18; roast
        // S12-enums/basic.t's "Enumeration:D.kv").
        if (m == "kv") {
            Value o = Value::array(); o.isList = true;
            o.arr()->push_back(Value::str(inv.enumName.str()));
            o.arr()->push_back(plain());
            return o;
        }
        // TYPE-level queries reach the enum type object — the tagged pair-list the
        // declaration built. `.enums` was implemented only there, so `Mass.enums`
        // worked and `g.enums` fell off the ladder. The VT::Array guard matters:
        // the type object carries enumType too, and forwarding from it recurses.
        if ((m == "enums" || m == "elems" || m == "pick" || m == "roll") && !inv.enumType.empty()) {
            if (Value* et = tctx_.cur->find(inv.enumType))
                if (et->t == VT::Array) return methodCall(*et, m, args, rwArgs);
            // …and when the declaration is not in scope here at all. A role's
            // `::EnumBits` capture hands the type object to the role BODY, whose
            // lexical chain never saw the consumer's `my enum MyBits`, so
            // `EnumBits.enums` — the line every BitEnum consumer runs — answered
            // "No such method 'enums'".
            auto ep = enumPairs_.find(inv.enumType.str());
            if (ep != enumPairs_.end() && ep->second.t == VT::Array)
                return methodCall(ep->second, m, args, rwArgs);
        }
    }
    // `.new` on an enum — its type or a member, Bool included — makes nothing
    if (m == "new" && ((!inv.enumType.empty() && (inv.t == VT::Array || inv.t == VT::Type || defined(inv))) ||
                       (inv.t == VT::Type && inv.s == "Bool") || inv.t == VT::Bool)) {
        std::string en = inv.t == VT::Bool || (inv.t == VT::Type && inv.s == "Bool") ? std::string("Bool")
                                                                                     : std::string(inv.enumType.str());
        throwTypedV("X::Constructor::BadType", {{"type", Value::typeObj(en)}},
                    "Enum '" + en + "' is insufficiently type-like to be instantiated. Did you mean 'class'?");
    }
    // An enum TYPE is Associative over its members: `E.pairs` is (a => 5, b => 7)
    // and `E.keys` the names — not the index-keyed view of the pair-list it is
    // stored as
    if (inv.t == VT::Array && !inv.enumType.empty() && inv.arr() &&
        (m == "pairs" || m == "keys" || m == "values" || m == "kv" || m == "antipairs" || m == "invert")) {
        Value o = Value::array(); o.isList = true; o.s = "Seq";
        for (auto& e : *inv.arr()) {
            if (e.t != VT::Pair) continue;
            Value k = Value::str(e.s), v = e.pairVal() ? *e.pairVal() : Value::any();
            if (m == "pairs") o.arr()->push_back(Value::pair(e.s, v));
            else if (m == "keys") o.arr()->push_back(k);
            else if (m == "values") o.arr()->push_back(v);
            else if (m == "kv") { o.arr()->push_back(k); o.arr()->push_back(v); }
            else {
                Value p = Value::pair(v.toStr(), k);
                if (v.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(v);
                o.arr()->push_back(p);
            }
        }
        return o;
    }
    if (inv.t == VT::Match && (m == "made" || m == "ast")) return inv.pairVal() ? *inv.pairVal() : Value::nil();
    if (inv.t == VT::Match && m == "Str") return Value::str(inv.s);
    // The engine records BYTE offsets into the subject, but Raku reports GRAPHEME
    // offsets — so `"áb" ~~ /b/` must say 1, not 2, and a combining mark must not
    // shift the answer at all. Converted here on the way out rather than in the
    // engine, because prematch/postmatch below genuinely want the byte offsets.
    auto graphemeOff = [](const Value& mv, long byteOff) -> Value {
        if (byteOff <= 0) return Value::integer(0);
        // Without the subject there is nothing to count graphemes IN, so answer the
        // offset unchanged. Falling back to `mv.s` — the MATCHED text — silently
        // clamped the offset to the match's own length, which turned a submatch's
        // `.from` into its length.
        if (!mv.ext()) return Value::integer((long long)byteOff);
        const std::string& orig = *std::static_pointer_cast<std::string>(mv.ext());
        size_t b = std::min((size_t)byteOff, orig.size());
        for (size_t i = 0; i < b; i++)             // pure ASCII: byte == grapheme
            if ((unsigned char)orig[i] >= 0x80)
                return Value::integer((long long)uniGraphemeCount(utf8cp(orig.substr(0, b))));
        return Value::integer((long long)b);
    };
    // A LIST of matches answers the span it covers: `.from` of the first, `.to` of
    // the last. `$/.list.from` is how a :g match reports where its matches start.
    if ((m == "from" || m == "to") && inv.t == VT::Array && inv.arr() && !inv.arr()->empty()) {
        const Value& end = m == "from" ? inv.arr()->front() : inv.arr()->back();
        if (end.t == VT::Match) return graphemeOff(end, m == "from" ? end.rFrom() : end.rTo());
    }
    if (inv.t == VT::Match && (m == "from")) return graphemeOff(inv, inv.rFrom());
    if (inv.t == VT::Match && (m == "to")) return graphemeOff(inv, inv.rTo());
    // `.pos` is where the engine has got to — meaningful on a match IN PROGRESS
    // (inside a `{…}` block), where it is the end of what has matched so far.
    if (inv.t == VT::Match && (m == "pos")) return graphemeOff(inv, inv.rTo());
    // `.target` is `.orig` under its Cursor-era name
    if (inv.t == VT::Match && (m == "orig" || m == "target" || m == "prematch" || m == "postmatch")) {
        std::string orig = inv.ext() ? *std::static_pointer_cast<std::string>(inv.ext()) : inv.s;
        if (m == "orig" || m == "target") return Value::str(orig);
        if (m == "prematch") return Value::str(orig.substr(0, std::min((size_t)inv.rFrom(), orig.size())));
        return Value::str((size_t)inv.rTo() <= orig.size() ? orig.substr(inv.rTo()) : "");
    }
    // Match.join joins the POSITIONAL CAPTURES (a Match is a Capture):
    // UUID.Str splits the hex run with /(....)(....).../ then .join("-")
    if (inv.t == VT::Match && m == "join" && inv.arr()) {
        Value lst = Value::array(); lst.isList = true;
        for (auto& e : *inv.arr()) lst.arr()->push_back(e);
        return methodCall(lst, "join", args, rwArgs);
    }
    // .caps: every positional AND named capture as key=>Match pairs, one entry
    // per occurrence (lists unfolded), ordered by where each occurrence matched.
    if (inv.t == VT::Match && m == "caps") {
        std::vector<std::pair<Value, Value>> entries;
        auto addEntry = [&](const Value& key, const Value& v) {
            if ((v.isList || v.t == VT::Array) && v.arr()) { for (auto& e : *v.arr()) entries.push_back({key, e}); }
            else entries.push_back({key, v});
        };
        if (inv.arr()) for (size_t i = 0; i < inv.arr()->size(); i++)
            addEntry(Value::integer((long long)i), (*inv.arr())[i]);
        if (inv.hash()) for (auto& kv : *inv.hash()) {
            if (!kv.first.empty() && kv.first[0] == '\x01') continue;
            addEntry(Value::str(kv.first), kv.second);
        }
        std::stable_sort(entries.begin(), entries.end(),
                         [](const std::pair<Value, Value>& a, const std::pair<Value, Value>& b) {
                             return a.second.rFrom() < b.second.rFrom();
                         });
        Value o = Value::array(); o.isList = true;
        for (auto& e : entries) {
            Value p = Value::pair(e.first.t == VT::Str ? e.first.s : e.first.toStr(), e.second);
            if (e.first.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(e.first);
            o.arr()->push_back(std::move(p));
        }
        return o;
    }
    if (inv.t == VT::Match && m == "actions")
        return inv.md() && inv.md()->actions ? *inv.md()->actions : Value::any();
    // .chunks: the .caps pairs with the text BETWEEN them as `~ => Str` pairs,
    // covering the whole match
    if (inv.t == VT::Match && m == "chunks") {
        std::string orig = inv.ext() ? *std::static_pointer_cast<std::string>(inv.ext()) : inv.s;
        Value caps = methodCall(inv, "caps", ValueList{});
        Value o = Value::array(); o.isList = true;
        long long pos = inv.rFrom();
        auto gap = [&](long long to) {
            if (to > pos && (size_t)to <= orig.size())
                o.arr()->push_back(Value::pair("~", Value::str(orig.substr((size_t)pos, (size_t)(to - pos)))));
        };
        if (caps.arr())
            for (auto& p : *caps.arr()) {
                const Value* v = p.pairVal();
                if (v && v->t == VT::Match) {
                    gap(v->rFrom());
                    pos = std::max(pos, (long long)v->rTo());
                }
                o.arr()->push_back(p);
            }
        gap(inv.rTo());
        return o;
    }
    if (inv.t == VT::Match && (m == "keys" || m == "values" || m == "list"
                               || m == "hash" || m == "pairs" || m == "kv" || m == "elems")) {
        if (m == "hash") { Value h = Value::makeHash(); if (inv.hash()) *h.hash() = *inv.hash(); return h; }
        if (m == "elems") return Value::integer(inv.arr() ? (long long)inv.arr()->size() : 0);
        Value o = Value::array(); o.isList = true;
        // Set/Bag/Mix keep the element's original type in the count's pairKey.
        auto typedKey = [](const std::pair<const std::string, Value>& kv) {
            return kv.second.pairKey() ? *kv.second.pairKey() : Value::str(kv.first);
        };
        if (m == "keys") {
            if (inv.arr()) for (size_t i = 0; i < inv.arr()->size(); i++) o.arr()->push_back(Value::integer((long long)i));
            if (inv.hash()) for (auto& kv : *inv.hash()) o.arr()->push_back(typedKey(kv));
        } else if (m == "values" || m == "list") {
            // `.values` SLIPS a list-valued positional capture, one level: `(\w)*`
            // over "abc" answers ("a","b","c"), not one three-element list —
            // Rakudo's Capture.values does the same, and DBDish::Pg walks exactly
            // this to take a Postgres array literal apart. `.list` keeps the shape.
            if (inv.arr()) for (auto& e : *inv.arr()) {
                if (m == "values" && e.t == VT::Array && e.arr())   // a positional capture is a Match unless it is list-valued
                    for (auto& x : *e.arr()) o.arr()->push_back(x);
                else o.arr()->push_back(e);
            }
            // …and a NAMED capture slips the same way: `<term>+` contributes each
            // sub-match, not one list of them. `$/.values».made` is how a grammar's
            // TOP action collects what its terms made (Path::Finder's glob parser
            // is written exactly that way), and with the list unflattened the
            // `».made` reached a List, which has no .made, so TOP made Nil.
            if ((m == "values") && inv.hash()) for (auto& kv : *inv.hash()) {
                if (kv.second.t == VT::Array && kv.second.arr())
                    for (auto& x : *kv.second.arr()) o.arr()->push_back(x);
                else o.arr()->push_back(kv.second);
            }
        } else { // pairs / kv
            if (inv.arr()) for (size_t i = 0; i < inv.arr()->size(); i++) {
                if (m == "kv") { o.arr()->push_back(Value::integer((long long)i)); o.arr()->push_back((*inv.arr())[i]); }
                else o.arr()->push_back(Value::pair(std::to_string(i), (*inv.arr())[i]));
            }
            if (inv.hash()) for (auto& kv : *inv.hash()) {
                if (m == "kv") { o.arr()->push_back(typedKey(kv)); o.arr()->push_back(kv.second); }
                else { Value p = Value::pair(kv.first, kv.second); p.pairKeyM() = kv.second.pairKey(); o.arr()->push_back(std::move(p)); }
            }
        }
        return o;
    }
    // `.of` on a typed container: `my Int @a` / `my Int %h` → Int (Mu when untyped).
    // For Hash[V,K]/Array[T] the value/element type is the first parameter component.
    if (m == "of" && (inv.t == VT::Array || inv.t == VT::Hash)) {
        // a quanthash's ofType is its KEY parameter; .of is the fixed value type
        if (inv.t == VT::Hash) if (const char* vt = quantValueType(inv.hashKind)) return Value::typeObj(vt);
        if (inv.ofType().empty()) return Value::typeObj("Mu");
        std::string ot = inv.ofType(); auto c = ot.find(','); if (c != std::string::npos) ot = ot.substr(0, c);
        return Value::typeObj(ot);
    }
    // `self.rakuseen(NAME, { … })` — Rakudo's cycle guard for a user-written
    // `.raku`: run the block, unless this very object is already being rendered
    // further up the same `.raku`, in which case print the short form instead of
    // recursing forever. Hash::Agnostic (and the Array::Agnostic family) build
    // every `.raku` on it, so without it the method died on the first call.
    if (m == "rakuseen" && args.size() >= 2 && args[1].t == VT::Code) {
        static thread_local std::set<const void*> inProgress;
        const void* id = inv.t == VT::Object && inv.obj() ? (const void*)inv.obj()
                       : inv.hash() ? (const void*)inv.hash()
                       : inv.arr()  ? (const void*)inv.arr() : nullptr;
        std::string nm = args[0].t == VT::Type ? args[0].s : args[0].toStr();
        if (id && inProgress.count(id)) return Value::str(nm + ".new(...)");
        if (id) inProgress.insert(id);
        struct Pop { std::set<const void*>& s; const void* k;
            ~Pop() { if (k) s.erase(k); } } pop{inProgress, id};
        ValueList none;
        return callCallable(args[1], none);
    }
    if (m == "WHAT") {
        // typed container -> its parameterized type object (Array[Int] / Hash[Int,Str])
        if ((inv.t == VT::Array || inv.t == VT::Hash) && !inv.ofType().empty()) {
            Value ty = Value::typeObj(inv.t == VT::Array ? "Array" : "Hash");
            ty.ofTypeM() = inv.ofType();
            return ty;
        }
        if (inv.t == VT::Type) return inv; // a (parameterized) type object is its own .WHAT
        // a CArray instance names its element type inside its own name
        // ("CArray[int32]"); its .WHAT carries the parameter where a
        // `CArray[int32]` written in code keeps it, so the two are `===`
        if (inv.t == VT::Str && inv.hashKind == "CArray" && !inv.enumName.empty()) {
            Value ty = Value::typeObj("CArray"); ty.ofTypeM() = inv.enumName.str(); return ty;
        }
        // ...and so does a live Pointer[T], or a CArray[T] a native call
        // returned, whose element type sits in an "of" slot
        if (inv.t == VT::Hash && inv.hash() && (inv.hashKind == "Pointer" || inv.hashKind == "CArray")) {
            auto it = inv.hash()->find("of");
            if (it != inv.hash()->end() && !it->second.toStr().empty()) {
                Value ty = Value::typeObj(inv.hashKind.str()); ty.ofTypeM() = it->second.toStr(); return ty;
            }
        }
        // native-container subclass instance parameterized as A[Int]
        if (inv.t == VT::Object && inv.obj() && inv.obj()->hasBoxed && inv.obj()->cls &&
            !inv.obj()->boxed.ofType().empty()) {
            Value ty = Value::typeObj(inv.obj()->cls->name); ty.ofTypeM() = inv.obj()->boxed.ofType(); return ty;
        }
        return Value::typeObj(inv.typeName());
    }
    // `Match!cursor_init($target, :c($n))` — reached through
    // `Match.^lookup("!cursor_init")`, the cursor String::Utils' replace starts
    // from: a Match over $target that has matched nothing yet (pos -3, as
    // MoarVM spells it) at position $n. A Regex called on it matches from
    // there (see callCallable).
    if (inv.t == VT::Type && inv.s == "Match" && m == "!cursor_init") {
        std::string target; long long c = 0; bool haveTarget = false;
        for (auto& a : args) {
            if (a.t == VT::Pair && a.s == "c") c = a.pairVal() ? a.pairVal()->toInt() : 0;
            else if (a.t != VT::Pair && !haveTarget) { target = a.toStr(); haveTarget = true; }
        }
        Value cur = Value::matchVal("", c, -3);   // -3: MoarVM's "matched nothing" pos
        cur.extM() = std::make_shared<std::string>(target);
        return cur;
    }
    // `$cursor.CURSOR_MORE` (via `Match.^lookup("CURSOR_MORE")`) — the next
    // match of the same regex after this one; an empty match steps one on
    if (inv.t == VT::Match && m == "CURSOR_MORE") {
        Value rx;
        if (inv.md() && inv.md()->named) {
            auto it = inv.md()->named->find("\x01rx");
            if (it != inv.md()->named->end()) rx = it->second;
        }
        long long next = inv.rTo() < 0 ? inv.rFrom()
                       : inv.rTo() == inv.rFrom() ? inv.rTo() + 1 : inv.rTo();
        Value cur = Value::matchVal("", next, -3);
        cur.extM() = inv.ext();
        if (rx.t != VT::Regex) return cur;
        ValueList one{cur};
        return callCallable(rx, std::move(one));
    }
    // `Rakudo::Iterator.OneValue($x)` / `.Empty` — the two ready-made iterators
    // paths hands back for a lone file and for nothing (Rakudo's internal
    // factory; the names are what the module spells)
    if (inv.t == VT::Type && inv.s == "Rakudo::Iterator" && (m == "OneValue" || m == "Empty")) {
        Value it = Value::makeHash(); it.hashKind = "Iterator";
        Value items = Value::array();
        if (m == "OneValue" && !args.empty()) items.arr()->push_back(args[0]);
        (*it.hash())["items"] = items;
        (*it.hash())["pos"] = Value::integer(0);
        return it;
    }
    if (m == "iterator") { // S07: make an Iterator over this value's elements
        Value it = Value::makeHash(); it.hashKind = "Iterator";
        Value items = Value::array();
        bool lazy = false;
        if (inv.t == VT::Array && inv.arr() && inv.ext()) {
            // a lazy sequence: keep the SOURCE value as the items (sharing its
            // materialised prefix and its LazySeqState) so the protocol methods
            // can extend it — copying the prefix froze `(1 xx *).iterator` after
            // its first cached element (issue #30)
            items = inv;
            lazy = inv.b || std::static_pointer_cast<LazySeqState>(inv.ext())->infinite;
        }
        else if (inv.t == VT::Array && inv.arr()) { *items.arr() = *inv.arr(); lazy = inv.b; }
        else if (inv.t == VT::Range) {
            // An ENDLESS range whose start is a string or a fraction cannot be
            // walked from the integer fields — `("d"..*)` would pull codepoints
            // and `(1.5..*)` whole numbers. `.list` knows how to step both, so
            // ask it rather than flatten(), which only sees the fields.
            const RangeEnds* re = rangeEnds(inv);
            if (inv.rTo() >= 9000000000000000000LL && re &&
                (re->from.t == VT::Str || inv.rNum())) {
                ValueList none;
                Value l = methodCall(inv, "list", none, nullptr);
                if (l.t == VT::Array && l.arr()) *items.arr() = *l.arr();
            }
            else *items.arr() = inv.flatten();
            lazy = inv.b || inv.rTo() >= 9000000000000000000LL; } // infinite / `lazy`-marked range
        else if (inv.t == VT::Hash) { // plain hash and Set/Bag/Mix iterate their pairs
            ValueList none;
            Value ps = methodCall(inv, "pairs", none, nullptr);
            if (ps.t == VT::Array && ps.arr()) *items.arr() = *ps.arr();
            // a hash has no promised order, so neither has its iterator
            (*it.hash())["nondeterministic"] = Value::boolean(true);
        }
        // An undefined value is not an EMPTY sequence — it is a one-element one
        // holding itself: `Nil.iterator.pull-one` is Nil and only the SECOND
        // pull is IterationEnd (Nil-Any sheet NA-03, NA-17).
        else items.arr()->push_back(inv);
        (*it.hash())["items"] = items;
        (*it.hash())["pos"] = Value::integer(0);
        if (lazy) (*it.hash())["lazy"] = Value::boolean(true);
        return it;
    }
    // Date/DateTime clone rebuilds via `.new` so `:field(v)` overrides apply AND
    // validate (rejecting e.g. `.clone(month => 13)`), recomputing posix.
    if (m == "clone" && inv.t == VT::Hash && (inv.hashKind == "DateTime" || inv.hashKind == "Date") && inv.hash()) {
        std::map<std::string, Value> merged;
        // the formatter travels with the clone, like every other conversion
        for (const char* k : {"year", "month", "day", "hour", "minute", "second", "timezone", "formatter"})
            if (inv.hash()->count(k)) merged[k] = (*inv.hash())[k];
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal()) merged[a.s] = *a.pairVal();
        ValueList na; for (auto& kv : merged) na.push_back(Value::pair(kv.first, kv.second));
        return methodCall(Value::typeObj(inv.hashKind), "new", na);
    }
    if (m == "clone") { // non-object clone: shallow copy of containers, self for immutables
        if (inv.t == VT::Array) { Value nv = inv; nv.setArr(makePayload<ValueList>(*inv.arr())); return nv; }
        if (inv.t == VT::Hash)  { Value nv = inv; nv.setHash(makePayload<ValueMap>(*inv.hash())); return nv; }
        // A PAIR is mutable through `.value` (Rakudo declares it `is rw`), and
        // every copy of the Value shares the one cell it points at — so the
        // immutable return below handed back an ALIAS, and `$p.clone.value = 7`
        // rewrote $p. Copy the cell: the clone owns its own value.
        if (inv.t == VT::Pair) {
            Value nv = inv;
            nv.setPairVal(std::make_shared<Value>(inv.pairVal() ? *inv.pairVal() : Value::any()));
            return nv;
        }
        // A MATCH is a distinct object after cloning too — mdW() copies the body
        // in place, since this Value and the original now share it.
        if (inv.t == VT::Match) { Value nv = inv; nv.mdW(); return nv; }
        // A ROUTINE/BLOCK clones into a NEW code object sharing the closure but
        // not the `state` slots — `my $c = $b.clone` restarts $b's state vars,
        // which is the whole reason to clone a closure. (Callable's copy leaves
        // the state holder empty; see Value.h.)
        if (inv.t == VT::Code && inv.code()) {
            Value nv = inv;
            nv.setCode(std::make_shared<Callable>(*inv.code()));
            return nv;
        }
        return inv; // Int/Num/Rat/Str/Bool/… are immutable — clone is the value itself
    }
    // `Metamodel::ClassHOW.new_type(:name, :ver, :auth)` — a class created at
    // RUNTIME. It registers like a declared one, so `.^add_method` and `.new`
    // then work on it exactly as they do on `class Foo { }`.
    if (inv.t == VT::Type && m == "new_type" &&
        (inv.s == "Metamodel::ClassHOW" || inv.s == "Metamodel::ParametricRoleHOW" ||
         inv.s == "Metamodel::ParametricRoleGroupHOW" ||
         // A PACKAGE/MODULE/GRAMMAR created at runtime builds the same way here —
         // rakupp models all of them with ClassInfo, and the HOW only decides what
         // the type reports about itself. Omitting PackageHOW meant
         // `Metamodel::PackageHOW.new_type(name => …)` threw "No such method", which
         // is what stopped roast's packages/S11-modules/lib/RuntimeCreatedPackage
         // from loading.
         inv.s == "Metamodel::PackageHOW" || inv.s == "Metamodel::ModuleHOW" ||
         inv.s == "Metamodel::GrammarHOW")) {
        // …unless a DECLARATION is asking, through its metaclass's own
        // `new_type` hook: the type exists already, and creating a second one
        // here would hand the metaclass a class nothing else refers to.
        if (!declaringType_.empty()) return Value::typeObj(declaringType_);
        auto ci = std::make_shared<ClassInfo>();
        ci->name = "<anon|1>";
        for (auto& a : args)
            if (a.t == VT::Pair && a.pairVal()) {
                if (a.s == "name") ci->name = a.pairVal()->toStr();
                else if (a.s == "ver") ci->ver = a.pairVal()->toStr();
                else if (a.s == "auth") ci->auth = a.pairVal()->toStr();
                else if (a.s == "api") ci->api = a.pairVal()->toStr();
            }
        noteSymbolMutation("runtime .new_type");
        ci->awaitingCompose = true;
        classes_[ci->name] = ci;
        return Value::typeObj(ci->name);
    }
    // `MyHOW.new_type(:name)` on a USER metaclass — a subclass of
    // Metamodel::ClassHOW — creates a type whose .HOW is an instance of it (the
    // very instance, when called on one: `Meta.new(:table(…)).new_type(…)`).
    // Red makes every model alias this way (`::?CLASS.new_type(:$name)`), and
    // the per-type state the metaclass keeps lives in that instance.
    if (m == "new_type" && (inv.t == VT::Type || inv.t == VT::Object) && declaringType_.empty()) {
        std::shared_ptr<ClassInfo> hcls;
        if (inv.t == VT::Object && inv.obj()) hcls = inv.obj()->cls;
        else if (inv.t == VT::Type) {
            auto it = classes_.find(inv.s);
            if (it == classes_.end()) it = classes_.find(resolveClassAlias(inv.s));
            if (it != classes_.end()) hcls = it->second;
        }
        bool isHow = false;
        for (ClassInfo* c = hcls.get(); c && !isHow; c = c->parent.get())
            if (c->nativeParent == "Metamodel::ClassHOW") isHow = true;
        if (isHow) {
            auto ci = std::make_shared<ClassInfo>();
            ci->name = "<anon|1>";
            for (auto& a : args)
                if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "name") ci->name = a.pairVal()->toStr();
                    else if (a.s == "ver") ci->ver = a.pairVal()->toStr();
                    else if (a.s == "auth") ci->auth = a.pairVal()->toStr();
                    else if (a.s == "api") ci->api = a.pairVal()->toStr();
                }
            // types are registered by name here, so a second type asking for a
            // taken name (Red's Specialisable re-types a model under its own
            // name) must not replace the one already there
            if (classes_.count(ci->name)) {
                static std::atomic<int> dup{0};
                ci->name += "+" + std::to_string(++dup);
            }
            // Rakudo's new_type makes its metaobject with `self.new` — a FRESH
            // one even when asked of an instance, and through the metaclass's
            // own `new` (Red's re-checks its experimental roles there)
            try { ci->howObj = methodCall(Value::typeObj(hcls->name), "new", ValueList{}); }
            catch (RakuError&) { ci->howObj = Value::any(); }
            if (ci->howObj.t != VT::Object || !ci->howObj.obj()) {
                auto od = makePayload<ObjectData>();
                od->cls = hcls;
                ValueList noArgs;
                runAttrDefaults(od, hcls, noArgs);
                Value h; h.t = VT::Object; h.setObj(std::move(od));
                ci->howObj = std::move(h);
            }
            if (ci->howObj.obj()) ci->howObj.obj()->attrs["__type"] = Value::typeObj(ci->name);
            noteSymbolMutation("runtime .new_type (user HOW)");
            ci->awaitingCompose = true;
            classes_[ci->name] = ci;
            return Value::typeObj(ci->name);
        }
    }
    // The HOW forms of the MOP operations take the type as their FIRST argument —
    // `$t.HOW.add_method($t, …)` — where the `.^` spelling passes it implicitly
    // as the invocant. Named explicitly: a metaclass also answers ORDINARY
    // methods (`.isa`, `.gist`), which must not be forwarded to their argument.
    // …and the walk has to see a BUILT-IN ancestor too: `class MetamodelX::Red::Model
    // is Metamodel::ClassHOW` has no ClassInfo parent — the base is recorded as
    // the native parent — so a HOW written that way found none of these
    // operations, and `self.add_role(type, Red::Model)` inside its `compose` was
    // a silent no-op. Every Red model then failed `~~ Red::Model`.
    if (((inv.t == VT::Type && inv.s == "Metamodel::ClassHOW") ||
         (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
          [&]{ for (ClassInfo* c = inv.obj()->cls.get(); c; c = c->parent.get())
                   if (c->name == "Metamodel::ClassHOW" ||
                       c->nativeParent == "Metamodel::ClassHOW") return true;
               return false; }())) &&
        !args.empty() && args[0].t == VT::Type) {
        static const std::set<std::string> howOps = {
            "add_method", "add_multi_method", "add_attribute", "add_parent", "add_role", "add_fallback",
            "compose", "compose_repr", "compose_attributes", "set_name", "set_shortname",
            "set_ver", "set_auth", "set_api", "set_rw",
            "publish_method_cache", "publish_type_cache", "invalidate_method_caches",
            // …and the one READ this list needs: a metaclass's own `compose` asks
            // for the roles it is about to compose (Red looks through them for
            // columns). The rest of the read side is NOT forwarded here — a name
            // neither side answers would bounce between this forward and the
            // .HOW fallback in the `^` ladder forever.
            "roles_to_compose", "attributes", "methods", "parents", "roles", "mro", "name",
            "declares_method", "method_table", "private_method_table", "attribute_table", "lookup", "find_method"};
        if (howOps.count(m)) {
            // `HOW.foo(type)` IS `type.^foo` — but the `^` ladder's last resort is
            // to ask the .HOW back, so a name the metaclass INHERITS rather than
            // answers itself bounces between the two forever (Red's `compose`
            // asking `self.attributes(type)` re-ran its own compose, endlessly).
            // Mark the name in flight; the fallback steps aside while it is.
            ValueList rest(args.begin() + 1, args.end());
            struct Mark {
                std::set<std::string>& s; std::string n; bool mine;
                Mark(std::set<std::string>& S, const std::string& N) : s(S), n(N), mine(s.insert(N).second) {}
                ~Mark() { if (mine) s.erase(n); }
            } mark{tctx_.metaForwarding, m};
            return methodCall(args[0], "^" + m, rest, rwArgs);
        }
    }
    // `^enum_from_value` — the enum member with this .value (Rakudo's meta
    // protocol; Getopt::Long converts "--order 0" through it). Works on the
    // pair-list form and on a plain type NAME that resolves to one.
    if (m == "^enum_from_value" || m == "enum_from_value") {
        // the built-in comparison enum first
        if (inv.t == VT::Type && inv.s == "Order" && !args.empty()) {
            long long want = args[0].toInt();
            return want >= -1 && want <= 1 ? Value::orderVal(want) : Value::any();
        }
        const Value* elist = nullptr;
        Value resolved;
        if (inv.t == VT::Array && !inv.enumType.empty() && inv.enumName.empty()) elist = &inv;
        else if (inv.t == VT::Type) {
            Value* ev = tctx_.cur ? tctx_.cur->find(inv.s) : nullptr;
            if (!ev && global_) ev = global_->find(inv.s);
            if (ev && ev->t == VT::Array && !ev->enumType.empty() && ev->enumName.empty())
                { resolved = *ev; elist = &resolved; }
        }
        if (elist && elist->arr() && !args.empty()) {
            long long want = args[0].toInt();
            for (auto& e : *elist->arr())
                if (e.t == VT::Pair && e.pairVal() && e.pairVal()->toInt() == want) {
                    Value out = *e.pairVal();
                    out.enumName = e.s; out.enumType = elist->enumType;
                    return out;
                }
            return Value::any();
        }
    }
    if (m == "HOW") {
        // an ENUM type object answers an EnumHOW, as Rakudo does — modules
        // pick their enum handling by `$type.HOW ~~ Metamodel::EnumHOW`
        // (Getopt::Long's enum-converter branch)
        if (inv.t == VT::Array && !inv.enumType.empty() && inv.enumName.empty())
            return Value::typeObj("Metamodel::EnumHOW");
        // …including when the enum arrives as a plain TYPE NAME (a Parameter's
        // `.type` is typeObj("Order")) — resolve the name to see what it is.
        // Order/Endian/Bool are the built-in enums.
        if (inv.t == VT::Type) {
            if (inv.s == "Order" || inv.s == "Endian" || inv.s == "Bool")
                return Value::typeObj("Metamodel::EnumHOW");
            Value* ev = tctx_.cur ? tctx_.cur->find(inv.s) : nullptr;
            if (!ev && global_) ev = global_->find(inv.s);
            if (ev && ev->t == VT::Array && !ev->enumType.empty() && ev->enumName.empty())
                return Value::typeObj("Metamodel::EnumHOW");
        }
        // …and a COERCION type object (`Foo(Str)`) answers a CoercionHOW, which
        // is how a module tells a coercion apart from a plain type before
        // asking for its two halves (Getopt::Long's coercion-converter branch).
        if (inv.t == VT::Type) {
            size_t o = inv.s.find('(');
            if (o != std::string::npos && o > 0 && inv.s.back() == ')')
                return Value::typeObj("Metamodel::CoercionHOW");
        }
        // A USER class gets ONE persistent metaobject, so `T.HOW does SomeRole`
        // mixins stick (Method::Also's AliasableClassHOW). Its class is named
        // Metamodel::ClassHOW, keeping `~~ Metamodel::ClassHOW` True. Built-ins
        // keep the plain type object.
        // A MODULE or PACKAGE answers its own metaobject, not a ClassHOW. They
        // are namespaces here rather than types, so they are not in `classes_`
        // at all and this is the only place that knows — measured: Rakudo gives
        // `Perl6::Metamodel::ModuleHOW` and `…::PackageHOW`, and L10N::ZH's
        // package test asserts both names.
        if (inv.t == VT::Type) {
            auto pk = pkgKind_.find(inv.s);
            if (pk != pkgKind_.end()) {
                auto& slot = pk->second == 1 ? howModuleClsInfo_ : howPackageClsInfo_;
                if (!slot) {
                    slot = std::make_shared<ClassInfo>();
                    slot->name = pk->second == 1 ? "Metamodel::ModuleHOW" : "Metamodel::PackageHOW";
                }
                Value h; h.t = VT::Object; h.setObj(makePayload<ObjectData>());
                h.obj()->cls = slot;
                h.obj()->attrs["__type"] = Value::typeObj(inv.s);
                return h;
            }
        }
        // a SUBSET (UInt among them) is made by SubsetHOW
        if (inv.t == VT::Type && !classes_.count(inv.s) && (subsets_.count(inv.s) || inv.s == "UInt"))
            return Value::typeObj("Metamodel::SubsetHOW");
        ClassInfo* hci = nullptr;
        if (inv.t == VT::Type) { auto it = classes_.find(inv.s); if (it != classes_.end()) hci = it->second.get(); }
        else if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) hci = inv.obj()->cls.get();
        if (hci) {
            if (hci->howObj.t != VT::Object) {
                if (!howClsInfo_) { howClsInfo_ = std::make_shared<ClassInfo>(); howClsInfo_->name = "Metamodel::ClassHOW"; }
                // a ROLE's metaobject is not a ClassHOW: Rakudo answers
                // ParametricRoleGroupHOW for the bare role name, and
                // Method::Protected refuses `is protected` on a role method by
                // testing `$method.package.HOW.WHAT =:= Metamodel::ClassHOW`
                if (hci->isRole && !howRoleClsInfo_) {
                    howRoleClsInfo_ = std::make_shared<ClassInfo>();
                    howRoleClsInfo_->name = "Metamodel::ParametricRoleGroupHOW";
                }
                Value h; h.t = VT::Object; h.setObj(makePayload<ObjectData>());
                h.obj()->cls = hci->isRole ? howRoleClsInfo_ : howClsInfo_;
                h.obj()->attrs["__type"] = Value::typeObj(hci->name);
                hci->howObj = std::move(h);
            }
            return hci->howObj;
        }
        return Value::typeObj("Metamodel::ClassHOW"); // metaclass (its own .HOW returns a HOW too)
    }
    if (m == "WHO") { // package stash — the PERSISTENT one, see pkgStashes_
        std::string pkg = inv.t == VT::Type ? inv.s : inv.typeName();
        // `MY.WHO`, `LEXICAL.WHO`, `CALLER.WHO`: the pseudo-package's stash
        if (inv.t == VT::Type && pkg != "OUR" && pkg != "GLOBAL" && isPseudoChain(pkg))
            return makePseudoStash(pkg);
        auto& stash = pkgStashes_[pkg];
        if (!stash) stash = makePayload<ValueMap>();
        if (global_) { // `our`-scoped symbols live as qualified globals; show them
            // A sigilled one is published sigil-FIRST (`&A::foo`, `$A::bar`),
            // so a plain `A::` prefix test never saw it and `A.WHO` came back
            // empty for every `our sub`/`our $var` a class declares. The stash
            // key keeps the sigil, as Rakudo's does: `A.WHO<&foo>`.
            std::string pre = pkg + "::";
            for (auto& kv : global_->vars) {
                std::string p, k;
                if (splitPkgSymbol(kv.first, p, k) && p == pkg &&
                    k.find("::") == std::string::npos)
                    (*stash)[k] = kv.second;
            }
        }
        // …and the packages nested INSIDE this one (`class A { class B {} }` —
        // `A.WHO<B>`), which live in the class table rather than as globals
        for (auto& kv : classes_)
            if (kv.first.rfind(pkg + "::", 0) == 0 &&
                kv.first.find("::", pkg.size() + 2) == std::string::npos)
                (*stash)[kv.first.substr(pkg.size() + 2)] = Value::typeObj(kv.first);
        // an ENUM type's stash holds its values (`Bool::.values` is (True, False))
        if (pkg == "Bool") {
            (*stash)["True"]  = Value::boolean(true);
            (*stash)["False"] = Value::boolean(false);
        }
        else if (pkg == "Order") { // the built-in comparison enum
            (*stash)["Less"] = Value::orderVal(-1);
            (*stash)["Same"] = Value::orderVal(0);
            (*stash)["More"] = Value::orderVal(1);
        }
        else if (pkg == "Signal") {
            for (auto& [nm, num] : signalNamesAndNumbers()) {
                Value e = Value::enumVal(nm, num); e.enumType = "Signal";
                (*stash)[nm] = e;
            }
        }
        else if (pkg == "Endian") {
            for (auto& [nm, v] : {std::pair<const char*, long long>{"NativeEndian", 0},
                                  {"LittleEndian", 1}, {"BigEndian", 2}}) {
                Value e = Value::enumVal(nm, v); e.enumType = "Endian";
                (*stash)[nm] = e;
            }
        }
        else if (Value* et = tctx_.cur->find(pkg)) {
            if (et->t == VT::Array && et->arr() && !et->enumType.empty() && et->enumName.empty())
                for (auto& pr : *et->arr())
                    if (pr.t == VT::Pair)
                        if (Value* ev = tctx_.cur->find(pr.s)) (*stash)[pr.s] = *ev;
        }
        Value st; st.t = VT::Hash; st.setHash(stash); st.hashKind = "Stash"; st.s = pkg;
        return st;
    }
    if (m == "WHICH") {
        // whichOf is the one home for identity; the object it comes back in
        // names WHICH KIND of identity it is — an immutable value compares by
        // its content (ValueObjAt), everything else by being itself (ObjAt).
        // Measured against Rakudo 2026.08: Array, List, Seq, Hash, Buf,
        // Instant, IO::Path, Code and every user object are ObjAt; Int, Rat,
        // Num, Str, Bool, Range, Pair, the Setty/Baggy family, Date, Complex,
        // a type object and Nil are ValueObjAt (sheet LA-36).
        Value w = Value::str(whichOf(inv));
        w.hashKind = whichIsObjAt(inv) ? "ObjAt" : "ValueObjAt";
        return w;
    }
    if (m == "WHERE") { // memory address of the value (an Int)
        const void* p = inv.t == VT::Object && inv.obj() ? (const void*)inv.obj()
                      : inv.t == VT::Array && inv.arr()  ? (const void*)inv.arr()
                      : inv.t == VT::Hash && inv.hash()  ? (const void*)inv.hash()
                      : (const void*)&inv;
        return Value::integer((long long)(intptr_t)p);
    }
    // `.REPR` — the representation a type was declared with. NativeCall code
    // gates on it (NativeHelpers::CStruct's `pointer-to(Mu:D $s where .REPR eq
    // 'CStruct')`), so a missing method there is not "no such method" but a
    // multi that never matches. A class with no `is repr(...)` is P6opaque, as
    // in Rakudo; the non-object types answer for their own kind.
    if (m == "REPR" && args.empty()) {
        std::shared_ptr<ClassInfo> ci;
        if (inv.t == VT::Object && inv.obj()) ci = inv.obj()->cls;
        else if (inv.t == VT::Type) {
            auto it = classes_.find(inv.s);
            if (it == classes_.end()) it = classes_.find(resolveClassAlias(inv.s));
            if (it != classes_.end()) ci = it->second;
        }
        if (ci && !ci->repr.empty()) return Value::str(ci->repr);
        return Value::str("P6opaque");
    }
    if (m == "DUMP") return Value::str(inv.t == VT::Type ? inv.s : inv.gist()); // debug snapshot (loose form)
    if (m == "does") { // .does(Role/Type) — role/type membership introspection
        if (args.empty()) return Value::boolean(false);
        // HOW form: `$obj.HOW.does($obj, Role)` — the metaclass takes (object, role).
        // The metaclass may be the plain type object OR a persistent .HOW metaobject.
        bool howInv = (inv.t == VT::Type && inv.s.rfind("Metamodel::", 0) == 0);
        if (!howInv && inv.t == VT::Object && inv.obj() && inv.obj()->cls)
            for (ClassInfo* c = inv.obj()->cls.get(); c && !howInv; c = c->parent.get())
                if (c->name.rfind("Metamodel::", 0) == 0) howInv = true;
        if (howInv && args.size() >= 2)
            return methodCall(args[0], "does", ValueList{args[1]}, rwArgs);
        std::string rn = args[0].t == VT::Type ? args[0].s : args[0].typeName();
        if (rn == "Any" || rn == "Mu") return Value::boolean(true);
        // a SUBSET answers by its constraint: `1.does(PosReal)` is True
        if (args[0].t == VT::Type && subsets_.count(rn))
            return Value::boolean(typeOrSubsetMatches(inv, rn));
        // an Attribute meta-object answering the JSON/META attribute-role
        // checks (`$attr.does(META6::MetaAttribute)`) — same trait-key mapping
        // `~~` uses (typeMatchesArg owns it, reached via typeOrSubsetMatches)
        if (inv.t == VT::Hash && inv.hashKind == "Attribute")
            return Value::boolean(typeOrSubsetMatches(inv, rn));
        // A byte buffer does the buffer roles — Blob, buf8/blob8, Positional —
        // and none of them is a CLASS it is an instance of, in either engine.
        // `~~` already knew (typeMatchesArg owns that table); `.does` walked the
        // class ancestry instead and answered False for all of them. That is
        // what failed `isa-ok md5(Blob.new), buf8`: Test's isa-ok is
        // `.isa || .does`, and .isa is False here on Rakudo too.
        if (inv.t == VT::Str && (inv.hashKind == "Blob" || inv.hashKind == "Buf" ||
                                 inv.hashKind == "utf8" || inv.hashKind == "CArray"))
            return Value::boolean(typeOrSubsetMatches(inv, rn));
        bool res = inv.typeName() == rn;
        ClassInfo* ci = nullptr;
        if (inv.t == VT::Object && inv.obj()) ci = inv.obj()->cls.get();
        else if (inv.t == VT::Type) { auto it = classes_.find(inv.s); if (it != classes_.end()) ci = it->second.get(); }
        if (!res && ci) {
            for (ClassInfo* c = ci; c; c = c->parent.get()) if (c->name == rn) { res = true; break; }
            if (!res) res = ci->doesRole(rn);
            // a built-in parent's ancestry counts: G.does(Grammar) and
            // G.does(Match) are True for a grammar, F.does(Real) for `is Int`
            if (!res)
                for (ClassInfo* c = ci; c && !res; c = c->parent.get())
                    if (!c->nativeParent.empty()) {
                        if (c->nativeParent == rn) res = true;
                        else for (auto& a : typeAncestry(c->nativeParent))
                            if (a == rn) { res = true; break; }
                    }
            // …and a RakuAST:: node does every name in the linearization the
            // registry carries. Those ancestors are extraParents, which this
            // walk does not climb, and `.does` is half of Test's isa-ok.
            if (!res && isRakuAstName(ci->name))
                for (auto& a : rakuAstAncestry(ci->name)) if (a == rn) { res = true; break; }
        }
        // a BUILT-IN value does the roles its ancestry lists (`Date.does(Dateish)`)
        if (!res && !ci)
            for (auto& a : typeAncestry(typeOfVal(inv))) if (a == rn) { res = true; break; }
        // …and the core ROLES as the smartmatch knows them: `Range.does(Positional)`,
        // `%h.does(Associative)`, `Pair.does(Associative)`
        if (!res && !ci) {
            static const std::set<std::string> kCoreRoles = {
                "Positional", "Associative", "Iterable", "Numeric", "Real", "Stringy",
                "Setty", "Baggy", "Mixy", "QuantHash", "Dateish"};
            if (kCoreRoles.count(rn)) res = typeOrSubsetMatches(inv, rn);
        }
        // a Code value does the Callable/Code/Routine/Block roles
        if (!res && inv.t == VT::Code &&
            (rn == "Callable" || rn == "Code" || rn == "Routine" || rn == "Block" || rn == "Sub"))
            res = true;
        // native numeric type objects do Real/Numeric; native `str` does Stringy
        if (!res && inv.t == VT::Type) {
            static const std::set<std::string> natNum = {
                "int","int8","int16","int32","int64","uint","uint8","uint16","uint32","uint64",
                "byte","atomicint","num","num32","num64"};
            if (natNum.count(inv.s) && (rn == "Real" || rn == "Numeric")) res = true;
            else if (inv.s == "str" && rn == "Stringy") res = true;
        }
        return Value::boolean(res);
    }
    // a DEFINITENESS-constrained type object reports its smiley, and
    // `.^base_type` is the same type without it
    if (m == "name" || m == "^name") {
        // the metaclass reports Rakudo's full name; HOW.name($obj) names the OBJECT's type
        if (inv.t == VT::Type && (inv.s == "Metamodel::ClassHOW" || inv.s == "Metamodel::EnumHOW")) {
            if (m == "name" && !args.empty()) return Value::str(args[0].typeName());
            return Value::str("Perl6::" + std::string(inv.s.str()));
        }
        // A typed container names its PARAMETER: `my Int @a; @a.^name` is
        // `Array[Int]`, and `my Int %h` is `Hash[Int]` (sheet LA-21). Only
        // `.^name` — typeName() stays the bare `Array`, because that is the
        // name every type check and error message is written against.
        if (m == "^name" && (inv.t == VT::Array || inv.t == VT::Hash) && !inv.isList &&
            !inv.ofType().empty() && inv.ofType() != "Mu" &&
            inv.typeName().find('[') == std::string::npos)
            return Value::str(inv.typeName() + "[" + inv.ofType() + "]");
        // plain .name is NOT a universal method: a user-class instance with no
        // name method/attr dies X::Method::NotFound like Rakudo ($.name typo)
        if (m == "^name" || !(inv.t == VT::Object && inv.obj() && inv.obj()->cls))
            return Value::str(inv.typeName());
    }

    // Set/Bag/Mix coercions and queries
    // the ROLE coercers name the immutable member of each family
    if (m == "Setty" || m == "Baggy" || m == "Mixy")
        return methodCall(inv, m == "Setty" ? "Set" : m == "Baggy" ? "Bag" : "Mix", args, rwArgs);
    // a Capture is already one
    if (m == "Capture" && inv.t == VT::Array && inv.hashKind == "Capture") return inv;
    if (m == "Set" || m == "SetHash" || m == "Bag" || m == "BagHash" || m == "Mix" || m == "MixHash") {
        if (inv.t == VT::Range && inv.rTo() >= 9000000000000000000LL)
            throwTyped("X::Cannot::Lazy", {{"what", m}}, "Cannot " + m + " a lazy list");
        // the coercer flattens one level, but only through a bare LIST:
        // (@a, %h).Bag takes @a's elements and %h's pairs. A real Array's
        // elements are itemized ([4,[5,6]].Bag keeps [5,6] whole — ».Bag nodal)
        ValueList items;
        bool oneLevel = inv.t == VT::Array && inv.isList;
        for (auto& x : toList(inv)) {
            if (!oneLevel) { items.push_back(x); continue; }
            if (x.t == VT::Array && x.arr() && !x.itemized) {
                for (auto& e : *x.arr()) items.push_back(e);
            }
            else if (x.t == VT::Hash && x.hash() && !x.itemized &&
                     (x.hashKind.empty() || x.hashKind == "Map" || quantValueType(x.hashKind))) {
                for (auto& kv : *x.hash()) {
                    Value p = Value::pair(kv.first, kv.second);
                    p.pairKeyM() = kv.second.pairKey();
                    items.push_back(p);
                }
            }
            else items.push_back(x);
        }
        return makeBaggy(items, m);
    }
    return std::nullopt;   // not handled here — fall through to the next segment
}

} // namespace rakupp
