// MethodCallPart1c.cpp — segment 1c of the method-dispatch chain
//
// One of the parts BuiltinsParts.h lists; what they share is declared there.
#include "BuiltinsParts.h"

namespace rakupp {
static long long g_spawnedSeq = 0;

// IO::Notification, as Rakudo's CORE declares it: the events IO::Path.watch
// emits, which user code also builds (Cro's recursive watcher re-emits its own).
static const char* const kIoNotificationSrc = R"RAKUNOTIFY(
class IO::Notification {
    method watch-path(Str() $path, :$scheduler) { $path.IO.watch }
}
class IO::Notification::Change {
    has $.path;
    has $.event;
    multi method gist(IO::Notification::Change:D:) { "$!path: $!event" }
    # a value type: two reports of the same change are the same (`.unique`)
    multi method WHICH(IO::Notification::Change:D:) {
        ValueObjAt.new("IO::Notification::Change|$!event|$!path")
    }
}
)RAKUNOTIFY";

void Interpreter::loadIoNotification() {
    if (notificationLoading_ || classes_.count("IO::Notification::Change")) return;
    notificationLoading_ = true;
    auto saved = tctx_.cur;
    tctx_.cur = global_;
    try { evalString(kIoNotificationSrc); }
    catch (...) { tctx_.cur = saved; notificationLoading_ = false; throw; }
    tctx_.cur = saved;
    notificationLoading_ = false;
}

// Segment 1c of the method-dispatch chain, split out of methodCallInner for
// compile time. The chain is ORDER-SENSITIVE (an earlier arm shadows a later
// one), so these arms run after the ones above and before methodCallPart2. nullopt = "not handled here".
std::optional<Value> Interpreter::methodCallPart1c(const Value& inv, const MName& m, ValueList& args,
                                                   const std::vector<ExprPtr>* rwArgs) {
    if (inv.t == VT::Hash && inv.hashKind == "Cancellation" && m == "can")
        return Value::boolean(!args.empty() && (args[0].toStr() == "cancel" || args[0].toStr() == "cancelled"));
    if (inv.t == VT::Hash && inv.hashKind == "Cancellation") {
        auto* cs = static_cast<CueState*>(inv.ext().get());
        if (m == "cancel")    { if (cs) cs->cancelled.store(true); return Value::boolean(true); }
        if (m == "cancelled") return Value::boolean(cs && cs->cancelled.load());
    }
    if (inv.t == VT::Type && (inv.s == "IO::Notification" || inv.s == "IO::Notification::Change") &&
        !classes_.count("IO::Notification::Change") && !notificationLoading_) {
        loadIoNotification();
        if (classes_.count("IO::Notification::Change")) return methodCall(inv, m, args, rwArgs);
    }
    // IO::CatHandle is Raku source (CatHandleSrc.cpp), compiled at first use
    if (inv.t == VT::Type && inv.s == "IO::CatHandle" && !classes_.count("IO::CatHandle") &&
        !catHandleLoading_) {
        catHandleLoading_ = true;
        auto saved = tctx_.cur;
        tctx_.cur = global_;
        try { evalString(kCatHandleSrc); }
        catch (...) { tctx_.cur = saved; catHandleLoading_ = false; throw; }
        tctx_.cur = saved;
        catHandleLoading_ = false;
        if (classes_.count("IO::CatHandle")) return methodCall(inv, m, args, rwArgs);
    }
    if (inv.t == VT::Type && inv.s == "IO::CatHandle" && m == "new" && !classes_.count("IO::CatHandle")) {
        // minimal CatHandle: a sequence of paths/handles slurped in order
        Value v = Value::makeHash(); v.hashKind = "CatHandle";
        Value files = Value::array();
        for (auto& a : args) {
            if (a.t == VT::Array && a.arr()) for (auto& x : *a.arr()) files.arr()->push_back(x);
            else if (a.t != VT::Pair) files.arr()->push_back(a);
        }
        (*v.hash())["files"] = files;
        return v;
    }
    if (inv.t == VT::Hash && inv.hashKind == "CatHandle") {
        if (m == "slurp") {
            std::string out;
            Value files = (*inv.hash())["files"];
            if (files.arr()) for (auto& f : *files.arr()) {
                ValueList none;
                Value p = Value::str(f.toStr()); p.hashKind = "IO"; // .slurp is IO::Path's
                Value one = methodCall(p, "slurp", none);
                out += one.toStr();
            }
            return Value::str(out);
        }
        if (m == "close") return Value::boolean(true);
        // Rakudo has not implemented the write half of CatHandle either — these
        // throw X::NYI there, and roast (A02) checks exactly that
        static const std::set<std::string> catNyi = {
            "flush", "out-buffer", "print", "printf", "print-nl", "put", "say", "write",
            "WRITE", "READ", "EOF"};
        if (catNyi.count(m))
            throw RakuError{Value::typeObj("X::NYI"),
                            m + " is not yet implemented. Sorry."};
        if (m == "slurp-rest")
            throwTyped("X::Obsolete", {{"old", "slurp-rest"}, {"replacement", "slurp"},
                                       {"when", "with IO::CatHandle"}},
                       "Unsupported use of slurp-rest; in Raku please use slurp with IO::CatHandle");
        if (m == "Str") return Value::str("<closed IO::CatHandle>");
    }
    if (inv.t == VT::Type && inv.s == "IO::Special" && m == "new") {
        Value sp = Value::str(args.empty() ? "" : args[0].toStr());
        sp.hashKind = "IO::Special"; return sp;
    }
    if (inv.t == VT::Type && m == "new" &&
        (inv.s == "IntStr" || inv.s == "NumStr" || inv.s == "RatStr" || inv.s == "ComplexStr")) {
        // IntStr.new(42, "42") — the number AND its string face
        Value n = args.empty() ? Value::integer(0) : args[0];
        std::string face = args.size() > 1 ? args[1].toStr() : n.toStr();
        n.hashKind = inv.s;
        n.s = face;
        return n;
    }
    if (inv.t == VT::Type && inv.s == "Version" && m == "new") {
        // A leading `v` in the STRING is a literal PART, not syntax: Rakudo reads
        // `Version.new("v0.0.1")` as parts ("v", 0, 0, 1) — which looks odd, and
        // is exactly how a module detects the mistake. META6 warns 'prefix "v"
        // seen in version string' off `.parts[0] eq "v"` and strips it; stripping
        // it here instead meant that check never fired. (The `v0.0.1` LITERAL is a
        // different path and still parses as 0.0.1.)
        // A LIST is the parts themselves: `Version.new((1,2,3))` and
        // `Version.new($match[0])` are v1.2.3, where stringifying the list
        // gave "1 2 3" and printed `v1 2 3`.
        std::string spelled;
        if (!args.empty() && args[0].t == VT::Array && !args[0].itemized) {
            for (const Value& part : toList(args[0])) {
                if (!spelled.empty()) spelled += '.';
                spelled += part.toStr();
            }
        }
        else if (!args.empty()) spelled = args[0].toStr();
        Value v = Value::str(spelled);
        v.hashKind = "Version";
        return v;
    }
    if ((inv.t == VT::Type && inv.s == "Duration" ||
         inv.isNumeric() && inv.hashKind == "Duration") && m == "new") {
        // Duration is a number of seconds, tagged so .WHAT/.^name answer Duration —
        // and kept EXACT: `Duration.new(4.5)` is the Rat 4.5, not a float
        Value d = args.empty() ? Value::integer(0) : args[0];
        if (!d.isNumeric()) {   // "meow" is X::Str::Numeric, thrown here and not handed back
            d = methodCall(d, "Numeric", ValueList{});
            if (d.t == VT::Hash && d.hashKind == "Failure") methodCall(d, "throw", ValueList{});
        }
        d.hashKind = "";
        d = applyArith("+", d, Value::integer(0));
        // …and a Rat, as Rakudo's `has Rat $.tai` makes it: Duration.new(6) and
        // Duration.new(4.5) - 1.5 are the same value (Inf/NaN stay as they are)
        if (d.t == VT::Int || (d.t == VT::Num && std::isfinite(d.n))) {
            Value r = methodCall(d, "Rat", ValueList{});
            if (r.t == VT::Rat) d = r;
        }
        d.hashKind = "Duration";
        return identify(d);
    }
    if (inv.isNumeric() && (inv.hashKind == "Duration" || inv.hashKind == "Instant")) {
        Value bare = inv; bare.hashKind = "";
        // `.tai` is the Rational underneath (Duration.new(Inf).tai is still one)
        if (m == "tai") return bare.t == VT::Rat ? bare : methodCall(bare, "Rat", ValueList{});
        if (m == "raku" && inv.hashKind == "Duration")
            return Value::str("Duration.new(" + methodCall(bare, "raku", ValueList{}).toStr() + ")");
        if (m == "raku") {   // Rakudo's spelling: back through the POSIX reading, leap flag and all
            bool inLeap = false;
            long long off = posixOffsetForTai(floorSecsLL(bare.toNum()), inLeap);
            Value px = applyArith("-", bare, Value::integer(off));
            return Value::str("Instant.from-posix(" + methodCall(px, "raku", ValueList{}).toStr() +
                              (inLeap ? ",True" : "") + ")");
        }
        if (m == "Num") return Value::number(inv.toNum());
        if (m == "Real" || m == "Bridge" || m == "narrow" || m == "Rat" || m == "FatRat" || m == "Numeric")
            return m == "Real" && inv.hashKind == "Instant" ? inv : methodCall(bare, m, args);
        if (m == "Int") return methodCall(bare, "Int", args);
    }
    if (inv.t == VT::Type && m == "bits") { // native int/num width (2026.06 addition)
        static const std::map<std::string, int> widths = {
            {"int",64},{"uint",64},{"int64",64},{"uint64",64},{"num",64},{"num64",64},
            {"int32",32},{"uint32",32},{"num32",32},{"int16",16},{"uint16",16},
            {"int8",8},{"uint8",8},{"byte",8}};
        auto it = widths.find(inv.s);
        if (it != widths.end()) return Value::integer(it->second);
    }
    // Instant.DateTime — an Instant is posix seconds (tagged Num); build the
    // DateTime from it (zef: `now.DateTime.earlier(:hours(N)).Instant`).
    // `$instant.to-posix` — the POSIX seconds and whether this is a leap second.
    // rakupp's Instant is TAI (POSIX + 10), so the trip back subtracts them.
    if (m == "to-posix" && inv.hashKind == "Instant") { // (any numeric answered it: `5.to-posix` is no method on Rakudo)
        // TAI back to POSIX: the leap seconds inserted so far come off, and the
        // second value says whether the Instant lies INSIDE one (Rakudo's rule)
        Value t = inv; t.hashKind = "";
        bool inLeap = false;
        long long off = posixOffsetForTai(floorSecsLL(t.toNum()), inLeap);
        Value o = Value::array(); o.isList = true;
        o.arr()->push_back(applyArith("-", t, Value::integer(off)));
        o.arr()->push_back(Value::boolean(inLeap));
        return o;
    }
    if (m == "Instant" && (inv.hashKind == "Instant" || (inv.t == VT::Type && inv.s == "Instant")))
        return inv;                                        // an Instant is its own Instant
    if (m == "Date" && inv.hashKind == "Instant" && inv.isNumeric())   // via its UTC DateTime
        return methodCall(methodCall(inv, "DateTime", ValueList{Value::pair("timezone", Value::integer(0))}),
                          "Date", ValueList{});
    // Instants are made from something (`now`, `.from-posix`, `DateTime.Instant`)
    if (inv.t == VT::Type && inv.s == "Instant" && m == "new")
        throwTypedV("X::Cannot::New", {{"class", Value::typeObj("Instant")}},
                    "Cannot make a Instant object using .new");
    if (m == "DateTime" && inv.hashKind == "Instant" && inv.isNumeric()) {
        ValueList mk{inv};  // DateTime.new(Instant) takes the leap seconds back off
        if (sixE()) { // 6.e: `.DateTime(:timezone = $*TZ)`, as on Date
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
    // `Date.new-from-daycount($n)` — days since the Modified Julian Date epoch
    if (inv.t == VT::Type && inv.s == "Date" && m == "new-from-daycount" && !args.empty()) {
        // MJD day 0 is 1858-11-17, which is 40587 days before the civil epoch
        long long y, mo, d;
        daysToCivil(args[0].toInt() - 40587, y, mo, d);
        Value v = Value::makeHash(); v.hashKind = "Date";
        (*v.hash())["year"] = Value::integer(y);
        (*v.hash())["month"] = Value::integer(mo);
        (*v.hash())["day"] = Value::integer(d);
        return v;
    }
    if (inv.t == VT::Type && inv.s == "Instant" && m == "from-posix") {
        // TAI = POSIX + the 10 pre-1972 leap seconds (Instant.from-posix(32) is 42)
        // — and it is an Instant, not a bare Num: untagged, its .^name was "Num"
        // and `===` compared it by value.
        // …and every leap second before it. `from-posix($p, True)` reads a POSIX
        // time two UTC moments share as the leap second rather than the midnight.
        Value p = args.empty() ? Value::integer(0) : args[0];
        if (!p.isNumeric()) p = Value::number(p.toNum());
        p.hashKind = "";
        bool prefer = args.size() > 1 && !args[1].namedArg && args[1].truthy();
        Value v = applyArith("+", p, Value::integer(
            taiOffsetForPosix(floorSecsLL(p.toNum()), prefer)));
        v.hashKind = "Instant";
        return identify(v);
    }
    // `List.tree` / `Array.tree` on a type object is identity (returns the type)
    if (inv.t == VT::Type && m == "tree") return inv;
    // type-level coercions: `DateTime.Date` is Date:U, `Date.DateTime` is DateTime:U
    if (inv.t == VT::Type && (inv.s == "DateTime" || inv.s == "Date") &&
        (m == "Date" || m == "DateTime"))
        return Value::typeObj(m);
    if (inv.t == VT::Type && inv.s == "StrDistance" && m == "new") {
        // StrDistance (the tr/// result value): numifies to .after.chars
        Value h = Value::makeHash(); h.hashKind = "StrDistance";
        for (auto& a2 : args) if (a2.t == VT::Pair && a2.pairVal()) (*h.hash())[a2.s] = *a2.pairVal();
        return h;
    }
    if (inv.t == VT::Type && (inv.s == "Buf" || inv.s == "Blob") && (m == "new" || m == "allocate")) {
        if (m == "allocate") {
            for (size_t k = 1; k < args.size(); k++) // fill args must be numeric-ish
                if (args[k].t == VT::Str && args[k].hashKind.empty())
                    throw RakuError{Value::typeObj("X::TypeCheck"),
                        "Cannot use a Str as a fill value in " + inv.s + ".allocate"};
            // allocate(N) → N zero bytes; allocate(N, fill) → N fills;
            // allocate(N, (list)) → the list repeated cyclically
            long long an = args.empty() ? 0 : args[0].toInt();
            if (an < 0) throw RakuError{Value::typeObj("X::AdHoc"), // Rakudo's message for a negative count
                "Unable to allocate an array of " + std::to_string((unsigned long long)an) + " elements"};
            std::string fill;
            if (args.size() > 1) {
                if (args[1].t == VT::Array || args[1].t == VT::Range)
                    for (auto& e : args[1].flatten()) fill += (char)(unsigned char)(e.toInt() & 0xFF);
                else if (args[1].t == VT::Str && (args[1].hashKind == "Blob" || args[1].hashKind == "Buf"))
                    fill = args[1].s.str();   // another Blob: its bytes are the pattern
                else fill += (char)(unsigned char)(args[1].toInt() & 0xFF);
            }
            if (fill.empty()) fill.push_back('\0');
            std::string bytes;
            for (long long k = 0; k < an; k++) bytes += fill[(size_t)(k % (long long)fill.size())];
            Value b = Value::str(bytes); b.hashKind = inv.s == "Buf" ? "Buf" : "Blob";
            b.s.promote();   // a native buffer needs stable, shared storage
            if (b.hashKind == "Buf") identify(b);
            return b;
        }
        std::string bytes;
        std::function<void(const Value&)> add = [&](const Value& v) {
            if (v.t == VT::Array && v.arr()) { for (auto& e : *v.arr()) add(e); }
            else if (v.t == VT::Range) { for (auto& e : v.flatten()) add(e); } // Buf.new(^10)
            // Buf.new($blob) — the copy candidate — takes the bytes; numifying the
            // Blob here would silently store its element COUNT as the one byte.
            // Rakudo accepts it only as the SOLE argument: anything else goes to
            // `new(*@codes)`, where each element must be a uint8, and a Blob is not.
            else if (v.t == VT::Str && (v.hashKind == "Buf" || v.hashKind == "Blob")) {
                if (args.size() == 1) bytes += v.s;
                else throw RakuError{Value::typeObj("X::TypeCheck"),
                    "Type check failed in initializing an element of " + inv.s +
                    "; expected uint8 but got " + v.hashKind};
            }
            else bytes += (char)(unsigned char)(v.toInt() & 0xFF);
        };
        for (auto& a : args) add(a);
        Value b = Value::str(bytes); b.hashKind = inv.s == "Buf" ? "Buf" : "Blob"; // Buf is mutable
        if (b.hashKind == "Buf") identify(b);
        return b;
    }
    if (inv.t == VT::Type && inv.s == "Pair" && m == "new") {
        Value key = Value::any(), val = Value::any();
        ValueList pos;
        for (auto& x : args) {
            if (x.t == VT::Pair && x.s == "key")        key = x.pairVal() ? *x.pairVal() : Value::any();
            else if (x.t == VT::Pair && x.s == "value") val = x.pairVal() ? *x.pairVal() : Value::any();
            else pos.push_back(x);
        }
        if (!pos.empty())      key = pos[0];
        if (pos.size() >= 2)   val = pos[1];
        Value p = Value::pair(key.toStr(), val);
        if (key.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(key);
        // `Pair.new` BINDS its value exactly as `=>` does (sheet HM-18): a
        // literal is read-only, a variable would carry its container. The
        // argument EXPRESSION is what says which, so ask it.
        if (pos.size() >= 2 && rwArgs && rwArgs->size() > 1) {
            p.pairValRO = !exprNamesContainer((*rwArgs)[1].get());
            // `Pair.new("foo", my Int $)` — a TYPED container: an assignment
            // through `.value` is checked against the type (see lvalue())
            if (const Expr* ae = (*rwArgs)[1].get(); ae && ae->kind == NK::VarExpr) {
                auto* ve = static_cast<const VarExpr*>(ae);
                if (ve->declare && !ve->declType.empty() && !ve->name.empty() && ve->name[0] == '$')
                    p.ofTypeM() = ve->declType;
            }
        }
        return p;
    }

    // Lock / Semaphore. No-ops under the GIL (it already serialises); backed by real
    // primitives in parallel mode so mutual exclusion actually holds.
    if (inv.t == VT::Type && (inv.s == "Lock" || inv.s == "Lock::Async" || inv.s == "Semaphore" ||
                              inv.s == "Lock::Soft")) {   // Lock::Soft: a Lock by another scheduler, same API
        if (m == "new") {
            Value v = Value::makeHash();
            if (inv.s == "Semaphore") {
                v.hashKind = "Semaphore";
                long n = args.empty() ? 1 : args[0].toInt();
                (*v.hash())["count"] = Value::integer(n);
                if (parallelMode_) { auto st = std::make_shared<SemaphoreState>(); st->count = n; v.extM() = st; }
            }
            else {
                // Lock::Async keeps its own type identity (so a `Lock::Async $!l`
                // container accepts it) but shares Lock's method implementations
                // under the cooperative GIL.
                v.hashKind = (inv.s == "Lock::Async") ? "Lock::Async" : "Lock";
                // NOT a real mutex under the GIL, and that is a known deviation
                // rather than a decision: the GIL serialises execution but is
                // released at every blocking point, so a protected block that
                // sleeps or does I/O IS interleaved (measured: a `start` block
                // taking the same Lock runs inside the holder's critical section,
                // where Rakudo makes it wait). Giving it a real recursive_mutex
                // here deadlocks IO::Socket::Async::SSL, whose module-level
                // $lib-lock is held across socket I/O by every socket in the
                // process — so the honest state is a no-op lock plus this note,
                // until await-inside-a-lock releases the lock the way Rakudo's
                // thread-pool await does.
                if (parallelMode_) v.extM() = std::make_shared<LockState>();
            }
            return v;
        }
    }
    // Raku.legacy — a CLASS method (type object only, as in Rakudo): does this
    // runtime still speak the classic pre-RakuAST compiler dialect? Rakudo
    // 2026.07 answers True, and so does rakupp — its use/EXPORT machinery is
    // the classic protocol, not RakuAST. The ecosystem `if` dist gates on
    // exactly this to pick which compiler guts to patch (neither of which
    // exists here — rakupp honors `:if` natively instead, see UseStmt).
    if (inv.t == VT::Type && inv.s == "Raku" && m == "legacy")
        return Value::boolean(!slangHost_); // False inside a slang's scratch host: its actions take the RakuAST branch
    // IO::String / Text::IO::String: an in-memory read handle over a string.
    // $*RAKU / $?RAKU and their .compiler — the runtime/implementation introspection object
    if (inv.t == VT::Hash && (inv.hashKind == "Raku" || inv.hashKind == "Compiler")) {
        bool isComp = inv.hashKind == "Compiler";
        // instance spelling mirrors Rakudo: .legacy is Raku:U:-constrained
        if (m == "legacy" && !isComp)
            throw RakuError{Value::typeObj("X::Parameter::InvalidConcreteness"),
                "Invocant of method 'legacy' must be a type object of type 'Raku', "
                "not an object instance of type 'Raku'.  Did you forget a 'multi'?"};
        std::string nm = isComp ? "Raku++" : "Raku";
        // Language revision the program is running under (6.c/6.d/6.e), from any
        // `use v6.*` pragma; the compiler object keeps its own version string.
        // …from the MAIN unit's revision, not this one's: `$*RAKU` is one object
        // for the process and Rakudo answers the same version inside a module
        // whatever that module's own `use v6.…` says (see rakuIntrospection).
        const int langRevForRaku = mainLangRevSet_ && beginDepth_ == 0 ? mainLangRev_ : langRev_;
        std::string langVer = langRevForRaku == 0 ? "6.c" : (langRevForRaku == 1 ? "6.d" : "6.e");
        if (m == "compiler") return rakuIntrospection(true);
        // rakupp's engine is a C++ tree-walking interpreter, not MoarVM. Rakudo
        // answers the same string here as in `$*VM.name`, so RAKUPP_VM_NAME has
        // to move both or the two spellings of one fact contradict each other.
        if (m == "backend") {
            if (const char* ov = std::getenv("RAKUPP_VM_NAME")) if (*ov) return Value::str(ov);
            return Value::str("cpp");
        }
        if (m == "KERNELnames" || m == "DISTROnames" || m == "VMnames") { // known-platform introspection lists
            Value out = Value::array(); out.isList = true;
            // The VM names are this engine's BACKENDS, the same shape Rakudo's
            // moar/jvm/js is: `cpp` for the interpreter and `--exe`, `js` for
            // `--target=js`. Roast's one hard check on the name (S02-magicals/
            // VM.t) is that `$*VM.name` is a member of this list.
            if (m == "VMnames") {
                out.arr()->push_back(Value::str("cpp")); out.arr()->push_back(Value::str("js"));
                // …and whatever RAKUPP_VM_NAME asked `$*VM.name` to answer, so
                // that Roast's membership check still holds under the override
                // (see vmName() in Interpreter.cpp for what the override is for).
                if (const char* ov = std::getenv("RAKUPP_VM_NAME"))
                    if (*ov && std::string(ov) != "cpp" && std::string(ov) != "js")
                        out.arr()->push_back(Value::str(ov));
                return out;
            }
            out.arr()->push_back(Value::str(m == "KERNELnames" ? platKernelName() : platDistroName()));
            return out;
        }
        if (m == "name") return Value::str(nm);
        // The COMPILER's .version answers in Rakudo's YEAR.MONTH scheme — the era
        // of the Rakudo we verify byte-identity against (the conformance oracle;
        // bump when the oracle bumps). This REVERSES an earlier decision to report
        // rakupp's own release here: ecosystem modules gate with
        // `$*RAKU.compiler.version < v2023.12` to ask "do I have modern
        // semantics?", and answering v1.5.x reads as a pre-2000 Rakudo — every
        // such module refused to load (JSON::Class was the witness). Our own
        // release stays visible in .release/.id, and .name still says who we are.
#ifndef RAKUPP_VERSION
#define RAKUPP_VERSION "0.0.0"
#endif
        // Documented for users in docs/faq/differences.md + REFERENCE.md §12.
        // kOracleEra now lives in Interpreter.h — one definition for this site
        // and the $*RAKU builder in Interpreter.cpp, which used to carry its
        // own copy of the literal.
        if (m == "version" || m == "lang-version") { Value v = Value::str(isComp && m == "version" ? kOracleEra : langVer); v.hashKind = "Version"; return v; }
        // The LANGUAGE's authority is the Raku community; the COMPILER's is
        // whoever wrote it, which for this one is a person, not a foundation.
        if (m == "auth" || m == "authority")
            return Value::str(isComp ? "Andrew Shitov" : "The Raku Community");
        if (m == "desc") return Value::str("Raku++ — a C++ Raku interpreter");
        if (m == "signature") return Value::typeObj("Blob");   // Rakudo: the Blob type object
        if (m == "id" || m == "release") return Value::str(RAKUPP_VERSION);
        // .build / .build-date identify THIS binary, which .id and .release
        // cannot: every build between two releases reports the same version, so
        // a bug report, a Rakugrid oracle stamp and a benchmark row all pointed
        // at "3.14.0" and nothing narrower. `git describe` gives
        // both an ordering (commits since the tag) and an exact commit.
        // Compiler-only: the LANGUAGE has no build.
        if (isComp && m == "build") return Value::str(rakupp::buildId());
        if (isComp && m == "build-date") return Value::str(rakupp::buildDate());
        if (m == "codename") return Value::str("Raku++");
        // Rakudo: `Raku (6.d)` for the language, `rakudo (2026.08)` for the
        // compiler — name plus the version THAT object reports, not the language
        // revision in both. .Str is the bare name.
        if (m == "Str") return Value::str(nm);
        if (m == "gist")
            return Value::str(nm + " (" + (isComp ? kOracleEra : langVer.c_str()) + ")");
        // .raku is the constructor form, which is what `dd $*RAKU` shows. Ours
        // reports real values where Rakudo's are undefined type objects (its
        // .desc and .signature are Str/Blob), so those fields differ in content
        // while the shape matches.
        if (m == "raku") {
            auto q = [](const std::string& x) { return "\"" + x + "\""; };
            std::string self = std::string(isComp ? "Compiler" : "Raku") + ".new("
                + (isComp ? "" : "compiler => " + [&]{ ValueList none;
                       Value c = rakuIntrospection(true);
                       return methodCall(c, "raku", none).toStr(); }() + ", ")
                + "id => " + q(RAKUPP_VERSION) + ", release => " + q(RAKUPP_VERSION)
                + (isComp ? ", build => " + q(rakupp::buildId())
                          + ", build-date => " + q(rakupp::buildDate()) : "")
                + ", codename => " + q("Raku++")
                + ", name => " + q(nm)
                + ", auth => " + q(isComp ? "Andrew Shitov" : "The Raku Community")
                + ", version => v" + (isComp ? kOracleEra : langVer.c_str())
                + ", signature => Blob"
                + ", desc => " + q("Raku++ — a C++ Raku interpreter") + ")";
            return Value::str(self);
        }
    }
    if (inv.t == VT::Type && (inv.s == "ThreadPoolScheduler" || inv.s == "CurrentThreadScheduler")) {
        if (m == "new") { Value s = Value::makeHash(); s.hashKind = "Scheduler"; (*s.hash())["name"] = Value::str(inv.s); return s; }
    }
    if (inv.t == VT::Type && inv.s == "Channel") {
        if (m == "new") {
            Value c = Value::makeHash(); c.hashKind = "Channel";
            (*c.hash())["queue"] = Value::array();
            (*c.hash())["closed"] = Value::boolean(false);
            auto ps = std::make_shared<PromiseState>();          // the `.closed` Promise
            c.extM() = ps;
            Value cp = Value::makeHash(); cp.hashKind = "Promise"; cp.extM() = ps;
            (*cp.hash())["status"] = Value::str("Planned");
            (*c.hash())["closedPromise"] = cp;
            return c;
        }
    }
    // Channel — a thread-safe queue. Under the GIL send/receive are simple deque
    // ops; `.closed` is a Promise kept once the channel is closed AND drained.
    if (inv.t == VT::Hash && inv.hashKind == "Channel") {
        // "a thread-safe queue" is now true OUTSIDE the GIL too: every touch of
        // the queue and the closed/failCause flags happens under the channel's
        // stripe (keyed on the hash — the same pool cas and atomic-* use). The
        // GIL made this free before; in parallel mode concurrent sends corrupted
        // the vector — hangs, lost items, and a spurious "Promise broken" were
        // all one bug. Blocking waits happen OUTSIDE the stripe, or no producer
        // could ever get in to send.
        std::recursive_mutex& chm = atomicStripe(inv.hash());
        PRef<ValueList> qp;
        { std::lock_guard<std::recursive_mutex> lk(chm); qp = (*inv.hash())["queue"].arrS(); }
        auto& q = *qp;
        auto isClosed = [&]() { return (*inv.hash())["closed"].b; };
        auto keepClosedIfDrained = [&]() {
            if (isClosed() && q.empty() && inv.ext()) {
                auto ps = std::static_pointer_cast<PromiseState>(inv.ext());
                bool failed = inv.hash()->count("failCause") > 0;
                std::lock_guard<std::mutex> lk(ps->m);
                if (!ps->done) {
                    if (failed) { ps->broken = true; ps->cause = (*inv.hash())["failCause"]; ps->causeMsg = (*inv.hash())["failCause"].toStr(); }
                    else ps->result = Value::boolean(true);
                    ps->done = true;
                }
                ps->cv.notify_all();
                if (inv.hash()->count("closedPromise")) (*(*inv.hash())["closedPromise"].hash())["status"] = Value::str(failed ? "Broken" : "Kept");
            }
        };
        if (m == "send") {
            Value v = args.empty() ? Value::any() : args[0];
            std::lock_guard<std::recursive_mutex> lk(chm);
            if (isClosed()) throw RakuError{Value::typeObj("X::Channel::SendOnClosed"), "Cannot send a message on a closed channel"};
            q.push_back(v); return v;
        }
        if (m == "poll") {
            std::lock_guard<std::recursive_mutex> lk(chm);
            if (q.empty()) { keepClosedIfDrained(); return Value::nil(); }
            Value v = q.front(); q.erase(q.begin()); keepClosedIfDrained(); return v;
        }
        if (m == "receive") {
          for (;;) {
            // `.receive` BLOCKS until an item arrives (or the channel closes) —
            // under the cooperative GIL that means handing off to the workers that
            // could send. With no async engaged nothing ever could, so answer Nil
            // rather than deadlock; likewise once every worker has finished.
            if (q.empty() && !isClosed() && (gilHeld_ || parallelMode_ || t_poll.isWorker)) {
                // No wall-clock deadline. A 300 ms cap used to stand in for "blocks
                // until an item arrives", which made the wait a RACE against the
                // producer: `start { sleep 0.2; $c.send(...) }` lost it whenever
                // scheduling the worker cost the other 100 ms, and `.receive` then
                // answered Nil — silently, with the program carrying on. That flaked
                // the macOS CI job (t/regression/negated-reduce-and-blocking-receive)
                // on runs that were otherwise green.
                //
                // The condition that actually terminates the wait is already here:
                // once no worker is live and no load is cued, nobody CAN send, so
                // waiting on is a deadlock rather than patience. While a worker is
                // live this waits as long as Rakudo would (measured: a 2 s producer
                // answers at 2005 ms here, 2006 ms there).
                //
                // A finer or backing-off quantum was tried and is WORSE — more GIL
                // churn than the overshoot it saves: S17-channel/stress.t ran 12 s at
                // 20 ms, 29-42 s at 0.5-2 ms. Leave it at 20 ms.
                for (;;) {
                    { std::lock_guard<std::recursive_mutex> lk(chm);
                      if (!q.empty() || isClosed()) break; }
                    // (a WORKER waits regardless: the main thread may still send —
                    // `start { await $c }` then `$c.send(…)` from the mainline)
                    if (!t_poll.isWorker && liveWorkers_.load() <= 0 && cuedLoads_.load() <= 0) break; // nobody left to send
                    // gilHeld_ alone does not mean "cooperative": engageGil sets it
                    // in parallel mode too, where yieldToWorkerFor returns at once.
                    // Gating on it alone made this a hot spin on the stripe, and
                    // macOS mutexes are unfair — the spinning receiver re-took the
                    // lock every time and starved the senders (S17-channel/stress.t
                    // hung after `ok 1`, about 1 run in 13 of its bogosort alone).
                    if (gilHeld_ && !parallelMode_) yieldToWorkerFor(0.02);
                    else std::this_thread::sleep_for(std::chrono::milliseconds(2)); // parallel mode: real wait
                }
            }
            std::lock_guard<std::recursive_mutex> lk(chm);
            if (q.empty()) {
                if (isClosed()) {
                    if (inv.hash()->count("failCause")) throw RakuError{(*inv.hash())["failCause"], "Channel failed"};
                    throw RakuError{Value::typeObj("X::Channel::ReceiveOnClosed"), "Cannot receive a message on a closed channel"};
                }
                // another receiver took the item this one woke for: a WORKER
                // waits again (the mainline keeps its deadlock guard)
                if (t_poll.isWorker) continue;
                return Value::nil(); // nothing running that could ever send
            }
            Value v = q.front(); q.erase(q.begin()); keepClosedIfDrained(); return v;
                  }
        }
        if (m == "close") {
            { std::lock_guard<std::recursive_mutex> lk(chm); (*inv.hash())["closed"] = Value::boolean(true); keepClosedIfDrained(); }
            // A `whenever $c` / `$c.Supply` reader gets to see everything sent
            // before the close returns: `$c.close; is-deeply @seen, …` right
            // after the last send (S17-channel/basic.t) found the reader still
            // parked between polls. Bounded, and never from the reader itself.
            auto readers = [&]() {
                std::lock_guard<std::recursive_mutex> lk(chm);
                auto it = inv.hash()->find("supplyReaders");
                return it != inv.hash()->end() ? it->second.toInt() : 0;
            };
            if (!t_poll.isWorker && readers() > 0) {
                auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (readers() > 0 && std::chrono::steady_clock::now() < until) {
                    if (gilHeld_ && !parallelMode_) yieldToWorkerFor(0.02);
                    else std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
            return Value::boolean(true);
        }
        // a Channel has no element count: it is a stream (Rakudo refuses)
        if (m == "elems" && args.empty())
            throw RakuError{Value::typeObj("X::AdHoc"), "Cannot determine number of elements on a Channel"};
        if (m == "fail") {
            std::lock_guard<std::recursive_mutex> lk(chm);
            (*inv.hash())["closed"] = Value::boolean(true);
            Value cause = args.empty() ? Value::str("Died") : args[0];
            if (cause.t != VT::Object) { // wrap a plain cause in X::AdHoc (like die/break)
                auto xit = classes_.find("X::AdHoc");
                if (xit != classes_.end()) { Value ex; ex.t = VT::Object; ex.setObj(makePayload<ObjectData>()); ex.obj()->cls = xit->second; ex.obj()->attrs["message"] = Value::str(cause.toStr()); ex.obj()->attrs["payload"] = cause; cause = ex; }
            }
            (*inv.hash())["failCause"] = cause;
            // once drained, the .closed Promise breaks with the failure cause
            if (q.empty() && inv.ext()) {
                auto ps = std::static_pointer_cast<PromiseState>(inv.ext());
                std::lock_guard<std::mutex> lk(ps->m);
                if (!ps->done) { ps->broken = true; ps->cause = cause; ps->causeMsg = cause.toStr(); ps->done = true; }
                ps->cv.notify_all();
                if (inv.hash()->count("closedPromise")) (*(*inv.hash())["closedPromise"].hash())["status"] = Value::str("Broken");
            }
            return Value::boolean(true);
        }
        if (m == "closed") { std::lock_guard<std::recursive_mutex> lk(chm); return (*inv.hash())["closedPromise"]; }
        // `.list`/`.Seq` CONSUME a closed channel: they yield the queued values
        // and drain it (so the .closed Promise then keeps). `.Supply` snapshots
        // without draining (a Supply is a re-tappable stream).
        if (m == "list" || m == "Seq") {
            // Rakudo: `.list` DRAINS the channel — it blocks until close and
            // yields everything sent. Snapshotting only the current queue
            // raced the producer once parallel became the default (the GIL's
            // spawn-time handoff used to let the producer finish first).
            // Same wait discipline as `.receive`: patience while somebody
            // could still send, never a deadlock once nobody can.
            Value o = Value::array(); o.isList = true;
            for (;;) {
                bool done = false;
                {   std::lock_guard<std::recursive_mutex> lk(chm);
                    for (auto& x : q) o.arr()->push_back(x);
                    q.clear();
                    done = isClosed();
                }
                if (done) break;
                if (liveWorkers_.load() <= 0 && cuedLoads_.load() <= 0) {
                    // nobody left to send — but the last sender may have sent and
                    // closed BETWEEN the drain above and this check (it decrements
                    // the worker count only after its close lands). One final
                    // locked sweep, or that tail is silently dropped: t/run's
                    // parallel example lost "100 121 144" exactly here once,
                    // under machine load. `.receive` re-checks under its lock
                    // after the same break; `.list` returned without looking.
                    std::lock_guard<std::recursive_mutex> lk(chm);
                    for (auto& x : q) o.arr()->push_back(x);
                    q.clear();
                    break;
                }
                if (gilHeld_ && !parallelMode_) yieldToWorkerFor(0.02);
                else std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            std::lock_guard<std::recursive_mutex> lk(chm);
            keepClosedIfDrained();
            // …and a channel that FAILED raises its cause once its queue has
            // been drained, exactly as `.receive` does: `for @$c { }` over a
            // failed channel must not end quietly (Roast Channel.t).
            if (inv.hash()->count("failCause")) {
                const Value& fc = (*inv.hash())["failCause"];
                std::string msg = "Channel failed";
                if (fc.t == VT::Object && fc.obj()) {
                    auto mit = fc.obj()->attrs.find("message");
                    if (mit != fc.obj()->attrs.end()) msg = mit->second.toStr();
                } else if (fc.t == VT::Str) msg = fc.s.str();
                throw RakuError{fc, msg};
            }
            return o;
        }
        if (m == "Supply") {
            // Every channel is Rakudo's on-demand `supply { whenever $c { emit
            // … } }`: each tap reads what is sent from then on and is done when
            // the channel closes (rakudo#1974, S17-channel/basic.t). It answered
            // a snapshot LIST, which nothing could tap.
            //
            // That includes a LIVE channel (`$supplier.Supply.Channel`). It used
            // to hand back a Supply on the same supplier, which delivered on the
            // EMITTER's thread; Rakudo's goes through the channel's queue on
            // the scheduler, and code relies on the hop. Cro's WebSocket
            // handler feeds the user's block through `.Channel.Supply`, so the
            // block's `await $message.body` waits for fragments that the frame
            // parser — the emitter — has yet to deliver.
            auto env = std::make_shared<Env>();
            env->parent = tctx_.cur ? tctx_.cur : global_;
            env->define("$__rakupp_chan", inv);
            auto saved = tctx_.cur;
            tctx_.cur = env;
            struct Restore { ExecContext& t; std::shared_ptr<Env> e; ~Restore() { t.cur = e; } } restore{tctx_, saved};
            return evalString("supply { whenever $__rakupp_chan -> \\v { emit v } }");
        }
        if (m == "elems") { std::lock_guard<std::recursive_mutex> lk(chm); return Value::integer((long long)q.size()); }
    }
    // Thread — under the GIL a Thread.start runs its block eagerly, but we bump
    // threadDepth_ so `is-initial-thread` correctly reads False inside the block.
    if (inv.t == VT::Type && inv.s == "Thread") {
        if (m == "is-initial-thread") return Value::boolean(threadDepth_ == 0 && !t_poll.isWorker);
        if (m == "start" || m == "run") { // a REAL thread, via the promise machinery
            Value code; for (auto& x : args) if (x.t == VT::Code) code = x;
            Value t = Value::makeHash(); t.hashKind = "Thread";
            for (auto& x : args) if (x.t == VT::Pair && x.s == "name" && x.pairVal()) (*t.hash())["name"] = *x.pairVal();
            (*t.hash())["id"] = Value::integer(newThreadId());   // one sequence with every worker's
            (*t.hash())["initial"] = Value::boolean(false);
            if (code.t == VT::Code) {
                t.extM() = std::static_pointer_cast<void>(
                    std::static_pointer_cast<PromiseState>(spawnPromise(code, t).ext()));
                yieldToWorker();
            }
            return t;
        }
        if (m == "new") {
            Value t = Value::makeHash(); t.hashKind = "Thread";
            for (auto& x : args) { if (x.t == VT::Code) (*t.hash())["code"] = x; else if (x.t == VT::Pair && x.pairVal()) (*t.hash())[x.s] = *x.pairVal(); }
            return t;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "Thread") {
        if (m == "is-initial-thread") return Value::boolean(inv.hash()->count("initial") ? (*inv.hash())["initial"].b : (threadDepth_ == 0));
        if (m == "finish" || m == "join") {
            if (inv.ext()) awaitPromise(std::static_pointer_cast<PromiseState>(inv.ext()));
            return inv;
        }
        if (m == "run" || m == "start") { // Thread.new(:code).run — start it now
            if (inv.hash()->count("code") && !inv.ext()) {
                Value t = inv;
                t.extM() = std::static_pointer_cast<void>(
                    std::static_pointer_cast<PromiseState>(spawnPromise((*inv.hash())["code"]).ext()));
                yieldToWorker();
                return t;
            }
            return inv;
        }
        if (m == "id") return inv.hash()->count("id") ? (*inv.hash())["id"] : Value::integer(1);
        if (m == "name") return inv.hash()->count("name") ? (*inv.hash())["name"] : Value::str("<anon>");
        if (m == "Str" || m == "gist") { // Thread<ID>(NAME)
            std::string id = inv.hash()->count("id") ? (*inv.hash())["id"].toStr() : "1";
            std::string nm = inv.hash()->count("name") ? (*inv.hash())["name"].toStr() : "<anon>";
            return Value::str("Thread<" + id + ">(" + nm + ")");
        }
    }
    // S-03: a Tap can be built directly, with an optional close hook. `.close`
    // answers True, and calling it twice is harmless — the hook simply runs
    // again, which is also what an `.on-close` hook does (S-12).
    if (inv.t == VT::Type && inv.s == "Tap" && m == "new") {
        Value t = Value::makeHash(); t.hashKind = "Tap";
        Value hooks = Value::array();
        for (auto& a : args) if (a.t == VT::Code) hooks.arr()->push_back(a);
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal() && a.s == "close" && a.pairVal()->t == VT::Code)
            hooks.arr()->push_back(*a.pairVal());
        (*t.hash())["closers"] = hooks;
        return t;
    }
    if (inv.t == VT::Type && (inv.s == "Supplier" || inv.s == "Supplier::Preserving")) {
        if (m == "new" || m == "preserving") {
            Value s = Value::makeHash(); s.hashKind = "Supplier"; (*s.hash())["taps"] = Value::array();
            // Supplier::Preserving buffers every emit and replays the buffer to any
            // tap that connects later (Cro emits the request into $!in before the
            // async connect pipeline taps it).
            if (inv.s == "Supplier::Preserving" || m == "preserving") {
                (*s.hash())["preserving"] = Value::boolean(true);
                (*s.hash())["buffer"] = Value::array();
            }
            return s;
        }
    }
    // Supplier: a live push source. Its Supply shares the taps list; emit/done
    // fan out to the taps present at that moment (S-05).
    //
    // Two protocol rules of the semantics sheet live on the tap RECORD rather
    // than on the supplier, because both are per-tap — a Supplier that is done
    // still feeds a tap registered afterwards, which is what Roast's basic.t
    // asserts: `ended` closes a tap to every later event (S-06, the grammar
    // emit* [done|quit]), and `depth` +
    // `pending` implement S-07 — a value emitted from inside a tap's own handler
    // reaches the OTHER taps at once but is deferred for the emitting one until
    // its handler returns, so one tap's handlers never nest.
    if (inv.t == VT::Hash && inv.hashKind == "Supplier") {
        if (m == "Supply") { Value s = Value::makeHash(); s.hashKind = "Supply"; (*s.hash())["supplier"] = inv; return s; } // live (no "values")
        auto H = inv.hash();
        auto tapEnded = [](const Value& t) {
            if (t.t != VT::Hash || !t.hash()) return true;
            auto h = t.hash();
            auto e = h->find("ended");
            if (e != h->end() && e->second.truthy()) return true;
            auto c = h->find("closed");
            return c != h->end() && c->second.truthy();
        };
        auto endTap = [](Value& t) { if (t.hash()) (*t.hash())["ended"] = Value::boolean(true); };
        // Supplier::Preserving keeps the events issued while nobody is listening
        // and replays them to the next first tap (S-09). "Listening" is what
        // decides, so a supplier whose last tap closed starts preserving again.
        bool preserving = H->count("preserving") && (*H)["preserving"].truthy();
        auto anyLiveTap = [&]() {
            auto it = H->find("taps");
            if (it == H->end() || !it->second.arr()) return false;
            for (auto& t : *it->second.arr()) if (!tapEnded(t)) return true;
            return false;
        };
        auto preserve = [&](const char* kind, const Value& v) {
            if (!preserving || anyLiveTap()) return false;
            Value ev = Value::makeHash();
            (*ev.hash())["kind"] = Value::str(kind);
            (*ev.hash())["val"] = v;
            if (!H->count("buffer")) (*H)["buffer"] = Value::array();
            (*H)["buffer"].arr()->push_back(ev);
            return true;
        };
        // One value through one tap: its transform chain, then its handler, with
        // the control exceptions a whenever body can raise.
        auto runEmitOnce = [&](Value& t, const Value& v) {
            bool complete = false;
            ValueList outs = applyTapChain(t, v, complete);
            if (t.hash()->count("emit") && (*t.hash())["emit"].t == VT::Code) {
                // push the tap's react ctx so `done` inside the whenever block
                // closes the enclosing react (the block runs here, on whatever
                // thread emitted — reactStack_ is thread-local, so it wasn't set)
                std::shared_ptr<ReactCtx> rctx = t.ext() ? std::static_pointer_cast<ReactCtx>(t.ext()) : nullptr;
                for (auto& o : outs) {
                    ValueList one{o};
                    if (rctx) reactStack_.push_back(rctx);
                    // `next` in a whenever skips this value; `last` closes the tap
                    try { callCallable((*t.hash())["emit"], one); if (rctx) reactStack_.pop_back(); }
                    catch (NextEx&) { if (rctx) reactStack_.pop_back(); }
                    catch (LastEx&) { if (rctx) reactStack_.pop_back(); (*t.hash())["closed"] = Value::boolean(true); complete = true; break; }
                    catch (DoneEx&) { if (rctx) reactStack_.pop_back(); (*t.hash())["closed"] = Value::boolean(true); complete = true; break; }
                    catch (RakuError& e) {
                        // …but an error ABOUT THE VALUE the emitter passed is the
                        // emitter's: `migrate` on a supply that emits something
                        // that is not a Supply throws where the emit happened
                        // (Roast migrate.t), not into a quit handler that would
                        // be told about somebody else's mistake.
                        if (e.payload.t == VT::Type && e.payload.s == "X::Supply::Migrate::Needs") {
                            if (rctx) reactStack_.pop_back();
                            throw;
                        }
                        // an exception in the tap's block QUITS the tap — the
                        // quit handler gets the exception and the EMITTER is
                        // not unwound (Cro's frame parser dies per malformed
                        // frame; the test taps `quit => { when X::… }`)
                        if (rctx) reactStack_.pop_back();
                        (*t.hash())["closed"] = Value::boolean(true);
                        // …but inside a REACT that rule is inverted: a die in a
                        // whenever BODY kills the whole react and propagates,
                        // and QUIT does NOT see it — QUIT is for the SOURCE's
                        // own quit (issue #18). The interval path already did
                        // this; a Supplier-fed whenever handed the body's death
                        // to QUIT and carried on, so `react { whenever
                        // $s.Supply { die } }` printed the QUIT message and
                        // exited 0 where Rakudo dies.
                        if (rctx) {
                            std::lock_guard<std::mutex> lk(rctx->m);
                            if (!rctx->quitFlag) {
                                rctx->quitFlag = true;
                                rctx->quitErr = e.payload.t == VT::Nil ? Value::str(e.message) : e.payload;
                            }
                            rctx->closed = true;
                            if (rctx->liveSources > 0) rctx->liveSources--;
                            rctx->cv.notify_all();
                            break;
                        }
                        if (t.hash()->count("quit") && (*t.hash())["quit"].t == VT::Code) {
                            ValueList one2{exceptionFor(e)};
                            try { callCallable((*t.hash())["quit"], one2); } catch (...) {}
                        }
                        if (t.ext()) { auto ctx2 = std::static_pointer_cast<ReactCtx>(t.ext()); std::lock_guard<std::mutex> lk(ctx2->m); if (ctx2->liveSources > 0) ctx2->liveSources--; ctx2->cv.notify_all(); }
                        break;
                    }
                    catch (...) { if (rctx) reactStack_.pop_back(); throw; }
                    if (rctx && rctx->closed) break; // `done` inside the block ended the react
                }
            }
            if (complete) { // head(n)/first done → fire the tap's done and release a react source
                (*t.hash())["closed"] = Value::boolean(true);
                if (t.hash()->count("done") && (*t.hash())["done"].t == VT::Code) { ValueList none; callCallable((*t.hash())["done"], none); }
                if (t.ext()) { auto ctx = std::static_pointer_cast<ReactCtx>(t.ext()); std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
            }
        };
        // S-07's deferral, and the drain that follows the handler's return.
        auto feed = [&](Value& t, const Value& v) {
            if (tapEnded(t)) return;
            auto h = t.hash();
            auto d = h->find("depth");
            if (d != h->end() && d->second.toInt() > 0) {
                if (!h->count("pending")) (*h)["pending"] = Value::array();
                (*h)["pending"].arr()->push_back(v);
                return;
            }
            struct Depth {
                ValueMap* h;
                Depth(ValueMap* hh) : h(hh) { (*h)["depth"] = Value::integer(1); }
                ~Depth() { (*h)["depth"] = Value::integer(0); }
            };
            { Depth g(h); runEmitOnce(t, v); }
            for (;;) {
                if (tapEnded(t)) break;
                auto pit = h->find("pending");
                if (pit == h->end() || !pit->second.arr() || pit->second.arr()->empty()) break;
                Value nx = pit->second.arr()->front();
                pit->second.arr()->erase(pit->second.arr()->begin());
                Depth g(h); runEmitOnce(t, nx);
            }
        };
        // Everything below fans out over a SNAPSHOT of the taps: S-05 says the
        // taps present at the moment of the event receive it, and a handler that
        // taps the same supplier must not invalidate the walk.
        auto tapSnapshot = [&]() {
            ValueList out;
            auto it = H->find("taps");
            if (it != H->end() && it->second.arr()) out = *it->second.arr();
            return out;
        };
        if (m == "emit") {
            Value v = args.empty() ? Value::any() : args[0];
            // Rakudo's contract: a Supplier's emissions are SERIALIZED per tap.
            // Under the GIL that held by accident; in parallel mode concurrent
            // emits ran the tap blocks simultaneously and interleaved into the
            // taps vector — the stress suite measured 1,694 of 2,000 arrivals.
            // The supplier's stripe serializes emit/done/quit AND registration.
            std::lock_guard<std::recursive_mutex> emitLk(supplierMutex(H));
            if (preserve("emit", v)) return Value::boolean(true);
            ValueList snap = tapSnapshot();
            for (auto& t : snap) { if (t.t != VT::Hash) continue; feed(t, v); }
            return Value::boolean(true);
        }
        if (m == "done") {
            std::lock_guard<std::recursive_mutex> doneLk(supplierMutex(H));
            // Remember the done state so a tap that registers LATER (an eager
            // `start { $s.emit(…); $s.done }` that ran before the react tapped it)
            // is closed immediately instead of leaving its react source live forever.
            (*H)["done_state"] = Value::boolean(true);
            if (preserve("done", Value::any())) return Value::boolean(true);
            ValueList snap = tapSnapshot();
            for (auto& t : snap) {
                if (t.t != VT::Hash || tapEnded(t)) continue;
                // a `.lines`/`.words` chain may still hold an unterminated last piece:
                // the stream ending is what completes it, so deliver it before `done`
                if (t.hash()->count("chain") &&
                    t.hash()->count("emit") && (*t.hash())["emit"].t == VT::Code) {
                    bool complete = false;
                    ValueList tail = applyTapChain(t, Value::any(), complete, /*flush=*/true);
                    for (auto& o : tail) {
                        ValueList one{o};
                        try { callCallable((*t.hash())["emit"], one); }
                        catch (NextEx&) {} catch (LastEx&) { break; } catch (DoneEx&) { break; }
                    }
                }
                endTap(t);
                if (t.hash()->count("done") && (*t.hash())["done"].t == VT::Code) { ValueList none; callCallable((*t.hash())["done"], none); }
                if (t.ext()) { auto ctx = std::static_pointer_cast<ReactCtx>(t.ext()); std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
            }
            return Value::boolean(true);
        }
        if (m == "quit") {
            std::lock_guard<std::recursive_mutex> quitLk(supplierMutex(H));
            // Recorded for the same reason as done_state: a Supply.wait on a
            // supplier that quits must return, not block forever.
            (*H)["quit_state"] = Value::boolean(true);
            // S-08: a bare string is the payload of an X::AdHoc; an exception
            // object travels as it is.
            Value ex = args.empty() ? Value::any() : args[0];
            if (ex.t == VT::Str) ex = exceptionFor(RakuError{Value::typeObj("X::AdHoc"), ex.s.str(), RakuError::NoCapture{}});
            if (preserve("quit", ex)) return Value::boolean(true);
            ValueList snap = tapSnapshot();
            for (auto& t : snap) {
                if (t.t != VT::Hash || tapEnded(t)) continue;
                endTap(t);
                if (t.hash()->count("quit") && (*t.hash())["quit"].t == VT::Code) { ValueList one{ex}; callCallable((*t.hash())["quit"], one); }
                else {
                    // S-02: the default quit handler RETHROWS at the point where
                    // the source quit, so an unhandled quit surfaces in the
                    // emitter instead of vanishing. The fan-out stops there —
                    // which is what Rakudo's taps observe too.
                    if (t.ext()) { auto ctx = std::static_pointer_cast<ReactCtx>(t.ext()); std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
                    std::string qmsg = "quit";
                    if (ex.t == VT::Object && ex.obj()) {
                        auto mit = ex.obj()->attrs.find("message");
                        if (mit != ex.obj()->attrs.end()) qmsg = mit->second.toStr();
                    } else if (ex.t == VT::Str) qmsg = ex.s.str();
                    throw RakuError{ex, qmsg};
                }
                if (t.ext()) { auto ctx = std::static_pointer_cast<ReactCtx>(t.ext()); std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
            }
            return Value::boolean(true);
        }
        if (m == "Seq" || m == "list") { Value o = Value::array(); o.isList = true; return o; }
    }

    // Supply as a type object: constructors that build an eager, list-backed Supply.
    if (inv.t == VT::Type && inv.s == "Supply") {
        // Supply's own INSTANCE methods need a Supply, not the type object.
        // Without this they fell through to the generic list handlers and
        // answered a Seq, so `dies-ok { Supply.reverse }` — the first assertion
        // of five S17-supply files — did not die. Only the methods Supply
        // actually defines: `Supply.sort` is Any.sort on a type object and must
        // keep working, as it does in Rakudo.
        // …but `collate` is NOT one of them: Roast asks for `Supply.collate` and
        // expects Any's one-element-list answer, `(Supply,).Seq`.
        static const std::set<std::string> kSupplyInstance = {
            "reverse", "words", "lines", "rotor", "produce", "reduce", "batch",
            "head", "tail", "skip", "squish", "unique", "elems", "min", "max",
            "minmax", "sum", "repeated", "roll", "pick", "snip",
        };
        if (kSupplyInstance.count(m.s))
            throw RakuError{Value::typeObj("X::Parameter::InvalidConcreteness"),
                            "Invocant of method '" + m.s + "' must be an object instance of type "
                            "'Supply', not a type object"};
        auto mkSupply = [&](ValueList vals) { Value s = Value::makeHash(); s.hashKind = "Supply"; Value v = Value::array(); *v.arr() = std::move(vals); (*s.hash())["values"] = v; return s; };
        // A combinator over sources that are not all list-backed: the values do
        // not exist yet, so the SPEC is what the method answers and every tap
        // subscribes to each source for itself (tapSupply's "combine" arm).
        auto mkCombine = [&](const char* op, ValueList streams, Value withOp, ValueList initial) {
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash())["kind"] = Value::str("combine");
            (*s.hash())["op"] = Value::str(op);
            Value srcs = Value::array(); *srcs.arr() = std::move(streams);
            (*s.hash())["sources"] = srcs;
            if (withOp.t == VT::Code) (*s.hash())["with"] = withOp;
            if (!initial.empty()) { Value iv = Value::array(); *iv.arr() = std::move(initial); (*s.hash())["initial"] = iv; }
            return s;
        };
        // S-01: a Supply has no public constructor. It comes from a Supplier, from
        // one of the factories, or from a `supply` block — anything else is a
        // programming error, and Rakudo names it X::Supply::New.
        if (m == "new")
            throw RakuError{Value::typeObj("X::Supply::New"),
                            "Cannot directly create a Supply. You might want:\n"
                            " - To use a Supplier in order to get a live supply\n"
                            " - To use Supply.on-demand to create an on-demand supply\n"
                            " - To create a Supply using a supply block"};
        if (m == "from-list") {
            // S-16: the single-argument rule. ONE Iterable argument is flattened
            // into values; SEVERAL arguments are each one value, even when they
            // are themselves lists — `from-list((1,2),(3,4))` is a two-value
            // supply, not a four-value one.
            ValueList out;
            // Reifying the argument is part of the STREAM, not of building it:
            // `Supply.from-list(gather { die })` is a supply that quits, and a
            // `whenever`'s QUIT phaser is entitled to handle it. Letting the
            // death out here would kill the caller instead (Roast syntax.t).
            try {
                // a lazy source is REIFIED here, inside the guard: that is where
                // its values come from, and where a `gather { die }` dies
                for (auto& a : args) forceLazy(a);
                if (args.size() == 1 && args[0].t == VT::Array && args[0].arr() && !args[0].itemized) {
                    for (auto& x : *args[0].arr()) out.push_back(x);
                } else if (args.size() == 1 && args[0].t == VT::Range) {
                    for (auto& x : args[0].flatten()) out.push_back(x);
                } else for (auto& a : args) {
                    // …a Slip is the exception the single-argument rule always
                    // makes: it splices into the slurpy however many arguments
                    // there are.
                    if (a.t == VT::Array && a.arr() && a.s == "Slip") { for (auto& x : *a.arr()) out.push_back(x); }
                    else out.push_back(a);
                }
            } catch (RakuError& e) {
                Value s2 = mkSupply(out);
                (*s2.hash())["quit-reason"] = exceptionFor(e);
                (*s2.hash())["quit-message"] = Value::str(e.message);
                return s2;
            }
            return mkSupply(out);
        }
        if (m == "list") { Value o = Value::array(); o.isList = true; o.arr()->push_back(inv); return o; } // Supply type → (Supply,)
        if (m == "merge") {
            ValueList streams;
            for (auto& a : flattenArgs(args)) {
                if (!(a.t == VT::Hash && a.hashKind == "Supply" && a.hash()))
                    // S-13: the exception names the combinator that rejected it.
                    throwTypedV("X::Supply::Combinator", {{"combinator", Value::str("merge")}},
                                "Can only use valid Supplies with merge");
                streams.push_back(a);
            }
            if (streams.empty()) return mkSupply({});
            if (streams.size() == 1) return streams[0];   // merging one supply is that supply
            bool allListy = true;
            for (auto& a : streams) if (!a.hash()->count("values")) { allListy = false; break; }
            if (allListy) {   // every value is already in hand: concatenate them
                ValueList all;
                for (auto& a : streams) for (auto& x : *(*a.hash())["values"].arr()) all.push_back(x);
                return mkSupply(all);
            }
            return mkCombine("merge", streams, Value::nil(), ValueList{});
        }
        if (m == "zip") {
            // zip N supplies element-wise (stopping at the shortest); an optional
            // :with(&op) combines each row instead of emitting a tuple List.
            ValueList streams; Value withOp;
            for (auto& a : args) {
                if (a.t == VT::Pair && (a.s == "with" || a.s == "as") && a.pairVal()) { withOp = *a.pairVal(); continue; }
                if (!(a.t == VT::Hash && a.hashKind == "Supply" && a.hash()))
                    throwTypedV("X::Supply::Combinator", {{"combinator", Value::str("zip")}},
                                "Can only use valid Supplies with zip");
                streams.push_back(a);
            }
            if (streams.size() == 1 && withOp.t != VT::Code) return streams[0]; // zipping one supply is a === noop
            {   bool allListy = true;
                for (auto& a : streams) if (!a.hash()->count("values")) { allListy = false; break; }
                if (!allListy) return mkCombine("zip", streams, withOp, ValueList{});
            }
            size_t n = SIZE_MAX;
            for (auto& s : streams) n = std::min(n, (*s.hash())["values"].arr()->size());
            if (streams.empty()) n = 0;
            ValueList out;
            for (size_t i = 0; i < n; i++) {
                ValueList row; for (auto& s : streams) row.push_back((*(*s.hash())["values"].arr())[i]);
                if (withOp.t == VT::Code) out.push_back(callCallable(withOp, row));
                else {
                    // S-45: an ITEMIZED List — `$(1, 4)`, one container holding
                    // the row, so a consumer sees one value and not two.
                    Value tup = Value::array(); tup.isList = true; tup.itemized = true;
                    *tup.arr() = std::move(row); out.push_back(tup);
                }
            }
            return mkSupply(out);
        }
        if (m == "interval") { // real ticker: 0 after $delay (default: immediately), then one per $interval
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash())["kind"] = Value::str("interval");
            (*s.hash())["interval"] = args.empty() ? Value::number(1) : args[0];
            double delay = 0;
            for (size_t i = 1; i < args.size(); i++)
                if (args[i].t != VT::Pair) { delay = args[i].toNum(); break; }
            (*s.hash())["delay"] = Value::number(delay);
            // …unless a SCHEDULER was named: then the ticks are its business, and
            // the supply asks it to cue them instead of running a clock of its own
            // (Roast interval.t drives a fake one through virtual time).
            for (auto& a : args)
                if (a.t == VT::Pair && a.pairVal() && a.s == "scheduler" && a.pairVal()->t == VT::Object)
                    (*s.hash())["scheduler"] = *a.pairVal();
            return s;
        }
        if (m == "empty") return mkSupply({});
        // S-46: zip-latest. Once every source has emitted once, each new value
        // from any source emits the LATEST of every source, itemized. `:initial`
        // seeds them in source order, so fewer sources have to speak before the
        // first output; `:with` folds instead of building the tuple. Synchronous
        // sources are subscribed in order, which is why the first source has
        // finished by the time the second starts.
        if (m == "zip-latest") {
            ValueList streams, initial; Value withOp;
            for (auto& a : args) {
                if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "with") { withOp = *a.pairVal(); continue; }
                    if (a.s == "initial") {
                        const Value& iv = *a.pairVal();
                        if (iv.t == VT::Array && iv.arr()) initial = *iv.arr();
                        else if (iv.t == VT::Range) initial = iv.flatten();
                        else initial.push_back(iv);
                        continue;
                    }
                    continue;
                }
                if (!(a.t == VT::Hash && a.hashKind == "Supply" && a.hash()))
                    throwTypedV("X::Supply::Combinator", {{"combinator", Value::str("zip-latest")}},
                                "Can only use valid Supplies with zip-latest");
                streams.push_back(a);
            }
            if (streams.empty()) return mkSupply({});
            if (streams.size() == 1 && withOp.t != VT::Code && initial.empty()) return streams[0];
            {   bool allListy = true;
                for (auto& a : streams) if (!a.hash()->count("values")) { allListy = false; break; }
                if (!allListy) return mkCombine("zip-latest", streams, withOp, initial);
            }
            const size_t n = streams.size();
            std::vector<Value> latest(n);
            std::vector<bool> have(n, false);
            for (size_t i = 0; i < n && i < initial.size(); i++) { latest[i] = initial[i]; have[i] = true; }
            ValueList out;
            for (size_t i = 0; i < n; i++)
                for (auto& v : *(*streams[i].hash())["values"].arr()) {
                    latest[i] = v; have[i] = true;
                    bool all = true;
                    for (size_t k = 0; k < n; k++) if (!have[k]) { all = false; break; }
                    if (!all) continue;
                    ValueList row(latest.begin(), latest.end());
                    if (withOp.t == VT::Code) out.push_back(callCallable(withOp, row));
                    else {
                        Value tup = Value::array(); tup.isList = true; tup.itemized = true;
                        *tup.arr() = std::move(row); out.push_back(tup);
                    }
                }
            return mkSupply(out);
        }
        // S-15: `Supply.on-demand(&producer, :closing)`. Every tap runs the
        // producer afresh, with a Supplier of its own, SYNCHRONOUSLY on the
        // tapping thread — so the values are there before `.tap` returns. It is
        // a supply block with a generated body, which is what gives it the
        // liveness, the completion rule and the die-becomes-quit rule for free.
        if (m == "on-demand") {
            Value producer = (!args.empty() && args[0].t == VT::Code) ? args[0] : Value::nil();
            Value closing;
            for (auto& a : args)
                if (a.t == VT::Pair && a.pairVal() && a.s == "closing") closing = *a.pairVal();
            Value blk; blk.t = VT::Code; blk.setCode(makePayload<Callable>());
            blk.code()->builtin = [producer, closing](Interpreter& I, ValueList&) -> Value {
                auto ctx = I.tctx_.tapStack.empty() ? nullptr : I.tctx_.tapStack.back();
                // :closing belongs to the TAP: it runs once when this activation
                // is torn down — on done, on a quit, or when the tap is closed.
                if (closing.t == VT::Code && ctx) {
                    if (ctx->tap) {
                        std::lock_guard<std::mutex> lk(ctx->tap->m);
                        if (!ctx->tap->closed) ctx->tap->closePhasers.push_back(closing);
                    } else ctx->closers.push_back(closing);
                }
                Value sup = Value::makeHash(); sup.hashKind = "Supplier";
                (*sup.hash())["taps"] = Value::array();
                Value rec = Value::makeHash();
                // routed at THIS activation, not at whatever the emitting thread
                // happens to have on its stack: the producer may hand the
                // Supplier to a `start` block and emit from there.
                Value e; e.t = VT::Code; e.setCode(makePayload<Callable>());
                e.code()->builtin = [ctx](Interpreter& I2, ValueList& a) -> Value {
                    if (!ctx || ctx->done) return Value::boolean(true);
                    Value v = a.empty() ? Value::any() : a[0];
                    if (ctx->collect) { ctx->collect->push_back(v); return Value::boolean(true); }
                    if (ctx->emitCb.t == VT::Code) {
                        ValueList one{v};
                        ctx->emitting++;
                        try { I2.callCallable(ctx->emitCb, one); } catch (...) { ctx->emitting--; throw; }
                        ctx->emitting--;
                    }
                    return Value::boolean(true);
                };
                // `$p.done` ends the SUPPLY, not the producer: the block runs on
                // to its last statement, and its later emits go nowhere.
                Value d; d.t = VT::Code; d.setCode(makePayload<Callable>());
                d.code()->builtin = [ctx](Interpreter& I2, ValueList&) -> Value {
                    if (ctx && !ctx->doneFired) {
                        ctx->done = true; ctx->doneFired = true;
                        if (!ctx->collect) {
                            if (ctx->doneCb.t == VT::Code) { ValueList na; try { I2.callCallable(ctx->doneCb, na); } catch (...) {} }
                            I2.closeTapHandle(ctx->tap);
                        }
                    }
                    return Value::boolean(true);
                };
                Value q; q.t = VT::Code; q.setCode(makePayload<Callable>());
                q.code()->builtin = [ctx](Interpreter& I2, ValueList& a) -> Value {
                    Value ex = a.empty() ? Value::any() : a[0];
                    if (ctx && !ctx->doneFired) {
                        ctx->done = true; ctx->doneFired = true;
                        if (!ctx->collect) {
                            if (ctx->quitCb.t == VT::Code) { ValueList one{ex}; try { I2.callCallable(ctx->quitCb, one); } catch (...) {} }
                            I2.closeTapHandle(ctx->tap);
                        }
                    }
                    return Value::boolean(true);
                };
                (*rec.hash())["emit"] = e; (*rec.hash())["done"] = d; (*rec.hash())["quit"] = q;
                (*sup.hash())["taps"].arr()->push_back(rec);
                ValueList one{sup};
                if (producer.t == VT::Code) I.callCallable(producer, one);
                return Value::any();
            };
            Value s2 = Value::makeHash(); s2.hashKind = "Supply";
            (*s2.hash())["block"] = blk;
            return s2;
        }
    }
    if (inv.t == VT::Type && inv.s == "Promise") {
        Value p = Value::makeHash(); p.hashKind = "Promise";
        if (m == "in" || m == "at") {
            // `.in($n)` is relative, `.at($instant)` absolute — normalize to the
            // absolute fire time (epoch seconds, the `now` clock) at creation, so
            // every consumer waits exactly the remainder (see timerRemainingSecs).
            (*p.hash())["kind"] = Value::str("timer");
            double arg = args.empty() ? 0 : (m == "at" ? instantSecsOf(args[0]) : args[0].toNum());
            (*p.hash())["seconds"] = args.empty() ? Value::number(0) : args[0];
            (*p.hash())["fires_at"] = Value::number(m == "in" ? epochNowSecs() + arg : arg);
            (*p.hash())["status"] = Value::str("Planned");
            return p;
        }
        if (m == "anyof" || m == "allof") {
            (*p.hash())["kind"] = Value::str(m); Value ps = Value::array();
            for (auto& x : flattenArgs(args)) {
                if (!(x.t == VT::Hash && x.hashKind == "Promise"))
                    throw RakuError{Value::typeObj("X::Promise::Combinator"),
                        "Can only create a Promise combinator out of defined Promises"};
                ps.arr()->push_back(x);
            }
            (*p.hash())["promises"] = ps; (*p.hash())["status"] = Value::str("Planned"); return p;
        }
        if (m == "new") {
            // A manual (vow-controlled) promise: starts Planned, later kept/broken.
            auto st = std::make_shared<PromiseState>();
            p.extM() = st;
            (*p.hash())["status"] = Value::str("Planned");
            return p;
        }
        if (m == "start") {
            // Promise.start(&code): run on a worker + cooperative yield, like `start`.
            Value code; for (auto& x : args) if (x.t == VT::Code) code = x;
            if (code.t != VT::Code) {
                auto st = std::make_shared<PromiseState>(); st->done = true; st->result = args.empty() ? Value::any() : args[0];
                p.extM() = st; (*p.hash())["result"] = st->result; (*p.hash())["status"] = Value::str("Kept");
                return p;
            }
            Value pr = spawnPromise(code);
            yieldToWorker();
            return pr;
        }
        if (m == "kept" || m == "broken") {
            auto st = std::make_shared<PromiseState>();
            Value v = args.empty() ? Value::boolean(true) : args[0];
            st->done = true;
            if (m == "broken") {
                // the cause is an EXCEPTION: a plain value rides in an X::AdHoc
                // as its payload, and no value at all is "Died"
                if (args.empty()) v = Value::str("Died");
                if (v.t != VT::Object) {
                    auto xit = classes_.find("X::AdHoc");
                    if (xit != classes_.end()) {
                        Value ex; ex.t = VT::Object; ex.setObj(makePayload<ObjectData>());
                        ex.obj()->cls = xit->second;
                        ex.obj()->attrs["message"] = Value::str(v.toStr());
                        ex.obj()->attrs["payload"] = v;
                        v = ex;
                    }
                }
                st->broken = true; st->cause = v; st->causeMsg = v.toStr();
                (*p.hash())["cause"] = v;
            }
            else st->result = v;
            p.extM() = st;
            if (m != "broken") (*p.hash())["result"] = v;
            (*p.hash())["status"] = Value::str(m == "broken" ? "Broken" : "Kept");
            return p;
        }
    }
    if (inv.t == VT::Type && inv.s == "Proc::Async") {
        if (m == "new") {
            Value p = Value::makeHash(); p.hashKind = "Proc::Async";
            Value argv = Value::array();
            for (auto& x : args) {
                if (x.t == VT::Pair) { // :w opens a stdin pipe at .start; :enc etc. are accepted
                    if (x.s == "w" && (!x.pairVal() || x.pairVal()->truthy())) (*p.hash())["w"] = Value::boolean(true);
                    // `:enc('latin-1')` — what the streams decode and the writes encode with
                    if (x.s == "enc" && x.pairVal()) (*p.hash())["enc"] = Value::str(canonEncodingName(x.pairVal()->toStr()));
                    continue;
                }
                // a Positional arg flattens into the command list (slurpy semantics):
                // zef's zrun-async passes ONE list — `Proc::Async.new((|@_).grep(…))`
                if (x.t == VT::Array && x.arr() && !x.itemized)
                    for (auto& e : *x.arr()) argv.arr()->push_back(e);
                else argv.arr()->push_back(x);
            }
            (*p.hash())["argv"] = argv; (*p.hash())["taps"] = Value::array();
            return p;
        }
    }
    // `$proc.stdout.native-descriptor` — a Promise of the descriptor the
    // stream travels on. An uncaptured stream is the child's inherited copy of
    // OURS, so that is the answer: 1 for stdout, 2 for stderr.
    if (inv.t == VT::Hash && inv.hashKind == "Supply" && inv.hash() && inv.hash()->count("proc") &&
        m == "native-descriptor") {
        auto ps = std::make_shared<PromiseState>();
        Value p = Value::makeHash(); p.hashKind = "Promise"; p.extM() = ps;
        const bool err = inv.hash()->count("stream") && (*inv.hash())["stream"].toStr() == "stderr";
        ps->done = true; ps->result = Value::integer(err ? 2 : 1);
        (*p.hash())["status"] = Value::str("Kept");
        (*p.hash())["result"] = ps->result;
        return p;
    }
    if (inv.t == VT::Hash && inv.hashKind == "Proc::Async") {
        // A stream is either BOUND to a handle or USED as a Supply, never both:
        // X::Proc::Async::BindOrUse whichever comes second
        auto bindOrUse = [&](const std::string& handle, const std::string& use) {
            throwTyped("X::Proc::Async::BindOrUse", {{"handle", handle}, {"use", use}},
                       "Cannot both bind " + handle + " to a handle and also " + use);
        };
        if (m == "stdout" || m == "stderr" || m == "Supply") {
            for (const char* h : {"stdout", "stderr"})
                if ((m == "Supply" || m == h) && inv.hash()->count(std::string("bound-") + h))
                    bindOrUse(h, m == "Supply" ? "get the merged stream" : std::string("get the ") + h + " Supply");
            // the stream has to be asked for BEFORE the spawn (a Supply taken
            // earlier may still be tapped after it — its output is captured)
            if (m != "Supply" && (inv.hash()->count("pid") || inv.hash()->count("spawn-token")))
                throwTyped("X::Proc::Async::TapBeforeSpawn", {{"handle", std::string(m)}},
                           "To avoid data races, you must tap " + std::string(m) + " before running the process");
            // the merged `.Supply` and the separate streams exclude each other
            if (m == "Supply" ? (inv.hash()->count("asked-stdout") || inv.hash()->count("asked-stderr"))
                              : inv.hash()->count("asked-Supply") != 0)
                throwTyped("X::Proc::Async::SupplyOrStd", {},
                           "Using .Supply on a Proc::Async implies merging stdout and stderr; .stdout "
                           "and .stderr cannot therefore be used in combination with it");
            (*inv.hash())["asked-" + std::string(m)] = Value::boolean(true);
            // one stream is EITHER characters or bytes: `.stdout` then `.stdout(:bin)`
            // (or the other way round) is X::Proc::Async::CharsOrBytes
            bool wantBin = false;
            for (auto& a : args)
                if (a.t == VT::Pair && a.s == "bin" && (!a.pairVal() || a.pairVal()->truthy())) wantBin = true;
            for (const char* h : {"stdout", "stderr"})
                if (m == h || m == "Supply") {
                    std::string mk = std::string("mode-") + h;
                    auto it = inv.hash()->find(mk);
                    if (it != inv.hash()->end() && it->second.truthy() != wantBin)
                        throwTyped("X::Proc::Async::CharsOrBytes", {{"handle", h}},
                                   std::string("Can only get ") + h + " as characters or bytes, not both");
                    (*inv.hash())[mk] = Value::boolean(wantBin);
                }
            if (m != "stderr") (*inv.hash())["used-stdout"] = Value::boolean(true);
            if (m != "stdout") (*inv.hash())["used-stderr"] = Value::boolean(true);
            Value s = Value::makeHash(); s.hashKind = "Supply";
            (*s.hash())["proc"] = inv; (*s.hash())["stream"] = Value::str(m);
            // `.stdout(:bin)` asks for the raw bytes: its taps keep Blob-kinded
            // chunks (zef Buf.appends them); a plain stream emits decoded Strs
            for (auto& a : args) {
                if (a.t == VT::Pair && a.s == "bin" && (!a.pairVal() || a.pairVal()->truthy()))
                    (*s.hash())["bin"] = Value::boolean(true);
                if (a.t == VT::Pair && a.s == "enc" && a.pairVal())
                    (*s.hash())["enc"] = Value::str(canonEncodingName(a.pairVal()->toStr()));
            }
            return s;
        }
        if (m == "w") return Value::boolean(inv.hash()->count("w") != 0);   // opened for writing
        // `.pid` — a Promise kept with the process id once it has started
        if (m == "pid") {
            auto pit = inv.hash()->find("pid");
            if (pit != inv.hash()->end()) {
                auto ps = std::make_shared<PromiseState>();
                Value p = Value::makeHash(); p.hashKind = "Promise"; p.extM() = ps;
                ps->done = true; ps->result = pit->second;
                (*p.hash())["status"] = Value::str("Kept");
                (*p.hash())["result"] = ps->result;
                return p;
            }
            ValueList none; return methodCall(inv, "ready", none);
        }
        // `.started` — has `.start` been called (the same marks the second-start check reads)
        if (m == "started")
            return Value::boolean(inv.hash()->count("pid") || inv.hash()->count("spawn-token") ||
                                  inv.hash()->count("started"));
        if (m == "start") {
            // Rakudo throws on a second .start; ours must too, or the realize
            // fallback would spawn the command a second time.
            if (inv.hash()->count("pid") || inv.hash()->count("spawn-token"))
                throw RakuError{Value::typeObj("X::Proc::Async::AlreadyStarted"),
                    "Process has already been started"};
            (*inv.hash())["started"] = Value::boolean(true);   // even when the spawn fails
            Value pr = Value::makeHash(); pr.hashKind = "Promise";
            (*pr.hash())["kind"] = Value::str("proc"); (*pr.hash())["proc"] = inv;
            (*pr.hash())["status"] = Value::str("Planned");
            // record :cwd so the run happens in the right directory — zef's tar
            // extract runs `tar -zxvf <basename>` with :cwd(archive dir)
            std::string cwd;
            std::vector<std::string> envKV; bool haveEnv = false;
            for (auto& a : args) {
                if (a.t == VT::Pair && a.s == "cwd" && a.pairVal()) {
                    cwd = a.pairVal()->toStr();
                    (*pr.hash())["cwd"] = Value::str(cwd);
                }
                // :ENV — the child's ENTIRE environment (Cro hands its services
                // their host and port this way)
                else if (a.t == VT::Pair && a.s == "ENV" && a.pairVal()) {
                    std::map<std::string, std::string> env;
                    if (envPairsFrom(*a.pairVal(), env)) {
                        haveEnv = true;
                        for (auto& kv : env) envKV.push_back(kv.first + "=" + kv.second);
                    }
                }
            }
            if (!haveEnv) syncEnvToProcess();   // the child sees `%*ENV<X> = …` made before it
            // The process spawns HERE — Rakudo's .start means "running from this
            // moment", not "run when awaited" (#29: a fire-and-forget daemon must
            // exist without an await, and outlive us). The promise stays Planned;
            // realizing it (await / whenever / anyof) drains the pipes and reaps.
            std::vector<std::string> argvv;
            if (inv.hash()->count("argv") && (*inv.hash())["argv"].arr())
                for (auto& x : *(*inv.hash())["argv"].arr()) argvv.push_back(x.toStr());
            if (!argvv.empty()) {
                auto takeFd = [&](const char* k) -> long long {
                    auto f = inv.hash()->find(k); if (f == inv.hash()->end()) return -1;
                    long long v = f->second.toInt(); inv.hash()->erase(f); return v;
                };
                auto tapped = [&](const char* k) {
                    auto t = inv.hash()->find(k);
                    return t != inv.hash()->end() && t->second.arr() && !t->second.arr()->empty();
                };
                SpawnStdio io;
                bool outBound = false, errBound = false;
#if !defined(_WIN32)
                io.stdinFd  = (int)takeFd("bind-in-fd");
                // `:w` — a fresh pipe: the child reads its end, we keep the write
                // end for .print/.write/.close-stdin (every write used to answer
                // True and reach nobody, and the child read OUR stdin)
                if (io.stdinFd < 0 && inv.hash()->count("w")) {
                    int wp[2];
                    if (pipe(wp) == 0) {
                        fcntl(wp[0], F_SETFD, FD_CLOEXEC); fcntl(wp[1], F_SETFD, FD_CLOEXEC); // dup2 onto 0 clears it for the child
                        io.stdinFd = wp[0];
                        (*inv.hash())["stdin-wfd"] = Value::integer(wp[1]);
                    }
                }
                io.stdoutFd = (int)takeFd("bind-out-fd");
                io.stderrFd = (int)takeFd("bind-err-fd");
                outBound = io.stdoutFd >= 0; errBound = io.stderrFd >= 0;
#else
                takeFd("bind-in-fd"); takeFd("bind-out-fd"); takeFd("bind-err-fd");
#endif
                // a tapped stream is captured and fed to the taps at realize; an
                // untapped, unbound one is INHERITED, like Rakudo (the old
                // capture-and-discard was a relic of the lazy model)
                // …or merely ASKED FOR: a Supply taken before the start may be
                // tapped after it, and must not have lost the output meanwhile
                io.captureOut = !outBound && (tapped("taps") || inv.hash()->count("used-stdout"));
                io.captureErr = !errBound && (tapped("taps-err") || inv.hash()->count("used-stderr"));
                SpawnedChild sc = spawnChildStart(argvv, cwd, haveEnv ? &envKV : nullptr, io);
#if !defined(_WIN32)
                // the child holds its dup2'd copies of the bind ends; ours must
                // go now, or a bound reader would never see EOF
                if (io.stdinFd  >= 0) close(io.stdinFd);
                if (io.stdoutFd >= 0) close(io.stdoutFd);
                if (io.stderrFd >= 0) close(io.stderrFd);
#endif
                if (sc.pid) {
                    (*inv.hash())["pid"] = Value::integer(sc.pid);
                    long long tok;
                    { std::lock_guard<std::mutex> lk(g_spawnedM);
#if !defined(_WIN32)
                      // reap what earlier fire-and-forgets left behind: a long-
                      // lived program (Sparky) may never await its children, and
                      // zombies must not accumulate. The status is kept, so a
                      // late await still sees the real exitcode.
                      for (auto& kv : g_spawned) {
                          if (kv.second.reaped) continue;
                          int st = 0;
                          if (waitpid((pid_t)kv.second.pid, &st, WNOHANG) == (pid_t)kv.second.pid) {
                              kv.second.reaped = true; kv.second.rawStatus = st;
                          }
                      }
#endif
                      tok = ++g_spawnedSeq; g_spawned[tok] = sc; }
                    (*inv.hash())["spawn-token"] = Value::integer(tok);
                }
            }
            // Inside a supply block nothing will realize the promise while the
            // block keeps working — Cro's runner starts a long-lived service,
            // relays its output through `whenever $proc.stdout.lines` and goes
            // on watching files. So the process is driven as Rakudo drives it:
            // a worker feeds the taps as output arrives (the GIL parked while
            // it waits) and keeps a real promise with the Proc when it exits.
            if (!tctx_.tapStack.empty() && inv.hash()->count("spawn-token")) {
                Value lazyP = pr;
                Value drive; drive.t = VT::Code; drive.setCode(makePayload<Callable>());
                drive.code()->builtin = [lazyP](Interpreter& I2, ValueList&) mutable -> Value {
                    I2.runProcPromise(lazyP, 0);
                    Value procv = (*lazyP.hash())["proc"];
                    procv.hashKind = "Proc";
                    return procv;
                };
                return spawnPromise(drive);
            }
            return pr;
        }
        // `.command` is the argv the process was constructed with, as a List
        if (m == "command") {
            auto it = inv.hash()->find("argv");
            Value out = Value::array(); out.isList = true;
            if (it != inv.hash()->end() && it->second.arr()) *out.arr() = *it->second.arrS();
            return out;
        }
        // `.ready` is Rakudo's "the process has started" Promise, kept with the
        // PID — real from `.start` on, now that the spawn is eager. One consulted
        // BEFORE the start has no PID to give and answers Nil — which is also
        // what Rakudo before 2018.04 did, and what Sparrow6's
        // `whenever $proc.ready` (an empty body) expects either way.
        if (m == "ready") {
            // started already: kept with the PID
            auto pit = inv.hash()->find("pid");
            if (pit != inv.hash()->end()) {
                auto ps = std::make_shared<PromiseState>();
                Value p = Value::makeHash(); p.hashKind = "Promise"; p.extM() = ps;
                ps->done = true; ps->result = pit->second;
                (*p.hash())["status"] = Value::str("Kept");
                (*p.hash())["result"] = ps->result;
                return p;
            }
            // a program that is not there: the promise is broken, with X::OS
            if (std::string why = procSpawnMissing(inv); !why.empty()) {
                auto ps = std::make_shared<PromiseState>();
                Value p = Value::makeHash(); p.hashKind = "Promise"; p.extM() = ps;
                ps->done = true; ps->broken = true;
                ps->cause = makeTypedEx("X::OS", {{"os-error", Value::str(why)}}, why);
                ps->causeMsg = why;
                (*p.hash())["status"] = Value::str("Broken");
                return p;
            }
            Value pr = Value::makeHash(); pr.hashKind = "Promise";
            (*pr.hash())["kind"] = Value::str("proc-ready");
            (*pr.hash())["proc"] = inv;
            (*pr.hash())["status"] = Value::str("Planned");
            return pr;
        }
        // $cat.bind-stdin($echo.stdout) — one process's stream wired straight
        // into another's stdin, as a REAL pipe between the two children. Both
        // spawn at .start and run concurrently, so a stream larger than the pipe
        // buffer cannot deadlock. The pipe is made here, before either start;
        // each end waits on its proc's hash for the spawn to dup2 it into place.
        // Both ends carry FD_CLOEXEC, so the reader's EOF arrives exactly when
        // the writer exits. First binding wins; after a start it's too late.
        // `.bind-stdin($fh)` / `.bind-stdout($fh)` / `.bind-stderr($fh)` — the
        // child's stream IS that handle's file. This IO layer keeps no open
        // descriptor on a path-opened handle, so the bind opens the path (a
        // standard handle lends its own 0/1/2); the spawn dup2s it into place.
        if ((m == "bind-stdin" || m == "bind-stdout" || m == "bind-stderr") && !args.empty() &&
            args[0].t == VT::Hash && args[0].hashKind == "FileHandle" && args[0].hash()) {
            const bool in = m == "bind-stdin";
            const std::string stream = m.s.substr(5);   // stdin / stdout / stderr
            if (in && inv.hash()->count("w")) bindOrUse("stdin", "use :w");
            if (!in && inv.hash()->count("used-" + stream)) bindOrUse(stream, "get the " + stream + " Supply");
#if !defined(_WIN32)
            const Value& h = args[0];
            int fd = -1;
            auto st = h.hash()->find("std");
            auto fdi = h.hash()->find("fd");
            auto pth = h.hash()->find("path");
            if (st != h.hash()->end()) {
                std::string w = st->second.toStr();
                fd = fcntl(w == "in" ? 0 : w == "err" ? 2 : 1, F_DUPFD_CLOEXEC, 3);
            }
            else if (fdi != h.hash()->end()) fd = fcntl((int)fdi->second.toInt(), F_DUPFD_CLOEXEC, 3);
            else if (pth != h.hash()->end())
                fd = ::open(pth->second.toStr().c_str(),
                            in ? (O_RDONLY | O_CLOEXEC) : (O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC), 0666);
            if (fd >= 0) {
                const char* key = in ? "bind-in-fd" : stream == "stdout" ? "bind-out-fd" : "bind-err-fd";
                (*inv.hash())[key] = Value::integer(fd);
                // the child writes the file now: the handle's own close must
                // APPEND its (empty) buffer, not truncate the child's output away
                if (!in) (*h.hash())["wrote"] = Value::boolean(true);
            }
#endif
            if (!in) (*inv.hash())["bound-" + stream] = Value::boolean(true);
            return Value::boolean(true);
        }
        if (m == "bind-stdin") {
#if !defined(_WIN32)
            if (!args.empty() && args[0].t == VT::Hash && args[0].hashKind == "Supply" &&
                args[0].hash() && args[0].hash()->count("proc") &&
                !inv.hash()->count("pid") && !inv.hash()->count("bind-in-fd")) {
                Value src = (*args[0].hash())["proc"];
                std::string stream = args[0].hash()->count("stream") ? (*args[0].hash())["stream"].toStr() : "stdout";
                if (src.hash() && !src.hash()->count("pid")) {
                    int p[2];
                    if (pipe(p) == 0) {
                        fcntl(p[0], F_SETFD, FD_CLOEXEC); fcntl(p[1], F_SETFD, FD_CLOEXEC);
                        (*inv.hash())["bind-in-fd"] = Value::integer(p[0]);
                        (*src.hash())[stream == "stderr" ? "bind-err-fd" : "bind-out-fd"] = Value::integer(p[1]);
                    }
                }
            }
#endif
            return Value::boolean(true);
        }
        // .kill sends a real signal (default SIGHUP, like Rakudo) — the process
        // has existed since .start. Realizing the promise still drains and reaps,
        // so an `await` after the kill returns. Skipped once the exitcode is in:
        // that pid is dead and may have been recycled.
        if ((m == "kill" || m == "close-stdin") && !inv.hash()->count("pid") && !inv.hash()->count("spawn-token") &&
            !inv.hash()->count("started"))
            throwTyped("X::Proc::Async::MustBeStarted", {{"method", m}},
                       "Process must be started first before calling '" + std::string(m) + "'");
        if (m == "kill") {
            auto pidIt = inv.hash()->find("pid");
            if (pidIt != inv.hash()->end() && !inv.hash()->count("exitcode")) {
#if defined(_WIN32)
                auto tokIt = inv.hash()->find("spawn-token");
                if (tokIt != inv.hash()->end()) {
                    std::lock_guard<std::mutex> lk(g_spawnedM);
                    auto it2 = g_spawned.find(tokIt->second.toInt());
                    if (it2 != g_spawned.end() && it2->second.hProcess)
                        TerminateProcess(it2->second.hProcess, 1);
                }
#else
                // if the zombie sweep already reaped it, the pid may have been
                // recycled — signalling it would hit an innocent process
                bool gone = false;
                auto tokIt = inv.hash()->find("spawn-token");
                if (tokIt != inv.hash()->end()) {
                    std::lock_guard<std::mutex> lk(g_spawnedM);
                    auto it2 = g_spawned.find(tokIt->second.toInt());
                    gone = it2 != g_spawned.end() && it2->second.reaped;
                }
                if (!gone) {
                    int sig = -1;
                    for (auto& a : args) if (a.t != VT::Pair) { sig = signalNumberOf(a); break; }
                    if (sig <= 0) sig = SIGHUP;
                    ::kill((pid_t)pidIt->second.toInt(), sig);
                }
#endif
            }
            return Value::boolean(true);
        }
        if (m == "close-stdin" || m == "print" || m == "say" || m == "write" || m == "put") {
#if defined(_WIN32)
            return Value::boolean(true);
#else
            if (!inv.hash()->count("w"))
                throwTyped("X::Proc::Async::OpenForWriting", {{"method", m}},
                    "Process must be opened for writing with :w to call '" + std::string(m) + "'");
            auto wf = inv.hash()->find("stdin-wfd");
            if (wf == inv.hash()->end()) {
                if (!inv.hash()->count("pid") && !inv.hash()->count("started"))
                    throwTyped("X::Proc::Async::MustBeStarted", {{"method", m}},
                               "Process must be started first before calling '" + std::string(m) + "'");
                throw RakuError{Value::typeObj("X::Proc::Async::OpenForWriting"), "The process's standard input is already closed"};
            }
            int wfd = (int)wf->second.toInt();
            if (m == "close-stdin") { ::close(wfd); inv.hash()->erase(wf); return Value::boolean(true); }
            // the bytes go out now; the Promise is kept with their count (Rakudo)
            std::string data = args.empty() ? "" : (m == "say" ? gistOf(args[0]) : args[0].toStr()); // a Blob is a byte-Str
            if (m == "say" || m == "put") data += "\n";
            // text goes out in the process's encoding (a Blob is bytes already)
            if (m != "write") {
                auto ei = inv.hash()->find("enc");
                if (ei != inv.hash()->end()) {
                    std::string enc = ei->second.toStr();
                    if (!enc.empty() && enc != "utf-8" && enc != "utf8") data = encodeTextEnc(data, enc);
                }
            }
            auto ps = std::make_shared<PromiseState>();
            Value p = Value::makeHash(); p.hashKind = "Promise"; p.extM() = ps;
            bool ok = true; size_t off = 0; int werr = 0;
            bool parked = gilPark(); // a full pipe blocks until the child reads
            while (off < data.size()) {
                ssize_t n = ::write(wfd, data.data() + off, data.size() - off);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) { ok = false; werr = errno; break; }
                off += (size_t)n;
            }
            gilUnpark(parked);
            ps->done = true;
            if (ok) { ps->result = Value::integer((long long)data.size()); (*p.hash())["status"] = Value::str("Kept"); (*p.hash())["result"] = ps->result; }
            else { ps->broken = true; ps->cause = Value::typeObj("X::IO"); ps->causeMsg = "Cannot write to the process's standard input: " + std::string(std::strerror(werr)); (*p.hash())["status"] = Value::str("Broken"); }
            return p;
#endif
        }
        // after runProcPromise stored the exit status on the proc:
        if (m == "exitcode") { auto it = inv.hash()->find("exitcode"); return it != inv.hash()->end() ? it->second : Value::integer(-1); }
        // the signal the process died from; 0 for a normal exit (TAP's Status
        // folds it as `exit +< 8 +| signal`)
        if (m == "signal") { auto it = inv.hash()->find("signal"); return it != inv.hash()->end() ? it->second : Value::integer(0); }
        if (m == "so" || m == "Bool") {
            auto it = inv.hash()->find("exitcode"), sg = inv.hash()->find("signal");
            return Value::boolean(it != inv.hash()->end() && it->second.toInt() == 0 &&
                                  (sg == inv.hash()->end() || sg->second.toInt() == 0));
        }
    }
    return std::nullopt;   // not handled here — fall through to the next segment
}

} // namespace rakupp
