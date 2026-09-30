// Builtins.cpp — helpers (processes, taps, .raku, sprintf, signatures, JSON), methodCall and methodCallInner — the first segment of the method-dispatch chain
//
// One of the parts BuiltinsParts.h lists; what they share is declared there.
#include "BuiltinsParts.h"

namespace rakupp {

// One mutex PER SUPPLIER, not a slot from the shared 64-stripe pool. The
// serialization contract (emissions are serialized per supplier, and done/quit
// callbacks join that order) requires holding a lock ACROSS user code — the
// tap blocks — and pool stripes must never do that: an unrelated container
// hashing onto the same slot turns hold-plus-wait into ABBA (the cas code
// form's livelock, thread.t). A dedicated mutex keeps the contract and removes
// cross-object collisions; the residual risk is a genuine user-level cycle
// (two suppliers emitting into each other's taps), which serializing
// implementations cannot avoid. Registry entries are never reclaimed — one
// mutex per supplier ever created, and address reuse just reuses a mutex.
std::recursive_mutex& supplierMutex(const void* key) {
    static std::mutex regM;
    static std::map<const void*, std::unique_ptr<std::recursive_mutex>> reg;
    std::lock_guard<std::mutex> lk(regM);
    auto& p = reg[key];
    if (!p) p = std::make_unique<std::recursive_mutex>();
    return *p;
}

// The subtest frame Test.rakumod wraps around a block: the "# Subtest:" banner,
// a plan and numbering of its own, and the single ok that carries the verdict to
// the enclosing level. `subtest` and `throws-like` share it because in Rakudo a
// throws-like IS a subtest — reporting it as one flat ok hid every assertion
// inside it (20 of S03-operators/arith.t's 184) and printed the wrong name.
bool Interpreter::runSubtestFrame(const std::string& desc,
                                 const std::function<void()>& body) {
    Interpreter& I = *this;
    // A pending `todo` marks this whole subtest TODO: inner failures neither die nor count.
    bool todod = false; std::string todoReason;
    if (I.todoRemaining_ > 0) { todod = true; todoReason = I.todoReason_; I.todoRemaining_--; }
    bool savedFailed = I.subtestFailed_;
    long savedPlanned = I.planned_, savedTestNum = I.testNum_; // a subtest has its own plan + numbering
    long savedFailCount = I.failCount_;
    // the "# Subtest: <name>" banner, at the ENCLOSING level's indent —
    // the TAP module's strict Sub-Test parser keys nested blocks off it
    std::cout << std::string(4 * I.subtestDepth_, ' ')
              << "# Subtest" << (desc.empty() ? "" : ": " + desc) << "\n";
    I.subtestDepth_++;
    std::string savedTodoSubReason = I.todoSubtestReason_;
    if (todod) { I.todoSubtestDepth_++; I.todoSubtestReason_ = todoReason; }
    I.subtestFailed_ = false;
    I.planned_ = -1; I.testNum_ = 0;
    // A pending `todo` counts tests in the CONTEXT it was written in, and a
    // subtest is a context of its own. Leaving the counter live let the FIRST
    // assertion inside the subtest eat one of the marks — so `todo(…, 2)` before
    // two `throws-like`s marked the first one and its inner "code dies", and the
    // second throws-like counted as a real failure (S15-unicode-information/
    // uniname.t). The subtest's own mark was taken above and is kept in `todod`.
    int savedTodoLeft = I.todoRemaining_; std::string savedTodoWhy = I.todoReason_;
    I.todoRemaining_ = 0; I.todoReason_.clear();
    try { body(); }
    catch (RakuError& e) {
        // the exception is the subtest's failure — but it must be SEEN:
        // silently marking the subtest failed hid a "No such private
        // method '!cursor_init'" for a whole String::Utils run
        I.subtestFailed_ = true;
        std::cerr << std::string(4 * I.subtestDepth_, ' ') << "# " << e.message << "\n";
    }
    I.todoRemaining_ = savedTodoLeft; I.todoReason_ = savedTodoWhy;
    // …and a plan that was not met fails the subtest, as Test.pm6's does
    // ("planned 3 tests, but ran 0" is how a loop that never ran shows)
    if (I.planned_ >= 0 && I.testNum_ != I.planned_) {
        std::cerr << std::string(4 * I.subtestDepth_, ' ') << "# Looks like you planned "
                  << I.planned_ << " test" << (I.planned_ == 1 ? "" : "s") << ", but ran "
                  << I.testNum_ << "\n";
        I.subtestFailed_ = true;
    }
    bool ok = !I.subtestFailed_;
    // no plan declared inside: the subtest's own trailing plan line closes
    // its block ("    1..N"), exactly as done-testing would have printed it
    if (I.planned_ < 0)
        std::cout << std::string(4 * I.subtestDepth_, ' ') << "1.." << I.testNum_ << "\n";
    if (todod) I.todoSubtestDepth_--;
    I.todoSubtestReason_ = savedTodoSubReason;
    I.subtestDepth_--;
    I.subtestFailed_ = savedFailed;
    I.planned_ = savedPlanned; I.testNum_ = savedTestNum; I.failCount_ = savedFailCount;
    I.emitTest(ok, desc, todod ? ("TODO" + (todoReason.empty() ? "" : " " + todoReason)) : "");
    return ok;
}


// A CORE type name — the set `.^add_method` may extend. isKnownTypeName is too
// loose for that: it blanket-accepts any X::, Metamodel:: or IO:: prefix, so a
// typo'd `Metamodel::Whatever.^add_method` would silently succeed there.
bool isCoreTypeName(const std::string& n) {
    if (n.empty()) return false;
    if (n.rfind("X::", 0) == 0 || n.rfind("Metamodel::", 0) == 0) return false;
    return isKnownTypeName(n);
}

// The handful of exception classes rakupp throws that Raku does not have. Each
// one is here because the situation it names has no Raku type of its own and
// the name says more than Rakudo's answer does — but a program written against
// Rakudo catches Rakudo's answer, so each is given that answer as its PARENT.
// `when X::AdHoc` then matches on both engines while `$e.^name` still says
// which operation failed. The parent is not a guess: it is what Rakudo throws
// for the same code, probed and recorded beside each row.
//
// A name that Raku DOES have never belongs here — it belongs in the generated
// table, which is Rakudo's own hierarchy. This list existing at all is a cost;
// keep it short, and prefer deleting a row (by throwing Rakudo's class) to
// adding one. Two came off it that way rather than going on it:
// X::DateTime::InvalidFormat, which had one stray throw site where every other
// spelled it X::Temporal::InvalidFormat as Rakudo does, and X::Buf::RO, whose
// honest parent would be X::Method::NotFound — Rakudo gives Blob no
// `write-int8` at all, so the divergence there is the METHOD existing, and no
// ancestry can paper over that.
static const char* rakuppOnlyExceptionParent(const std::string& n) {
    static const std::map<std::string, const char*> only = {
        // "Too few positionals passed; expected 2 arguments but got 1"
        {"X::Signature::ArityMismatch", "X::AdHoc"},
        // "Required named parameter 'b' not passed"
        {"X::Parameter::RequiredNamed", "X::AdHoc"},
        // "Cannot specify :at and :in at the same time"
        {"X::Scheduler::Cue",           "X::AdHoc"},
        // The IO family. Rakudo answers every one of these X::AdHoc, which is
        // not a decision it made: X::AdHoc is what a `die` with a plain value is
        // wrapped in, and its .payload here is the message string — so Rakudo's
        // core simply dies with a string and never classifies the failure. The
        // cost of copying that is real, because the message does not classify it
        // either: "Failed to open file /gone/f" is what a failed slurp, spurt
        // and open all say. These names say which; the parent keeps the
        // `when X::AdHoc` such code is written against firing.
        {"X::IO::Open",                 "X::AdHoc"},
        {"X::IO::Spurt",                "X::AdHoc"},
        {"X::IO::Exists",               "X::AdHoc"},
        {"X::IO::Exclusive",            "X::AdHoc"},
        // No Rakudo counterpart at all: this is a --slim cut answering for a
        // feature that was not built in. X::AdHoc is where an unclassified
        // engine failure belongs.
        {"X::Feature::NotBuilt",        "X::AdHoc"},
        // …and rakupp's recursion cap, which Rakudo does not have — it grows
        // the stack until the OS stops it, with no exception to copy.
        {"X::Recursion",                "X::AdHoc"},
    };
    auto it = only.find(n);
    return it == only.end() ? nullptr : it->second;
}

// An X:: name's ancestry: itself, whatever the generated table says it derives
// from or does, then Exception, Any, Mu. Those last three are implied for every
// X:: name, table or no table — a `class X::Mine` a user writes IS an Exception,
// and so is a name rakupp invents at a throw site.
//
// Cached because the return is a REFERENCE and the chain is built per name;
// thread_local because a `start` block asks the same questions on its own
// thread. std::map nodes are stable, so the reference outlives later inserts.
static const std::vector<std::string>& exceptionAncestry(const std::string& t) {
    thread_local std::map<std::string, std::vector<std::string>> cache;
    auto it = cache.find(t);
    if (it != cache.end()) return it->second;
    auto split = [](std::vector<std::string>& out, const char* extras) {
        for (const char* p = extras; *p; ) {
            const char* c = std::strchr(p, ',');
            out.emplace_back(p, c ? (size_t)(c - p) : std::strlen(p));
            if (!c) break;
            p = c + 1;
        }
    };
    std::vector<std::string> v{t};
    if (const char* extras = exceptionExtraAncestry(t)) split(v, extras);
    else if (const char* parent = rakuppOnlyExceptionParent(t)) {
        // the parent, and then the parent's OWN chain — the generated table
        // holds the transitive closure, so one lookup finishes the walk
        v.emplace_back(parent);
        if (const char* up = exceptionExtraAncestry(parent)) split(v, up);
    }
    v.insert(v.end(), {"Exception", "Any", "Mu"});
    return cache.emplace(t, std::move(v)).first->second;
}

// The built-in type lattice, narrowest-first, widest-last: read by .isa/.does/
// .^mro, `.are` (via lubType) and the augment lookup.
const std::vector<std::string>& typeAncestry(const std::string& t) {
    static const std::map<std::string, std::vector<std::string>> A = {
        {"Int",     {"Int","Real","Numeric","Cool","Any","Mu"}},
        // a value type's identity IS an ObjAt, the narrower kind of one
        {"ValueObjAt", {"ValueObjAt","ObjAt","Any","Mu"}},
        // an allomorph is Allomorph, Str AND its number (Rakudo's MRO order)
        {"IntStr",     {"IntStr","Allomorph","Str","Int","Stringy","Real","Numeric","Cool","Any","Mu"}},
        {"RatStr",     {"RatStr","Allomorph","Str","Rat","Stringy","Rational","Real","Numeric","Cool","Any","Mu"}},
        {"NumStr",     {"NumStr","Allomorph","Str","Num","Stringy","Real","Numeric","Cool","Any","Mu"}},
        {"ComplexStr", {"ComplexStr","Allomorph","Str","Complex","Stringy","Numeric","Cool","Any","Mu"}},
        {"Rat",     {"Rat","Rational","Real","Numeric","Cool","Any","Mu"}},
        // FatRat is NOT a Rat in Rakudo — both DO Rational, and its MRO is
        // FatRat/Cool/Any/Mu. Claiming the inheritance made `when Rat` swallow a
        // FatRat, so DBDish::mysql sent one as a double instead of a decimal.
        {"FatRat",  {"FatRat","Rational","Real","Numeric","Cool","Any","Mu"}},
        {"Rational",{"Rational","Real","Numeric","Cool","Any","Mu"}},
        {"Num",     {"Num","Real","Numeric","Cool","Any","Mu"}},
        {"Complex", {"Complex","Numeric","Cool","Any","Mu"}},
        {"Real",    {"Real","Numeric","Cool","Any","Mu"}},
        {"Numeric", {"Numeric","Cool","Any","Mu"}},
        {"Str",     {"Str","Stringy","Cool","Any","Mu"}},              // Str does Stringy
        {"Bool",    {"Bool","Int","Real","Numeric","Cool","Any","Mu"}}, // Bool IS an Int (an Int-backed enum): True.isa(Int), Bool.^mro
        {"Cool",    {"Cool","Any","Mu"}},
        // Nil is a Cool — which is what makes its inherited list and string
        // methods real — and Failure is a Nil, so `Failure.new ~~ Nil` is True
        // (Nil-Any sheet NA-02, NA-10).
        {"Nil",     {"Nil","Cool","Any","Mu"}},
        {"Failure", {"Failure","Nil","Cool","Any","Mu"}},
        {"Date",    {"Date","Dateish","Any","Mu"}},
        {"DateTime",{"DateTime","Dateish","Any","Mu"}},
        // the grammar/match family: a grammar IS a Match (that is how `self`
        // works inside rule methods), and a Match IS a Capture
        {"Grammar", {"Grammar","Match","Capture","Cool","Any","Mu"}},
        {"Match",   {"Match","Capture","Cool","Any","Mu"}},
        {"Capture", {"Capture","Any","Mu"}},
        // the Code family: a Sub/Method IS a Routine IS a Block IS Code
        // (type objects smartmatch through this: `Sub ~~ Routine`)
        {"Sub",       {"Sub","Routine","Block","Code","Callable","Any","Mu"}},
        {"Method",    {"Method","Routine","Block","Code","Callable","Any","Mu"}},
        {"Submethod", {"Submethod","Routine","Block","Code","Callable","Any","Mu"}},
        {"Routine",   {"Routine","Block","Code","Callable","Any","Mu"}},
        {"Block",     {"Block","Code","Callable","Any","Mu"}},
        {"WhateverCode", {"WhateverCode","Code","Callable","Any","Mu"}},
        {"Code",      {"Code","Callable","Any","Mu"}},
        // the IO::Spec family: `$*SPEC` is an IO::Spec, so an attribute or a
        // parameter typed IO::Spec takes it (IO::Glob, Config)
        {"IO::Spec",         {"IO::Spec","Any","Mu"}},
        {"IO::Spec::Unix",   {"IO::Spec::Unix","IO::Spec","Any","Mu"}},
        {"IO::Spec::Win32",  {"IO::Spec::Win32","IO::Spec::Unix","IO::Spec","Any","Mu"}},
        {"IO::Spec::Cygwin", {"IO::Spec::Cygwin","IO::Spec::Unix","IO::Spec","Any","Mu"}},
        {"IO::Spec::QNX",    {"IO::Spec::QNX","IO::Spec::Unix","IO::Spec","Any","Mu"}},
        // a built-in encoding IS an Encoding::Builtin, which does the Encoding role
        {"Encoding", {"Encoding","Encoding::Builtin","Any","Mu"}},
        {"Encoding::Builtin", {"Encoding::Builtin","Encoding","Any","Mu"}},
        // the root of the X:: tree, which is not itself an X:: name
        {"Exception", {"Exception","Any","Mu"}},
        // IO::Socket is the ROLE a synchronous socket does — every wrapper
        // declares its parameter as that (IO::Socket::SSL takes an IO::Socket).
        // It is not an IO, and IO::Socket::Async does not do it either.
        {"IO::Socket::INET", {"IO::Socket::INET","IO::Socket","Any","Mu"}},
        {"IO::Socket",       {"IO::Socket","Any","Mu"}},
        // The Uni family: each normalisation form is a Uni SUBCLASS, and Uni
        // does Positional/Iterable — `"x".NFD ~~ Uni` is True on Rakudo, and
        // JSON::Fast binds `Uni:D \codes` to exactly such a value.
        // The byte-buffer family: `Buf` DOES `Blob`, and both are Positional +
        // Stringy. `utf8` is a Blob but not Cool. Missing, `Buf ~~ Blob` was
        // False — which is how DBDish::mysql decided a BLOB column needed
        // .decode and choked on the first byte over 0x7F.
        {"Blob",  {"Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"Buf",   {"Buf","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"blob8", {"blob8","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"blob16",{"blob16","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"blob32",{"blob32","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"blob64",{"blob64","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"buf8",  {"buf8","Buf","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"buf16", {"buf16","Buf","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"buf32", {"buf32","Buf","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"buf64", {"buf64","Buf","Blob","Positional","Stringy","Cool","Any","Mu"}},
        {"utf8",  {"utf8","Blob","Positional","Stringy","Any","Mu"}},
        // The list family is Cool — `(1, 2, 3, (1, 2, 3)).are` is Cool, not Any
        // (Nil-Any sheet NA-31, roast S32-list/are.t) — and Mu is the ROOT, so
        // it has no ancestors of its own to fall back through.
        {"List",  {"List","Cool","Positional","Iterable","Any","Mu"}},
        {"Array", {"Array","List","Cool","Positional","Iterable","Any","Mu"}},
        {"Seq",   {"Seq","Cool","Iterable","Any","Mu"}},
        {"Slip",  {"Slip","List","Cool","Positional","Iterable","Any","Mu"}},
        {"Mu",    {"Mu"}},
        {"Any",   {"Any","Mu"}},
        {"Uni",  {"Uni","Positional","Iterable","Any","Mu"}},
        {"NFC",  {"NFC","Uni","Positional","Iterable","Any","Mu"}},
        {"NFD",  {"NFD","Uni","Positional","Iterable","Any","Mu"}},
        {"NFKC", {"NFKC","Uni","Positional","Iterable","Any","Mu"}},
        {"NFKD", {"NFKD","Uni","Positional","Iterable","Any","Mu"}},
    };
    static const std::vector<std::string> fallback = {"Any","Mu"};
    auto it = A.find(t);
    if (it != A.end()) return it->second;
    if (t.rfind("X::", 0) == 0) return exceptionAncestry(t);
    // Rakudo's own tree classes, from the registry's measured table — the same
    // delegation the X:: tree gets, and for the same reason: the hierarchy is
    // defined elsewhere and reproduced, not invented. Everything that reads an
    // ancestry (`.isa`, `.does`, `~~` on type objects, lubType) is then right
    // about RakuAST:: without a second table to keep in step.
    if (isRakuAstName(t)) { const auto& anc = rakuAstAncestry(t); if (!anc.empty()) return anc; }
    return fallback;
}
// The names in the ancestry table that are ROLES, not classes. `.does` counts
// them; `.isa` and `.^mro` do not (Rakudo: `Date.isa(Dateish)` is False).
bool isBuiltinRole(const std::string& n) {
    static const std::set<std::string> roles = {
        "Real", "Numeric", "Stringy", "Dateish", "Rational", "Callable",
        "Positional", "Associative", "Iterable", "Baggy", "Setty", "Mixy", "Sequence",
        "IO::Socket"};
    // …and the X:: exception roles (X::Comp, X::Syntax, X::IO, …), which the
    // generated table owns because Rakudo's hierarchy is what defines them.
    return roles.count(n) > 0 || isExceptionRole(n);
}
std::string typeOfVal(const Value& v) { return v.t == VT::Type ? v.s : v.typeName(); }
std::string lubType(const std::string& a, const std::string& b) {
    if (a == b) return a;
    // an allomorph does BOTH its numeric type (in the ancestry) AND Str — a linear
    // ancestry can't hold the diamond, so pair it with Str/Stringy explicitly
    auto allo = [](const std::string& t) { return t == "IntStr" || t == "RatStr" || t == "NumStr" || t == "ComplexStr"; };
    if (allo(a) && (b == "Str" || b == "Stringy")) return b;
    if (allo(b) && (a == "Str" || a == "Stringy")) return a;
    for (auto& x : typeAncestry(a)) for (auto& y : typeAncestry(b)) if (x == y) return x;
    return "Mu";
}

#if !defined(_WIN32)
extern "C" { extern char **environ; } // swapped in the child for run(:env(...)) — a pointer store, async-signal-safe
#endif

// Build the "K=V\0K=V\0\0" block CreateProcessA wants from a K=V list.
#if defined(_WIN32)
static std::string winEnvBlock(const std::vector<std::string>& kvs) {
    std::string blk;
    for (auto& kv : kvs) { blk += kv; blk += '\0'; }
    blk += '\0';
    return blk;
}
#endif

// Children Proc::Async.start has running, keyed by the token stored on the proc
// hash. Mutex-guarded: realizations drain with the GIL parked, so one thread can
// be spawning (or .kill-ing) while another reaps.
std::mutex g_spawnedM;
std::map<long long, SpawnedChild> g_spawned;
// First half: spawn the child and return at once. The fork happens with the GIL
// held, so forks serialise (safe in a multithreaded process).
//
// `ownPgroup` puts the child in a process group of its own so a timeout can kill
// its grandchildren along with it. It must stay OFF otherwise: a child in a group
// that is not the terminal's foreground group is a BACKGROUND job, and the kernel
// stops it with SIGTTOU the moment it calls tcsetattr, or SIGTTIN the moment it
// reads the terminal. `run 'stty', '-echo'` hung there forever — which is how
// `fez login` came to echo the password and then wedge (issue #72) — and so did
// every interactive child, `less` and `vi` and a `sudo` password prompt included.
SpawnedChild spawnChildStart(const std::vector<std::string>& argv, const std::string& cwd,
                                    const std::vector<std::string>* envKV, const SpawnStdio& io,
                                    bool ownPgroup) {
    SpawnedChild sc;
    if (argv.empty()) return sc;
    // Anything we have written but not yet handed to the OS must go out BEFORE
    // the child starts. A child that inherits our stdout writes to the same fd
    // directly, so whatever is still sitting in std::cout's buffer would land
    // AFTER it — `print "building: "; run 'make'` came out in the wrong order
    // whenever our own output was redirected rather than a terminal.
    {
        std::lock_guard<std::mutex> lk(rtOutMutex());
        std::cout.flush(); std::cerr.flush();
    }
#if defined(_WIN32)
    (void)ownPgroup; // no POSIX process groups here; a timeout kills the pid
    // Windows: CreateProcess with inherited pipes; the finish half polls the
    // read ends via PeekNamedPipe. Compile-verified under mingw g++; behaviour
    // mirrors the POSIX path below.
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = nullptr; sa.bInheritHandle = TRUE;
    HANDLE outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
    if (io.captureOut) {
        if (!CreatePipe(&outR, &outW, &sa, 0)) return sc;
        SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    }
    if (io.captureErr) {
        if (!CreatePipe(&errR, &errW, &sa, 0)) { if (outR) { CloseHandle(outR); CloseHandle(outW); } return sc; }
        SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
    }
    HANDLE nul = (!io.captureErr && io.errToNull) ? CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr) : INVALID_HANDLE_VALUE;
    // Quote an argument only when it NEEDS it. Quoting unconditionally breaks a
    // command processor switch — cmd.exe does not recognise a quoted `"/c"` — and
    // `cmd.exe /c "…"` is exactly the shape shell() and the not-an-.exe fallback
    // below both produce.
    std::string cmd;
    for (size_t i = 0; i < argv.size(); i++) {
        if (i) cmd += ' ';
        const std::string& a0 = argv[i];
        bool needQuote = a0.empty() || a0.find_first_of(" \t\"") != std::string::npos;
        if (!needQuote) { cmd += a0; continue; }
        cmd += '"';
        for (char c : a0) { if (c == '"') cmd += '\\'; cmd += c; }
        cmd += '"';
    }
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    // STARTF_USESTDHANDLES requires ALL THREE handles to be valid AND inheritable.
    // GetStdHandle does not guarantee either — under a host that redirected stdin,
    // or with no console at all, it can hand back INVALID_HANDLE_VALUE, and then
    // CreateProcess fails for every command. Fall back to NUL and mark it
    // inheritable rather than passing a handle the child cannot use.
    HANDLE inH = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE inNul = INVALID_HANDLE_VALUE;
    if (inH == nullptr || inH == INVALID_HANDLE_VALUE) {
        inNul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, nullptr);
        inH = inNul;
    }
    else SetHandleInformation(inH, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    si.hStdInput = inH;
    // stdout: the capture pipe, or our own stdout (an un-tapped Proc::Async
    // stream goes where ours goes, like Rakudo) — with the same NUL fallback
    // as stdin, since STARTF_USESTDHANDLES needs all three handles usable.
    HANDLE outNul = INVALID_HANDLE_VALUE, outH = INVALID_HANDLE_VALUE;
    if (!io.captureOut) {
        // `:!out` wants the output GONE, not on our console — the same rule
        // `:!err` already follows.
        outH = io.outToNull ? INVALID_HANDLE_VALUE : GetStdHandle(STD_OUTPUT_HANDLE);
        if (outH == nullptr || outH == INVALID_HANDLE_VALUE) {
            outNul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
            outH = outNul;
        }
        else SetHandleInformation(outH, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }
    si.hStdOutput = io.captureOut ? outW : outH;
    // STARTF_USESTDHANDLES needs an inheritable handle for stderr too; the same
    // caveat as stdin above applies, so an unusable one falls back to NUL.
    HANDLE errNul = INVALID_HANDLE_VALUE, errH = INVALID_HANDLE_VALUE;
    if (!io.captureErr && !io.errToNull) {
        errH = GetStdHandle(STD_ERROR_HANDLE);
        if (errH == nullptr || errH == INVALID_HANDLE_VALUE) {
            errNul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
            errH = errNul;
        }
        else SetHandleInformation(errH, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }
    si.hStdError = io.captureErr ? errW
                 : (io.mergeErr && io.captureOut) ? outW   // :merge — one handle for both
                 : (io.errToNull ? nul : errH);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::vector<char> cmdbuf(cmd.begin(), cmd.end()); cmdbuf.push_back('\0');
    std::string envblk; if (envKV) envblk = winEnvBlock(*envKV);
    BOOL started = CreateProcessA(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE, 0, envKV ? (LPVOID)envblk.data() : nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    DWORD spawnErr = started ? 0 : GetLastError();
    // Not an .exe? It may be a .bat/.cmd, or a name the command processor knows.
    // CreateProcess cannot start those directly, so retry through COMSPEC — which
    // is what makes `run 'somescript.bat'` work at all on Windows.
    std::vector<char> cmdbuf2;
    if (!started && (spawnErr == ERROR_FILE_NOT_FOUND || spawnErr == ERROR_BAD_EXE_FORMAT ||
                     spawnErr == ERROR_ACCESS_DENIED)) {
        const char* comspec = std::getenv("COMSPEC");
        std::string shellCmd = std::string(comspec && *comspec ? comspec : "cmd.exe") + " /c " + cmd;
        cmdbuf2.assign(shellCmd.begin(), shellCmd.end()); cmdbuf2.push_back('\0');
        started = CreateProcessA(nullptr, cmdbuf2.data(), nullptr, nullptr, TRUE, 0, envKV ? (LPVOID)envblk.data() : nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
        if (!started) spawnErr = GetLastError();
    }
    if (outW) CloseHandle(outW); if (errW) CloseHandle(errW); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (inNul != INVALID_HANDLE_VALUE) CloseHandle(inNul);
    if (outNul != INVALID_HANDLE_VALUE) CloseHandle(outNul);
    if (errNul != INVALID_HANDLE_VALUE) CloseHandle(errNul);
    if (!started) {
        // A silent -1 with no output is undiagnosable — say WHY. The caller
        // routes this to the error stream when one was asked for, otherwise
        // to our own stderr.
        char msg[512] = {0};
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                       spawnErr, 0, msg, sizeof msg - 1, nullptr);
        std::string text = "Could not spawn '" + argv[0] + "': " + msg;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        sc.spawnErr = text;
        if (outR) CloseHandle(outR); if (errR) CloseHandle(errR);
        return sc;
    }
    sc.pid = (long long)pi.dwProcessId;
    sc.hProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    sc.outR = outR; sc.errR = errR;
    return sc;
#else
    // Build the argv vector BEFORE fork — malloc between fork and execvp is unsafe
    // in a multithreaded process (another thread can hold the allocator lock at
    // fork, deadlocking the child pre-exec).
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
    cargv.push_back(nullptr);
    // run(:env(...)) — the replacement environment, built BEFORE fork (no malloc
    // in the child); the child swaps `environ` (a pointer store) before execvp
    std::vector<char*> cenv;
    if (envKV) {
        cenv.reserve(envKV->size() + 1);
        for (auto& kv : *envKV) cenv.push_back(const_cast<char*>(kv.c_str()));
        cenv.push_back(nullptr);
    }
    int pipefd[2] = {-1, -1}, errfd[2] = {-1, -1};
    if (io.captureOut && pipe(pipefd) != 0) return sc;
    if (io.captureErr && pipe(errfd) != 0) { if (io.captureOut) { close(pipefd[0]); close(pipefd[1]); } return sc; }
    // The exec-status pipe. Its write end is CLOEXEC, so a SUCCESSFUL execvp
    // closes it silently and the parent reads end-of-file; a FAILED one has the
    // child write its errno through first. That is the only way to tell "no such
    // command" from "the command ran and exited 127" — and the difference is
    // visible: Rakudo reports a failed spawn as exit code -1 with an `OS error`
    // clause naming the reason (roast S29-os/system.t asserts both).
    int xfd[2] = {-1, -1};
    const bool haveX = pipe(xfd) == 0;
    if (haveX) fcntl(xfd[1], F_SETFD, FD_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) {
        if (io.captureOut) { close(pipefd[0]); close(pipefd[1]); }
        if (io.captureErr) { close(errfd[0]); close(errfd[1]); }
        if (haveX) { close(xfd[0]); close(xfd[1]); }
        return sc;
    }
    if (pid == 0) { // child — async-signal-safe only from here
        if (haveX) close(xfd[0]);
        if (ownPgroup) setpgid(0, 0); // only when a timeout may have to kill the group
        if (io.stdinFd >= 0) dup2(io.stdinFd, STDIN_FILENO);
        if (io.captureOut) dup2(pipefd[1], STDOUT_FILENO);
        else if (io.stdoutFd >= 0) dup2(io.stdoutFd, STDOUT_FILENO);
        else if (io.outToNull) { int devnull = open("/dev/null", O_WRONLY); if (devnull >= 0) dup2(devnull, STDOUT_FILENO); }
        // none of those: STDOUT_FILENO is left exactly as we got it (inherited)
        if (io.captureErr) dup2(errfd[1], STDERR_FILENO);
        else if (io.mergeErr && io.captureOut) dup2(pipefd[1], STDERR_FILENO); // :merge — one pipe for both
        else if (io.stderrFd >= 0) dup2(io.stderrFd, STDERR_FILENO);
        else if (io.errToNull) { int devnull = open("/dev/null", O_WRONLY); if (devnull >= 0) dup2(devnull, STDERR_FILENO); }
        close(pipefd[0]); close(pipefd[1]);
        close(errfd[0]); close(errfd[1]);
        // the bind-* fds carry FD_CLOEXEC: every copy but the dup2'd one (which
        // shed the flag) vanishes at exec, so a bound reader's EOF arrives
        // exactly when its writer exits
        if (!cwd.empty()) { if (::chdir(cwd.c_str()) != 0) _exit(126); }
        if (envKV) environ = cenv.data();
        execvp(cargv[0], cargv.data());
        if (haveX) { int e = errno; ssize_t w = write(xfd[1], &e, sizeof e); (void)w; }
        _exit(127);
    }
    // Wait for the exec to resolve — microseconds, and the only place the parent
    // can learn that the program does not exist.
    if (haveX) {
        close(xfd[1]);
        int childErrno = 0;
        ssize_t n;
        while ((n = read(xfd[0], &childErrno, sizeof childErrno)) == -1 && errno == EINTR) {}
        close(xfd[0]);
        if (n == (ssize_t)sizeof childErrno) {   // execvp never got off the ground
            int st = 0;
            while (waitpid(pid, &st, 0) == -1 && errno == EINTR) {}   // it has already _exit'ed
            if (io.captureOut) { close(pipefd[0]); close(pipefd[1]); }
            if (io.captureErr) { close(errfd[0]); close(errfd[1]); }
            std::string why = std::strerror(childErrno);
            if (!why.empty()) why[0] = (char)ascii::tolower((unsigned char)why[0]); // libuv's own casing
            sc.spawnErr = "Failed to spawn process " + argv[0] + ": " + why +
                          " (error code -" + std::to_string(childErrno) + ")";
            return sc;                                                // pid stays 0: "never ran"
        }
    }
    sc.pid = (long long)pid;
    // parent: don't let a concurrent spawn (another worker) inherit our read ends
    // across its execvp — that would keep the write end open and defer our EOF.
    if (io.captureOut) {
        fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
        close(pipefd[1]);
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        sc.outFd = pipefd[0];
    }
    if (io.captureErr) {
        fcntl(errfd[0], F_SETFD, FD_CLOEXEC);
        close(errfd[1]);
        fcntl(errfd[0], F_SETFL, O_NONBLOCK);
        sc.errFd = errfd[0];
    }
    sc.ownPgroup = ownPgroup;
    return sc;
#endif
}

// Second half: drain the capture pipes to EOF, then reap. EOF, not the child's
// exit, is the only reliable "all output captured" signal: reaping with waitpid
// does not guarantee the final buffered write has been drained, and grandchildren
// may still hold the write end. `timeoutSec` bounds the whole wait; on expiry the
// child is killed, and its process group with it when it was given one.
// `gil` (if non-null) is the interpreter: the GIL is parked for the wait so
// sibling worker threads run — and spawn their own children — concurrently.
static void spawnChildFinish(SpawnedChild& sc, double timeoutSec,
                             std::string& out, std::string* errOut,
                             int& exitCode, bool& timedout, Interpreter* gil,
                             const ChildChunkSink* sink = nullptr,
                             std::exception_ptr* sinkErr = nullptr) {
    exitCode = -1; timedout = false;
    if (!sc.pid) return;
    bool parked = gil ? gil->gilPark() : false; // drop the GIL for the wait below
    auto start = std::chrono::steady_clock::now();
    char buf[8192];
    // Hand a chunk to the sink with the GIL back in hand, then park again for the
    // next wait. A throw from the callback (a user `whenever` block dying) is
    // held rather than let out: the child still has to be reaped and its
    // descriptors closed, so the caller rethrows once that is done.
    auto deliver = [&](bool isErr, const char* d, size_t n) {
        if (!sink || !*sink || n == 0) return;
        if (parked) { gil->gilUnpark(true); parked = false; }
        try { (*sink)(isErr, d, n); }
        catch (...) { if (sinkErr && !*sinkErr) *sinkErr = std::current_exception(); }
        if (gil) parked = gil->gilPark();
    };
#if defined(_WIN32)
    bool oEof = (sc.outR == nullptr), eEof = (sc.errR == nullptr);
    auto drain = [&](HANDLE h, std::string* dst, bool isErr, bool& eof) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) { eof = true; return; }
        while (avail > 0) {
            DWORD want = avail > sizeof buf ? (DWORD)sizeof buf : avail, rd = 0;
            if (!ReadFile(h, buf, want, &rd, nullptr) || rd == 0) { eof = true; return; }
            if (dst) dst->append(buf, rd);
            deliver(isErr, buf, rd);
            if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) { eof = true; return; }
        }
    };
    while (!oEof || !eEof) {
        if (!oEof) drain(sc.outR, &out, false, oEof);
        if (!eEof) drain(sc.errR, errOut, true, eEof);
        if (oEof && eEof) break;
        bool exited = WaitForSingleObject(sc.hProcess, 0) == WAIT_OBJECT_0;
        if (timeoutSec > 0) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (el > timeoutSec) { TerminateProcess(sc.hProcess, 1); timedout = true; break; }
        }
        if (!exited) Sleep(2);
    }
    if (!timedout) {
        // the deadline binds even with no pipe left to key on (none was asked
        // for, or the child closed its ends and lives on)
        DWORD waitMs = INFINITE;
        if (timeoutSec > 0) {
            double rem = timeoutSec - std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            waitMs = rem > 0 ? (DWORD)(rem * 1000) : 0;
        }
        if (WaitForSingleObject(sc.hProcess, waitMs) != WAIT_OBJECT_0 && timeoutSec > 0) {
            TerminateProcess(sc.hProcess, 1); timedout = true;
            WaitForSingleObject(sc.hProcess, INFINITE);
        }
    }
    else WaitForSingleObject(sc.hProcess, INFINITE);
    DWORD ec = 0; if (!timedout && GetExitCodeProcess(sc.hProcess, &ec)) exitCode = (int)ec;
    if (sc.outR) CloseHandle(sc.outR);
    if (sc.errR) CloseHandle(sc.errR);
    CloseHandle(sc.hProcess);
#else
    pid_t pid = (pid_t)sc.pid;
    int fd = sc.outFd, efd = sc.errFd;
    bool oEof = (fd < 0), eEof = (efd < 0);
    while (!oEof || !eEof) {
        struct pollfd pfds[2]; int nf = 0;
        if (!oEof) { pfds[nf] = {fd, POLLIN, 0}; nf++; }
        if (!eEof) { pfds[nf] = {efd, POLLIN, 0}; nf++; }
        poll(pfds, nf, 50);
        if (!oEof) for (;;) {
            ssize_t n = read(fd, buf, sizeof buf);
            if (n > 0) { out.append(buf, (size_t)n); deliver(false, buf, (size_t)n); continue; }
            if (n == 0) oEof = true;
            break;
        }
        if (!eEof) for (;;) {
            ssize_t n = read(efd, buf, sizeof buf);
            if (n > 0) { if (errOut) errOut->append(buf, (size_t)n); deliver(true, buf, (size_t)n); continue; }
            if (n == 0) eEof = true;
            break;
        }
        if (oEof && eEof) break;
        if (timeoutSec > 0) {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeoutSec) {
                // a child the zombie sweep already reaped must NOT be signalled:
                // the pid may have been recycled (only its grandchildren still
                // hold the pipe, and those we leave be)
                if (!sc.reaped) { if (sc.ownPgroup) kill(-pid, SIGKILL); kill(pid, SIGKILL); }
                timedout = true; break;
            }
        }
    }
    int status = 0;
    if (sc.reaped) status = sc.rawStatus; // the zombie sweep got there first
    else if (timedout) { while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {} }
    else if (timeoutSec > 0) {
        // the deadline binds even with no pipe left to key on (none was asked
        // for, or the child closed its ends and lives on): poll for the exit
        for (;;) {
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r != 0) break;
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeoutSec) {
                if (sc.ownPgroup) kill(-pid, SIGKILL);
                kill(pid, SIGKILL); timedout = true;
                while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {}
                break;
            }
            poll(nullptr, 0, 10);
        }
    }
    else { while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {} } // reap; retry on EINTR
    if (!timedout) exitCode = procStatusFold(status); // 256+N for a signal; the stores split it
    if (fd >= 0) close(fd);
    if (efd >= 0) close(efd);
#endif
    if (parked) gil->gilUnpark(true);    // reacquire the GIL before touching interpreter state
}
// Spawn a child process, capture its stdout, with an optional wall-clock timeout —
// the two halves back-to-back, which is what run()/shell()/qx need.
// `errOut` non-null captures the child's stderr; otherwise `errInherit` decides
// between INHERITING our own stderr (Rakudo's default for an un-adverbed run —
// the child's diagnostics reach the terminal or the CI log) and discarding it
// (`:!err`). Both used to mean /dev/null, which is how a MAIN usage message
// from a child rakupp vanished and left a failing raku-eye leg undiagnosable.
// `outMode` says the same three things about stdout: 1 capture (`:out`),
// 0 discard (`:!out`), -1 INHERIT ours — which is the un-adverbed default and
// the only one that is LIVE, the child writing to our fd as it goes.
void spawnCapture(const std::vector<std::string>& argv, double timeoutSec,
                         std::string& out, int& exitCode, bool& timedout,
                         Interpreter* gil, std::string* errOut,
                         const std::string& cwd, long long* pidOut,
                         const std::vector<std::string>* envKV,
                         bool errInherit, int outMode,
                         const ChildChunkSink* sink,
                         std::exception_ptr* sinkErr,
                         int stdinFd, bool mergeErr,
                         std::string* spawnErrOut) {
    out.clear(); exitCode = -1; timedout = false;
    if (errOut) errOut->clear();
    if (argv.empty()) return;
    SpawnStdio io;
#if !defined(_WIN32)
    io.stdinFd = stdinFd; // `:in($handle)` — the child's stdin IS this descriptor
#else
    (void)stdinFd;
#endif
    io.captureOut = outMode == 1;
    io.outToNull  = outMode == 0;
    io.captureErr = errOut != nullptr;
    io.errToNull = !errOut && !errInherit;
    io.mergeErr = mergeErr;
    SpawnedChild sc = spawnChildStart(argv, cwd, envKV, io, timeoutSec > 0);
    if (!sc.pid) {
        // The command never started: exitCode stays -1, and the caller that
        // asked for the reason gets it to put in the Proc (Rakudo's `OS error`).
        if (spawnErrOut) *spawnErrOut = sc.spawnErr;
#if defined(_WIN32)
        // Windows has always ALSO reported it on the spot, and keeps doing so:
        // there, a CreateProcess failure is the only diagnostic a caller gets.
        // POSIX never printed anything here and still does not — Rakudo does not
        // either, and the reason now reaches the caller through the Proc.
        if (!sc.spawnErr.empty()) {
            if (errOut) *errOut = sc.spawnErr + "\n"; else std::cerr << sc.spawnErr << "\n";
        }
#endif
        return;
    }
    if (pidOut) *pidOut = sc.pid;
    spawnChildFinish(sc, timeoutSec, out, errOut, exitCode, timedout, gil, sink, sinkErr);
}

// Spawn a child, feed `input` to its stdin, and collect its output. Uses poll on
// every open pipe so it won't deadlock when the child's output exceeds the pipe
// buffer while we're still writing input (as pandoc can on a large page).
//
// `outMode` and `errOut`/`errInherit` mean exactly what they mean in
// spawnCapture: stdout is captured (1), discarded (0) or INHERITED (-1); stderr
// is captured when `errOut` is non-null, otherwise inherited or discarded as
// `errInherit` says. This path used to hardcode "capture stdout, send stderr to
// /dev/null", so `run(cmd, :in, :out, :err)` came back with an empty `.err` and
// `run(cmd, :in)` swallowed both streams — where Rakudo inherits both.
void spawnWithInput(const std::vector<std::string>& argv, const std::string& input,
                           std::string& out, int& exitCode, Interpreter* gil,
                           const std::vector<std::string>* envKV, const std::string& cwd,
                           std::string* errOut, bool errInherit, int outMode) {
    out.clear(); exitCode = -1;
    if (errOut) errOut->clear();
    if (argv.empty()) return;
    const bool capOut = outMode == 1, capErr = errOut != nullptr;
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = nullptr; sa.bInheritHandle = TRUE;
    HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
    if (!CreatePipe(&inR, &inW, &sa, 0)) return;
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    if (capOut) {
        if (!CreatePipe(&outR, &outW, &sa, 0)) { CloseHandle(inR); CloseHandle(inW); return; }
        SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    }
    if (capErr) {
        if (!CreatePipe(&errR, &errW, &sa, 0)) {
            CloseHandle(inR); CloseHandle(inW);
            if (outR) { CloseHandle(outR); CloseHandle(outW); }
            return;
        }
        SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
    }
    HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    std::string cmd;
    for (size_t i = 0; i < argv.size(); i++) { if (i) cmd += ' '; cmd += '"'; for (char c : argv[i]) { if (c == '"') cmd += '\\'; cmd += c; } cmd += '"'; }
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR;
    si.hStdOutput = capOut ? outW : (outMode == 0 ? nul : GetStdHandle(STD_OUTPUT_HANDLE));
    si.hStdError  = capErr ? errW : (errInherit ? GetStdHandle(STD_ERROR_HANDLE) : nul);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::vector<char> cmdbuf(cmd.begin(), cmd.end()); cmdbuf.push_back('\0');
    std::string envblk; if (envKV) envblk = winEnvBlock(*envKV);
    BOOL started = CreateProcessA(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE, 0, envKV ? (LPVOID)envblk.data() : nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(inR);
    if (outW) CloseHandle(outW);
    if (errW) CloseHandle(errW);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) { CloseHandle(inW); if (outR) CloseHandle(outR); if (errR) CloseHandle(errR); return; }
    bool parked = gil ? gil->gilPark() : false;
    size_t written = 0; char buf[8192]; bool wOpen = true;
    // Drain whatever is sitting in one pipe; false once it is closed or broken,
    // so a stream that ends early stops being polled.
    auto drain = [&buf](HANDLE h, std::string* into) -> bool {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
        while (avail > 0) {
            DWORD want = avail > sizeof buf ? (DWORD)sizeof buf : avail, rd = 0;
            if (!ReadFile(h, buf, want, &rd, nullptr) || rd == 0) return false;
            into->append(buf, rd);
            if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
        }
        return true;
    };
    bool oAlive = outR != nullptr, eAlive = errR != nullptr;
    for (;;) {
        if (wOpen) {
            if (written < input.size()) {
                DWORD want = (DWORD)((input.size() - written < sizeof buf) ? input.size() - written : sizeof buf), wn = 0;
                if (WriteFile(inW, input.data() + written, want, &wn, nullptr) && wn) written += wn;
                else { CloseHandle(inW); wOpen = false; }
            } else { CloseHandle(inW); wOpen = false; }
        }
        if (oAlive) oAlive = drain(outR, &out);
        if (eAlive) eAlive = drain(errR, errOut);
        if (!wOpen) {
            if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
                if (oAlive) drain(outR, &out);      // whatever the child left behind
                if (eAlive) drain(errR, errOut);
                break;
            }
            Sleep(2);
        }
    }
    if (wOpen) CloseHandle(inW);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 0; if (GetExitCodeProcess(pi.hProcess, &ec)) exitCode = (int)ec;
    if (outR) CloseHandle(outR);
    if (errR) CloseHandle(errR);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    if (parked) gil->gilUnpark(true);
    return;
#else
    std::vector<char*> cargv; // build argv before fork (no malloc between fork & exec)
    cargv.reserve(argv.size() + 1);
    for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
    cargv.push_back(nullptr);
    std::vector<char*> cenv; // run(:env(...)) — see spawnCapture
    if (envKV) {
        cenv.reserve(envKV->size() + 1);
        for (auto& kv : *envKV) cenv.push_back(const_cast<char*>(kv.c_str()));
        cenv.push_back(nullptr);
    }
    int inPipe[2], outPipe[2] = {-1, -1}, errPipe[2] = {-1, -1};
    if (pipe(inPipe) != 0) return;
    if (capOut && pipe(outPipe) != 0) { close(inPipe[0]); close(inPipe[1]); return; }
    if (capErr && pipe(errPipe) != 0) {
        close(inPipe[0]); close(inPipe[1]);
        if (capOut) { close(outPipe[0]); close(outPipe[1]); }
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(inPipe[0]); close(inPipe[1]);
        if (capOut) { close(outPipe[0]); close(outPipe[1]); }
        if (capErr) { close(errPipe[0]); close(errPipe[1]); }
        return;
    }
    if (pid == 0) { // child — async-signal-safe from here
        dup2(inPipe[0], STDIN_FILENO);
        // A stream that is neither captured nor discarded is left alone, so the
        // child writes to OUR descriptor as it goes — Rakudo's default for an
        // un-adverbed run, and the only mode that is live rather than buffered.
        int devnull = -1;
        if (!capOut && outMode == 0) { devnull = open("/dev/null", O_WRONLY); if (devnull >= 0) dup2(devnull, STDOUT_FILENO); }
        if (!capErr && !errInherit) {
            if (devnull < 0) devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        }
        if (capOut) dup2(outPipe[1], STDOUT_FILENO);   // capture and discard are exclusive
        if (capErr) dup2(errPipe[1], STDERR_FILENO);
        close(inPipe[0]); close(inPipe[1]);
        if (capOut) { close(outPipe[0]); close(outPipe[1]); }
        if (capErr) { close(errPipe[0]); close(errPipe[1]); }
        if (devnull >= 0) close(devnull);
        if (!cwd.empty()) { if (::chdir(cwd.c_str()) != 0) _exit(126); }
        if (envKV) environ = cenv.data();
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
    close(inPipe[0]);
    if (capOut) close(outPipe[1]);
    if (capErr) close(errPipe[1]);
    fcntl(inPipe[1], F_SETFD, FD_CLOEXEC);
    int wfd = inPipe[1], rfd = capOut ? outPipe[0] : -1, efd = capErr ? errPipe[0] : -1;
    fcntl(wfd, F_SETFL, O_NONBLOCK);
    if (rfd >= 0) { fcntl(rfd, F_SETFD, FD_CLOEXEC); fcntl(rfd, F_SETFL, O_NONBLOCK); }
    if (efd >= 0) { fcntl(efd, F_SETFD, FD_CLOEXEC); fcntl(efd, F_SETFL, O_NONBLOCK); }
    // (SIGPIPE is ignored process-wide at startup — Runtime.cpp)
    bool parked = gil ? gil->gilPark() : false; // drop the GIL for the feed/read wait below
    size_t written = 0;
    char buf[8192];
    bool rOpen = rfd >= 0, eOpen = efd >= 0, wOpen = true;
    while (rOpen || eOpen || wOpen) {
        struct pollfd pfds[3]; int nf = 0;
        int ri = -1, ei = -1, wi = -1;
        if (rOpen) { pfds[nf] = {rfd, POLLIN, 0}; ri = nf; nf++; }
        if (eOpen) { pfds[nf] = {efd, POLLIN, 0}; ei = nf; nf++; }
        if (wOpen) { pfds[nf] = {wfd, POLLOUT, 0}; wi = nf; nf++; }
        poll(pfds, nf, 50);
        if (rOpen && ri >= 0 && (pfds[ri].revents & (POLLIN | POLLHUP))) {
            ssize_t n;
            while ((n = read(rfd, buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
            if (n == 0) { rOpen = false; close(rfd); }
        }
        if (eOpen && ei >= 0 && (pfds[ei].revents & (POLLIN | POLLHUP))) {
            ssize_t n;
            while ((n = read(efd, buf, sizeof buf)) > 0) errOut->append(buf, (size_t)n);
            if (n == 0) { eOpen = false; close(efd); }
        }
        if (wOpen && wi >= 0 && (pfds[wi].revents & POLLOUT)) {
            if (written < input.size()) {
                ssize_t n = write(wfd, input.data() + written, input.size() - written);
                if (n > 0) written += (size_t)n;
                else if (n < 0 && errno != EAGAIN) { wOpen = false; close(wfd); }
            }
            if (written >= input.size()) { wOpen = false; close(wfd); } // done: signal EOF to child
        }
    }
    int status = 0;
    while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {}
    exitCode = procStatusFold(status); // 256+N for a signal; the stores split it
    if (parked) gil->gilUnpark(true); // reacquire the GIL before touching interpreter state
#endif
}

std::pair<size_t, size_t> nextLogicalNewline(const std::string& s, size_t from) {
    for (size_t i = from; i < s.size(); i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c == 0x0D) // CR, and CRLF is ONE terminator
            return {i, (i + 1 < s.size() && s[i + 1] == 0x0A) ? 2u : 1u};
        if (c == 0x0A || c == 0x0B || c == 0x0C) return {i, 1};  // LF, VT, FF
        if (c == 0xC2 && i + 1 < s.size() && (unsigned char)s[i + 1] == 0x85)
            return {i, 2};                                        // NEL  U+0085
        if (c == 0xE2 && i + 2 < s.size() && (unsigned char)s[i + 1] == 0x80 &&
            ((unsigned char)s[i + 2] == 0xA8 || (unsigned char)s[i + 2] == 0xA9))
            return {i, 3};                                        // LS/PS U+2028-9
    }
    return {std::string::npos, 0};
}
size_t danglingNewlinePrefix(const std::string& s) {
    if (s.empty()) return 0;
    const unsigned char last = (unsigned char)s.back();
    if (last == 0x0D) return 1;                      // "\r" may yet become "\r\n"
    if (last == 0xC2) return 1;                      // NEL lead byte
    if (last == 0xE2) return 1;                      // LS/PS lead byte
    if (s.size() >= 2 && (unsigned char)s[s.size() - 2] == 0xE2 && last == 0x80) return 2;
    return 0;
}

// Run one emitted value through a live-Supply tap's transform chain (grep/map/head/…).
// Threads the value(s) through each step in order; per-step mutable state lives in the
// step's "state" hash. Sets `complete` when a head/first step reaches its limit.
ValueList Interpreter::applyTapChain(Value& tap, const Value& in, bool& complete, bool flush) {
    complete = false;
    ValueList cur;
    if (!flush) cur.push_back(in);
    if (!(tap.t == VT::Hash && tap.hash()->count("chain"))) return cur;
    for (auto& step : *(*tap.hash())["chain"].arr()) {
        const std::string op = (*step.hash())["op"].toStr();
        Value arg = step.hash()->count("arg") ? (*step.hash())["arg"] : Value::nil();
        Value& state = (*step.hash())["state"];
        auto sInt = [&](const char* k) -> long long { auto it = state.hash()->find(k); return it == state.hash()->end() ? 0 : it->second.toInt(); };
        ValueList next;
        for (auto& v : cur) {
            if (op == "map") { next.push_back(arg.t == VT::Code ? callCallable(arg, ValueList{v}) : v); }
            else if (op == "grep") {
                bool match;
                if (arg.t == VT::Code) match = predAnswerTruthy(*this, callCallable(arg, ValueList{v}), v);
                else if (arg.t == VT::Regex) match = regexMatch(v.toStr(), arg.s).truthy();
                else match = applyArith("~~", v, arg).truthy();
                if (match) next.push_back(v);
            }
            else if (op == "skip") { long long n = arg.toInt(); long long c = sInt("c"); if (c < n) (*state.hash())["c"] = Value::integer(c + 1); else next.push_back(v); }
            else if (op == "head") {
                double lim = arg.t == VT::Nil ? 1 : (arg.t == VT::Whatever ? std::numeric_limits<double>::infinity() : arg.toNum());
                long long c = sInt("c");
                if (c < lim) { next.push_back(v); (*state.hash())["c"] = Value::integer(c + 1); if (c + 1 >= lim) complete = true; }
                else complete = true;
            }
            else if (op == "first") {
                bool match = true;
                if (arg.t == VT::Code) match = predAnswerTruthy(*this, callCallable(arg, ValueList{v}), v);
                else if (arg.t == VT::Regex) match = regexMatch(v.toStr(), arg.s).truthy();
                else if (arg.t != VT::Nil) match = applyArith("~~", v, arg).truthy();
                if (match) { next.push_back(v); complete = true; }
            }
            else if (op == "unique" || op == "squish") {
                Value asF = step.hash()->count("as") ? (*step.hash())["as"] : Value::nil();
                Value key = asF.t == VT::Code ? callCallable(asF, ValueList{v}) : v;
                std::string ks = key.toStr();
                if (op == "unique") {
                    // S-35: `:expires($n)` lets a key through again once $n seconds
                    // have passed since it was last EMITTED, so the seen-set
                    // remembers WHEN, not merely that. `:with` replaces identity
                    // with a comparator, and then every key seen so far has to be
                    // asked — which is why that case keeps an ordered list and the
                    // ordinary one keeps a hash.
                    double exp = step.hash()->count("expires") ? (*step.hash())["expires"].toNum() : 0;
                    Value withF = step.hash()->count("with") ? (*step.hash())["with"] : Value::nil();
                    if (withF.t == VT::Code) {
                        if (!state.hash()->count("list")) (*state.hash())["list"] = Value::array();
                        ValueList* seen = (*state.hash())["list"].arr();
                        bool emit = true;
                        for (auto& e : *seen) {
                            if (!callCallable(withF, ValueList{(*e.hash())["k"], key}).truthy()) continue;
                            if (exp > 0 && epochNowSecs() - (*e.hash())["t"].toNum() >= exp) {
                                (*e.hash())["k"] = key;
                                (*e.hash())["t"] = Value::number(epochNowSecs());
                            } else emit = false;
                            break;
                        }
                        if (emit) {
                            bool known = false;
                            for (auto& e : *seen)
                                if (callCallable(withF, ValueList{(*e.hash())["k"], key}).truthy()) { known = true; break; }
                            if (!known) {
                                Value e = Value::makeHash();
                                (*e.hash())["k"] = key;
                                (*e.hash())["t"] = Value::number(epochNowSecs());
                                seen->push_back(e);
                            }
                            next.push_back(v);
                        }
                    } else {
                        if (!state.hash()->count("seen")) (*state.hash())["seen"] = Value::makeHash();
                        Value& seen = (*state.hash())["seen"];
                        auto it = seen.hash()->find(ks);
                        bool emit = it == seen.hash()->end();
                        if (!emit && exp > 0) emit = epochNowSecs() - it->second.toNum() >= exp;
                        if (emit) { (*seen.hash())[ks] = Value::number(exp > 0 ? epochNowSecs() : 0); next.push_back(v); }
                    }
                } else { // squish: drop only if equal to the immediately preceding key
                    // …and `:with` decides sameness, called with the previously KEPT
                    // key first and the new one second (S-36).
                    Value withF = step.hash()->count("with") ? (*step.hash())["with"] : Value::nil();
                    bool same = false;
                    if (state.hash()->count("has")) {
                        if (withF.t == VT::Code) same = callCallable(withF, ValueList{(*state.hash())["prevv"], key}).truthy();
                        else same = (*state.hash())["prev"].toStr() == ks;
                    }
                    if (!same) {
                        next.push_back(v);
                        (*state.hash())["prev"] = Value::str(ks);
                        (*state.hash())["prevv"] = key;
                        (*state.hash())["has"] = Value::boolean(true);
                    }
                }
            }
            else if (op == "lines" || op == "words") {
                // A stream SPLITTER: the message boundaries are not the piece
                // boundaries, so the unfinished tail is held in state and joined to
                // the next message. `.lines(:!chomp)` keeps the newline on each line
                // (TAP feeds its parser that way — issue #34).
                bool chomp = !step.hash()->count("chomp") || (*step.hash())["chomp"].truthy();
                std::string buf = state.hash()->count("buf") ? (*state.hash())["buf"].toStr() : std::string();
                buf += v.toStr();
                size_t start = 0;
                if (op == "lines") {
                    // the same logical-newline set a Str breaks on — and a tail that
                    // could still GROW into one (a lone "\r" before an unseen "\n",
                    // a truncated NEL/LS/PS) is held for the next message rather than
                    // terminating a line early
                    const size_t safe = buf.size() - danglingNewlinePrefix(buf);
                    for (;;) {
                        auto [at, len] = nextLogicalNewline(buf, start);
                        if (at == std::string::npos || at + len > safe) break;
                        next.push_back(Value::str(buf.substr(start, at - start + (chomp ? 0 : len))));
                        start = at + len;
                    }
                }
                else { // words: a piece ends at whitespace, so a trailing partial word waits
                    size_t i = start;
                    for (;;) {
                        while (i < buf.size() && ascii::isspace((unsigned char)buf[i])) i++;
                        size_t b = i;
                        while (i < buf.size() && !ascii::isspace((unsigned char)buf[i])) i++;
                        if (b == i) break;                       // nothing but whitespace left
                        if (i == buf.size()) { i = b; break; }   // may still grow: hold it
                        next.push_back(Value::str(buf.substr(b, i - b)));
                    }
                    start = i;
                }
                (*state.hash())["buf"] = Value::str(buf.substr(start));
            }
            // ---- the stateful folds, for a source whose values arrive over time
            else if (op == "produce" || op == "reduce") {
                if (!(state.hash()->count("has") && (*state.hash())["has"].truthy())) {
                    (*state.hash())["acc"] = v; (*state.hash())["has"] = Value::boolean(true);
                } else if (arg.t == VT::Code) {
                    (*state.hash())["acc"] = callCallable(arg, ValueList{(*state.hash())["acc"], v});
                }
                if (op == "produce") next.push_back((*state.hash())["acc"]);
            }
            else if (op == "elems") {
                long long c = sInt("c") + 1;
                (*state.hash())["c"] = Value::integer(c);
                double secs = arg.t != VT::Nil ? arg.toNum() : 0;
                if (secs <= 0) { next.push_back(Value::integer(c)); }
                else {
                    // S-41: at most one report per `$seconds` bucket. The first
                    // bucket is the one the SUBSCRIPTION fell in, so a source that
                    // says everything inside it reports only at done.
                    long long b = (long long)std::floor(epochNowSecs() / secs);
                    long long b0 = state.hash()->count("bucket")
                        ? (*state.hash())["bucket"].toInt()
                        : (long long)std::floor((state.hash()->count("t0") ? (*state.hash())["t0"].toNum() : epochNowSecs()) / secs);
                    if (b != b0) {
                        (*state.hash())["bucket"] = Value::integer(b);
                        (*state.hash())["rep"] = Value::integer(c);
                        next.push_back(Value::integer(c));
                    }
                }
            }
            else if (op == "batch") {
                // S-39: `:elems` closes a batch when it is full; `:seconds`
                // closes it when the wall clock crosses into a new bucket
                // (floor(now / seconds)), which is noticed when the first value
                // of the new bucket arrives. Neither adverb means one-element
                // batches.
                long long want = step.hash()->count("elems") ? (*step.hash())["elems"].toInt() : 0;
                double secs = step.hash()->count("seconds") ? (*step.hash())["seconds"].toNum() : 0;
                if (want < 1 && secs <= 0) want = 1;
                if (!state.hash()->count("buf")) (*state.hash())["buf"] = Value::array();
                ValueList* buf = (*state.hash())["buf"].arr();
                if (secs > 0) {
                    long long bucket = (long long)std::floor(epochNowSecs() / secs);
                    if (state.hash()->count("bucket") && (*state.hash())["bucket"].toInt() != bucket
                        && !buf->empty()) {
                        Value b = Value::array(); b.isList = true; *b.arr() = *buf;
                        buf->clear();
                        next.push_back(b);
                    }
                    (*state.hash())["bucket"] = Value::integer(bucket);
                }
                buf->push_back(v);
                if (want > 0 && (long long)buf->size() >= want) {
                    Value b = Value::array(); b.isList = true; *b.arr() = *buf;
                    buf->clear();
                    next.push_back(b);
                }
            }
            else if (op == "classify" || op == "categorize") {
                // S-31: each key seen for the first time emits `key => Supply`,
                // and the inner supply PRESERVES, so a tap that arrives later
                // still gets that key's values from the beginning.
                if (!state.hash()->count("keys")) (*state.hash())["keys"] = Value::makeHash();
                if (!state.hash()->count("sups")) (*state.hash())["sups"] = Value::array();
                Value keys = Value::array();
                if (arg.t == VT::Code) {
                    Value r = callCallable(arg, ValueList{v});
                    if (op == "categorize" && r.t == VT::Array && r.arr()) *keys.arr() = *r.arr();
                    else keys.arr()->push_back(r);
                } else if (arg.t == VT::Hash || arg.t == VT::Array || arg.t == VT::Range) {
                    // an Associative mapper is `mapper{$_}` and a Positional one
                    // `mapper[$_]` — including the container's `is default(…)`,
                    // which is how Roast's Hash mapper names its 0 bucket
                    Value r = rtIndexGet(arg, v, arg.t == VT::Hash);
                    // a container declared `is default(…)` names its own missing
                    // key — Roast's Hash mapper relies on it for the 0 bucket
                    if (!defined(r) && arg.elemDefault()) r = *arg.elemDefault();
                    if (op == "categorize" && r.t == VT::Array && r.arr()) *keys.arr() = *r.arr();
                    else keys.arr()->push_back(r);
                } else keys.arr()->push_back(v);
                for (auto& k : *keys.arr()) {
                    const std::string id = whichOf(k);
                    Value& seen = (*state.hash())["keys"];
                    Value sup;
                    if (!seen.hash()->count(id)) {
                        sup = Value::makeHash(); sup.hashKind = "Supplier";
                        (*sup.hash())["taps"] = Value::array();
                        (*sup.hash())["preserving"] = Value::boolean(true);
                        (*sup.hash())["buffer"] = Value::array();
                        (*seen.hash())[id] = sup;
                        (*state.hash())["sups"].arr()->push_back(sup);
                        Value inner = Value::makeHash(); inner.hashKind = "Supply";
                        (*inner.hash())["supplier"] = sup;
                        Value pr = Value::pair(k.toStr(), inner);
                        if (k.t != VT::Str) pr.pairKeyM() = std::make_shared<Value>(k);
                        next.push_back(pr);
                    } else sup = (*seen.hash())[id];
                    ValueList one{v};
                    methodCall(sup, "emit", one);
                }
            }
            else next.push_back(v);
        }
        // the stateful folds have their own last word when the source completes
        if (flush) {
            if (op == "reduce") {
                next.push_back(state.hash()->count("has") && (*state.hash())["has"].truthy()
                               ? (*state.hash())["acc"] : Value::nil());
            }
            else if (op == "elems" && arg.t != VT::Nil && arg.toNum() > 0) {
                // the final count, when the last bucket did not already report it
                long long c = sInt("c");
                bool reported = state.hash()->count("rep") && (*state.hash())["rep"].toInt() == c;
                if (!reported) next.push_back(Value::integer(c));
            }
            else if (op == "batch" && state.hash()->count("buf") && !(*state.hash())["buf"].arr()->empty()) {
                Value b = Value::array(); b.isList = true; *b.arr() = *(*state.hash())["buf"].arr();
                (*state.hash())["buf"].arr()->clear();
                next.push_back(b);
            }
            else if ((op == "classify" || op == "categorize") && state.hash()->count("sups")) {
                for (auto& sup : *(*state.hash())["sups"].arr()) { ValueList na; methodCall(sup, "done", na); }
            }
        }
        // Draining: nothing can grow any more, so what was held back as ambiguous
        // is now decided. A tail like "b\r" is a TERMINATED line (the "\r" can no
        // longer become "\r\n"), not a line whose text ends in a carriage return.
        if (flush && (op == "lines" || op == "words")) {
            bool chomp = !step.hash()->count("chomp") || (*step.hash())["chomp"].truthy();
            std::string buf = state.hash()->count("buf") ? (*state.hash())["buf"].toStr() : std::string();
            if (op == "lines") {
                size_t start = 0;
                for (;;) {
                    auto [at, len] = nextLogicalNewline(buf, start);
                    if (at == std::string::npos) break;
                    next.push_back(Value::str(buf.substr(start, at - start + (chomp ? 0 : len))));
                    start = at + len;
                }
                if (start < buf.size()) next.push_back(Value::str(buf.substr(start)));
            }
            else if (!buf.empty()) next.push_back(Value::str(buf)); // words: the last word
            (*state.hash())["buf"] = Value::str("");
        }
        cur = std::move(next);
        if (complete) break;
    }
    return cur;
}
// Realize a Proc::Async .start promise: the process has been RUNNING since
// `.start` (spawnChildStart in the method handler); this drains its capture
// pipes, feeds them to the Supply taps, reaps it, and marks the promise Kept
// (finished) or Broken (timed out). The fallback path spawns here, lazily —
// for a promise whose eager spawn never happened (empty argv, fork failure).
// A Proc::Async whose program cannot be found on PATH will never start —
// the reason, in libuv's words, or "" when it looks runnable. (The spawn
// itself happens late, when something awaits or drives the process; its
// `.ready` and a `whenever` on its streams have to know sooner.)
std::string procSpawnMissing(const Value& proc) {
#if defined(_WIN32)
    (void)proc; return "";
#else
    if (!(proc.t == VT::Hash && proc.hash())) return "";
    auto it = proc.hash()->find("argv");
    if (it == proc.hash()->end() || !it->second.arr() || it->second.arr()->empty()) return "";
    std::string prog = (*it->second.arr())[0].toStr();
    if (prog.empty()) return "";
    auto bad = [&] {
        return "Failed to spawn process " + prog + ": no such file or directory (error code -2)";
    };
    if (prog.find('/') != std::string::npos) return ::access(prog.c_str(), X_OK) == 0 ? "" : bad();
    const char* path = std::getenv("PATH");
    std::string ps = path ? path : "/usr/bin:/bin";
    size_t st = 0;
    for (;;) {
        size_t c = ps.find(':', st);
        std::string dir = ps.substr(st, c == std::string::npos ? std::string::npos : c - st);
        if (dir.empty()) dir = ".";
        if (::access((dir + "/" + prog).c_str(), X_OK) == 0) return "";
        if (c == std::string::npos) break;
        st = c + 1;
    }
    return bad();
#endif
}

void Interpreter::runProcPromise(Value& promise, double timeoutSec) {
    if (!promise.hash()) return;
    if (promise.hash()->count("status") && (*promise.hash())["status"].toStr() != "Planned") return; // already run
    auto pit = promise.hash()->find("proc");
    if (pit == promise.hash()->end() || !pit->second.hash()) { (*promise.hash())["status"] = Value::str("Kept"); return; }
    Value& proc = pit->second;
    std::string out, err; int code = -1; bool timedout = false;
    long long childPid = 0;
    // Walk one stream's taps. A tap is a RECORD ({emit, done, quit, bin, lines}
    // — MethodCallPart2's tap branch); a bare callable is tolerated for older
    // callers. `body` decides what to do with each one.
    auto eachTap = [&](const char* key, const std::function<void(Value& cb, Value& done, bool bin, bool lines)>& body) {
        auto taps = proc.hash()->find(key);
        if (taps == proc.hash()->end() || !taps->second.arr()) return;
        for (auto& t : *taps->second.arr()) {
            Value cb = t, done; bool bin = false, lines = false;
            if (t.t == VT::Hash && t.hash()->count("emit")) {
                cb = (*t.hash())["emit"];
                auto d = t.hash()->find("done"); if (d != t.hash()->end()) done = d->second;
                auto b = t.hash()->find("bin");  bin = b != t.hash()->end() && b->second.truthy();
                auto l = t.hash()->find("lines"); lines = l != t.hash()->end() && l->second.truthy();
            }
            body(cb, done, bin, lines);
        }
    };
    // One chunk, straight from the pipe, to every tap on that stream. This runs
    // WHILE the child is alive — that is the whole point: `whenever
    // $proc.stdout.lines` used to fire only once the process had exited, so a
    // runner relaying a build's progress relayed it all after the build (issue
    // #51). rakupp's own react loop drives it, so a `whenever` block printing a
    // line prints it now.
    // A character stream decodes with the tap's `:enc`, else the process's,
    // else UTF-8 — and bytes that are not UTF-8 QUIT the stream (a trailing
    // partial sequence is only a chunk boundary, not an error).
    auto validUtf8 = [](const std::string& d) {
        size_t i = 0, n = d.size();
        while (i < n) {
            unsigned char c = (unsigned char)d[i];
            int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
            if (!len || i + len > n) return false;
            for (int k = 1; k < len; k++) if (((unsigned char)d[i + k] >> 6) != 2) return false;
            i += len;
        }
        return true;
    };
    // A chunk boundary may fall INSIDE a character: its first bytes end this
    // chunk and the rest open the next. The incomplete tail is held back per
    // stream and put in front of the next chunk, so a character stream only
    // ever sees whole characters (and malformed means malformed).
    std::map<std::string, std::string> utf8Carry;
    auto completeUtf8Prefix = [](const std::string& d) -> size_t {
        size_t n = d.size();
        for (size_t back = 1; back <= 3 && back <= n; back++) {
            unsigned char c = (unsigned char)d[n - back];
            if ((c >> 6) == 2) continue;                       // a continuation byte: keep looking
            int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
            return (size_t)len > back ? n - back : n;          // the lead's sequence is incomplete
        }
        return n;
    };
    auto emitChunk = [&](const char* key, const std::string& data0) {
        if (data0.empty()) return;
        auto taps = proc.hash()->find(key);
        if (taps == proc.hash()->end() || !taps->second.arr()) return;
        std::string& carry = utf8Carry[key];
        std::string whole = carry + data0;
        const size_t cut = completeUtf8Prefix(whole);
        carry = whole.substr(cut);
        whole.resize(cut);
        const std::string procEnc = proc.hash()->count("enc") ? (*proc.hash())["enc"].toStr() : std::string();
        for (auto& t : *taps->second.arr()) {
            Value cb = t; bool bin = false;
            std::string enc = procEnc;
            Value quitCb;
            if (t.t == VT::Hash && t.hash()->count("emit")) {
                cb = (*t.hash())["emit"];
                auto b = t.hash()->find("bin");  bin = b != t.hash()->end() && b->second.truthy();
                auto e = t.hash()->find("enc"); if (e != t.hash()->end()) enc = e->second.toStr();
                auto q = t.hash()->find("quit"); if (q != t.hash()->end()) quitCb = q->second;
                if (t.hash()->count("quit-fired")) continue;
            }
            if (cb.t != VT::Code && quitCb.t != VT::Code) continue;
            std::string data = data0;
            if (!bin) {
                if (enc.empty() || enc == "utf-8" || enc == "utf8") {
                    data = whole;
                    if (data.empty()) continue;   // only part of a character so far
                    if (!validUtf8(data)) {
                        if (t.t == VT::Hash) (*t.hash())["quit-fired"] = Value::boolean(true);
                        if (quitCb.t == VT::Code) {
                            Value ex = makeTypedEx("X::AdHoc", {{"payload", Value::str("Malformed UTF-8")}},
                                                   "Malformed UTF-8");
                            ValueList qa{ex}; callCallable(quitCb, qa);
                        }
                        continue;
                    }
                }
                else data = decodeTextEnc(data, enc);
            }
            if (cb.t != VT::Code) continue;   // a tap with only a :quit
            Value chunk = Value::str(data);
            // `.stdout(:bin)` taps get the bytes as a Blob (zef's fetcher
            // Buf.appends them); a plain tap gets the DECODED Str, as
            // Rakudo emits — a Blob chunk made every `whenever` block that
            // string-matched its lines see Blob.new(...) instead of text.
            if (bin) chunk.hashKind = "Blob";
            // character mode decodes with newline translation: "\r\n" is "\n"
            else if (data.find("\r\n") != std::string::npos) {
                std::string t; t.reserve(data.size());
                for (size_t i = 0; i < data.size(); i++)
                    if (!(data[i] == '\r' && i + 1 < data.size() && data[i + 1] == '\n')) t += data[i];
                chunk = Value::str(t);
            }
            ValueList ca{chunk};
            callCallable(cb, ca);
        }
    };
    std::exception_ptr sinkErr;
    ChildChunkSink sink = [&](bool isErr, const char* d, size_t n) {
        emitChunk(isErr ? "taps-err" : "taps", std::string(d, n));
    };
    long long tok = 0;
    { auto t = proc.hash()->find("spawn-token");
      if (t != proc.hash()->end()) { tok = t->second.toInt(); proc.hash()->erase(t); } }
    if (tok) {
        SpawnedChild sc;
        { std::lock_guard<std::mutex> lk(g_spawnedM);
          auto it = g_spawned.find(tok);
          if (it != g_spawned.end()) { sc = it->second; g_spawned.erase(it); } }
        childPid = sc.pid;
        spawnChildFinish(sc, timeoutSec, out, &err, code, timedout, this, &sink, &sinkErr);
    }
    else {
        std::vector<std::string> argv;
        if (proc.hash()->count("argv")) for (auto& x : *(*proc.hash())["argv"].arr()) argv.push_back(x.toStr());
        std::string cwd;
        { auto c = promise.hash()->find("cwd"); if (c != promise.hash()->end()) cwd = c->second.toStr(); }
        spawnCapture(argv, timeoutSec, out, code, timedout, this, &err, cwd, &childPid,
                     nullptr, false, 1, &sink, &sinkErr);
    }
    if (childPid) (*proc.hash())["pid"] = Value::integer(childPid);
    // The stream is closed once its process ended: release a line-splitter's
    // unterminated tail, then fire the tap's :done (TAP's stderr relay is
    // `.act({…}, :done({$err.done}))`) — with or without output, and whatever
    // the exit code.
    auto finishTaps = [&](const char* key) {
        // an incomplete character left at the very end goes out as it is
        {
            auto ci = utf8Carry.find(key);
            if (ci != utf8Carry.end() && !ci->second.empty()) {
                std::string rest; rest.swap(ci->second);
                eachTap(key, [&](Value& cb, Value&, bool bin, bool) {
                    if (bin || cb.t != VT::Code) return;
                    ValueList ca{Value::str(rest)};
                    callCallable(cb, ca);
                });
            }
        }
        eachTap(key, [&](Value& cb, Value& done, bool, bool lines) {
            if (lines && cb.t == VT::Code) {
                ValueList fin{Value::str(""), Value::boolean(true)};
                callCallable(cb, fin);
            }
            if (done.t == VT::Code) { ValueList none; callCallable(done, none); }
        });
    };
    finishTaps("taps");
    finishTaps("taps-err");
    storeProcStatus(proc, code); // exitcode + signal
    (*proc.hash())["timedout"] = Value::boolean(timedout);
    (*promise.hash())["status"] = Value::str(timedout ? "Broken" : "Kept");
    // A tap block that died threw inside the drain loop, where letting it out
    // would have left the child unreaped and its descriptors open. It was held
    // until here; now that the process is settled, it goes on its way.
    if (sinkErr) std::rethrow_exception(sinkErr);
}

// An attribute's SIGIL is a container type: `has @.a` holds an Array and
// `has %.h` a Hash, whatever shape the initialiser produced. Without this a
// `has @.a = (1,2)` kept the List and `has %.h = (a=>1)` kept the bare Pair, so
// `.WHAT` and the default renderer both disagreed with Rakudo.
Value coerceToSigil(Value v, char sigil) {
    if (sigil == '@') {
        // `has @.a is Buf`: a byte buffer IS the Positional container
        if (v.t == VT::Str && (v.hashKind == "Buf" || v.hashKind == "Blob")) return v;
        // an ITEMIZED list (`$[1,2,3]`, a `$x` holding an Array) is ONE element
        if (v.t == VT::Array && v.itemized && v.enumName.empty()) {
            Value a = Value::array();
            a.arr()->push_back(v);
            return a;
        }
        // …and the attribute OWNS its buffer. Construction is assignment, not
        // binding: `T.new(pts => @src)` fills @!pts FROM @src, and passing the
        // caller's array through here let the class's own later
        // `@!pts = @!pts.pairs` rewrite @src under the caller's feet
        // (Algorithm::KDimensionalTree).
        if (v.t == VT::Array) {
            v.isList = false; v.itemized = false;
            if (v.arr() && !v.payloadUnique()) {
                Value own = Value::array();
                *own.arr() = *v.arr();
                own.ofTypeM() = v.ofType();
                own.elemDefaultM() = v.elemDefault();
                return own;
            }
            return v;
        }
        if (v.t == VT::Nil || v.t == VT::Any) return v;
        Value a = Value::array();
        if (v.t == VT::Range) *a.arr() = v.flatten(); else a.arr()->push_back(v);
        return a;
    }
    if (sigil == '%') {
        if (v.t == VT::Hash) return v;
        if (v.t == VT::Nil || v.t == VT::Any) return v;
        Value h = Value::makeHash();
        ValueList items;
        if (v.t == VT::Array && v.arr()) items = *v.arr();
        // an object that says what it holds (`.list`/`.iterator`, declared or
        // delegated) spreads its pairs here rather than vanishing — a `%` attribute
        // initialised from such a wrapper is how zef hands its config around
        else if (!(v.t == VT::Object && g_objListItems && g_objListItems(v, items)))
            items = ValueList{v};
        for (auto& e : items)
            if (e.t == VT::Pair) (*h.hash())[e.s] = e.pairVal() ? *e.pairVal() : Value::any();
        return h;
    }
    return v;
}

bool defined(const Value& v) { return rtIsDefined(v); } // one rule (rtIsDefined owns it: enum type objects are undefined)

// √z from the MODULUS, the way Rakudo computes it — not std::sqrt(std::complex),
// whose libc++ form loses a ULP on the real part: (-3+4i).sqrt came out as
// 1.0000000000000002+2i instead of 1+2i. Both the method and the sub must use
// this, or `$z.sqrt` and `sqrt($z)` disagree.
Value complexSqrt(double re, double im) {
    double a = std::hypot(re, im);
    return Value::complex(std::sqrt((a + re) / 2.0),
                          std::copysign(std::sqrt((a - re) / 2.0), im));
}

// The declared KEY TYPE of an OBJECT hash (`my %h{Int}`), or "" for a plain one.
// Parser.cpp records it as the second half of declType ("Any,Int") and typedDefault
// copies that into ofType; `.keyof` reads the same half.
std::string objHashKeyType(const Value& h) {
    if (h.t != VT::Hash || !h.hashKind.empty()) return "";
    size_t c = h.ofType().find(',');
    if (c == std::string::npos) return "";
    // the SECOND component only: `:{ }` carries a third (see the `ctx%{}`
    // composer), and "Mu,Any" is not a key type
    size_t c2 = h.ofType().find(',', c + 1);
    return h.ofType().substr(c + 1, c2 == std::string::npos ? std::string::npos : c2 - c - 1);
}

// The REAL key of a hash entry, from whichever of the three sources has it.
//
// The store is `map<std::string, Value>`, so a key is a lookup STRING and its
// original value has to come from somewhere else. A Set/Bag/Mix parks it in the
// count's pairKey; an object hash reconstructs it from the declared key type; a
// plain hash genuinely keys on Str. Every site that hands a key back to a program
// — .keys, .pairs, .kv, iteration, .raku — needs the same answer, and before this
// existed each spelled the first two cases out for itself (or, mostly, didn't).
//
// A key type we cannot rebuild from a string (a class, or bare Any/Mu, where
// `%h{3}` and `%h<3>` are different keys in Rakudo and the same one here) stays a
// Str. That is the pre-existing gap — Hash keys being plain strings — narrowed to
// where it actually bites rather than papered over with a guess.
Value hashEntryKey(const Value& h, const std::string& k, const Value& stored) {
    if (stored.elemKey()) return *stored.elemKey();
    // An object-keyed hash remembers what the subscript actually named (see
    // ValueHash::objKeys_) — the string is only how the payload indexes it.
    if (h.t == VT::Hash && h.hash())
        if (const Value* ok = h.hash()->objKey(k)) return *ok;
    const std::string kt = objHashKeyType(h);
    if (kt.empty()) return Value::str(k);
    static const std::set<std::string> numericKey = {
        "Int", "UInt", "int", "Num", "num", "Rat", "FatRat", "Numeric", "Real", "Cool"
    };
    if (numericKey.count(kt)) {
        // numifyStr already picks the Raku-correct type ("33" -> Int, "1.5" -> Rat),
        // which is the same ladder that put the key there; a key that will not
        // numify (a Cool hash keyed by a Str) stays the Str it was.
        Value n = numifyStrFailure(k);
        if (n.isNumeric()) return n;
    }
    return Value::str(k);
}

// `.raku` / `.perl` — an EVAL-round-trippable representation of a value (as opposed
// to `.gist`, which is the human-readable form). Recursive over containers.
std::string rakuReprImpl(const Value& v, int depth, std::set<const void*>& seen);
// True while rendering an element of an `[…]` Array. Every Array slot is its own
// scalar container, so an itemized value nested there needs no `$` marker —
// Rakudo prints `[[1, 2],]`. Inside a `(…)` List the marker does matter.
static bool g_reprInArrayElem = false;

// A SELF-REFERENTIAL container cannot be written as a literal, so `.raku` names
// it: Rakudo renders `my $a = [42]; $a[1] = $a` as
//     ((my @Array_5513076856128) = $[42, @Array_5513076856128])
// — a declaration whose initializer mentions the very variable it declares. We
// printed `[...]` there, which is not Raku at all: roast's
// S02-names-vars/list_array_perl.t EVALs what .raku gives back and the parse
// error took the file's remaining ten tests with it.
//
// Containers that turn out to be referenced from inside themselves land here
// during a render, keyed by container, with the name to use. The name is
// invented at the BACK-reference; the frame that owns the container (the one
// that put it in `seen`) finds it afterwards and wraps its body in the
// declaration — which is why a cycle deeper in the structure declares itself
// there rather than at the top: `$[1, ((my @Array_x) = [2, @Array_x])]`.
static thread_local std::map<const void*, std::string> g_reprSelfRef;
static std::string reprSelfName(const void* p, bool isHash) {
    auto it = g_reprSelfRef.find(p);
    if (it != g_reprSelfRef.end()) return it->second;
    std::string nm = (isHash ? "%Hash_" : "@Array_") +
                     std::to_string((unsigned long long)(uintptr_t)p);
    g_reprSelfRef.emplace(p, nm);
    return nm;
}
std::string rakuRepr(const Value& v, int depth, std::set<const void*>& seen) {
    const void* ctr = v.t == VT::Array ? (const void*)v.arr()
                    : v.t == VT::Hash  ? (const void*)v.hash() : nullptr;
    // A container already being rendered is the back-reference itself — let the
    // impl answer it (with the name), and leave the wrapping to the frame that
    // owns it, further out.
    if (!ctr || seen.count(ctr)) return rakuReprImpl(v, depth, seen);
    std::string body = rakuReprImpl(v, depth, seen);
    auto it = g_reprSelfRef.find(ctr);
    if (it == g_reprSelfRef.end()) return body;
    std::string nm = it->second;
    g_reprSelfRef.erase(it);
    return "((my " + nm + ") = " + body + ")";
}
std::string rakuRepr(const Value& v) {
    std::set<const void*> seen;
    // a render can run user code (a `.raku` method), so the cycle names are
    // saved and restored rather than simply cleared
    auto saved = std::move(g_reprSelfRef);
    g_reprSelfRef.clear();
    std::string o = rakuRepr(v, 0, seen);
    g_reprSelfRef = std::move(saved);
    return o;
}
// Value.cpp renders Range endpoints with .raku; hand it this implementation.
static const bool g_rakuReprInstalled = ((g_rakuRepr = &rakuRepr), true);
void rejectNulPath(const std::string& path) {
    if (path.find('\0') != std::string::npos)
        throw RakuError{Value::typeObj("X::IO::Null"),
            "Cannot use null character (U+0000) as part of the path"};
}
static std::string rakuStrLit(const std::string& s) {
    static const char* H = "0123456789ABCDEF";
    auto hex = [&](uint32_t cp) {
        std::string r; bool lead = false;
        for (int sh = 28; sh >= 0; sh -= 4) {
            int d = (cp >> sh) & 0xF;
            if (d || lead || sh == 0) { r += H[d]; lead = true; }
        }
        return r;
    };
    // ASCII is a byte walk; anything above it has to be read as CODEPOINTS,
    // because two of the rules are not byte-shaped: a C1 control (U+80..U+9F)
    // is hexified like a C0 one, and a GRAPHEME that begins with a combining
    // mark is written as its codepoints, comma-joined, so that it reads back
    // (Str sheet ST-62). Everything else — a spacing mark, a keycap, a
    // variation selector, ZWJ, a tag, a jamo filler — has combining class 0
    // and goes out verbatim, which is exactly the line Rakudo draws.
    bool high = false;
    for (unsigned char c : s) if (c >= 0x80) { high = true; break; }
    std::string o = "\"";
    auto ascii1 = [&](unsigned char c) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') o += "\\r";
        else if (c == '$' || c == '@' || c == '%' || c == '&' || c == '{') { o += '\\'; o += (char)c; } // would interpolate
        else if (c == '\0') o += "\\0";
        else if (c == '\b') o += "\\b";
        // any other C0 control (and DEL) has no literal spelling — it went out
        // RAW, so `"\x[3]".raku` printed a string that looked empty and could
        // not be read back. Rakudo's form is `\x[1B]`, hex digits upper-case.
        else if (c < 0x20 || c == 0x7F) { o += "\\x["; o += hex(c); o += ']'; }
        else o += (char)c;
    };
    if (!high) { for (unsigned char c : s) ascii1(c); return o + "\""; }
    auto cps = utf8cp(s);
    auto starts = uniGraphemeStarts(cps);
    for (size_t g = 0; g < starts.size(); g++) {
        size_t from = starts[g], to = g + 1 < starts.size() ? starts[g + 1] : cps.size();
        if (to - from == 1 && cps[from] < 0x80) { ascii1((unsigned char)cps[from]); continue; }
        if (uniCombiningClass(cps[from]) > 0) {
            o += "\\x[";
            for (size_t k = from; k < to; k++) { if (k > from) o += ','; o += hex(cps[k]); }
            o += ']';
            continue;
        }
        for (size_t k = from; k < to; k++) {
            if (cps[k] >= 0x80 && cps[k] <= 0x9F) { o += "\\x["; o += hex(cps[k]); o += ']'; }
            else if (cps[k] < 0x80) ascii1((unsigned char)cps[k]);
            else o += cpToU8(cps[k]);
        }
    }
    return o + "\"";
}
// Does this key print as a COLON PAIR (`:a-b(1)`) rather than as an arrow pair
// (`"a-1" => 1`)? Rakudo asks whether the key is a Raku identifier, which is
// not the ASCII word-plus-hyphen set this used to test (sheet HM-16):
//
//   ident := <alpha> <word>*  [ <[-']> <alpha> <word>* ]*
//
// so a hyphen or apostrophe only counts BETWEEN word characters and the part
// after it must start with a LETTER: `a-b` and `a'b` are identifiers, `a-`,
// `-a` and `a-1` are not. And `alpha`/`word` are the Unicode classes, not the
// ASCII ones — `é`, `αβ` and `日本` are identifiers, `x·y` is not.
static bool rakuIdentKey(const std::string& s) {
    if (s.empty()) return false;
    bool ascii7 = true;
    for (unsigned char c : s) if (c >= 0x80) { ascii7 = false; break; }
    auto isAlphaCp = [](uint32_t cp) {
        if (cp < 128) return ascii::isalpha((unsigned char)cp) || cp == '_';
        return uniMatchesProp(cp, "L");
    };
    auto isWordCp = [&](uint32_t cp) {
        if (cp < 128) return ascii::isalnum((unsigned char)cp) || cp == '_';
        return isAlphaCp(cp) || uniMatchesProp(cp, "Nd");
    };
    std::vector<uint32_t> cps;
    if (ascii7) { cps.reserve(s.size()); for (unsigned char c : s) cps.push_back(c); }
    else cps = utf8cp(s);
    size_t i = 0;
    for (;;) {
        if (i >= cps.size() || !isAlphaCp(cps[i])) return false;   // each part starts with a letter
        i++;
        while (i < cps.size() && isWordCp(cps[i])) i++;
        if (i == cps.size()) return true;
        if (cps[i] != '-' && cps[i] != '\'') return false;
        i++;                                                       // …and another part must follow
    }
}
// An unfilled slot of an array reads as that array's DEFAULT, and `.raku`
// prints what a read would give: `my @a is default(7); @a[2] = 1` renders
// `[7, 7, 1]`, and a typed `my Int @a` renders its holes as `Int` (sheet
// LA-21). A hole is an undefined Any; a stored Any type object looks the same
// to a reader, which is how Rakudo prints those too.
static Value arrayHoleFill(const Value& arr, const Value& e) {
    if (e.t != VT::Any) return e;
    if (arr.elemDefault()) return *arr.elemDefault();
    if (!arr.ofType().empty() && arr.ofType() != "Mu" &&
        arr.ofType().find(',') == std::string::npos &&
        !ascii::islower((unsigned char)arr.ofType()[0]))
        return Value::typeObj(arr.ofType());
    return e;
}

std::string rakuReprImpl(const Value& v, int depth, std::set<const void*>& seen) {
    // a FAILURE renders as the constructor call that rebuilds it, `handled`
    // flag included: `.raku.EVAL` round-trips it
    if (v.t == VT::Hash && v.hashKind == "Failure" && v.hash()) {
        auto ex = v.hash()->find("exception");
        auto hd = v.hash()->find("handled");
        std::string out = "Failure.new(exception => " +
            (ex != v.hash()->end() ? rakuRepr(ex->second, depth + 1, seen) : std::string("X::AdHoc.new"));
        if (hd != v.hash()->end() && hd->second.truthy()) out += ", handled => Bool::True";
        return out + ")";
    }
    forceLazy(v);   // an unpulled gather renders its ELEMENTS, not `().Seq`
    // an ENDLESS sequence renders its cached prefix and MARKS the rest, Rakudo
    // style — nested occurrences included. (The .raku method arm pre-materialises
    // 100 elements for the top-level call; a nested one shows what is cached.)
    if (v.t == VT::Array && v.arr() && v.ext() &&
        std::static_pointer_cast<LazySeqState>(v.ext())->infinite) {
        // A lazy ARRAY says only that it is one — `[...]` — whatever it has
        // reified; a lazy List or Seq shows up to 100 of its elements and then
        // the marker, with `.Seq` only when it IS one (sheet LA-02).
        if (!v.isList) return "[...]";
        std::string out = "(";
        for (size_t i = 0; i < v.arr()->size() && i < 100; i++) {
            if (i) out += ", ";
            out += rakuRepr((*v.arr())[i], depth + 1, seen);
        }
        out += "...).lazy";
        if (v.s == "Seq") out += ".Seq";
        return out;
    }
    // `.raku` of a Proxy shows the VALUE it holds, not its FETCH/STORE pair —
    // URI::Query hands back lists of Proxy containers to keep them immutable.
    if (v.t == VT::Hash && v.hashKind == "Proxy" && v.hash() && g_deproxy)
        return rakuRepr(g_deproxy(v), depth, seen);
    // Guard against self-referential / deeply-nested data (`$foo<b> = $foo`): recursing
    // blindly builds an unbounded string and exhausts memory. Detect a revisited
    // container (a cycle) and stop; a large depth cap backstops pathological nesting.
    if (depth > 512) return "...";
    if (v.isAllomorph()) { // IntStr.new(42, "42") — round-trips via EVAL
        Value num = v; num.hashKind.clear();
        std::string face = num.s; num.s.clear();
        return v.typeName() + ".new(" + rakuRepr(num, depth + 1, seen) + ", " + rakuStrLit(face) + ")";
    }
    // An enum VALUE renders QUALIFIED — `Order::Less`, `Colour::Red` — because
    // .raku must round-trip through EVAL and the bare key need not be in scope.
    // Bool already spells itself out below; every other enum answered the bare
    // key, which is what `3 cmp 5` .raku showing `Less` instead of `Order::Less`
    // came from. Junctions tag themselves with enumName too (any/all/one/none)
    // and are NOT enums — they carry no enumType, which is what tells them apart.
    if (!v.enumName.empty() && !v.enumType.empty() && v.t != VT::Bool)
        return v.enumType.str() + "::" + v.enumName.str();
    switch (v.t) {
        case VT::Nil:  return "Nil";
        case VT::Any:  return "Any";
        case VT::Bool: return v.b ? "Bool::True" : "Bool::False";
        case VT::Type: {
            // a role pun renders as what it was written as, `Foo[Int]`, not as
            // the registry key that keeps two of them the same type
            std::string d = g_typeDispName ? g_typeDispName(v.s) : std::string();
            if (!d.empty()) return d;
            // …and a parameterized built-in names its parameter: array[str]
            return v.ofType().empty() ? v.s.str() : v.s.str() + "[" + v.ofType() + "]";
        }
        case VT::Str:
            // a Buf/Blob is a Str only in REPRESENTATION — its .raku is the
            // constructor that rebuilds it, over its ELEMENTS, not a string
            // literal of its raw bytes
            if (v.hashKind == "Buf" || v.hashKind == "Blob") {
                std::string nm = !v.enumName.empty() ? v.enumName   // an encoding names its own type
                               : v.hashKind + (v.ofType().empty() ? "" : "[" + v.ofType() + "]");
                std::string o = nm + ".new("; bool f = true;
                for (auto& e : v.blobList()) { if (!f) o += ","; f = false; o += std::to_string(e.toInt()); }
                return o + ")";
            }
            // an IO::Path's .raku is its full constructor, SPEC and CWD included
            // (its .gist is the short `"foo/bar".IO` form)
            if (v.hashKind == "IO") {
                char cbuf[4096];
                std::string cwd = v.ofType().empty()                      // an explicit :CWD wins
                                ? (getcwd(cbuf, sizeof cbuf) ? cbuf : ".")
                                : v.ofType();
                // a FLAVORED path names its flavor in the class, so it needs no
                // :SPEC; the plain one spells the spec out
                if (!v.enumName.empty())
                    return "IO::Path::" + v.enumName + ".new(" + rakuStrLit(v.s) +
                           ", :CWD(" + rakuStrLit(cwd) + "))";
                return "IO::Path.new(" + rakuStrLit(v.s) +
                       ", :SPEC(IO::Spec::Unix), :CWD(" + rakuStrLit(cwd) + "))";
            }
            // a standard handle's path rebuilds by name, not as a path literal
            if (v.hashKind == "IO::Special")
                return "IO::Special.new(" + rakuStrLit(v.s) + ")";
            if (v.hashKind == "CArray") { // a locally-built CArray rebuilds the same way
                std::string o = "CArray.new("; bool f = true;
                for (auto& e : v.blobList()) { if (!f) o += ","; f = false; o += std::to_string(e.toInt()); }
                return o + ")";
            }
            return rakuStrLit(v.s);
        case VT::Int:  return v.toStr();
        case VT::Rat: {
            std::string n = v.ratN() ? v.ratN()->toString() : "0";
            std::string d = v.ratD() ? v.ratD()->toString() : "1";
            if (v.fatRat()) return "FatRat.new(" + n + ", " + d + ")"; // FatRat.raku is explicit
            // Terminating decimal (denominator 2^a·5^b) prints as a decimal literal
            // with a fraction part kept, so EVAL round-trips to Rat: 0.25, -7.0, 0.1.
            // Anything else (incl. zero-denominator, or a denominator wider than
            // uint64 — 0.9999999999999999999999.raku) is the <n/d> form.
            if (v.ratD() && !v.ratD()->isZero() && v.ratD()->fitsU64()) {
                BigInt den = *v.ratD(); int p2 = 0, p5 = 0; BigInt q, r;
                while (true) { BigInt::divmod(den, BigInt(2), q, r); if (!r.isZero()) break; den = q; p2++; }
                while (true) { BigInt::divmod(den, BigInt(5), q, r); if (!r.isZero()) break; den = q; p5++; }
                if (den.fitsLL() && den.toLL() == 1) {
                    int k = std::max(p2, p5);
                    BigInt scaled = *v.ratN();
                    for (int t = 0; t < k - p2; t++) scaled = scaled * BigInt(2);
                    for (int t = 0; t < k - p5; t++) scaled = scaled * BigInt(5);
                    std::string digits = scaled.toString();
                    bool neg = !digits.empty() && digits[0] == '-';
                    if (neg) digits.erase(0, 1);
                    while ((int)digits.size() <= k) digits.insert(0, "0");
                    std::string out = digits.substr(0, digits.size() - k) + "." +
                                      (k ? digits.substr(digits.size() - k) : "0");
                    if (!k) out = digits + ".0";
                    return (neg ? "-" : "") + out;
                }
            }
            return "<" + n + "/" + d + ">";
        }
        case VT::Num: { // Nums round-trip with an exponent so EVAL doesn't read a Rat
            std::string g = v.toStr();
            if (g == "Inf" || g == "-Inf" || g == "NaN") return g;
            if (g.find('e') == std::string::npos && g.find('E') == std::string::npos)
                g += "e0";
            return g;
        }
        case VT::Match: {
            // Rakudo's Match.raku is a constructor call, not the ｢…｣ gist: the
            // positional captures come back as :list and the named ones as :hash.
            std::string o = "Match.new(:orig(" +
                (v.ext() ? rakuStrLit(*std::static_pointer_cast<std::string>(v.ext())) : rakuStrLit(v.s)) +
                "), :from(" + std::to_string(v.rFrom()) + "), :pos(" + std::to_string(v.rTo()) + ")";
            if (v.arr() && !v.arr()->empty()) {
                o += ", :list((";
                for (size_t k = 0; k < v.arr()->size(); k++) {
                    if (k) o += ", ";
                    o += rakuRepr((*v.arr())[k], depth + 1, seen);
                }
                o += (v.arr()->size() == 1 ? ",))" : "))");
            }
            if (v.hash() && !v.hash()->empty()) {
                o += ", :hash(Map.new((";
                bool first = true;
                for (auto& kv : *v.hash()) {
                    if (!first) o += ", ";
                    first = false;
                    o += ":" + kv.first + "(" + rakuRepr(kv.second, depth + 1, seen) + ")";
                }
                o += ")))";
            }
            return o + ")";
        }
        case VT::Regex:
            return v.s.find('/') == std::string::npos ? "rx/" + v.s + "/" : "rx{" + v.s + "}";
        case VT::Complex: {
            // An INFINITE or NaN imaginary part needs the `\i` spelling to read
            // back: `Infi` is a word, `Inf\i` is the number (Str sheet ST-06).
            std::string g = v.gist();
            const double im = v.im();
            if ((std::isinf(im) || std::isnan(im)) && g.size() > 1 && g.back() == 'i' &&
                g[g.size() - 2] != '\\')
                g.insert(g.size() - 1, "\\");
            return "<" + g + ">";
        }
        case VT::Range:
            // Str range: the endpoints are STRING LITERALS, escapes and all —
            // `'!'..'&'` is `"!".."\&"`, since `&` opens an interpolation
            // …but carried endpoints win even here: `"5"..9` is a string range
            // whose RIGHT end was written as a number, and the codepoints have
            // forgotten that.
            if (v.ofType() == "Str" && !rangeEnds(v))
                return rakuStrLit(cpToU8((uint32_t)v.rFrom())) + (v.rExFrom() ? "^" : "") + ".." +
                       (v.rExTo() ? "^" : "") + rakuStrLit(cpToU8((uint32_t)v.rTo()));
            // …and a range whose endpoints are objects renders THOSE, as gist
            // does: `Supply.minmax` builds "a".."ccc", whose ends have no
            // integer form for the fields to hold.
            // …and the same for any OTHER carried endpoint: the integer fields are
            // only the floors, so a Num-shifted range (`^42 - 2e0`) printed
            // `-2..^40` where its own .min and .max already said -2e0 and 40e0.
            // gist has always spelled these; .raku only did it for Strs.
            if (const RangeEnds* re = rangeEnds(v))
                return rakuRepr(re->from, depth + 1, seen) + (v.rExFrom() ? "^" : "") + ".." +
                       (v.rExTo() ? "^" : "") + rakuRepr(re->to, depth + 1, seen);
            // an endless endpoint is Inf, not the long long it is parked in, and
            // `^Inf` keeps the long form (0..^Inf) — gist already spells both
            if (v.rTo() >= 9000000000000000000LL || v.rFrom() <= -9000000000000000000LL)
                return v.gist();
            // `0..^N` is `^N` here too — Rakudo shows the short form for .raku as
            // well as gist. Same Int-zero-only rule as Value::gist.
            if (!v.rExFrom() && v.rExTo() && v.rFrom() == 0 && !v.rNum())
                return "^" + std::to_string(v.rTo());
            // A fractional range keeps its real endpoints in n/im and its integer
            // fields are their floors, so this printed `1.5..2.5` as `1..2`.
            // gist already spells the endpoints, including a Rat's.
            if (v.rNum()) return v.gist();
            return std::to_string(v.rFrom()) + (v.rExFrom() ? "^" : "") + ".." + (v.rExTo() ? "^" : "") + std::to_string(v.rTo());
        case VT::Pair: {
            Value val = v.pairVal() ? *v.pairVal() : Value::nil();
            if (v.pairKey()) { // non-string key (Int, nested Pair, …)
                std::string krepr = rakuRepr(*v.pairKey(), depth + 1, seen);
                if (v.pairKey()->t == VT::Pair) krepr = "(" + krepr + ")"; // parenthesize a pair-key
                // …and a type-object key, or it would read back as an autoquoted Str
                else if (v.pairKey()->t == VT::Type || v.pairKey()->t == VT::Any || v.pairKey()->t == VT::Nil) krepr = "(" + krepr + ")";
                return krepr + " => " + rakuRepr(val, depth + 1, seen);
            }
            if (!rakuIdentKey(v.s))
                return rakuStrLit(v.s) + " => " + rakuRepr(val, depth + 1, seen);
            // A Bool VALUE under an identifier key is the flag spelling that
            // wrote it: `:a` and `:!a`, never `:a(Bool::True)` (sheet HM-16).
            // Only a Bool value — `:a(Bool)` is the type object. This is the
            // PAIR's own rendering, so it reaches a Pair inside a list or an
            // Array (`[:a]`); Hash.raku spells its entries out in full.
            if (val.t == VT::Bool && val.enumType.empty())
                return (val.b ? ":" : ":!") + v.s;
            return ":" + v.s + "(" + rakuRepr(val, depth + 1, seen) + ")";
        }
        case VT::Array: {
            if (v.s == "Slip" && (!v.arr() || v.arr()->empty())) return "Empty";
            if (v.hashKind == "Capture") { // \(…) literal round-trips as itself
                std::string o = "\\(";
                bool first = true;
                if (v.arr()) for (auto& e : *v.arr()) {
                    if (!first) o += ", "; first = false;
                    // a NAMED part is a colonpair; a Pair that went in as a
                    // POSITIONAL stays a Pair: `\(1, "a" => 3)` round-trips
                    if (e.t == VT::Pair && e.namedArg)
                        o += ":" + e.s + "(" + rakuRepr(e.pairVal() ? *e.pairVal() : Value(), depth + 1, seen) + ")";
                    else if (e.t == VT::Pair)
                        o += (e.pairKey() ? rakuRepr(*e.pairKey(), depth + 1, seen) : rakuStrLit(e.s.str())) +
                             " => " + rakuRepr(e.pairVal() ? *e.pairVal() : Value(), depth + 1, seen);
                    else o += rakuRepr(e, depth + 1, seen);
                }
                return o + ")";
            }
            // Junctions render as their constructor form: none(1, 2, 3)
            if (!v.enumName.empty() && v.arr() &&
                (v.enumName == "any" || v.enumName == "all" || v.enumName == "one" || v.enumName == "none")) {
                std::string o = v.enumName + "(";
                bool first = true;
                for (auto& e : *v.arr()) { if (!first) o += ", "; first = false; o += rakuRepr(e, depth + 1, seen); }
                return o + ")";
            }
            // a cycle: name the container and let its owning frame declare it
            if (v.arr() && !seen.insert(v.arr()).second) return reprSelfName(v.arr(), false);
            // A TYPED array round-trips through its parameterized constructor:
            // `my Int @a = 1, 2` is `Array[Int].new(1, 2)`, a native one
            // `array[int].new(1, 2)`, and an empty one still names its type.
            // Only .raku carries it — .gist stays `[1 2]` (sheet LA-21).
            if (!v.isList && !v.ofType().empty() && v.ofType() != "Mu" &&
                v.ofType().find(',') == std::string::npos && !v.shape()) {
                std::string ctor = ascii::islower((unsigned char)v.ofType()[0])
                                       ? "array[" + v.ofType() + "]" : "Array[" + v.ofType() + "]";
                std::string ty = ctor + ".new(";
                bool wasTyElem = g_reprInArrayElem;
                g_reprInArrayElem = true;
                bool tyFirst = true;
                if (v.arr()) for (auto& e : *v.arr()) {
                    if (!tyFirst) ty += ", ";
                    tyFirst = false;
                    ty += rakuRepr(arrayHoleFill(v, e), depth + 1, seen);
                }
                g_reprInArrayElem = wasTyElem;
                if (v.arr()) seen.erase(v.arr());
                ty += ")";
                return v.itemized && !wasTyElem ? "$(" + ty + ")" : ty;
            }
            std::string o(1, v.isList ? '(' : '[');
            bool wasElem = g_reprInArrayElem;
            if (v.arr()) {
                bool first = true;
                g_reprInArrayElem = !v.isList;
                for (auto& e : *v.arr()) {
                    if (!first) o += ", ";
                    first = false;
                    // an `is default(7)` array shows its holes as 7, not Any
                    o += rakuRepr(v.isList ? e : arrayHoleFill(v, e), depth + 1, seen);
                }
                g_reprInArrayElem = wasElem;
                if (v.isList && v.arr()->size() == 1) o += ",";
                // a 1-element ARRAY holding an iterable disambiguates with a
                // trailing comma too: [1..5,] (else the raku form would flatten)
                if (!v.isList && v.arr()->size() == 1 &&
                    ((*v.arr())[0].t == VT::Range || (*v.arr())[0].t == VT::Array ||
                     (*v.arr())[0].t == VT::Hash))
                    o += ",";
                seen.erase(v.arr());
            }
            o += v.isList ? ')' : ']';
            // a Seq's .raku is the list form plus the coercion that rebuilds it:
            // `(1, 2).Seq`. Only .raku carries it — .gist/.Str stay `(1 2)`.
            // It goes on BEFORE the itemization marker: the `$( … )` has to
            // enclose the coercion as well, or `$(().Seq)` reads back as the
            // itemized empty list with a `.Seq` called on it.
            bool seq = v.isList && v.s == "Seq";
            if (seq) o += ".Seq";
            // a Slip's .raku is Rakudo's `slip(1, 2, 3)` — the round-tripping
            // form. It rendered as a plain list here, so a Slip and a List with
            // the same elements were indistinguishable in test output: both
            // sides of Array::Agnostic's failing `is-deeply` printed `(1, 2, …)`.
            bool slipv = v.isList && v.s == "Slip";
            if (slipv) o = "slip" + o;
            // an ITEMIZED container carries its `$` marker — `($t,)` for a
            // `$`-held list is `($(1, 2),)` — except as an ARRAY element, whose
            // slot itemizes anyway (`[$x,]` is `[[1, 2],]`)
            if (v.itemized && !wasElem) {
                if (seq || slipv) o = "$(" + o + ")";
                else if (v.isList && v.arr() && v.arr()->empty()) o = "$( )"; // Rakudo's empty item
                else o = "$" + o;
            }
            return o;
        }
        case VT::Hash: {
            // a bare `Mu.new` / `Any.new` instance: no attributes, no container
            if ((v.hashKind == "Mu" || v.hashKind == "Any") && (!v.hash() || v.hash()->empty()))
                return v.hashKind + ".new";
            // a Signature inside a list renders as its literal, as it does alone
            // (`(:(Int $), "x").raku` is `(:(Int $), "x")`, not the record behind it)
            if (v.hashKind == "Signature" && v.hash()) {
                auto rs = v.hash()->find("rakustr");
                auto st = v.hash()->find("str");
                std::string body = rs != v.hash()->end() ? rs->second.toStr()
                                 : st != v.hash()->end() ? st->second.toStr() : std::string("()");
                return ":" + body;
            }
            if (v.hash() && !seen.insert(v.hash()).second) return reprSelfName(v.hash(), true); // cycle
            std::vector<std::string> keys;
            if (v.hash()) for (auto& kv : *v.hash()) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());
            // A QuantHash renders as the expression that rebuilds it, not as a
            // Hash literal: the weighted kinds as a pair list coerced to the
            // kind, the Set family as a constructor over their elements, and a
            // Map as Map.new((…)).
            // An ELEMENT renders as itself, not as its key string: the key is a
            // lookup string, and the original value rides in the count's pairKey
            // (baggyKey). `set(1,2).raku` is `Set.new(1,2)`, not `Set.new("1","2")`.
            auto elemRepr = [&](const std::string& k) {
                const Value& cnt = v.hash()->at(k);
                return cnt.pairKey() ? rakuRepr(*cnt.pairKey(), depth + 1, seen) : rakuStrLit(k);
            };
            // An EMPTY immutable one renders as its SUB form — `set()`, `bag()`,
            // `mix()` — which is how Rakudo writes it and what round-trips. The
            // constructor spelling is kept for the non-empty case, and for the
            // mutable *Hash kinds, which stay `SetHash.new()` / `().BagHash`
            // however empty they are.
            if (keys.empty() &&
                (v.hashKind == "Set" || v.hashKind == "Bag" || v.hashKind == "Mix")) {
                if (v.hash()) seen.erase(v.hash());
                std::string n = v.hashKind.str();
                for (auto& ch : n) ch = (char)ascii::tolower((unsigned char)ch);
                return n + "()";
            }
            if (v.hashKind == "Set" || v.hashKind == "SetHash") {
                std::string o = v.hashKind + ".new("; bool f = true;
                for (auto& k : keys) { if (!f) o += ","; f = false; o += elemRepr(k); }
                if (v.hash()) seen.erase(v.hash());
                return o + ")";
            }
            if (v.hashKind == "Bag" || v.hashKind == "BagHash" ||
                v.hashKind == "Mix" || v.hashKind == "MixHash") {
                std::string o = "("; bool f = true;
                for (auto& k : keys) {
                    if (!f) o += ","; f = false;
                    o += elemRepr(k) + "=>" + rakuRepr(v.hash()->at(k), depth + 1, seen);
                }
                if (v.hash()) seen.erase(v.hash());
                return o + ")." + v.hashKind;
            }
            if (v.hashKind == "Map") {
                // The EMPTY Map is `Map.new` — no argument list at all, which is
                // also how it reads back (sheet HM-13). `.gist` keeps the `(())`.
                if (keys.empty()) {
                    if (v.hash()) seen.erase(v.hash());
                    return v.itemized && !g_reprInArrayElem ? "$(Map.new)" : "Map.new";
                }
                std::string o = "Map.new(("; bool f = true;
                for (auto& k : keys) {
                    if (!f) o += ","; f = false;
                    Value val = v.hash()->at(k);
                    o += rakuIdentKey(k) ? ":" + k + "(" + rakuRepr(val, depth + 1, seen) + ")"
                                         : rakuStrLit(k) + " => " + rakuRepr(val, depth + 1, seen);
                }
                if (v.hash()) seen.erase(v.hash());
                o += "))";
                // an itemized Map carries the `$` marker a `$`-held container
                // shows, and it has to enclose the WHOLE constructor
                if (v.itemized && !g_reprInArrayElem) o = "$(" + o + ")";
                return o;
            }
            // An OBJECT hash renders as the DECLARATION that rebuilds it —
            // `(my Any %{Int} = 3 => "a")` — because `{3 => "a"}` would round-trip
            // through EVAL as a plain Str-keyed hash and lose the constraint. The
            // entries are the same as below, except the key is its real value.
            const std::string okt = objHashKeyType(v);
            if (!okt.empty()) {
                std::string vt = v.ofType().substr(0, v.ofType().find(','));
                std::string o = "(my " + (vt.empty() ? "Any" : vt) + " %{" + okt + "}";
                bool f = true;
                for (auto& k : keys) {
                    o += f ? " = " : ", "; f = false;
                    Value val = v.hash()->at(k);
                    if (val.t == VT::Array || val.t == VT::Hash) val.itemized = true;
                    std::string rv = rakuRepr(val, depth + 1, seen);
                    // the key's OWN spelling, not the payload's index string —
                    // an object hash indexes by identity (`Str|a`), so asking
                    // whether THAT looked like an identifier never said yes
                    Value rk = hashEntryKey(v, k, v.hash()->at(k));
                    o += (rk.t == VT::Str && rakuIdentKey(rk.s))
                             ? ":" + rk.s.str() + "(" + rv + ")"
                             : rakuRepr(rk, depth + 1, seen) + " => " + rv;
                }
                if (v.hash()) seen.erase(v.hash());
                return v.itemized && !g_reprInArrayElem ? "$" + o + ")" : o + ")";
            }
            // A VALUE-TYPED hash names its type the same way — `(my Int % = :a(1))`
            // — because `{:a(1)}` would read back untyped (sheet HM-05/HM-06).
            // The `is default` knob alone does NOT trigger this form: Rakudo
            // prints `my %h is default(0) = a => 1` as a plain `{:a(1)}`.
            {
                const std::string vt = v.ofType().substr(0, v.ofType().find(','));
                if (!vt.empty() && vt != "Mu" && !ascii::islower((unsigned char)vt[0])) {
                    std::string o = "(my " + vt + " %";
                    bool f = true;
                    for (auto& k : keys) {
                        o += f ? " = " : ", "; f = false;
                        Value val = v.hash()->at(k);
                        if (val.t == VT::Array || val.t == VT::Hash) val.itemized = true;
                        std::string rv = rakuRepr(val, depth + 1, seen);
                        o += rakuIdentKey(k) ? ":" + k + "(" + rv + ")"
                                             : rakuStrLit(k) + " => " + rv;
                    }
                    if (v.hash()) seen.erase(v.hash());
                    return v.itemized && !g_reprInArrayElem ? "$" + o + ")" : o + ")";
                }
            }
            std::string o = "{"; bool first = true;
            for (auto& k : keys) {
                if (!first) o += ", "; first = false;
                Value val = v.hash()->at(k);
                // Every hash VALUE sits in a Scalar container, so an Array or Hash
                // stored there is itemized whether or not the flag happens to be set
                // — `%h<k> = [1,2]` set it, `my %h = k => [1,2]` never did, and
                // Rakudo renders `$[1, 2]` for both.
                if (val.t == VT::Array || val.t == VT::Hash) val.itemized = true;
                bool wasElem = g_reprInArrayElem; g_reprInArrayElem = false;
                std::string rv = rakuRepr(val, depth + 1, seen);
                g_reprInArrayElem = wasElem;
                o += rakuIdentKey(k) ? ":" + k + "(" + rv + ")"
                                     : rakuStrLit(k) + " => " + rv;
            }
            if (v.hash()) seen.erase(v.hash());
            o += "}";
            // an itemized hash (one held in a `$`) shows the same `$` marker a
            // list does — except as an Array element, whose slot itemizes anyway
            if (v.itemized && !g_reprInArrayElem) o = "$" + o;
            return o;
        }
        case VT::Object: {
            if (!v.obj() || !v.obj()->cls) return v.gist();
            // a class's OWN `.raku` renders its objects, nested ones included
            if (g_objMethodStr) { std::string o; if (g_objMethodStr(v, "raku", o)) return o; }
            std::string r = v.obj()->cls->name + ".new";
            // INHERITED attributes count: iterating only cls->attrs dropped every
            // one, so `class Q is P` reprd as `Q.new(q => 2)` and would not survive
            // a round trip through EVAL.
            std::vector<const ClassAttr*> pub;
            collectPubAttrs(v.obj()->cls.get(), pub);
            std::string inner;
            // An exception's `message` is a METHOD in Rakudo, not an attribute
            // it was built with: `X::AdHoc.new(payload => "…")` is the whole of
            // its .raku (ours keeps the text in a slot so .message can answer)
            bool isEx = false;
            for (ClassInfo* c = v.obj()->cls.get(); c && !isEx; c = c->parent.get())
                if (c->name == "Exception" || c->nativeParent == "Exception") isEx = true;
            for (auto* at : pub) {
                if (isEx && at->name == "message") {
                    bool own = false;   // …unless the class declares one itself
                    for (auto& a2 : v.obj()->cls->attrs) if (&a2 == at) own = true;
                    if (!own || v.obj()->cls->name == "X::AdHoc" || v.obj()->cls->name == "Exception") continue;
                }
                auto it = v.obj()->attrs.find(at->name);
                // an UNSET typed attribute shows its declared type (`i => Int`)
                Value av = it != v.obj()->attrs.end() ? it->second
                         : (at->type.empty() ? Value::any() : Value::typeObj(at->type));
                if (av.t == VT::Any && !at->type.empty()) av = Value::typeObj(at->type);
                if (!inner.empty()) inner += ", ";
                inner += at->name + " => " + rakuRepr(av, depth + 1, seen);
            }
            return inner.empty() ? r : r + "(" + inner + ")";
        }
        default: return v.gist();
    }
}

// Adverbs the shared occurrence-selection code (substSelect) understands. An
// ordinal written as one token (`:2nd`, `:3x`) counts too. `.match` routes
// through that code only when EVERY adverb is one of these — `:overlap` and
// `:exhaustive` are not implemented there and it THROWS on an unknown name,
// which would abort the caller rather than degrade to a plain match.
bool substSelectKnowsAdverb(const std::string& k) {
    static const std::set<std::string> known = {
        "g", "global", "x", "nth", "st", "nd", "rd", "th", "p", "pos",
        "c", "continue", "i", "ignorecase", "samecase", "ii", "s", "sigspace",
        "samespace", "ss", "samemark", "mm", "m", "ignoremark",
        // `:as` says what each match should be handed back AS. It was absent
        // here, which made `.match` treat the whole call as unrecognised and
        // fall back to a plain one-match search — silently dropping the `:g`
        // or `:x` that came with it.
        "as", "ov", "overlap"};
    if (known.count(k)) return true;
    if (k.size() >= 2 && ascii::isdigit((unsigned char)k[0])) { // :2nd / :3x
        size_t d = 0; while (d < k.size() && ascii::isdigit((unsigned char)k[d])) d++;
        std::string suf = k.substr(d);
        return suf == "st" || suf == "nd" || suf == "rd" || suf == "th" || suf == "x";
    }
    return false;
}
// Does `v` satisfy a MATCHER argument (the thing `.grep`/`.first` take)?
// A Regex goes through the engine — the generic `~~` in applyArith does not know
// regexes — and a JUNCTION of matchers is tested eigenstate by eigenstate so a
// regex inside one still matches (`.grep(none /<[aeiou]>/)`).
bool matcherAccepts(Interpreter& I, const Value& v, const Value& mt) {
    // The pattern `.grep`/`.first` was HANDED — the match is ours, not the
    // caller's, so it publishes no `$/` (Rakudo: `<ab ac>.grep(/(a)/)` leaves
    // `$/` undefined). Scoped to this arm only: the Code arm below is the
    // caller's own block and must keep its own `$/`.
    if (mt.t == VT::Regex) { Interpreter::MatchVarGuard noSlash;
                             return I.regexMatch(v.toStr(), mt.s).truthy(); }
    if (mt.t == VT::Array && mt.arr() &&
        (mt.enumName == "any" || mt.enumName == "all" ||
         mt.enumName == "one" || mt.enumName == "none")) {
        JunctionCollapse jc(mt.enumName);          // short-circuits; see Value.h
        for (auto& e : *mt.arr()) { jc.feed(matcherAccepts(I, v, e)); if (jc.done()) break; }
        return jc.verdict();
    }
    if (mt.t == VT::Code) return predAnswerTruthy(I, I.callCallable(const_cast<Value&>(mt), ValueList{v}), v);
    // A matcher OBJECT (one whose class defines ACCEPTS) is what `.grep`/`.first`
    // get handed when the pattern is a custom matcher — `.grep(glob("*.txt"))`.
    // applyArith knows nothing about ACCEPTS, so those greps came back empty.
    if (mt.t == VT::Object && mt.obj() && mt.obj()->cls) {
        for (ClassInfo* ci = mt.obj()->cls.get(); ci; ci = ci->parent.get())
            if (ci->methods.count("ACCEPTS")) {
                ValueList one{v};
                return I.methodCall(const_cast<Value&>(mt), "ACCEPTS", one).truthy();
            }
    }
    return I.smartmatchValue("~~", v, mt).truthy(); // an element that IS `*` is a value, not a curry
}

// Truth of a CODE matcher's ANSWER, for element `elem`. A block may answer a
// Regex — `{ .defined && /re/ }` returns the `&&` right side as the object —
// and Rakudo boolifies that answer by MATCHING it against the element, writing
// the caller's `$/` (Regex.Bool reads the topic and stores the match through
// getlexcaller). Reading the Regex value's plain truth instead was always-true
// and left `$/` unset: HTTP::Tiny's multipart-boundary `~$/` came back ""
// (battery regression, v3.20.1). The closure's frame is already popped when
// this runs, so setMatchVar lands the match in the CALLER's `$/`, which is
// exactly where Rakudo puts it.
bool predAnswerTruthy(Interpreter& I, const Value& res, const Value& elem) {
    if (res.t == VT::Regex) {
        Value m = I.regexMatch(elem.toStr(), res.s);
        bool t = m.truthy();
        I.setMatchVar(std::move(m));
        return t;
    }
    return res.truthy();
}

// The positional arity of a Code value — how many elements `.map`/`for` feed it
// per iteration (`-> $k,$v {…}` → 2; `{ $^a … $^b }` → 2; `{ $_ }` / builtin → 1).
size_t codeArity(const Value& code) {
    if (code.t != VT::Code || !code.code()) return 1;
    // A WhateverCode takes one argument per `*` it was written with, and that is
    // how many elements `.map` hands it: `<1 2 3 4>.map(* + *)` is (3, 7), and
    // `%h.kv.map($ns ~ * => *)` pairs each key with its value (YAMLish).
    if (code.code()->isWhateverCode && code.code()->whateverArity > 1)
        return (size_t)code.code()->whateverArity;
    if (code.code()->params) {
        size_t n = 0;
        for (auto& p : *code.code()->params) if (!p.named && !p.slurpy) n++;
        if (n) return n;
    }
    if (size_t n = code.code()->placeholderPos()) return n;
    return 1;
}

// ---- UTF-8 / codepoint helpers ----
std::vector<uint32_t> utf8cp(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1f; len = 2; }
        else if ((c >> 4) == 0xe) { cp = c & 0x0f; len = 3; }
        else if ((c >> 3) == 0x1e) { cp = c & 0x07; len = 4; }
        else { cp = c; len = 1; }
        // a lead byte whose continuation bytes are not there (utf8-c8 keeps
        // such bytes as they came: `Buf.new(76, 0xE9, 111, 110)`) is a
        // character of its own — as the continuation-skipping byte walkers
        // count it, so an index here and a byte offset there agree
        for (int k = 1; k < len; k++)
            if (i + k >= n || ((unsigned char)s[i + k] & 0xC0) != 0x80) { cp = c; len = 1; break; }
        if (len > 1)
            for (int k = 1; k < len; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3f);
        out.push_back(cp); i += len;
    }
    return out;
}
// How many leading bytes of `s` are plain ASCII, looking at no more than
// `limit` of them. Where the run reaches, a codepoint index IS a byte index,
// which is what lets the nqp string ops skip utf8cp() altogether — see the
// comment above their cases in rtNqpOp. Eight bytes at a time: these ops are
// how pure-Raku tokenizers walk text, so this runs once per character scanned.
size_t asciiRun(const std::string& s, size_t limit) {
    size_t n = std::min(limit, s.size()), i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        std::memcpy(&w, s.data() + i, 8);
        if (w & 0x8080808080808080ULL) break;   // a high bit somewhere in this word
    }
    for (; i < n; i++) if ((unsigned char)s[i] & 0x80) break;
    return i;
}
bool allAscii(const std::string& s) { return asciiRun(s, s.size()) == s.size(); }

// The same three questions, answered once per string instead of once per
// character. A CowStr long enough to have been promoted carries an immutable
// body (StrBody), and these properties are pure functions of that text — so the
// answer can be cached there. Without the cache the "ASCII fast path" in the
// scanning ops below still walked the prefix on every single call, which is
// what made a pure-Raku tokenizer quadratic no matter how cheap the walk was.
// A short (unpromoted) string has no body; rescanning ~64 bytes is free.
bool cowAllAscii(const CowStr& s) {
    const StrBody* b = s.body();
    if (!b) return allAscii(s.str());
    signed char c = b->allAscii.load(std::memory_order_relaxed);
    if (c < 0) {
        c = allAscii(b->text) ? 1 : 0;
        b->allAscii.store(c, std::memory_order_relaxed);
    }
    return c == 1;
}
bool cowByteIsGraphemeIndex(const CowStr& s) {
    const StrBody* b = s.body();
    if (!b) return byteIsGraphemeIndex(s.str());
    if (!cowAllAscii(s)) return false;
    signed char c = b->crFree.load(std::memory_order_relaxed);
    if (c < 0) {
        c = std::memchr(b->text.data(), '\r', b->text.size()) == nullptr ? 1 : 0;
        b->crFree.store(c, std::memory_order_relaxed);
    }
    return c == 1;
}
long long cowGraphemeCount(const CowStr& s) {
    const StrBody* b = s.body();
    if (!b) return graphemeCount(s.str());
    long long n = b->nGraphemes.load(std::memory_order_relaxed);
    if (n < 0) {
        n = cowByteIsGraphemeIndex(s) ? (long long)b->text.size()
                                      : (long long)uniGraphemeCount(utf8cp(b->text));
        b->nGraphemes.store(n, std::memory_order_relaxed);
    }
    return n;
}

// The positional-op tables for NON-ASCII text (see StrBody in Value.h).
// cowCpIndex: byte offset of every codepoint + an end sentinel — the nqp ops
// (ordat/eqat/substr/index/…) index codepoints. cowGraphemeIndex: byte offset
// of every grapheme + sentinel — the Raku-level methods index graphemes. Both
// answer nullptr for a short (unpromoted) string, where a per-call rescan of
// ≤64 bytes is free and there is no body to cache on. Built at most once per
// body; the CAS loser deletes its copy.
const std::vector<uint32_t>* cowCpIndex(const CowStr& s) {
    const StrBody* b = s.body();
    if (!b) return nullptr;
    const std::vector<uint32_t>* t = b->cpIndex.load(std::memory_order_acquire);
    if (t) return t;
    const std::string& x = b->text;
    auto* mine = new std::vector<uint32_t>;
    mine->reserve(x.size() / 2 + 2);
    for (size_t i = 0; i < x.size(); i++)
        if ((static_cast<unsigned char>(x[i]) & 0xC0) != 0x80) mine->push_back((uint32_t)i);
    mine->push_back((uint32_t)x.size());
    const std::vector<uint32_t>* expect = nullptr;
    if (b->cpIndex.compare_exchange_strong(expect, mine, std::memory_order_acq_rel))
        return mine;
    delete mine;
    return expect;
}
const std::vector<uint32_t>* cowGraphemeIndex(const CowStr& s) {
    const StrBody* b = s.body();
    if (!b) return nullptr;
    const std::vector<uint32_t>* t = b->gIndex.load(std::memory_order_acquire);
    if (t) return t;
    const std::string& x = b->text;
    const std::vector<uint32_t>* ci = cowCpIndex(s); // byte offset per codepoint
    auto cps = utf8cp(x);
    auto* mine = new std::vector<uint32_t>;
    if (ci && cps.size() + 1 == ci->size()) {
        auto starts = uniGraphemeStarts(cps); // cluster starts, codepoint space
        mine->reserve(starts.size() + 1);
        for (size_t g : starts) mine->push_back((*ci)[g]);
        mine->push_back((uint32_t)x.size());
    }
    // else: decode disagreement (malformed UTF-8) — install the EMPTY table as
    // a cached negative so the disagreement is not re-derived per call; callers
    // treat empty as "no table" and keep the legacy path
    const std::vector<uint32_t>* expect = nullptr;
    if (!b->gIndex.compare_exchange_strong(expect, mine, std::memory_order_acq_rel)) {
        delete mine;
        mine = const_cast<std::vector<uint32_t>*>(expect);
    }
    return mine->empty() ? nullptr : mine;
}
// Decode the ONE codepoint whose lead byte sits at `b` — the per-call
// replacement for decoding the whole string.
uint32_t cpAtByte(const std::string& s, size_t b) {
    unsigned char c = (unsigned char)s[b];
    if (c < 0x80) return c;
    int w = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
    static const uint32_t mask[5] = {0, 0x7F, 0x1F, 0x0F, 0x07};
    uint32_t cp = c & mask[w];
    for (int i = 1; i < w && b + i < s.size(); i++)
        cp = (cp << 6) | ((unsigned char)s[b + i] & 0x3F);
    return cp;
}

// True when a BYTE index into `s` is also a GRAPHEME index — which is what Raku
// string positions actually are. Needs two things: every byte ASCII (so one byte
// is one codepoint), and no CR, because CR LF is the one ASCII sequence that
// clusters (GB3, which is why "a\r\nb".chars is 3). When it holds, .chars is a
// byte count and .substr is a byte slice, with nothing to decode.
// Is byte offset `p` of `s` a GRAPHEME boundary? Only a following non-ASCII
// byte (at or past U+0300) can extend a cluster, so ASCII text never pays.
bool atGraphemeBoundary(const std::string& s, size_t p) {
    const size_t len = s.size();
    if (p == 0 || p >= len || (unsigned char)s[p] < 0x80 ||
        ((unsigned char)s[p] >= 0xC2 && (unsigned char)s[p] <= 0xCB)) return true;
    if (((unsigned char)s[p] & 0xC0) == 0x80) return false;   // mid-codepoint
    size_t b = p - 1;
    while (b > 0 && ((unsigned char)s[b] & 0xC0) == 0x80) b--;
    return uniClusterEndUtf8(s, b, len) <= p;
}
// std::string::find, but a match must begin AND end on grapheme boundaries:
// "b" is not found in "ab\x[308]c", whose b̈ is one character (Rakudo's NFG)
size_t graphemeFind(const std::string& hay, const std::string& ndl, size_t from) {
    size_t f = hay.find(ndl, from);
    while (f != std::string::npos) {
        if (atGraphemeBoundary(hay, f) && atGraphemeBoundary(hay, f + ndl.size())) return f;
        f = hay.find(ndl, f + 1);
    }
    return f;
}
bool byteIsGraphemeIndex(const std::string& s) {
    return allAscii(s) && std::memchr(s.data(), '\r', s.size()) == nullptr;
}

// Drop every combining mark, keeping ONE base character per grapheme — the
// folding `:ignoremark` compares through. Character positions survive it, so an
// index into the folded text is an index into the original.
std::string markFold(const std::string& in) {
    std::vector<uint32_t> cps;
    for (uint32_t c : utf8cp(in)) cps.push_back(c);
    std::string out;
    for (size_t i = 0; i < cps.size();) {
        std::vector<uint32_t> g{cps[i++]};
        while (i < cps.size() && rakupp::uniCombiningClass(cps[i]) != 0) g.push_back(cps[i++]);
        bool wrote = false;
        for (uint32_t cp : rakupp::uniNormalize(g, 0))
            if (rakupp::uniCombiningClass(cp) == 0 && !wrote) { out += cpToU8(cp); wrote = true; }
        if (!wrote) out += cpToU8(g[0]); // a lone combining mark stands for itself
    }
    return out;
}
// A CHARACTER offset as a byte offset (Raku positions count characters).
size_t charToByte(const std::string& s, long long chars) {
    if (chars <= 0) return 0;
    size_t b = 0; long long n = 0;
    while (b < s.size() && n < chars) {
        b++;
        while (b < s.size() && (static_cast<unsigned char>(s[b]) & 0xC0) == 0x80) b++;
        n++;
    }
    return b;
}
std::string cpToUtf8(uint32_t cp) {
    std::string r;
    if (cp < 0x80) r += (char)cp;
    else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 0x3f)); }
    else if (cp < 0x10000) { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 0x3f)); r += (char)(0x80 | (cp & 0x3f)); }
    else { r += (char)(0xF0 | (cp >> 18)); r += (char)(0x80 | ((cp >> 12) & 0x3f)); r += (char)(0x80 | ((cp >> 6) & 0x3f)); r += (char)(0x80 | (cp & 0x3f)); }
    return r;
}
// Simple (1:1) case mappings from the full UnicodeData tables.
uint32_t toLowerCp(uint32_t c) { return uniSimpleLower(c); }
bool strHasNoUpper(const std::string& s) {
    for (auto c : utf8cp(s)) if (toLowerCp(c) != c) return false;
    return true;
}
uint32_t toUpperCp(uint32_t c) { return uniSimpleUpper(c); }
// Grapheme-level case change (NFG-aware), driven by the full Unicode case
// tables. `kind`: 0=lc, 1=uc, 3=fc (fold). `tcMode`: 0 = map every grapheme
// with `kind`; 1 = titlecase the first grapheme only (rest unchanged); 2 =
// titlecase first, lowercase the rest (.tclc).
//
// Within a grapheme cluster the base may expand (ﬀ→FF, ǰ→J+◌̌): the FIRST
// resulting codepoint takes the cluster's position, the cluster's own combining
// marks follow it, and any remaining expansion codepoints trail after — so
// "ﬀ+◌̣".uc is F, ◌̣, F (the combiner stays with the first F). The whole result
// is then NFC-normalised back to NFG.
std::string mapCase(const std::string& s, int kind, int tcMode) {
    // ASCII is a byte map, and the general path below is a poor way to do one:
    // it decodes to codepoints, segments graphemes, allocates a case-mapping
    // vector per character and re-normalises the result. None of that can change
    // an ASCII answer — there are no multi-codepoint graphemes to segment, no
    // Final_Sigma (Greek), no multi-codepoint expansions (ß, ﬁ) and nothing for
    // NFC to compose. `.uc` on a short ASCII string was the single hottest thing
    // in a dispatch-heavy profile, almost all of it allocation.
    if (allAscii(s)) {
        std::string r = s;
        if (r.empty()) return r;
        if (tcMode) {
            r[0] = (char)ascii::toupper((unsigned char)r[0]);         // title == upper in ASCII
            if (tcMode == 2)
                for (size_t i = 1; i < r.size(); i++) r[i] = (char)ascii::tolower((unsigned char)r[i]);
            return r;
        }
        if (kind == 0 || kind == 3)                                                   // 0 = lc; 3 = fc, which
             for (char& c : r) c = (char)ascii::tolower((unsigned char)c);            // folds to lower in ASCII
        else for (char& c : r) c = (char)ascii::toupper((unsigned char)c);            // 1 = uc, 2 = tc
        return r;
    }
    auto cps = utf8cp(s);
    if (cps.empty()) return s;
    auto starts = uniGraphemeStarts(cps);
    std::vector<uint32_t> out;
    out.reserve(cps.size());
    for (size_t gi = 0; gi < starts.size(); gi++) {
        size_t b = starts[gi], e = (gi + 1 < starts.size()) ? starts[gi + 1] : cps.size();
        int k;                                   // -1 = leave this grapheme unchanged
        if (tcMode) k = (gi == 0) ? 2 : (tcMode == 2 ? 0 : -1);
        else        k = kind;
        std::vector<uint32_t> tail;
        for (size_t i = b; i < e; i++) {
            if (k < 0) { out.push_back(cps[i]); continue; }
            // Final_Sigma (SpecialCasing): lowercased word-final Σ is ς — preceded
            // by a letter and not followed by one (case-ignorables approximated)
            if (k == 0 && cps[i] == 0x03A3) {
                auto isL = [](uint32_t c) { std::string g = uniGeneralCategory(c); return !g.empty() && g[0] == 'L'; };
                bool prevL = i > 0 && isL(cps[i - 1]);
                bool nextL = i + 1 < cps.size() && isL(cps[i + 1]);
                if (prevL && !nextL) { out.push_back(0x03C2); continue; }
            }
            auto m = uniCaseMap(cps[i], k);
            out.push_back(m[0]);
            for (size_t j = 1; j < m.size(); j++) tail.push_back(m[j]);
        }
        for (uint32_t t : tail) out.push_back(t);
    }
    std::string r; r.reserve(s.size());
    for (uint32_t c : out) r += cpToUtf8(c);
    return nfcNormalize(r); // a case change is NFG-normalised (Ι+◌̈ composes to Ϊ)
}
long long cpCount(const std::string& s) {
    if (allAscii(s)) return (long long)s.size();   // one byte, one codepoint
    return (long long)utf8cp(s).size();
}

// NFC-normalise a UTF-8 string — Raku stores strings in NFG (NFC of graphemes),
// so `"e" ~ "\x[301]"` composes to "é" (1 codepoint). Pure-ASCII is already NFC
// (the hot path), and a string with no composable combiners returns unchanged.
std::string nfcNormalize(std::string s) { // by value: the ASCII fast path moves through, no copy
    bool ascii = true;
    for (unsigned char c : s) if (c >= 0x80) { ascii = false; break; }
    if (ascii) return s;
    auto cps = utf8cp(s);
    auto norm = uniNormalize(cps, 1 /*NFC*/);
    if (norm == cps) return s;
    std::string out; out.reserve(s.size());
    for (uint32_t cp : norm) out += cpToUtf8(cp);
    return out;
}
// Unicode combining marks (Mn/Mc/Me — the common ranges) — they attach to the preceding grapheme.
// Count grapheme clusters via the full UAX #29 algorithm (emoji/flags/Hangul-aware).
// How many bytes of a growing UTF-8 buffer are safe to hand over as TEXT.
//
// A byte stream does not arrive on character boundaries, let alone grapheme
// ones, so two things are held back: an incomplete trailing UTF-8 sequence,
// and the final grapheme — the next chunk could open with a combining mark.
// The exception is a grapheme ending in a control nothing can extend or join
// (LF, TAB, NUL), which is why a line arrives whole; CR is NOT one of those,
// because CR LF is a single cluster, and neither is a space, because a mark
// can attach to it. Oracle-checked against Rakudo's own stream decoder.
size_t utf8TextPrefixLen(const std::string& b) {
    size_t end = b.size();
    for (size_t i = end, back = 0; i > 0 && back < 4; i--, back++) {
        unsigned char c = (unsigned char)b[i - 1];
        if ((c & 0xC0) == 0x80) continue;          // continuation byte
        size_t need = c < 0x80          ? 1
                    : (c & 0xE0) == 0xC0 ? 2
                    : (c & 0xF0) == 0xE0 ? 3
                    : (c & 0xF8) == 0xF0 ? 4 : 1;
        if (end - (i - 1) < need) end = i - 1;
        break;
    }
    if (!end) return 0;
    size_t lastStart = 0, p = 0;
    while (p < end) {
        size_t e = uniClusterEndUtf8(b, p, end);
        if (e <= p) break;
        lastStart = p; p = e;
    }
    std::vector<uint32_t> tail = utf8cp(b.substr(lastStart, end - lastStart));
    uint32_t last = tail.empty() ? 0u : tail.back();
    bool terminal = (last < 0x20 || last == 0x7F) && last != 0x0D;
    return terminal ? end : lastStart;
}

long long graphemeCount(const std::string& s) {
    // `.chars` on a long ASCII string was the worst of the quadratics: called
    // once per character it decoded the whole text AND ran the full UAX #29 walk
    // over it, 11.5 s for a 30k scan. A byte count answers it outright.
    if (byteIsGraphemeIndex(s)) return (long long)s.size();
    return (long long)uniGraphemeCount(utf8cp(s));
}

// Rakudo dies opening a missing file for reading ("Failed to open file
// /abs/path: No such file or directory") — match it, absolute path included.
// The TYPE is X::IO::Open, which Rakudo does not have: Rakudo answers X::AdHoc
// and so cannot tell a failed slurp from a failed spurt from a failed open —
// its message says "Failed to open file" for all three. Keeping the name costs
// nothing now that X::IO::Open IS-A X::AdHoc (rakuppOnlyExceptionParent), so
// the `when X::AdHoc` that code in the wild contains still fires.
[[noreturn]] void throwFailedOpen(const std::string& path) {
    std::string abs = path;
    if (abs.empty() || (abs[0] != '/' && !(abs.size() > 1 && abs[1] == ':'))) {
        char buf[4096];
        if (getcwd(buf, sizeof buf)) abs = std::string(buf) + "/" + path;
    }
    // X::IO::Open, which IS-A X::AdHoc (rakuppOnlyExceptionParent), so the
    // `CATCH { when X::AdHoc {…} }` code in the wild is written with still
    // fires and `.payload` still reads the message — the name only adds which
    // call failed, which Rakudo's own X::AdHoc cannot say. The JS lane names it
    // the same (js-rt/70-host.js); the two have to agree or a program catches
    // different things on the two backends.
    throw RakuError{Value::typeObj("X::IO::Open"),
                    "Failed to open file " + abs + ": No such file or directory"};
}

std::string canonEncodingName(const std::string& name, bool* known) {
    if (known) *known = true;
    std::string key;
    for (char c : name) key += (char)ascii::tolower((unsigned char)c);
    if (key == "bin") return "";
    static const std::map<std::string, std::string> alias = {
        {"utf-8", "utf8"},        {"utf8", "utf8"},
        {"utf-8-c8", "utf8-c8"},  {"utf8-c8", "utf8-c8"},
        {"utf-16", "utf16"},      {"utf16", "utf16"},
        {"utf-16le", "utf16le"},  {"utf16le", "utf16le"},
        {"utf-16be", "utf16be"},  {"utf16be", "utf16be"},
        {"utf-32", "utf32"},      {"utf32", "utf32"},
        {"ascii", "ascii"},
        {"latin1", "iso-8859-1"}, {"latin-1", "iso-8859-1"},
        {"iso_8859-1", "iso-8859-1"}, {"iso-8859-1", "iso-8859-1"},
        {"windows1251", "windows-1251"}, {"windows-1251", "windows-1251"},
        {"windows1252", "windows-1252"}, {"windows-1252", "windows-1252"},
        {"gb2312", "gb2312"}, {"gb18030", "gb18030"},
        {"shiftjis", "windows-932"}, {"windows932", "windows-932"}, {"windows-932", "windows-932"},
    };
    auto it = alias.find(key);
    if (it != alias.end()) return it->second;
    if (known) *known = false;
    return key;
}

std::string joinValues(const ValueList& items, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < items.size(); i++) {
        if (i) out += sep;
        out += items[i].toStr();
    }
    // NFC-composed, as concatenation is: joining ("a", COMBINING RING) yields
    // the composed grapheme in Rakudo's NFG strings
    return nfcNormalize(std::move(out));
}

// A lazy @-array over the integers from `start` upward (an infinite `…..Inf` range).
Value makeInfArray(long long start) {
    Value a = Value::array(); a.isList = true;
    auto st = std::make_shared<LazySeqState>(); st->infinite = true;
    auto next = std::make_shared<long long>(start);
    st->appendNext = [next](ValueList& cache) -> bool { cache.push_back(Value::integer((*next)++)); return true; };
    a.extM() = st;
    return a;
}

ValueList toList(const Value& v) {
    forceLazy(v);   // an unpulled gather lists its ELEMENTS
    if (v.t == VT::Array && v.arr()) return *v.arr();
    if (v.t == VT::Range) return v.flatten();
    // a Blob/Buf lists as its ELEMENTS (`$blob.rotor(3, :partial)` in Base64;
    // 32-bit words for blob32) — mirrors the `for`-iteration rule in the
    // interpreter (itemized stays one item)
    if (v.t == VT::Str && !v.itemized && (v.hashKind == "Blob" || v.hashKind == "Buf"))
        return v.blobList();
    // A Pod block is an OBJECT that happens to be stored as a hash. It is not
    // Iterable and not Associative, so it lists as ITSELF — flattening it into
    // its own `podclass`/`contents` pairs is what `Array.new($block)` did, and
    // Pod::Utils' pod-title built a two-pair soup where Rakudo builds a
    // one-element array holding the block.
    if (v.t == VT::Hash && v.hashKind == "Pod") return {v};
    if (v.t == VT::Hash && v.hash()) {
        ValueList out;
        // The object-hash test is hoisted: this is the hot path for every hash
        // iteration, and a plain hash must cost exactly what it did before —
        // one shared_ptr copy, not a Value built and thrown away per entry.
        const bool objHash = !objHashKeyType(v).empty();
        for (auto& kv : *v.hash()) {
            Value p = hashEntryPair(v, kv.first, kv.second);
            // A key OBJECT rides along only when it is not already the index —
            // a plain Str key IS the index, and attaching it as a key object
            // made the pair render in the arrow form (`"a" => 42`) where
            // Rakudo writes `:a(42)`.
            if (kv.second.elemKey()) {
                const Value& pk2 = *kv.second.elemKey();
                if (!(pk2.t == VT::Str && pk2.hashKind.empty() && pk2.enumName.empty() && pk2.s == kv.first))
                    p.pairKeyM() = kv.second.elemKey();
            }
            else if (objHash) {
                Value rk = hashEntryKey(v, kv.first, kv.second);
                if (rk.t != VT::Str) p.pairKeyM() = std::make_shared<Value>(std::move(rk));
            }
            out.push_back(std::move(p));
        }
        return out;
    }
    return {v};
}

// simple sprintf supporting %s %d %i %x %o %b %c %f %g %e %%
// Format an integer in an arbitrary radix honouring the printf flag/width/precision
// grammar with Raku's conventions: sign-magnitude for negatives, `#` prefixes 0b/0o/0x,
// precision = minimum digits (precision 0 of value 0 → empty), `0` flag ignored with a
// precision or with left-justify.
static std::string fmtRadix(long long val, int base, bool upper, const std::string& flags,
                            int width, int prec, bool signFlags, int langRev = 1) {
    bool neg = val < 0;
    unsigned long long u = neg ? (unsigned long long)(0 - (unsigned long long)val)
                               : (unsigned long long)val;
    const char* dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string digits;
    if (u == 0) digits = "0";
    else while (u) { digits = std::string(1, dig[u % base]) + digits; u /= base; }
    if (prec >= 0) {
        if (val == 0 && prec == 0) digits = "";
        else if ((int)digits.size() < prec) digits = std::string(prec - digits.size(), '0') + digits;
    }
    // `#` prefix: 0b/0B for binary, 0x/0X for hex; octal forces a single leading 0
    // (skipped when the digits already begin with 0, matching C/Raku).
    std::string prefix;
    if (flags.find('#') != std::string::npos && val != 0) {
        if (base == 2)       prefix = upper ? "0B" : "0b";
        else if (base == 16) prefix = upper ? "0X" : "0x";
        else if (base == 8 && !digits.empty() && digits[0] != '0') prefix = "0";
    }
    // A radix value that formats to no digits (precision 0 of 0) drops sign/prefix entirely:
    // sprintf("% .0b",0) → '' (only space width padding applies). Decimal is the exception —
    // sprintf("% .0d",0) → ' ' — so it falls through to keep the sign flag.
    if (digits.empty() && base != 10) {
        if ((int)width > 0) return std::string(width, ' ');
        return "";
    }
    // The +/space sign flags apply only to signed conversions (d/i/b); o/x/X/u ignore them.
    std::string sign;
    if (neg) sign = "-";
    else if (signFlags && flags.find('+') != std::string::npos) sign = "+";
    else if (signFlags && flags.find(' ') != std::string::npos) sign = " ";
    // 6.e (langRev>=2) puts the sign first for every base: sprintf("%#x",-256) →
    // "-0x100". 6.c/6.d keep the historical "bogus" prefix-before-sign for octal/hex
    // ("0x-100", "0-100" — roast's 6.d sprintf files assert exactly this); binary
    // always keeps the sign first.
    bool prefixFirst = (langRev < 2) && (base == 8 || base == 16);
    std::string core = prefixFirst ? prefix + sign + digits : sign + prefix + digits;
    if ((int)core.size() < width) {
        int pad = width - (int)core.size();
        if (flags.find('-') != std::string::npos) core += std::string(pad, ' ');
        else if (flags.find('0') != std::string::npos && prec < 0)
            // zero-pad fills after the prefix — but only where the sign leads.
            // Under the 6.c/6.d prefix-first reading the zeros go in FRONT of the
            // whole thing, so `%#08x` of 1 is "000000x1" and of -256 "000x-100"
            // (octal reads the same either way; binary keeps sign+prefix first).
            core = prefixFirst ? std::string(pad, '0') + core
                               : sign + prefix + std::string(pad, '0') + digits;
        else core = std::string(pad, ' ') + core;
    }
    return core;
}


// Digits of a BigInt in the given radix (2/8/16), sign included — for %b/%o/%x
// on arbitrary-precision Ints (toInt() would truncate at 64 bits).
static std::string bigRadixDigits(const BigInt& v, int radix, bool upper) {
    static const char* lo = "0123456789abcdef";
    static const char* up = "0123456789ABCDEF";
    const char* digs = upper ? up : lo;
    BigInt n = v.abs(), r10(radix);
    if (n.isZero()) return "0";
    std::string out;
    while (!n.isZero()) {
        BigInt q, r;
        BigInt::divmod(n, r10, q, r);
        out += digs[(int)r.toLL()];
        n = q;
    }
    if (v.sign < 0) out += '-';
    std::string rev(out.rbegin(), out.rend());
    return rev;
}

// Format an exact decimal-digit string (BigInt) for %d: honors width and the
// '-', '0', '+', ' ' flags (precision on integers is rare; digits stay exact).
static std::string fmtBigDec(std::string digits, const std::string& flags, long long width, long long prec = -1) {
    bool neg = !digits.empty() && digits[0] == '-';
    std::string sign = neg ? "-" : (flags.find('+') != std::string::npos ? "+" :
                                    flags.find(' ') != std::string::npos ? " " : "");
    if (neg) digits = digits.substr(1);
    // integer precision zero-pads the digit run itself (`%32.32x` of a 128-bit value)
    if (prec > (long long)digits.size()) digits = std::string(prec - digits.size(), '0') + digits;
    std::string body = sign + digits;
    if ((long long)body.size() >= width) return body;
    if (flags.find('-') != std::string::npos) return body + std::string(width - body.size(), ' ');
    if (flags.find('0') != std::string::npos)
        return sign + std::string(width - body.size(), '0') + digits;
    return std::string(width - body.size(), ' ') + body;
}

// Raku's %f is DECIMAL-string based where C's is binary: it renders the value at
// its shortest round-trip decimal and then rounds or zero-pads THAT string to the
// asked precision. So `%.50f` of 1.115 is "1.115" followed by zeros rather than
// the double's 1.11499999999999999111… expansion, and `%.2f` of it rounds the
// string half-up-away-from-zero to 1.12 where C's binary value gives 1.11.
// Takes |v| (finite); the caller owns the sign, the flags and the width.
static std::string fixedFromShortest(double av, int prec) {
    char buf[64];
    int sig = 0; // the fewest significant digits that read back as this double
    for (; sig < 17; sig++) {
        cnum::snprintf(buf, sizeof buf, "%.*e", sig, av);
        if (cnum::strtod(buf, nullptr) == av) break;
    }
    cnum::snprintf(buf, sizeof buf, "%.*e", sig, av);
    std::string s(buf);
    size_t ep = s.find('e');
    int exp10 = std::atoi(s.c_str() + ep + 1);
    std::string digits;
    for (size_t k = 0; k < ep; k++) if (s[k] != '.') digits += s[k];
    int pointAt = exp10 + 1; // digits belonging before the decimal point
    std::string intPart, fracPart;
    if (pointAt <= 0) { intPart = "0"; fracPart = std::string(-pointAt, '0') + digits; }
    else if ((int)digits.size() <= pointAt) {
        intPart = digits + std::string(pointAt - digits.size(), '0');
    } else { intPart = digits.substr(0, pointAt); fracPart = digits.substr(pointAt); }
    if ((int)fracPart.size() > prec) {
        bool up = fracPart[prec] >= '5';
        fracPart.resize(prec);
        if (up) { // ripple the carry through the whole number, growing it if it runs off
            std::string all = intPart + fracPart;
            int k = (int)all.size() - 1;
            for (; k >= 0; k--) { if (all[k] == '9') all[k] = '0'; else { all[k]++; break; } }
            if (k < 0) all = "1" + all;
            size_t ilen = all.size() - fracPart.size();
            intPart = all.substr(0, ilen); fracPart = all.substr(ilen);
        }
    } else fracPart += std::string(prec - (int)fracPart.size(), '0');
    return prec > 0 ? intPart + "." + fracPart : intPart;
}

// `sprintf("%d", 0^1)` is not a number-in-disguise — a Junction has no single
// value to render, so Raku names the directive and the type rather than
// collapsing it. (Rakudo raises this for every non-numeric value; we raise it
// where the value genuinely has no scalar reading.)
[[noreturn]] static void sprintfBadType(char conv, const Value& v, const std::string& fmt) {
    std::string type = isJunction(v) ? "Junction" : v.typeName();
    std::string msg = std::string("Directive %") + conv + " not applicable for value of type " +
                      type + " (" + (g_rakuRepr ? g_rakuRepr(v) : type) + ") in format '" + fmt + "'";
    Value ex = g_makeTypedEx
             ? g_makeTypedEx("X::Str::Sprintf::Directives::BadType",
                             {{"type", Value::str(type)}, {"directive", Value::str(std::string(1, conv))},
                              {"value", v}, {"format", Value::str(fmt)}}, msg)
             : Value::typeObj("X::Str::Sprintf::Directives::BadType");
    throw RakuError{ex, msg};
}

// A directive Raku has no rendering for at all — `%q`, `%y`, `%n`, `%p`, or a
// trailing bare `%`. Rakudo names the letter and quotes the whole format.
[[noreturn]] static void sprintfUnsupported(const std::string& directive, const std::string& fmt) {
    std::string msg = "Directive " + directive + (directive.empty() ? "" : " ") +
                      "is not valid in sprintf format '" + fmt + "'";
    Value ex = g_makeTypedEx
             ? g_makeTypedEx("X::Str::Sprintf::Directives::Unsupported",
                             {{"directive", Value::str(directive)}, {"sequence", Value::str(fmt)}}, msg)
             : Value::typeObj("X::Str::Sprintf::Directives::Unsupported");
    throw RakuError{ex, msg};
}

std::string doSprintf(const std::string& fmt, const ValueList& args, int langRev) {
    std::string out;
    size_t ai = 0;                 // the IMPLICIT cursor
    long long valIdx = -1;         // this directive's explicit `%N$`, 1-based; -1 = none
    auto argAt = [&](long long n1) -> Value {   // 1-based, out of range → Any
        size_t k = (size_t)(n1 - 1);
        return n1 >= 1 && k < args.size() ? args[k] : Value::any();
    };
    // An EXPLICIT index selects its argument and leaves the implicit cursor
    // exactly where it was: `sprintf('%2$d %d %d', 1, 2, 3)` is "2 1 2", not
    // "2 3 0" — the two implicit directives still read 1 then 2. (C and Perl 5
    // both work this way; so does Rakudo outside 6.e.)
    size_t used = 0;       // arguments the directives ASK for, however many exist
    bool explicitIdx = false;  // any `%N$`: Rakudo drops the count check entirely then
    auto nextArg = [&]() -> Value {
        used++;
        if (valIdx >= 1) return argAt(valIdx);
        return ai < args.size() ? args[ai++] : Value::any();
    };
    // A `*` width/precision takes its own argument: `%N$` if it carries one
    // (`%2$*1$d`), otherwise the next implicit one — never the directive's.
    auto starArg = [&](long long n1) -> Value {
        used++;
        if (n1 >= 1) return argAt(n1);
        return ai < args.size() ? args[ai++] : Value::any();
    };
    // Parse an optional `digits $` at k, returning the 1-based index (or -1) and
    // consuming it only when the `$` is really there — a bare width like `%2d`
    // must be left alone.
    auto takeIndex = [&](size_t& k) -> long long {
        size_t d = k;
        while (d < fmt.size() && ascii::isdigit((unsigned char)fmt[d])) d++;
        if (d > k && d < fmt.size() && fmt[d] == '$') {
            long long n = std::atoll(fmt.substr(k, d - k).c_str());
            k = d + 1;
            return n;
        }
        return -1;
    };
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] != '%') { out += fmt[i]; continue; }
        size_t j = i + 1;
        valIdx = takeIndex(j); // explicit positional argument: %2$s
        if (valIdx >= 1) explicitIdx = true;
        std::string flags;
        while (j < fmt.size() && std::strchr("-+ 0#", fmt[j])) flags += fmt[j++];
        // width (digits or `*` = from argument; negative `*` implies left-justify)
        const long long SPRINTF_MAX = 10'000'000; // guard against int overflow (UB) and multi-GB pads
        int width = 0; bool hasWidth = false;
        if (j < fmt.size() && fmt[j] == '*') { j++; long long w = starArg(takeIndex(j)).toInt();
            if (w < 0) { flags += '-'; w = -w; } if (w > SPRINTF_MAX) w = SPRINTF_MAX; width = (int)w; hasWidth = true; }
        else { long long w = 0;
            while (j < fmt.size() && ascii::isdigit((unsigned char)fmt[j])) { w = w * 10 + (fmt[j]-'0'); if (w > SPRINTF_MAX) w = SPRINTF_MAX; hasWidth = true; j++; }
            width = (int)w; }
        // precision (.digits or .* ; a negative `.*` means "no precision")
        int prec = -1;
        if (j < fmt.size() && fmt[j] == '.') { j++; prec = 0;
            if (j < fmt.size() && fmt[j] == '*') { j++; long long p = starArg(takeIndex(j)).toInt(); prec = p < 0 ? -1 : (int)std::min(p, SPRINTF_MAX); }
            else { long long p = 0; while (j < fmt.size() && ascii::isdigit((unsigned char)fmt[j])) { p = p * 10 + (fmt[j]-'0'); if (p > SPRINTF_MAX) p = SPRINTF_MAX; j++; } prec = (int)p; }
        }
        while (j < fmt.size() && std::strchr("lhqLVjzt", fmt[j])) j++; // length modifiers, ignored
        if (j >= fmt.size()) sprintfUnsupported(fmt.substr(i + 1), fmt); // a `%` with nothing to end it
        char conv = fmt[j];
        switch (conv) {
            case '%': out += '%'; break;
            case 'd': case 'i': {
                // an arbitrary-precision Int (or a Rat/Num too big for long long)
                // formats from its exact decimal digits, not a saturated toInt()
                Value av = nextArg();
                if (isJunction(av)) sprintfBadType(conv, av, fmt);
                if (av.t == VT::Int && av.big()) { out += fmtBigDec(av.big()->toString(), flags, width, prec); break; }
                if (av.t == VT::Rat && av.ratN() && av.ratD() && !av.ratD()->isZero()) {
                    BigInt q, r; BigInt::divmod(*av.ratN(), *av.ratD(), q, r);
                    if (q.toString().size() > 18) { out += fmtBigDec(q.toString(), flags, width, prec); break; }
                }
                out += fmtRadix(av.toInt(), 10, false, flags, width, prec, true); break;
            }
            case 'u': case 'b': case 'B': case 'o': case 'x': case 'X': {
                int radix = (conv == 'u') ? 10 : (conv == 'o') ? 8 : (conv == 'x' || conv == 'X') ? 16 : 2;
                bool upper = (conv == 'B' || conv == 'X');
                bool prefixable = (conv == 'b' || conv == 'B');
                // 6.e: the space and + flags no longer apply to binary (`% b`, `%+b`) —
                // o/x already ignore them; the # prefix flag is kept.
                std::string flags2 = flags;
                if (langRev >= 2 && (conv == 'b' || conv == 'B')) {
                    size_t p2; while ((p2 = flags2.find(' ')) != std::string::npos) flags2.erase(p2, 1);
                    while ((p2 = flags2.find('+')) != std::string::npos) flags2.erase(p2, 1);
                }
                Value av = nextArg();
                if (isJunction(av)) sprintfBadType(conv, av, fmt);
                if (av.t == VT::Int && av.big()) { // arbitrary-precision: exact digits
                    out += fmtBigDec(bigRadixDigits(*av.big(), radix, upper), flags2, width, prec);
                    break;
                }
                if (av.t == VT::Rat && av.ratN() && av.ratD() && !av.ratD()->isZero()) {
                    BigInt q, r;
                    BigInt::divmod(*av.ratN(), *av.ratD(), q, r); // truncate toward zero
                    if (q.toString().size() > 18) {
                        out += fmtBigDec(bigRadixDigits(q, radix, upper), flags2, width, prec);
                        break;
                    }
                }
                out += fmtRadix(av.toInt(), radix, upper, flags2, width, prec, prefixable, langRev);
                break;
            }
            case 'c': { // codepoint → UTF-8; width counts characters, not bytes
                uint32_t cp = (uint32_t)nextArg().toInt();
                std::string s;
                if (cp < 0x80) s += (char)cp;
                else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
                else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
                int pad = width - 1; // one character
                if (pad > 0) { char fill = (flags.find('0') != std::string::npos && flags.find('-') == std::string::npos) ? '0' : ' ';
                    s = (flags.find('-') != std::string::npos) ? s + std::string(pad,' ') : std::string(pad,fill) + s; }
                out += s; break; }
            case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
                // `#` on a float is version-split: 6.e honors it (forces the decimal
                // point — sprintf("%#.0f",0) → "0."), 6.c/6.d ignore it (→ "0").
                std::string ff;
                for (char c : flags) if (c != '#' || langRev >= 2) ff += c;
                Value fa = nextArg();
                if (isJunction(fa)) sprintfBadType(conv, fa, fmt);
                double fv = fa.toNum();
                // `%f` with both `-` and `0` is version-split. 6.e: `0` wins — the
                // value is zero-padded, not left-justified (opposite of C), precision
                // unchanged (sprintf("%-08.2f",0) → "00000.00"). 6.c/6.d: the historical
                // "bogus but provided" form — a non-negative value with no sign flag is
                // formatted with precision+1, zero-padded ("%-08.2f",0 → "0000.000").
                bool leftJ = ff.find('-') != std::string::npos;
                bool zeroF = ff.find('0') != std::string::npos;
                int fprec = prec >= 0 ? prec : 6;
                if ((conv == 'f' || conv == 'F') && leftJ && zeroF) {
                    if (langRev >= 2) {
                        std::string t; for (char c : ff) if (c != '-') t += c; ff = t;
                        leftJ = false;
                    } else if (prec >= 0 && hasWidth) {
                        bool signFlag = ff.find('+') != std::string::npos || ff.find(' ') != std::string::npos;
                        if (!signFlag && fv >= 0) fprec = prec + 1;
                        leftJ = false;
                    }
                }
                // %f goes through the decimal-string renderer above; every other
                // float conversion keeps C's.
                if ((conv == 'f' || conv == 'F') && std::isfinite(fv)) {
                    std::string sign = std::signbit(fv) ? "-"
                                     : ff.find('+') != std::string::npos ? "+"
                                     : ff.find(' ') != std::string::npos ? " " : "";
                    std::string body = fixedFromShortest(std::fabs(fv), fprec);
                    if (fprec == 0 && langRev >= 2 && flags.find('#') != std::string::npos) body += ".";
                    std::string core = sign + body;
                    if ((int)core.size() < width) {
                        int pad = width - (int)core.size();
                        if (leftJ) core += std::string(pad, ' ');
                        else if (zeroF) core = sign + std::string(pad, '0') + body;
                        else core = std::string(pad, ' ') + core;
                    }
                    out += core; break;
                }
                std::string spec = "%" + ff;
                if (hasWidth) spec += std::to_string(width);
                if (prec >= 0) spec += "." + std::to_string(prec);
                spec += conv;
                std::vector<char> buf(std::max(64, width + prec + 64));
                cnum::snprintf(buf.data(), buf.size(), spec.c_str(), fv);
                std::string fs = buf.data();
                if (std::isnan(fv) || std::isinf(fv)) {
                    // Raku spells them NaN / Inf / -Inf. From 6.e %G — and only %G —
                    // uppercases them: `sprintf('%G', -Inf)` is "-INF" there and
                    // "-Inf" at 6.d. (C uppercases for %E and %F too; Raku does not,
                    // and the two Roast copies of S32-str/sprintf.t pin one version
                    // each: 6.d/…:234 wants "Inf", …/sprintf.t:241 wants "INF".)
                    bool up = (conv == 'G' && langRev >= 2);
                    const char* rep = std::isnan(fv) ? (up ? "NAN" : "NaN")
                                                     : (up ? "INF" : "Inf");
                    for (const char* bad : {"nan", "NAN", "inf", "INF"}) {
                        size_t at = fs.find(bad);
                        if (at != std::string::npos) { fs.replace(at, 3, rep); break; }
                    }
                }
                out += fs; break;
            }
            case 's': {
                Value sa = nextArg();
                // `%s` is a .Str, so an object's own one answers it — this is
                // outside the interpreter, where only the raw rendering was
                // reachable and `sprintf("%s", $obj)` printed Class<address>.
                std::string sv;
                if (sa.t == VT::Any || sa.t == VT::Nil) sv = "";
                else if (!(g_userStr && g_userStr(sa, sv))) sv = sa.toStr();
                // Width/precision count characters (codepoints), not bytes, so multibyte
                // text pads correctly: sprintf("%8s","🦋🦋🦋") → "     🦋🦋🦋".
                auto cpCount = [](const std::string& s) { int n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++; return n; };
                if (prec >= 0 && cpCount(sv) > prec) { // keep the first `prec` codepoints
                    int n = 0; size_t bi = 0;
                    while (bi < sv.size() && n < prec) { bi++; while (bi < sv.size() && (((unsigned char)sv[bi]) & 0xC0) == 0x80) bi++; n++; }
                    sv = sv.substr(0, bi);
                }
                int chars = cpCount(sv);
                if (chars < width) { int pad = width - chars;
                    // `-` always wins over `0` for %s (left-justify with spaces). The
                    // `0` fill itself is version-split: 6.e zero-fills even with a
                    // precision (%08.2s of "Foo" → "000000Fo"); 6.c/6.d only zero-fill
                    // without a precision (with one it pads with spaces → "      Fo").
                    bool zeroFill = flags.find('0') != std::string::npos && flags.find('-') == std::string::npos
                                    && (langRev >= 2 || prec < 0);
                    char fill = zeroFill ? '0' : ' ';
                    sv = (flags.find('-') != std::string::npos) ? sv + std::string(pad,' ') : std::string(pad,fill) + sv; }
                out += sv; break;
            }
            // Every directive Raku does not define is an error, not literal text —
            // `%n`/`%p` are Perl-compat holes, the rest never existed.
            default: sprintfUnsupported(std::string(1, conv), fmt);
        }
        i = j;
    }
    // Raku insists the directives and the arguments agree in NUMBER, so a stray
    // interpolated `$` (`"%s => $v"` where $v itself holds a `%s`) is reported
    // rather than silently formatting an (Any). An explicit `%N$` index makes the
    // correspondence non-positional, and Rakudo drops the check there.
    if (!explicitIdx && used != args.size()) {
        auto plural = [](size_t n, const char* noun) {
            return std::to_string(n) + " " + noun + (n == 1 ? "" : "s");
        };
        std::string msg = "Your printf-style directives specify " + plural(used, "argument") +
                          ", but " + (args.empty() ? std::string("no argument") : plural(args.size(), "argument")) +
                          (args.size() <= 1 ? " was" : " were") + " supplied to format '" + fmt + "'." +
                          (used > args.size() ? " Are you using an interpolated '$'?" : "");
        Value ex = g_makeTypedEx
                 ? g_makeTypedEx("X::Str::Sprintf::Directives::Count",
                                 {{"args-used", Value::integer((long long)used)},
                                  {"args-have", Value::integer((long long)args.size())},
                                  {"format", Value::str(fmt)}}, msg)
                 : Value::typeObj("X::Str::Sprintf::Directives::Count");
        throw RakuError{ex, msg};
    }
    return out;
}

// Structural equality that ignores TYPE at the leaves ("11" == 11). This is
// NOT `eqv` and no longer backs is-deeply, which uses eqv; the one caller
// left is 6.e `snip`, where the value predicate is a smartmatch.
bool deepEq(const Value& a, const Value& b) {
    forceLazy(a); forceLazy(b);   // a lazy gather compares by its ELEMENTS
    // A Proxy is a container: compare what it HOLDS, at any depth. URI::Query
    // hands back lists of Proxy containers to keep them immutable, so
    // `is-deeply $q<foo>, $('1','3')` was comparing containers with strings.
    if (a.t == VT::Hash && a.hashKind == "Proxy" && a.hash() && g_deproxy)
        return deepEq(g_deproxy(a), b);
    if (b.t == VT::Hash && b.hashKind == "Proxy" && b.hash() && g_deproxy)
        return deepEq(a, g_deproxy(b));
    // the undefined value (VT::Any) and the `Any` type object are the same thing
    auto anyish = [](const Value& v) { return v.t == VT::Any || (v.t == VT::Type && v.s == "Any"); };
    if (anyish(a) && anyish(b)) return true;
    // a Junction on either side autothreads (is-deeply $x, 'a'|'b';
    // is-deeply any(1,2,3), none(4,5,6) collapses to True)
    auto junct = [](const Value& v) {
        return v.t == VT::Array && v.arr() &&
               (v.enumName == "any" || v.enumName == "all" || v.enumName == "one" || v.enumName == "none");
    };
    if (junct(b)) {
        JunctionCollapse jc(b.enumName);           // short-circuits; see Value.h
        for (auto& e : *b.arr()) { jc.feed(deepEq(a, e)); if (jc.done()) break; }
        return jc.verdict();
    }
    if (junct(a)) {
        JunctionCollapse jc(a.enumName);
        for (auto& e : *a.arr()) { jc.feed(deepEq(e, b)); if (jc.done()) break; }
        return jc.verdict();
    }
    if (a.t == VT::Array && b.t == VT::Array) {
        if (a.arr()->size() != b.arr()->size()) return false;
        // A Capture's POSITIONALS are ordered but its NAMEDS are a map:
        // \(1, :a, :b) is-deeply \(1, :b, :a). Getopt::Long's suite compares
        // parse-order nameds against source-order literals.
        if (a.hashKind == "Capture" && b.hashKind == "Capture") {
            std::vector<const Value*> ap, bp;
            std::map<std::string, const Value*> an, bn;
            for (auto& e : *a.arr()) { if (e.t == VT::Pair) an[e.s] = &e; else ap.push_back(&e); }
            for (auto& e : *b.arr()) { if (e.t == VT::Pair) bn[e.s] = &e; else bp.push_back(&e); }
            if (ap.size() != bp.size() || an.size() != bn.size()) return false;
            for (size_t i = 0; i < ap.size(); i++) if (!deepEq(*ap[i], *bp[i])) return false;
            for (auto& kv : an) {
                auto it = bn.find(kv.first);
                if (it == bn.end() || !deepEq(*kv.second, *it->second)) return false;
            }
            return true;
        }
        for (size_t i = 0; i < a.arr()->size(); i++)
            if (!deepEq((*a.arr())[i], (*b.arr())[i])) return false;
        return true;
    }
    if (a.t == VT::Hash && b.t == VT::Hash) {
        if (a.hash()->size() != b.hash()->size()) return false;
        for (auto& kv : *a.hash()) {
            auto it = b.hash()->find(kv.first);
            if (it == b.hash()->end() || !deepEq(kv.second, it->second)) return false;
        }
        return true;
    }
    if (a.t == VT::Pair && b.t == VT::Pair) {
        // typed keys (an object key stringifies with its identity now, so the
        // rendering can't stand in for the key) compare structurally
        bool keyOk = a.pairKey() && b.pairKey() ? deepEq(*a.pairKey(), *b.pairKey())
                                            : a.s == b.s;
        return keyOk && deepEq(a.pairVal() ? *a.pairVal() : Value::any(),
                               b.pairVal() ? *b.pairVal() : Value::any());
    }
    if (a.t == VT::Rat && b.t == VT::Rat) // structural (eqv): <0/0> eqv <0/0> is True; toNum would NaN-compare
        return a.fatRat() == b.fatRat() &&
               a.ratN() && b.ratN() && a.ratD() && b.ratD() &&
               BigInt::cmp(*a.ratN(), *b.ratN()) == 0 && BigInt::cmp(*a.ratD(), *b.ratD()) == 0;
    if (a.t == VT::Num && b.t == VT::Num && std::isnan(a.n) && std::isnan(b.n))
        return true; // structural: NaN eqv NaN (numeric == would say false)
    // two objects: structural, not stringified — object .Str carries identity
    // now, so the toStr fallback would call every clone unequal to its source
    if (a.t == VT::Object && b.t == VT::Object)
        return objectStructEqv(a, b, deepEq);
    return valueEq(a, b);
}

// Build a Set/Bag/Mix (hash-backed, hashKind tag) from a flat list of values/pairs.
// Buf/Blob binary IO: bit-addressed (read|write)-(u)bits and byte-addressed
// numeric forms. Bits are MSB-first within the byte stream; values may exceed
// 64 bits (BigInt). Writes mutate `buf` in place (the caller routes an lvalue).
// `.splice` on a Buf replaces a window IN PLACE and answers the removed bytes.
// It needs the invocant's own slot: methodCall takes its invocant BY VALUE, and a
// Buf's bytes are a plain std::string rather than a shared_ptr the way an Array's
// elements are — so unlike Array.splice it cannot mutate through a copy. Same
// reason bufBitOp lives out here.
Value Interpreter::bufSplice(Value& buf, ValueList& args) {
    long long n = (long long)buf.s.size();
    long long from = args.size() > 0 && args[0].t != VT::Pair ? args[0].toInt() : 0;
    if (from < 0) from += n;
    if (from < 0) from = 0; if (from > n) from = n;
    long long len = args.size() > 1 && args[1].t != VT::Pair ? args[1].toInt() : n - from;
    if (len < 0) len = 0; if (from + len > n) len = n - from;
    std::string repl; // the replacement flattens: .splice(0, 3, <3 2 1>)
    for (size_t k = 2; k < args.size(); k++) {
        if (args[k].t == VT::Pair) continue;
        if (args[k].t == VT::Array || args[k].t == VT::Range)
            for (auto& e : args[k].flatten()) repl += (char)(unsigned char)(e.toInt() & 0xFF);
        // A Blob/Buf replacement is Positional over its BYTES, and splicing one
        // buffer into another is the ordinary way to build a binary message.
        // It is a VT::Str internally, so it fell to the scalar arm below and
        // went in as ONE byte holding its element COUNT — BSON::Simple wrote
        // `5` where "hello" belonged.
        else if (args[k].t == VT::Str &&
                 (args[k].hashKind == "Buf" || args[k].hashKind == "Blob"))
            for (auto& e : args[k].blobList()) repl += (char)(unsigned char)(e.toInt() & 0xFF);
        else repl += (char)(unsigned char)(args[k].toInt() & 0xFF);
    }
    Value removed = Value::str(buf.s.substr((size_t)from, (size_t)len));
    removed.hashKind = buf.hashKind;
    removed.ofTypeM() = buf.ofType();   // the removed bytes are the same Buf[uint8]
    if (removed.hashKind == "Buf") identify(removed); // a fresh Buf, not the spliced one
    buf.s.replace((size_t)from, (size_t)len, repl);
    return removed;
}

Value Interpreter::bufBitOp(Value& buf, const std::string& m, ValueList& args) {
    std::string& bytes = buf.s.mut();
    auto endianOf = [&](const Value& v) -> int { // 0 native, 1 little, 2 big
        std::string e = !v.enumName.empty() ? v.enumName : v.toStr();
        if (e == "LittleEndian") return 1;
        if (e == "BigEndian") return 2;
        return 0;
    };
    static const bool hostLittle = [] { uint16_t x = 1; return *(uint8_t*)&x == 1; }();
    auto isLittle = [&](int e) { return e == 1 || (e == 0 && hostLittle); };
    if (m == "read-ubits" || m == "read-bits") {
        long long from = args.size() > 0 ? args[0].toInt() : 0;
        long long bits = args.size() > 1 ? args[1].toInt() : 0;
        long long total = (long long)bytes.size() * 8;
        if (from < 0 || bits < 1 || from + bits > total)
            throw RakuError{Value::typeObj("X::OutOfRange"),
                "bit range " + std::to_string(from) + "+" + std::to_string(bits) +
                " out of 0.." + std::to_string(total)};
        BigInt acc(0);
        for (long long i = 0; i < bits; i++) {
            long long bp = from + i;
            int bit = ((unsigned char)bytes[bp / 8] >> (7 - bp % 8)) & 1;
            acc = acc * BigInt(2) + BigInt(bit);
        }
        if (m == "read-bits" && bits > 0) { // two's complement sign
            long long tp = from;
            if (((unsigned char)bytes[tp / 8] >> (7 - tp % 8)) & 1)
                acc = acc - BigInt(2).pow(bits);
        }
        return acc.fitsLL() ? Value::integer(acc.toLL()) : Value::bigint(acc);
    }
    if (m == "write-ubits" || m == "write-bits") {
        long long from = args.size() > 0 ? args[0].toInt() : 0;
        long long bits = args.size() > 1 ? args[1].toInt() : 0;
        if (from < 0 || bits < 1)
            throw RakuError{Value::typeObj("X::OutOfRange"),
                "bit range " + std::to_string(from) + "+" + std::to_string(bits) + " out of range"};
        Value val = args.size() > 2 ? args[2] : Value::integer(0);
        BigInt v = val.big() ? *val.big() : BigInt(val.toInt());
        if (v.sign < 0) v = v + BigInt(2).pow(bits); // low `bits` bits of the 2's complement
        // grow to fit
        long long need = (from + bits + 7) / 8;
        if ((long long)bytes.size() < need) bytes.resize(need, '\0');
        // peel value bits LSB-first into positions from+bits-1 .. from
        for (long long i = bits - 1; i >= 0; i--) {
            BigInt q, r; BigInt::divmod(v, BigInt(2), q, r);
            v = q;
            long long bp = from + i;
            unsigned char& byte = (unsigned char&)bytes[bp / 8];
            unsigned char mask = (unsigned char)(1u << (7 - bp % 8));
            if (!r.isZero()) byte |= mask; else byte &= (unsigned char)~mask;
        }
        return buf;
    }
    // byte-addressed numeric forms: (read|write)-(num|int|uint)(32|64)?(offset[,value][,endian])
    bool isWrite = m.rfind("write-", 0) == 0;
    std::string kind = m.substr(isWrite ? 6 : 5); // num32 / num64 / uint64 / int32 / …
    int width = 0;
    if (!kind.empty() && ascii::isdigit((unsigned char)kind.back()))
        { size_t d = kind.find_first_of("0123456789"); width = std::atoi(kind.c_str() + d); kind = kind.substr(0, d); }
    if ((kind != "num" && kind != "int" && kind != "uint") ||
        (width != 0 && width != 8 && width != 16 && width != 32 && width != 64 && width != 128) ||
        (kind == "num" && width != 0 && width < 32))
        throw RakuError{Value::typeObj("X::Method::NotFound"), "No such method '" + m + "' for Buf"};
    long long off = args.size() > 0 ? args[0].toInt() : 0;
    size_t vi = 1; // value index for writes; endian index varies
    Value val = (isWrite && args.size() > 1) ? args[1] : Value::number(0);
    int endian = 0;
    for (size_t k = vi + (isWrite ? 1 : 0); k < args.size(); k++)
        if (args[k].t != VT::Pair) { endian = endianOf(args[k]); break; }
    int nb = width ? width / 8 : 8;
    if (off < 0)
        throw RakuError{Value::typeObj("X::OutOfRange"), "offset " + std::to_string(off) + " out of range"};
    // Typed bufs (buf16/32/64): Rakudo addresses `pos` in ELEMENTS — the value
    // lands at byte pos*W — and grows the buffer to (pos + nb) ELEMENTS,
    // zero-filled (verified: buf32.new(1,2,3).write-uint64(3, v) puts v in
    // elems 3..4 and .elems becomes 11). Scale both accordingly; the byte
    // math below is then unchanged. Digest::MD5 appends its bit-length with
    // `$b.write-uint64: $b.elems, $bits` on a buf32 and relies on this.
    int elemW = buf.blobElemSize();
    if (elemW > 1) {
        long long growElems = off + nb;           // Rakudo's quirk: nb ELEMENTS, not bytes
        off *= elemW;
        if (isWrite && (long long)bytes.size() < growElems * elemW)
            bytes.resize((size_t)(growElems * elemW), '\0');
    }
    if (nb > 8) { // int128/uint128: BigInt byte-peeling (the raw[8] fast path below caps at 64 bits)
        if (isWrite) {
            if ((long long)bytes.size() < off + nb) bytes.resize(off + nb, '\0');
            BigInt v = val.big() ? *val.big() : BigInt(val.toInt());
            if (v.sign < 0) v = v + BigInt(2).pow(nb * 8);
            for (int i = 0; i < nb; i++) { // peel LSB-first
                BigInt q, r; BigInt::divmod(v, BigInt(256), q, r); v = q;
                int pos = isLittle(endian) ? i : nb - 1 - i;
                bytes[off + pos] = (char)(unsigned char)r.toLL();
            }
            return buf;
        }
        if ((long long)bytes.size() < off + nb)
            throw RakuError{Value::typeObj("X::OutOfRange"), "read past end of buffer"};
        BigInt acc(0);
        for (int i = 0; i < nb; i++) { // accumulate MSB-first
            int pos = isLittle(endian) ? nb - 1 - i : i;
            acc = acc * BigInt(256) + BigInt((long long)(unsigned char)bytes[off + pos]);
        }
        if (kind == "int") { // two's complement sign
            int top = isLittle(endian) ? nb - 1 : 0;
            if ((unsigned char)bytes[off + top] & 0x80) acc = acc - BigInt(2).pow(nb * 8);
        }
        return acc.fitsLL() ? Value::integer(acc.toLL()) : Value::bigint(acc);
    }
    if (isWrite) {
        if ((long long)bytes.size() < off + nb) bytes.resize(off + nb, '\0');
        unsigned char raw[8] = {0};
        if (kind == "num") {
            if (nb == 4) { float f = (float)val.toNum(); std::memcpy(raw, &f, 4); }
            else { double d = val.toNum(); std::memcpy(raw, &d, 8); }
        } else {
            unsigned long long u;
            if (val.big()) { // low 64 bits (toInt would saturate past int64)
                BigInt v = *val.big(); if (v.sign < 0) v = v + BigInt(2).pow(64);
                BigInt q, lo; BigInt::divmod(v, BigInt(4294967296LL), q, lo);
                BigInt q2, hi; BigInt::divmod(q, BigInt(4294967296LL), q2, hi);
                u = ((unsigned long long)hi.toLL() << 32) | (unsigned long long)lo.toLL();
            }
            else u = (unsigned long long)val.toInt();
            std::memcpy(raw, &u, nb <= 8 ? nb : 8);
        }
        // raw[] is host order; reorder per requested endianness
        for (int i = 0; i < nb; i++) {
            int src = isLittle(endian) == hostLittle ? i : nb - 1 - i;
            bytes[off + i] = (char)raw[src];
        }
        return buf;
    }
    if ((long long)bytes.size() < off + nb)
        throw RakuError{Value::typeObj("X::OutOfRange"), "read past end of buffer"};
    unsigned char raw[8] = {0};
    for (int i = 0; i < nb; i++) {
        int dst = isLittle(endian) == hostLittle ? i : nb - 1 - i;
        raw[dst] = (unsigned char)bytes[off + i];
    }
    if (kind == "num") {
        if (nb == 4) { float f; std::memcpy(&f, raw, 4); return Value::number((double)f); }
        double d; std::memcpy(&d, raw, 8); return Value::number(d);
    }
    unsigned long long u = 0; std::memcpy(&u, raw, nb <= 8 ? nb : 8);
    if (kind == "int") { // sign-extend from nb bytes
        if (nb < 8 && (u & (1ULL << (nb * 8 - 1)))) u |= ~((1ULL << (nb * 8)) - 1);
        return Value::integer((long long)u);
    }
    if (nb == 8 && (u >> 63)) { // uint64 beyond long long
        BigInt b((long long)(u & 0x7FFFFFFFFFFFFFFFULL));
        return Value::bigint(b + BigInt(2).pow(63));
    }
    return Value::integer((long long)u);
}

// The canonical identity of a value — what `.WHICH` answers and what a Set/Bag/Mix
// keys on. Two values are the same element exactly when these agree, so it has to
// separate things that merely LOOK alike when stringified:
//   * an allomorph carries BOTH of its halves (`IntStr|Int|42|Str|42`), which is
//     what keeps `<42>` distinct from `42` and from `"42"`;
//   * a Rat identifies by its exact numerator/denominator (`Rat|421/10`), not by
//     its decimal rendering;
//   * a Complex by its two parts;
//   * a Bool by 1/0.
// Is this value's identity its OWN (an ObjAt), rather than its content (a
// ValueObjAt)? Measured against Rakudo 2026.08: Array, List, Seq, Hash, Buf,
// Instant, IO::Path, Code and every user object are ObjAt; Int, Rat, Num, Str,
// Bool, Range, the Setty/Baggy family, Date, Complex, a type object and Nil are
// ValueObjAt (sheet LA-36). A PAIR is whichever its parts are (sheet HM-19):
// `(a => 1)` is a value, `(a => [1])` is an object, because the Array inside it
// is one — which is why two such pairs are not `===` and `.unique` keeps both.
// …unless its class writes its own `method WHICH` answering a ValueObjAt.
static bool userWhichIsValue(const Value& v) {
    std::string w; bool isValue = false;
    return g_userWhich && g_userWhich(v, w, isValue) && isValue;
}
bool whichIsObjAt(const Value& v) {
    if (v.t == VT::Pair)
        return v.pairLive() ||   // a live container inside: the Pair is an object (pair.t)
               (v.pairKey() && whichIsObjAt(*v.pairKey())) ||
               (v.pairVal() && whichIsObjAt(*v.pairVal()));
    return (v.t == VT::Array && v.hashKind != "Capture" && v.enumName.empty()) ||
           (v.t == VT::Hash && v.hashKind.empty()) ||
           (v.t == VT::Hash && (v.hashKind == "Hash" || v.hashKind == "SetHash" ||
                                v.hashKind == "BagHash" || v.hashKind == "MixHash")) ||
           (v.t == VT::Str && (v.hashKind == "Buf" || v.hashKind == "IO")) ||
           (v.isNumeric() && v.hashKind == "Instant") ||
           v.t == VT::Code || (v.t == VT::Object && !userWhichIsValue(v));
}
std::string whichOf(const Value& v) {
    auto ratPart = [](const Value& r) {
        if (r.ratN() && r.ratD()) return r.ratN()->toString() + "/" + r.ratD()->toString();
        return r.toStr();
    };
    // a Buf/Instant/Duration is a reference type wearing a scalar's clothes: its
    // identity is the token stamped at construction (see identify() in Value.h),
    // not its bytes or its seconds. Without this `Buf.new(1,2).WHICH` was the
    // useless "Buf|" — the bytes are unprintable — for every buffer alive.
    // A token-less one (a construction site that predates the stamp) keeps the
    // old value rendering rather than claiming to be a different object.
    if (identityScalar(v) && v.ext()) {
        char idb[24];
        std::snprintf(idb, sizeof idb, "|%p", v.ext().get());
        return v.typeName() + idb;
    }
    if (v.isAllomorph()) {
        // the numeric half is the same value with the string side dropped
        Value num = v; num.hashKind.clear(); num.s.clear();
        std::string numName = v.typeName() == "IntStr"     ? "Int"
                            : v.typeName() == "RatStr"     ? "Rat"
                            : v.typeName() == "NumStr"     ? "Num"
                            : v.typeName() == "ComplexStr" ? "Complex" : "Num";
        std::string numId = numName == "Rat" ? ratPart(num) : num.toStr();
        if (numName == "Complex") numId = Value::number(num.n).toStr() + "|" + Value::number(num.im()).toStr();
        return v.typeName() + "|" + numName + "|" + numId + "|Str|" + v.s;
    }
    // A Date identifies by its DAYCOUNT — Rakudo's is "Date|57385". A
    // `:formatter` changes how it PRINTS, and the default arm below builds the
    // identity out of exactly that rendering, so a formatted Date stopped being
    // `===` to the same day unformatted. (A DateTime's WHICH *is* its .Str,
    // formatter and all — that is Rakudo's too, so it keeps the default arm;
    // what makes the two eqv regardless is valueEqv, which skips the key.)
    if (v.t == VT::Hash && v.hashKind == "Date" && v.hash())
        return "Date|" + std::to_string((long long)dateNumeric(v));
    switch (v.t) {
        case VT::Bool:    return "Bool|" + std::string(v.b ? "1" : "0");
        case VT::Rat:     return "Rat|" + ratPart(v);
        case VT::Complex: return "Complex|" + Value::number(v.n).toStr() + "|" + Value::number(v.im()).toStr();
        // An OBJECT is identified by its address, not by its contents — two
        // instances with equal attributes are different elements of a Set. This
        // lived only in the `.WHICH` arm, so `.WHICH` said they differed while
        // baggyKeyStr (which keys on the RENDERING, `A<obj>` for every instance
        // of A) merged them: `set($x, $y).elems` was 1.
        case VT::Object:  if (v.obj()) {
                              std::string w; bool isV = false;
                              if (g_userWhich && g_userWhich(v, w, isV)) return w;
                              char buf[24];
                              std::snprintf(buf, sizeof buf, "|%p", (void*)v.obj());
                              return v.typeName() + buf;
                          }
                          return v.typeName() + "|<null>";
        // a Range's identity is its GIST, exclusion markers and all — expanding
        // it makes `1..^5` and `1..4` the same value, and builds a huge string
        // for a large range on the way
        case VT::Range:   return "Range|" + v.gist();
        // A Pair identifies by its PARTS — but only while both are values. With
        // an Array (or any other reference type) inside, the Pair is an object
        // and identifies by BEING itself: the payload it was built with, which
        // every copy of the Pair shares and no other Pair has.
        case VT::Pair:    if (whichIsObjAt(v) || (v.pairValRO && v.i)) {
                              // (a FROZEN container pair keeps the identity it had)
                              char buf[24];
                              std::snprintf(buf, sizeof buf, "|%p",
                                            v.pairValRO && v.i ? (void*)(intptr_t)v.i : (void*)v.pairVal());
                              return "Pair" + std::string(buf);
                          }
                          return "Pair|" + (v.pairKey() ? whichOf(*v.pairKey()) : "Str|" + v.s.str()) +
                                 "|" + (v.pairVal() ? whichOf(*v.pairVal()) : "Any|");
        // a CAPTURE is a VALUE — `\(1,2) === \(1,2)` is True in Rakudo, alone
        // among the Arrays — so it identifies by its PARTS, each with its own
        // identity. Rendering them (the old "Capture|1 2") merged `\(1)` with
        // `\("1")` and `\(:a)` with a positional "a".
        // A Hash — and a Map — is an OBJECT: it identifies by BEING itself, the
        // payload every copy of the value shares. A refill (`%h = …`) and a
        // write into a nested Hash both leave that payload where it is, so the
        // WHICH holds still, as Rakudo's `Map|…`/`Hash|…` does (S32-hash/map.t).
        case VT::Hash:    if (v.hash() && (v.hashKind.empty() || v.hashKind == "Hash" || v.hashKind == "Map")) {
                              char buf[24];
                              std::snprintf(buf, sizeof buf, "|%p", (void*)v.hash());
                              return v.typeName() + buf;
                          }
                          return v.typeName() + "|" + v.toStr();
        case VT::Array:   if (v.hashKind == "Capture") {
                              std::string pos;
                              std::vector<std::string> named; // named parts are unordered
                              if (v.arr()) for (auto& e : *v.arr()) {
                                  if (e.t == VT::Pair && e.namedArg)
                                      named.push_back(":" + e.s + "(" +
                                                      (e.pairVal() ? whichOf(*e.pairVal()) : "") + ")");
                                  else pos += "(" + whichOf(e) + ")";
                              }
                              std::sort(named.begin(), named.end());
                              for (auto& nm : named) pos += nm;
                              return "Capture|" + pos;
                          }
                          // a JUNCTION is a value too: its kind and its eigenstates, so
                          // two `any(True, True)` are one key of a Mu-keyed hash and
                          // `any(True, False)` another (classify.t)
                          if (v.arr() && (v.enumName == "any" || v.enumName == "all" ||
                                          v.enumName == "one" || v.enumName == "none")) {
                              std::string s = "Junction|" + v.enumName.str() + "(";
                              for (auto& e : *v.arr()) s += whichOf(e) + ";";
                              return s + ")";
                          }
                          return v.typeName() + "|" + v.toStr();
        default:          return v.typeName() + "|" + v.toStr();
    }
}

// The typed key of an element, preserved in the count Value's `pairKey` so
// .keys/.pairs/.min/.max recover the original type (a Bag of Ints keeps Int
// keys, not the stringified form). Null for a plain Str — that round-trips
// through the string key, so Set-of-strings behaviour stays byte-identical.
std::shared_ptr<Value> baggyKey(const Value& v) {
    if (v.t == VT::Str && v.hashKind.empty() && v.enumName.empty()) return nullptr;
    return std::make_shared<Value>(v);
}
// The LOOKUP key of a quanthash element.
//
// This SHOULD be the element's identity (`whichOf`), because two elements are the
// same one exactly when their `.WHICH` agrees — keying on the rendering makes
// `42`, `"42"` and `<42>` a single element and `set(1,"1")` one-element instead of
// two. That change was written, measured and backed out, because it exposes a
// deeper mismatch it cannot fix on its own: rakupp's Hash keys are plain strings,
// so `{42 => 'a'}` contributes the STRING "42" to a set while a literal `42`
// contributes an Int. Today both render "42" and compare equal; under identity
// keys they become different elements, and roast's set operators — which compare
// an operator result against a `set(…)` literal — lose about 30 assertions.
//
// Fixing it properly means Hash keys carrying their original key object, which is
// its own piece of work. Until then the rendering below is what makes results
// print correctly; the keys stay renderings.
std::string baggyKeyStr(const Value& v) {
    if (v.t == VT::Type || v.t == VT::Any) return v.gist(); // type objects ARE elements
    // An ALLOMORPH is neither of its halves: `<42>` renders "42" exactly like the
    // Int 42, so keying on the rendering merged them and `42 ∈ <42 55 1>` was True.
    // Narrow on purpose — plain Str and Int keys keep their rendering, so the set
    // operators that compare a result against a `set(…)` literal are untouched.
    if (v.isAllomorph()) return whichOf(v);
    // …and an OBJECT, whose rendering is `Class<obj>` for every instance, so two
    // distinct objects collapsed into one Set element.
    if (v.t == VT::Object) return whichOf(v);
    // …and a Dateish, whose rendering a `:formatter` rewrites: two Dates of the
    // same day are ONE set element however either of them prints (whichOf keys a
    // Date on its daycount, a DateTime on its rendering — both as Rakudo does).
    if (v.t == VT::Hash && (v.hashKind == "Date" || v.hashKind == "DateTime")) return whichOf(v);
    return v.toStr();
}
// A Bag count must stay EXACT: the old long-long path saturated weights near
// 10^19 at LLONG_MAX, so distinct weights collapsed into equal ones and a
// weighted .roll drew ~uniform. Truncate to an Int through the BigInt tower.
static Value exactIntWeight(const Value& w) {
    if (w.t == VT::Int) return w;                        // Int / IntStr — big-capable as is
    if (w.t == VT::Str && !w.isAllomorph()) {
        Value nv = numifyStr(w.s);                       // callers pre-validate: this parses
        return nv.t == VT::Int ? nv : exactIntWeight(nv);
    }
    if (w.t == VT::Rat && w.ratN() && w.ratD() && !w.ratD()->isZero()) {
        BigInt q, r; BigInt::divmod(*w.ratN(), *w.ratD(), q, r);
        return Value::bigint(q);
    }
    double d = w.toNum();                                // Num / Bool / NumStr
    if (!std::isfinite(d)) return Value::integer(0);     // zero-denominator Rat: old toInt() gave 0
    if (d >= -9.19e18 && d <= 9.19e18) return Value::integer((long long)d);
    char buf[440]; std::snprintf(buf, sizeof buf, "%.0f", d); // a double this big is an exact integer
    return Value::bigint(BigInt::fromString(buf));
}
// pairsAsElements: constructors (set()/Set.new) treat a Pair item as ONE element
// (`set [foo=>1, bar=>2]` has two Pair elements); coercions (.Set/.Bag on a
// Hash, new-from-pairs) keep the pair→count reading.
// The EMPTY immutable Set/Bag/Mix is one object: `().Bag =:= bag()`. Only
// the constructors that hand the value straight to the program use it —
// makeBaggy's other callers may fill what they get in place.
Value emptyQuantSingleton(const std::string& kind) {
    static Value emptySet, emptyBag, emptyMix;
    static std::once_flag once;
    std::call_once(once, [] {
        emptySet = Value::makeHash(); emptySet.hashKind = "Set";
        emptyBag = Value::makeHash(); emptyBag.hashKind = "Bag";
        emptyMix = Value::makeHash(); emptyMix.hashKind = "Mix";
    });
    return kind == "Set" ? emptySet : kind == "Bag" ? emptyBag : emptyMix;
}

Value makeBaggy(const ValueList& items, const std::string& kind, bool pairsAsElements) {
    Value h = Value::makeHash();
    h.hashKind = kind;
    bool isSet = kind.find("Set") == 0;
    bool isMix = kind.find("Mix") == 0; // Mix weights keep their full numeric value (2.5 stays a Rat)
    auto add = [&](const std::string& k, const Value& cnt, const std::shared_ptr<Value>& tk) {
        auto it = h.hash()->find(k);
        auto keep = it != h.hash()->end() && it->second.pairKey() ? it->second.pairKey() : tk;
        if (isSet) {
            if (cnt.big() ? cnt.big()->sign > 0 : cnt.i > 0) { Value b = Value::boolean(true); b.pairKeyM() = keep; (*h.hash())[k] = std::move(b); }
            else h.hash()->erase(k);
            return;
        }
        Value c = it != h.hash()->end() ? rtAdd(it->second, cnt) : cnt;
        // A BAG holds POSITIVE weights only — a zero or negative one drops the
        // element (sheet HM-15). A Mix keeps any non-zero weight, negatives
        // included, which is the whole difference between the two kinds.
        bool drop = isMix ? (c.big() ? c.big()->isZero() : c.i == 0)
                          : (c.big() ? c.big()->sign <= 0 : c.i <= 0);
        if (!drop) { c.pairKeyM() = keep; (*h.hash())[k] = std::move(c); }
        else h.hash()->erase(k);
    };
    for (auto& v : items) {
        if (v.t == VT::Pair && pairsAsElements) {
            add(baggyKeyStr(v), Value::integer(1), std::make_shared<Value>(v)); // the Pair itself is the element
            continue;
        }
        if (v.t == VT::Pair) {
            Value w = v.pairVal() ? *v.pairVal() : Value::integer(0);
            if (!isSet) { // a Bag/Mix weight must coerce to a real number
                // (a non-real Complex is X::Numeric::Real, a weight that is not
                // finite X::OutOfRange — Rakudo's two refusals)
                if (w.t == VT::Complex && w.im() != 0.0)
                    throw RakuError{Value::typeObj("X::Numeric::Real"),
                        "Can not convert " + w.gist() + " to Real: imaginary part not zero"};
                if (w.t == VT::Num && !std::isfinite(w.n)) {
                    if (isMix)
                        throw RakuError{Value::typeObj("X::OutOfRange"),
                            "Value out of range. Is: " + w.gist() + ", should be in -Inf^..^Inf"};
                    throw RakuError{Value::typeObj("X::Numeric::CannotConvert"),
                        "Cannot convert " + w.gist() + " to Int"};
                }
                if (w.t == VT::Str && !w.isAllomorph()) {
                    const char* p = w.s.c_str();
                    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
                    char* end = nullptr;
                    if (*p) cnum::strtod(p, &end);
                    while (end && (*end == ' ' || *end == '\t' || *end == '\n')) end++;
                    if (*p && (end == p || *end))
                        throw RakuError{Value::typeObj("X::Str::Numeric"),
                            "Cannot convert string to number: " + w.s};
                }
            }
            // (a BOOL weight is not fractional — `(:a, :b).Mix` is a => 1, not
            // a => True; storing the Bool made that Mix un-`eqv` to <a b>.Mix)
            if (isMix && w.t != VT::Int && w.t != VT::Bool && w.isNumeric()) { // fractional weight
                const std::string mk = v.pairKey() ? baggyKeyStr(*v.pairKey()) : v.s.str();
                auto it = h.hash()->find(mk);
                auto keep = it != h.hash()->end() && it->second.pairKey() ? it->second.pairKey() : v.pairKey();
                if (it != h.hash()->end()) {
                    // through the EXACT tower, not a C double: the Rats 1/10 and 1/50
                    // summed as doubles gave 0.12000000000000001, and being a Num the
                    // result then printed at full Num precision too
                    Value sum = applyArith("+", it->second, w);
                    if (sum.toNum() == 0.0) h.hash()->erase(mk);
                    else { sum.pairKeyM() = keep; (*h.hash())[mk] = std::move(sum); }
                } else if (w.toNum() != 0.0) { w.pairKeyM() = keep; (*h.hash())[mk] = w; }
                continue;
            }
            // Set membership is the value's TRUTHINESS (`:e<meow>` joins, `:0d`/`:f('')`
            // do not); Bag/Mix use the numeric weight. (typed key travels in pairKey)
            // The LOOKUP key comes from the key OBJECT when there is one: an
            // object hash indexes its own entries by identity, and feeding that
            // index in as an element key made `%objhash.Set` disjoint from the
            // same set built any other way.
            add(v.pairKey() ? baggyKeyStr(*v.pairKey()) : v.s,
                isSet ? Value::integer(w.truthy() ? 1 : 0) : exactIntWeight(w), v.pairKey());
        }
        else add(baggyKeyStr(v), Value::integer(1), baggyKey(v));
    }
    return h;
}

// Build a Signature introspection value from a routine's parameters.
// Rendered as a Hash tagged "Signature" carrying its .raku text and arity/count.
// A parameter's default as Rakudo renders it: only simple literals and type
// names constant-fold; anything else is a thunk, printed as `Code.new`.
static std::string renderDefault(const Param& p) {
    if (!p.defaultRaku.empty()) return p.defaultRaku;
    const Expr* d = p.defaultVal.get();
    if (!d) return "";
    switch (d->kind) {
        case NK::IntLit: {
            auto* n = static_cast<const IntLit*>(d);
            return n->big.empty() ? std::to_string(n->v) : n->big;
        }
        case NK::NumLit:  return rakuRepr(Value::number(static_cast<const NumLit*>(d)->v));
        case NK::StrLit:  return rakuStrLit(static_cast<const StrLit*>(d)->v);
        case NK::BoolLit: return static_cast<const BoolLit*>(d)->v ? "Bool::True" : "Bool::False";
        case NK::NameTerm: {
            const std::string& n = static_cast<const NameTerm*>(d)->name;
            // a bare type name folds; a called-by-name term does not
            if (!n.empty() && (ascii::isupper((unsigned char)n[0]) || n == "Nil")) return n;
            return "Code.new";
        }
        default: return "Code.new";
    }
}

// One parameter, Rakudo-style: `Int:D $x`, `:$a!`, `:b($a) = 2`, `*@r`, `|c`.
// `inSignature` is the same distinction ctorParamStr draws: a NAMELESS parameter
// that has a type renders as the type alone inside a signature — `(Int, $, Str
// $s)` — and as `Int $` on its own, which is what Parameter.gist answers. We
// rendered the sigil in both places, so a signature carrying an anonymous typed
// parameter read `(Int $, …)` where every other implementation writes `(Int, …)`.
static std::string renderParam(const Param& p, bool inSignature = false) {
    std::string o;
    if (!p.type.empty()) {
        o += p.typeShown.empty() ? p.type : p.typeShown;
        // A COERCION renders both halves — `Int(Cool) $a`, and `Int()` as
        // `Int(Any)`, which is what it means. Only the target was printed, so
        // `:(Int(Cool) $a).raku` came back `:(Int $a)` and read as an ordinary
        // type constraint (roast S02-names-vars/signature.t). `.type` already
        // answered the coercion; this is the rendering catching up with it.
        if (p.coerce)
            o += "(" + (p.coerceFrom.empty() ? std::string("Any") : p.coerceFrom) + ")";
        if (p.defConstraint == 1) o += ":D";
        else if (p.defConstraint == 2) o += ":U";
        o += " ";
    }
    // `|c` is a capture, not a `*`-slurpy — it renders with its own leading `|`
    bool capture = p.slurpy && p.slurpyKind == 0 && (p.sigil == '|' || p.sigil == '\\');
    auto subSigStr = [&]() -> std::string {   // `%h ($a, $b)` — a destructuring sub-signature
        if (!p.subSig) return "";
        std::string ss = " (";
        for (size_t k = 0; k < p.subSig->size(); k++) {
            if (k) ss += ", ";
            ss += renderParam((*p.subSig)[k], true);
        }
        return ss + ")";
    };
    if (capture) return o + "|" + p.name + subSigStr();
    if (p.slurpy) o += p.slurpyKind == 'n' ? "**" : p.slurpyKind == '1' ? "+" : "*";
    // the anonymous-but-typed case: `Int` in a signature, `Int $` alone. An
    // anonymous UNTYPED one is `$` either way — there would be nothing left.
    if (inSignature && p.name.empty() && !p.type.empty() && !p.named && !p.slurpy && p.sigil == '$' &&
        !p.optional && !p.isRw && !p.isCopy && !p.whereExpr && !p.hadWhere &&
        renderDefault(p).empty()) {
        o.pop_back();   // the space renderParam put after the type name
        return o;
    }
    std::string var = p.name.empty() ? std::string(1, p.sigil) : p.name;
    if (p.named) {
        std::string bare = p.name.size() > 1 ? p.name.substr(1) : p.name;
        // `:$a` when the external name matches the variable, else nested alias
        // layers `:b(:c($a))` — every key answers
        if ((p.namedKey.empty() || p.namedKey == bare) && p.aliasKeys.empty()) o += ":" + var;
        else {
            std::string inner = var;
            for (auto it = p.aliasKeys.rbegin(); it != p.aliasKeys.rend(); ++it)
                inner = ":" + *it + "(" + inner + ")";
            o += ":" + (p.namedKey.empty() ? bare : p.namedKey) + "(" + inner + ")";
        }
    }
    else o += (p.sigil == '\\' && !p.name.empty() ? "\\" : "") + var;   // `\x` keeps its backslash
    o += subSigStr();
    std::string def = renderDefault(p);
    if (p.named) { if (p.required) o += "!"; }
    else if (p.optional && def.empty() && !p.slurpy) o += "?";
    if (p.isRw) o += " is rw";
    if (p.isCopy) o += " is copy";
    if (p.whereExpr || p.hadWhere) o += " where { ... }";
    if (!def.empty()) o += " = " + def;
    return o;
}

// Param owns unique_ptr expressions, so a residual signature can't hold copies of
// them — snapshot the plain fields and pre-render what the exprs contribute.
std::shared_ptr<Param> signatureParamCopy(const Param& p) {
    auto q = std::make_shared<Param>();
    q->name = p.name; q->sigil = p.sigil; q->type = p.type; q->namedKey = p.namedKey;
    q->aliasBoth = p.aliasBoth; q->aliasKeys = p.aliasKeys; q->pod = p.pod;
    q->slurpyKind = p.slurpyKind; q->named = p.named; q->slurpy = p.slurpy;
    q->optional = p.optional; q->required = p.required; q->invocant = p.invocant; q->pastDoubleSemi = p.pastDoubleSemi;
    q->defConstraint = p.defConstraint; q->coerce = p.coerce; q->coerceFrom = p.coerceFrom;
    q->isRw = p.isRw; q->isCopy = p.isCopy;
    q->hadWhere = p.whereExpr != nullptr || p.hadWhere;
    q->defaultRaku = renderDefault(p);
    return q;
}

// Signature ~~ Signature, Rakudo's algorithm (Signature.ACCEPTS(Signature)):
// does every call the TOPIC's signature binds also bind to this one?
// Parameter ~~ Parameter asks the same of one parameter: its type narrows,
// its names are a subset, and a sub-signature narrows in turn.
static bool sigAcceptsSig(Interpreter& I, const std::vector<Param>& S, const std::string& sRet,
                          const std::vector<Param>& T, const std::string& tRet);
// The literal a LITERAL parameter (`:("foo")`, `:(1e0)`) stands for, and the
// type it constrains to (an angle literal is its number, not the allomorph)
static bool sigLiteral(Interpreter& I, const Param& p, Value& out, std::string& type) {
    if (!p.litVal) return false;
    out = I.eval(p.litVal.get());
    type = out.typeName();
    if (out.isAllomorph())
        type = out.t == VT::Int ? "Int" : out.t == VT::Rat ? "Rat" : out.t == VT::Num ? "Num"
             : out.t == VT::Complex ? "Complex" : type;
    return true;
}
static std::string sigParamType(const Param& p) {
    if (!p.type.empty()) return p.type;
    return "Any";
}
static std::set<std::string> sigParamNames(const Param& p) {
    std::set<std::string> n;
    if (!p.namedKey.empty()) n.insert(p.namedKey);
    else if (p.name.size() > 1) n.insert(p.name.substr(1));
    for (auto& k : p.aliasKeys) n.insert(k);
    if (p.aliasBoth && p.name.size() > 1) n.insert(p.name.substr(1));
    return n;
}
static bool sigParamOptional(const Param& p) {
    if (p.named) return !p.required;
    return p.optional || p.defaultVal || !p.defaultRaku.empty();
}
static bool sigParamAccepts(Interpreter& I, const Param& s, const Param& t) {
    if (s.named != t.named) return false;
    // a LITERAL parameter binds exactly one value: it accepts the same literal
    // and nothing wider (`:(Str) ~~ :("foo")` is False, `:("foo") ~~ :(Str)` True)
    Value sLit, tLit;
    std::string sLitT, tLitT;
    const bool sIsLit = sigLiteral(I, s, sLit, sLitT), tIsLit = sigLiteral(I, t, tLit, tLitT);
    if (sIsLit) {
        if (!tIsLit || sLitT != tLitT) return false;
        const bool nan = sLit.t == VT::Num && std::isnan(sLit.toNum());
        if (nan ? !(tLit.t == VT::Num && std::isnan(tLit.toNum())) : sLit.toStr() != tLit.toStr())
            return false;
    }
    const std::string st = sigParamType(s), tt = tIsLit && t.type.empty() ? tLitT : sigParamType(t);
    if (st != "Mu" && st != tt && !(tt != "Mu" && st == "Any") &&
        !I.typeOrSubsetMatches(Value::typeObj(tt), st))
        return false;
    if (st == "Any" && tt == "Mu") return false;
    if (s.named) {
        auto sn = sigParamNames(s), tn = sigParamNames(t);
        for (auto& k : tn) if (!sn.count(k)) return false;
    }
    if (s.subSig) {
        if (!t.subSig) return false;
        if (!sigAcceptsSig(I, *s.subSig, "", *t.subSig, "")) return false;
    }
    // `&bar:(Str --> Bool)`: a Callable's signature constraint narrows the
    // same way, its return type included
    if (s.codeSig) {
        if (!t.codeSig) return false;
        if (!sigAcceptsSig(I, *s.codeSig, s.codeSigRet, *t.codeSig, t.codeSigRet)) return false;
    }
    return true;
}
static bool sigAcceptsSig(Interpreter& I, const std::vector<Param>& S, const std::string& sRet,
                          const std::vector<Param>& T, const std::string& tRet) {
    std::vector<const Param*> spos, tpos, sn, tn;
    for (auto& p : S) if (!p.invocant) (p.named || (p.slurpy && p.sigil == '%') ? sn : spos).push_back(&p);
    for (auto& p : T) if (!p.invocant) (p.named || (p.slurpy && p.sigil == '%') ? tn : tpos).push_back(&p);
    // (a capture parameter, as `.capture` answers it: `|`, and `|c`)
    auto capt = [](const Param* p) {
        return p->sigil == '|' || (p->slurpy && p->slurpyKind == 0 && p->sigil == '\\');
    };
    size_t si = 0, ti = 0;
    while (si < spos.size()) {
        if (ti >= tpos.size()) break;
        const Param* t = tpos[ti++];
        const Param* s = spos[si++];
        if (s->slurpy || capt(s)) { si = spos.size(); ti = tpos.size(); break; }
        if (t->slurpy || capt(t)) {
            bool any = false;
            for (size_t k = si; k < spos.size(); k++) if (spos[k]->slurpy || capt(spos[k])) any = true;
            if (!any) return false;
            si = spos.size(); ti = tpos.size(); break;
        }
        if (!sigParamOptional(*s) && sigParamOptional(*t)) return false;
        if (!sigParamAccepts(I, *s, *t)) return false;
    }
    if (ti < tpos.size()) return false;
    if (si < spos.size()) {
        const Param* f = spos[si];
        if (!(sigParamOptional(*f) || f->slurpy || capt(f))) return false;
    }
    for (auto* s : sn) {
        if (sigParamOptional(*s) || s->slurpy || !s->named) continue;
        int n = 0;
        for (auto* t : tn) if (t->named && !sigParamOptional(*t) && sigParamAccepts(I, *s, *t)) n++;
        if (n != 1) return false;
    }
    std::vector<const Param*> here(sn.begin(), sn.end());
    bool hasSlurpy = false;
    for (auto* s : sn) if (s->slurpy) hasSlurpy = true;
    for (auto* s : spos) if (capt(s)) hasSlurpy = true;   // `|c` takes the nameds too
    for (auto* t : tn) {
        if (t->slurpy) {
            for (auto* h : here) if (!h->slurpy && sigParamType(*h) != "Mu") return false;
            return hasSlurpy;
        }
        bool found = false;
        for (size_t k = 0; k < here.size(); k++)
            if (!here[k]->slurpy && sigParamAccepts(I, *here[k], *t)) { here.erase(here.begin() + k); found = true; break; }
        if (!found && !hasSlurpy) return false;
    }
    return sRet == tRet;
}

// Is the class named `pkg` declared `is hidden`? Installed by InterpreterBinding.cpp,
// which owns the class registry (a method of a hidden class has no implicit *%_)
bool (*g_pkgIsHidden)(const std::string&) = nullptr;

Value makeSignature(const Callable* c) {
    // A multi group's signature is its PROTO's: `proto method relpath(Mu $path)`
    // answers `(Mu $path)`, not the empty signature a synthesized dispatcher
    // carries. `^lookup` hands back the dispatcher, so that is what introspection
    // sees — Path::Finder reads `.signature.count` off it to decide how to call
    // the matcher, and an empty one made every proto-declared matcher unusable.
    bool protoLess = c && c->isMultiDispatcher && !c->params;
    if (c && c->isMultiDispatcher && !c->params)
        for (auto& cand : c->candidates)
            if (cand.code() && (cand.code()->isProto || cand.code()->isProtoBody) &&
                cand.code()->params) { c = cand.code(); protoLess = false; break; }
    // a .assuming wrapper carries its residual params; everything else renders
    // its declared params
    std::vector<const Param*> ps;
    if (c) {
        if (c->hasPrimed) { for (auto& sp : c->primedParams) ps.push_back(sp.get()); }
        else if (c->params) for (auto& p : *c->params) ps.push_back(&p);
    }
    // A routine with NO written signature has the one its body implies: its
    // placeholders (`sub c { $^a }` is `($a)`) and, for `@_` / `%_`, the implicit
    // slurpies (`(*@_, *%_)`)
    std::vector<Param> synth;
    if (ps.empty() && c && !c->hasPrimed && !c->hadSig &&
        (!c->placeholders.empty() || c->implicitArgs)) {
        synth.reserve(c->placeholders.size() + 2);
        for (auto& ph : c->placeholders) {
            if (ph.size() < 3) continue;
            Param p; p.sigil = ph[0];
            p.name = std::string(1, ph[0]) + ph.substr(2);
            p.named = ph[1] == ':';
            p.required = p.named;   // `$:name` is a REQUIRED named: `:$name!`
            synth.push_back(std::move(p));
        }
        if (c->implicitArgs & 1) { Param p; p.sigil = '@'; p.name = "@_"; p.slurpy = true; p.slurpyKind = 'f'; synth.push_back(std::move(p)); }
        if (c->implicitArgs & 2) { Param p; p.sigil = '%'; p.name = "%_"; p.slurpy = true; p.slurpyKind = 'f'; synth.push_back(std::move(p)); }
        for (auto& p : synth) ps.push_back(&p);
    }
    // A multi with no proto answers the proto Rakudo generates for it,
    // `(;; Mu |)`: any capture at all (S06-other/introspection.t)
    if (ps.empty() && protoLess) {
        Param p; p.sigil = '|'; p.type = "Mu"; p.slurpy = true; p.pastDoubleSemi = true;
        synth.push_back(std::move(p));
        ps.push_back(&synth.back());
    }
    // A bare `{ … }` block with no written signature carries an IMPLICIT `$_`:
    // `{;}.signature` is `(;; $_? is raw = OUTER::<$_>)`, arity 0 but count 1.
    // `-> {…}` and `sub {…}` do NOT (both are `()`), and a placeholder block has
    // its own real signature — hence the isBlock/!hadSig/no-placeholders guard.
    if (ps.empty() && c && c->isBlock && !c->hadSig && !c->isSigLiteral &&
        c->placeholders.empty()) {
        Value s = Value::makeHash(); s.hashKind = "Signature";
        (*s.hash())["str"] = Value::str("(;; $_? is raw = OUTER::<$_>)");
        (*s.hash())["rakustr"] = Value::str("(;; Mu $_? is raw = OUTER::<$_>)");
        (*s.hash())["arity"] = Value::integer(0);
        (*s.hash())["count"] = Value::integer(1);
        Value params = Value::array(); params.isList = true;
        Value pv = Value::makeHash(); pv.hashKind = "Parameter";
        (*pv.hash())["str"] = Value::str("$_? is raw = OUTER::<$_>");
        (*pv.hash())["name"] = Value::str("$_");
        (*pv.hash())["usage-name"] = Value::str("_");
        (*pv.hash())["type"] = Value::str("Mu");
        (*pv.hash())["type-obj"] = Value::typeObj("Mu");   // Rakudo: the implicit topic is Mu
        (*pv.hash())["optional"] = Value::boolean(true);
        (*pv.hash())["slurpy"] = Value::boolean(false);
        (*pv.hash())["named"] = Value::boolean(false);
        (*pv.hash())["raw"] = Value::boolean(true);
        (*pv.hash())["readonly"] = Value::boolean(false);
        (*pv.hash())["rw"] = Value::boolean(false);
        (*pv.hash())["suffix"] = Value::str("?");
        (*pv.hash())["multi-invocant"] = Value::boolean(false);
        params.arr()->push_back(std::move(pv));
        (*s.hash())["params"] = std::move(params);
        return s;
    }
    std::string sig = "(", rsig = "(";
    long long arity = 0, count = 0; bool slurpy = false, first = true, prevPastSemi = false;
    for (const Param* pp : ps) {
        const Param& p = *pp;
        // The INVOCANT counts. Rakudo reports `method file(Bool $v = True)` as
        // count 2 / arity 1 — the invocant is a required positional like any
        // other, and code that asks how many arguments a method takes subtracts
        // one for it (`$method.signature.count - 1`, which is how Path::Finder
        // decides whether a matcher takes a value or a list). It is still not
        // RENDERED here: the Callable does not know the class it was declared
        // in, so `Mu $:` would be a worse answer than leaving it out.
        if (p.invocant) { count++; arity++; continue; }
        if (!first) { std::string sep = p.pastDoubleSemi && !prevPastSemi ? ";; " : ", "; sig += sep; rsig += sep; }
        else if (p.pastDoubleSemi) { sig += ";; "; rsig += ";; "; }   // `(;; $x)`: nothing before the `;;`
        first = false;
        prevPastSemi = p.pastDoubleSemi;
        sig += renderParam(p, /*inSignature=*/true);
        rsig += renderParam(p);
        // `*%opts` slurps NAMED arguments and takes no positional at all, so it
        // does not make the count Inf — only *@ / **@ / +@ do. Every method
        // carries an implicit one, which is how this reached signatures that never
        // wrote it: Path::Finder builds a filter's Capture from
        // `signature.count`, and Inf sent one-argument filters down the
        // many-arguments branch, wrapping the pattern in a Seq.
        if (!p.named && !(p.slurpy && p.sigil == '%'))
        { if (p.slurpy) slurpy = true; else { count++; if (!p.optional && !p.defaultVal && p.defaultRaku.empty()) arity++; } }
    }
    // A METHOD carries an implicit `*%_`, and its signature says so (Rakudo:
    // `method m1($a)` is `(Foo $: $a, *%_)`) — unless it wrote a named slurpy
    // of its own, or its class is `is hidden`, which gives it none
    if (c && c->isMethod && !c->isMultiDispatcher && !c->isBlock && !c->hasPrimed) {
        bool ownSlurpy = false;
        for (const Param* pp : ps)
            if (pp->slurpy && (pp->sigil == '%' || pp->sigil == '|' || pp->sigil == '\\')) ownSlurpy = true;
        if (!ownSlurpy && !(g_pkgIsHidden && !c->pkg.empty() && g_pkgIsHidden(c->pkg))) {
            const char* sep = first ? "" : ", ";
            sig += std::string(sep) + "*%_"; rsig += std::string(sep) + "*%_";
        }
    }
    // a declared return type is part of the signature's rendering: `($x --> Int)`
    // (space-separated, no comma — and `(--> Int)` when there are no parameters)
    // (Rakudo separates with a space either way, so an empty parameter list
    // renders as `( --> Str)`)
    if (c && !c->retType.empty()) {
        std::string rt = c->retType;
        if (char sm = retTypeSmiley(rt)) rt = retTypeName(rt) + ":" + std::string(1, sm);
        sig += " --> " + rt; rsig += " --> " + rt;
    }
    sig += ")"; rsig += ")";
    Value s = Value::makeHash(); s.hashKind = "Signature";
    (*s.hash())["str"] = Value::str(sig);
    if (rsig != sig) (*s.hash())["rakustr"] = Value::str(rsig);
    // `.returns` / `.of` — the DECLARED return type. DBDish's TypeConverter keys
    // its conversion table by it (`%!Conversions{$_.signature.returns} = $_`), so
    // without this the whole table was built under one key.
    if (c && !c->retType.empty()) (*s.hash())["returns"] = Value::typeObj(retTypeName(c->retType));
    // the declared parameters themselves (AST-owned, program lifetime), for
    // Signature ~~ Signature
    if (c && !c->hasPrimed && c->params && synth.empty()) {
        (*s.hash())["\x01sigptr"] = Value::integer((long long)(intptr_t)c->params);
        (*s.hash())["\x01ret"] = Value::str(c->retType);
        if (c->isMethod) (*s.hash())["\x01method"] = Value::boolean(true);   // an invocant comes first
    }
    (*s.hash())["arity"] = Value::integer(arity);
    (*s.hash())["count"] = slurpy ? Value::number(std::numeric_limits<double>::infinity()) : Value::integer(count);
    Value params = Value::array(); params.isList = true;
    for (const Param* pp : ps) {
        const Param& p = *pp;
        // A parameter carrying user traits keeps the ONE meta-object its traits
        // were dispatched against: a `trait_mod:<is>` mixes roles into it at
        // declaration time, and a freshly rendered copy would have forgotten
        // them. Only trait-carrying parameters are cached — every other
        // signature renders as it always did.
        if (!p.userTraits.empty() && p.metaBox) {
            params.arr()->push_back(p.metaBox->v);
            continue;
        }
        Value pv = Value::makeHash(); pv.hashKind = "Parameter";
        // how the parameter renders on its own — Value::gist reads this, so a
        // `say $sig.params[0]` shows `Int $one` rather than the attribute dump
        (*pv.hash())["str"] = Value::str(renderParam(p));
        // an ANONYMOUS parameter has an empty .name, not its bare sigil
        // (a sigilless `\x` or capture `|c` carries no sigil: its name is it)
        const bool sigilless = p.sigil == '|' || p.sigil == '\\';
        (*pv.hash())["name"] = Value::str(p.name.size() > 1 || (sigilless && !p.name.empty())
                                              ? p.name : std::string());
        // `.twigil`: `$*a` → "*", `$!a` → "!", `$.a` → "."
        (*pv.hash())["twigil"] = Value::str(
            !sigilless && p.name.size() > 2 && std::strchr("*!.?^:=~", p.name[1])
                ? std::string(1, p.name[1]) : std::string());
        // `.usage-name` is the name without its sigil AND its twigil, so the
        // dynamic `Str @*l` is usable as plain `l`
        {
            std::string un = p.name.size() > 1 ? p.name.substr(1) : std::string();
            if (!un.empty() && std::strchr("*?!.=~^:", un[0])) un = un.substr(1);
            (*pv.hash())["usage-name"] = Value::str(un);
        }
        // the parameter's declarator doc (`#= …` / a leading `#|`) — .WHY
        // reads it; it already drives $*USAGE's option list
        if (!p.pod.empty()) {
            (*pv.hash())["why"] = Value::str(p.podLead.empty() || p.podTrail.empty() ? p.pod
                                                                               : p.podLead + "\n" + p.podTrail);
            if (!p.podTrail.empty()) (*pv.hash())["whyTrail"] = Value::str(p.podTrail);
        }
        (*pv.hash())["type"] = Value::str(p.type);
        // the TYPE OBJECT for `.type` (compared `=:= Str` etc. by Cro's router).
        // Unconstrained is Mu; a slurpy/@-sigil param is Positional, %-sigil
        // Associative, &-sigil Callable — the constraint its sigil implies.
        // An unconstrained parameter is Any on a ROUTINE and Mu on a bare
        // `:( … )` literal; the sigil implies its own constraint either way.
        {
            // a TYPED @/% parameter reports the PARAMETRIC container type, as
            // Rakudo does: `Str :@foo` is Positional[Str] with .of = Str —
            // Getopt::Long derives the option's element type from exactly that
            Value tv;
            if (!p.type.empty() && (p.sigil == '@' || p.sigil == '%') && !p.slurpy) {
                tv = Value::typeObj(p.sigil == '@' ? "Positional" : "Associative");
                tv.ofTypeM() = p.type; // renders as Positional[Str]; .of answers Str
            }
            // a COERCION parameter reports the coercion type itself, as Rakudo
            // does: `Foo(Str) :$foo` is `Foo(Str)` and a bare `Foo()` is
            // `Foo(Any)`. Both halves live in the name — see the ^coerce /
            // ^target_type / ^constraint_type surface, which is how
            // Getopt::Long decides what to parse an option's argument into.
            else if (p.coerce && !p.type.empty())
                tv = Value::typeObj(p.type + "(" +
                                    (p.coerceFrom.empty() ? std::string("Any") : p.coerceFrom) + ")");
            // a LITERAL parameter `:(3)` is typed by its literal
            else if (p.type.empty() && p.litVal &&
                     (p.litVal->kind == NK::IntLit || p.litVal->kind == NK::StrLit ||
                      p.litVal->kind == NK::NumLit || p.litVal->kind == NK::BoolLit))
                tv = Value::typeObj(p.litVal->kind == NK::IntLit ? "Int"
                                  : p.litVal->kind == NK::StrLit ? "Str"
                                  : p.litVal->kind == NK::NumLit ? "Num" : "Bool");
            else tv = Value::typeObj(
                !p.type.empty() ? p.type
                : p.sigil == '@' ? "Positional"
                : p.sigil == '%' ? "Associative"
                : p.sigil == '&' ? "Callable"
                // An untyped scalar parameter defaults to Mu on a BLOCK and on a
                // bare signature literal, and to Any on a routine — Rakudo
                // measured: `(-> $a {}).signature.params[0].type` is Mu where
                // `(sub ($a) {}).…` is Any. Blocks answered Any here, so a
                // signature literal never compared equal to the pointy block it
                // describes (roast S02-names-vars/signature.t).
                : (c && (c->isSigLiteral || c->isBlock)) ? "Mu" : "Any");
            (*pv.hash())["type-obj"] = std::move(tv);
        }
        // trait/shape flags the introspection API exposes one method each for
        (*pv.hash())["raw"]  = Value::boolean(p.isRaw || (p.sigil == '\\' && !p.slurpy && !p.isCopy));
        (*pv.hash())["copy"] = Value::boolean(p.isCopy);
        (*pv.hash())["readonly"] = Value::boolean(!(p.isRw || p.isCopy || p.isRaw ||
                                                  (p.sigil == '\\' && !p.slurpy)));
        (*pv.hash())["rw"]   = Value::boolean(p.isRw);
        (*pv.hash())["capture"] = Value::boolean(p.slurpy && p.slurpyKind == 0 &&
                                               (p.sigil == '|' || p.sigil == '\\'));
        (*pv.hash())["invocant"] = Value::boolean(p.invocant);
        (*pv.hash())["multi-invocant"] = Value::boolean(!p.pastDoubleSemi); // only `;;` makes it False
        // `.prefix`/`.suffix`/`.modifier` — how the parameter is SPELLED
        (*pv.hash())["prefix"] = Value::str(
            !p.slurpy ? "" : p.slurpyKind == 'n' ? "**" : p.slurpyKind == '1' ? "+"
            : (p.sigil == '|' || p.sigil == '\\') ? "|" : "*");
        (*pv.hash())["suffix"] = Value::str(p.named ? (p.required ? "!" : "")
                                                  : (p.optional && !p.defaultVal ? "?" : ""));
        (*pv.hash())["modifier"] = Value::str(p.defConstraint == 1 ? ":D"
                                          : p.defConstraint == 2 ? ":U" : "");
        // a `*%h` slurpy takes the NAMED arguments, and says so
        (*pv.hash())["named"] = Value::boolean(p.named || (p.slurpy && p.sigil == '%'));
        // `sub h(::T $x)` — the type variables the parameter captures
        if (p.typeCapture && !p.type.empty()) {
            Value tc = Value::array(); tc.isList = true;
            tc.arr()->push_back(Value::str(p.type));
            (*pv.hash())["type_captures"] = std::move(tc);
        }
        // `.default` is a Callable producing the default — undefined when the
        // parameter has none
        if (p.defaultVal) {
            const Expr* de = p.defaultVal.get();
            Value dc; dc.t = VT::Code; dc.setCode(std::make_shared<Callable>());
            dc.code()->builtin = [de](Interpreter& I, ValueList&) -> Value {
                return I.eval(const_cast<Expr*>(de));
            };
            (*pv.hash())["default"] = dc;
        }
        // with no default at all `.default` is the Code TYPE OBJECT — the
        // attribute's declared type — not a bare Any
        else (*pv.hash())["default"] = Value::typeObj("Code");
        // a named parameter is optional unless marked `!`
        (*pv.hash())["optional"] = Value::boolean(p.optional || p.defaultVal != nullptr ||
                                                  (p.named && !p.required));
        (*pv.hash())["slurpy"] = Value::boolean(p.slurpy);
        // `.constraints`: a literal parameter ('greet' in `get -> 'greet', $n {}`)
        // answers its literal value; otherwise Mu (matches Rakudo's use in Cro)
        {   // `.constraints` is a JUNCTION, as in Rakudo: all() when the
            // parameter is unconstrained (Getopt::Long stores it into a
            // `has Junction:D $.constraints` attribute), all(<literal>) for a
            // literal parameter — static context, so decode the common literal
            // node kinds directly (StrLit/IntLit). A `where` clause is scored
            // at dispatch here and is not yet carried as a Code eigenstate.
            Value cj = Value::array(); cj.enumName = "all";
            if (p.litVal) {
                Expr* le = p.litVal.get();
                if (le->kind == NK::StrLit) cj.arr()->push_back(Value::str(static_cast<StrLit*>(le)->v));
                else if (le->kind == NK::IntLit) cj.arr()->push_back(Value::integer(static_cast<IntLit*>(le)->v));
                else if (le->kind == NK::NumLit) cj.arr()->push_back(Value::number(static_cast<NumLit*>(le)->v));
                else if (le->kind == NK::BoolLit) cj.arr()->push_back(Value::boolean(static_cast<BoolLit*>(le)->v));
            }
            // …and a `where` clause joins it: a block or a smartmatch target
            // (`where { $_ %% 2 }`, `where Int`), so `5 ~~ $p.constraints`
            // asks it: a Callable that evaluates the clause when asked and
            // smartmatches the candidate against it.
            if (p.whereExpr) {
                const Expr* we = p.whereExpr.get();
                Value wc; wc.t = VT::Code; wc.setCode(std::make_shared<Callable>());
                wc.code()->builtin = [we](Interpreter& I, ValueList& a) -> Value {
                    return Value::boolean(I.attrWhereOk(we, a.empty() ? Value::any() : a[0]));
                };
                cj.arr()->push_back(std::move(wc));
            }
            (*pv.hash())["constraints"] = std::move(cj);
        }
        {   // `.named_names`: every name this named parameter answers to —
            // INNERMOST first, as Rakudo orders them: `:fooo(:f(:@foo))` is
            // ("foo", "f", "fooo"). Getopt::Long keys the option on
            // named_names[0], so the outermost-first order renamed --foo's
            // capture entry to :fooo.
            Value nn = Value::array(); nn.isList = true;
            if (p.named) {
                if (p.namedKey.empty() || p.aliasBoth) {
                    std::string bare = p.name.size() > 2 && (p.name[1] == '!' || p.name[1] == '.')
                                     ? p.name.substr(2) : (p.name.size() > 1 ? p.name.substr(1) : p.name);
                    nn.arr()->push_back(Value::str(bare));
                }
                for (auto it2 = p.aliasKeys.rbegin(); it2 != p.aliasKeys.rend(); ++it2)
                    nn.arr()->push_back(Value::str(*it2));
                if (!p.namedKey.empty()) nn.arr()->push_back(Value::str(p.namedKey));
            }
            (*pv.hash())["named_names"] = nn;
        }
        // …and remember it, for the identity the trait dispatch above relies on
        if (!p.userTraits.empty()) {
            p.metaBox = std::make_shared<ParamMetaBox>();
            p.metaBox->v = pv;
        }
        params.arr()->push_back(pv);
    }
    // A METHOD's reflected .params carries the implicit pieces Rakudo's do: the
    // invocant first (unless one was declared) and a trailing `*%_` slurpy
    // (unless the method declares its own named slurpy). Data::Dump renders
    // method signatures as `.params[1 .. *-2]`, which is only right with both
    // in place. The rendered `str` stays as it was — the Callable does not know
    // the class it was declared in, so `Mu $:` would be a worse answer than
    // leaving the invocant out of the rendering.
    if (c && c->isMethod) {
        bool haveInv = false, haveNamedSlurpy = false;
        for (const Param* pp : ps) {
            if (pp->invocant) haveInv = true;
            if (pp->slurpy && pp->sigil == '%') haveNamedSlurpy = true;
        }
        // …and it counts: Rakudo reports `method file(Bool $v = True)` as count 2
        // / arity 1. Code that asks how many arguments a method takes subtracts
        // one for the invocant — `$method.signature.count - 1` is how Path::Finder
        // tells a matcher that takes a value from one that takes a list, and with
        // the invocant uncounted every one of them looked unusable.
        if (!haveInv) {
            arity++;
            if (!slurpy) count++;
            (*s.hash())["arity"] = Value::integer(arity);
            (*s.hash())["count"] = slurpy ? Value::number(std::numeric_limits<double>::infinity())
                                          : Value::integer(count);
        }
        auto mkParam = [](const std::string& str, const std::string& name,
                          bool invocant, bool slurpy, bool named) {
            Value pv = Value::makeHash(); pv.hashKind = "Parameter";
            (*pv.hash())["str"] = Value::str(str);
            (*pv.hash())["name"] = Value::str(name);
            (*pv.hash())["usage-name"] = Value::str(name.size() > 1 ? name.substr(1) : "");
            (*pv.hash())["type"] = Value::str("Mu");
            (*pv.hash())["type-obj"] = Value::typeObj("Mu");
            (*pv.hash())["invocant"] = Value::boolean(invocant);
            (*pv.hash())["multi-invocant"] = Value::boolean(true);
            (*pv.hash())["named"] = Value::boolean(named);
            (*pv.hash())["slurpy"] = Value::boolean(slurpy);
            (*pv.hash())["optional"] = Value::boolean(named || slurpy);
            (*pv.hash())["raw"] = Value::boolean(invocant);
            (*pv.hash())["readonly"] = Value::boolean(true);
            (*pv.hash())["rw"] = Value::boolean(false);
            (*pv.hash())["copy"] = Value::boolean(false);
            (*pv.hash())["capture"] = Value::boolean(false);
            (*pv.hash())["prefix"] = Value::str(slurpy ? "*" : "");
            (*pv.hash())["suffix"] = Value::str("");
            (*pv.hash())["modifier"] = Value::str("");
            (*pv.hash())["default"] = Value::typeObj("Code");
            (*pv.hash())["constraints"] = Value::typeObj("Mu");
            Value nn = Value::array(); nn.isList = true;
            (*pv.hash())["named_names"] = nn;
            return pv;
        };
        if (!haveInv)
            // anonymous, as Rakudo's implicit invocant is: its .name is ""
            params.arr()->insert(params.arr()->begin(), mkParam("Mu $", "", true, false, false));
        if (!haveNamedSlurpy)
            params.arr()->push_back(mkParam("*%_", "%_", false, true, true));
    }
    (*s.hash())["params"] = params;
    return s;
}

// say/print/put/note honour a user-overridden $*OUT/$*ERR: if the dynamic
// variable holds a user object (e.g. a mock IO capturing output), send the text
// to its .print method; otherwise write straight to the real stream.
// The one lock every runtime write to the process's own streams takes. A
// function-local static rather than a namespace-scope object so it is
// constructed on first use — output can happen during static initialisation,
// and an ordering bug there would be maddening to find.
std::mutex& rtOutMutex() {
    static std::mutex m;
    return m;
}

// ---- IO::Handle.out-buffer ---------------------------------------------------
// $*OUT / $*ERR are synthesized fresh on every read of the dynamic — there is no
// container to write an attribute into — so `.out-buffer` for them lives here,
// one slot per stream, for as long as the process does.
//
// BOTH start at 0, which is what Rakudo reports and how Rakudo behaves: a `say`
// is due the moment it is written. std::cerr already was unbuffered (unitbuf);
// std::cout was not, and on a pipe or a file that meant every `say` sat in its
// block buffer until the program ended — so a rakupp program in a pipeline was
// silent while it ran (issue #51, hit three separate ways). The cost is one
// write(2) per say: a 200k-line filter piped out goes 0.25s -> 0.38s here,
// against 0.36-0.67s for Rakudo doing the same thing. A program that wants the
// block back asks for it — `$*OUT.out-buffer = 65536` — which is more than
// Rakudo offers, since it ignores the setting on its standard handles.
long long& rtStdOutBuffer(bool err) {
    static long long out = 0, er = 0;
    return err ? er : out;
}
// Bytes written to standard output through std::cout, for `$*OUT.tell` on a
// handle that cannot seek: on a tty or a pipe Rakudo answers the count of
// bytes it has sent so far, and S32-io/tell.t asks for that after its TAP
// output. Counted by a streambuf wrapped around cout's own at start-up, so
// every writer counts — the Test module prints straight to std::cout, and
// ioEmit's lock is outside this, so the counter only needs to be atomic.
namespace {
struct CountingOutBuf : std::streambuf {
    std::streambuf* inner;
    std::atomic<long long> n{0};
    explicit CountingOutBuf(std::streambuf* i) : inner(i) {}
    int overflow(int ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof())) return inner->pubsync() == 0 ? 0 : traits_type::eof();
        int r = inner->sputc(traits_type::to_char_type(ch));
        if (!traits_type::eq_int_type(r, traits_type::eof())) ++n;
        return r;
    }
    std::streamsize xsputn(const char* s, std::streamsize c) override {
        std::streamsize w = inner->sputn(s, c);
        if (w > 0) n += w;
        return w;
    }
    int sync() override { return inner->pubsync(); }
};
CountingOutBuf* g_stdoutCounter = nullptr;
}
void rtInstallStdoutCounter() {
    if (g_stdoutCounter) return;
    g_stdoutCounter = new CountingOutBuf(std::cout.rdbuf());
    std::cout.rdbuf(g_stdoutCounter);
}
long long rtStdoutBytesWritten() { return g_stdoutCounter ? g_stdoutCounter->n.load() : 0; }
// $*IN has no output to buffer; it answers 0 and keeps the two output slots
// out of reach — routing it to $*OUT's would let `$*IN.out-buffer = 0` silently
// unbuffer someone else's stream.
static long long g_stdInOutBuffer = 0;

// Rakudo's coercion: :!out-buffer / False is none, True is "the default size",
// an Int is that many bytes. A negative size is no buffer at all.
long long outBufferSize(const Value& v) {
    if (v.t == VT::Bool) return v.truthy() ? kDefaultOutBuffer : 0;
    if (v.t == VT::Any || v.t == VT::Nil) return kDefaultOutBuffer;
    long long n = v.toInt();
    return n < 0 ? 0 : n;
}

long long Interpreter::fhOutBuffer(const Value& h) {
    if (h.t != VT::Hash || !h.hash()) return kDefaultOutBuffer;
    auto st = h.hash()->find("std");
    if (st != h.hash()->end()) {
        const std::string which = st->second.toStr();
        return which == "in" ? g_stdInOutBuffer : rtStdOutBuffer(which == "err");
    }
    auto it = h.hash()->find("out-buffer");
    return it == h.hash()->end() ? kDefaultOutBuffer : it->second.toInt();
}

// Can this handle's pending bytes reach a file at all? An IN-MEMORY handle —
// Proc.out, $*ARGFILES, IO::String, anything opened read-only — has nowhere to
// put them: its "buffer" IS the content, and emptying it to make room would
// simply lose data. Those keep the unlimited buffer they always had.
static bool fhWritesToFile(const ValueMap& m) {
    auto p = m.find("path");
    if (p == m.end() || p->second.toStr().empty()) return false;
    auto mo = m.find("mode");
    if (mo == m.end()) return false;
    const std::string mode = mo->second.toStr();
    return mode == "w" || mode == "a" || mode == "rw" || mode == "update";
}

void Interpreter::fhAppendToFile(const std::shared_ptr<ValueMap>& h, const std::string& s) {
    if (!fhWritesToFile(*h)) return;
    std::string mode = (*h)["mode"].toStr();
    // A read-write handle (`:update`, `:rw`) writes IN PLACE at its write
    // position — overwriting, never truncating — and `.seek` moves it.
    // …and so does a `:w` handle that was `.seek`ed (an `:a` one still appends)
    if (mode == "rw" || mode == "update" || (mode == "w" && h->count("wpos"))) {
        const std::string path = (*h)["path"].toStr();
        { std::ofstream touch(path, std::ios::binary | std::ios::app); }   // `:rw` creates
        std::fstream io(path, std::ios::binary | std::ios::in | std::ios::out);
        long long wpos = 0;
        auto wp = h->find("wpos");
        if (wp != h->end()) wpos = wp->second.toInt();
        // no seek yet: the write follows the READ cursor — `.lines` then `.say`
        // appends after what was read
        else {
            auto ln = h->find("lines"), ps = h->find("pos");
            if (ln != h->end() && ps != h->end() && ln->second.arr()) {
                auto eo = h->find("line-eols");
                long long n = ps->second.toInt();
                auto& L = *ln->second.arr();
                for (long long i = 0; i < n && i < (long long)L.size(); i++) {
                    wpos += (long long)L[(size_t)i].toStr().size();
                    if (eo != h->end() && eo->second.arr() && (size_t)i < eo->second.arr()->size())
                        wpos += (long long)(*eo->second.arr())[(size_t)i].toStr().size();
                }
            }
        }
        if (h->count("rwappend")) {   // `:ra`: always at the end
            std::error_code ec;
            auto sz = std::filesystem::file_size(path, ec);
            wpos = ec ? 0 : (long long)sz;
        }
        if (io) { io.seekp(wpos); io << s; }
        (*h)["wpos"] = Value::integer(wpos + (long long)s.size());
        (*h)["wrote"] = Value::boolean(true);
        return;
    }
    bool wrote = (*h)["wrote"].truthy();   // earlier bytes are already out there
    std::ofstream out((*h)["path"].toStr(),
                      std::ios::binary | ((mode == "a" || wrote) ? std::ios::app : std::ios::trunc));
    if (out) out << s;
    // Even an EMPTY write settles the truncate question: the file has been
    // opened for this handle, so the next one must append rather than start over.
    (*h)["wrote"] = Value::boolean(true);
    if (!s.empty()) h->erase("wpos");   // an `:a` write lands at the end whatever was sought
}

bool Interpreter::fhFlush(const Value& h) {
    if (h.t != VT::Hash || !h.hash()) return false;
    auto st = h.hash()->find("std");
    if (st != h.hash()->end()) {
        std::lock_guard<std::mutex> lk(rtOutMutex());
        if (st->second.toStr() == "err") std::cerr.flush(); else std::cout.flush();
        return true;
    }
    auto m = h.hashS();
    if (!m || !fhWritesToFile(*m)) return false;
    std::lock_guard<std::mutex> lk(rtOutMutex());
    // A COPY, not a reference into the map: fhAppendToFile writes "wrote" back,
    // and an insert can rehash the map out from under a reference to a value in it.
    std::string pending = (*m)["buffer"].s;
    if (pending.empty()) return false;
    (*m)["buffer"] = Value::str("");
    fhAppendToFile(m, pending);
    return true;
}

void Interpreter::fhWrite(const Value& h, const std::string& s) {
    auto m = h.hashS();
    if (!m) return;
    // -1: no file behind this handle, so no size can ever force a write out.
    long long size = fhWritesToFile(*m) ? fhOutBuffer(h) : -1;
    // Appending is a READ-MODIFY-WRITE on state the handle shares, so two
    // threads writing to one file both read the buffer, both append, and one
    // write is simply lost. Under the output lock it is one update at a time.
    std::lock_guard<std::mutex> lk(rtOutMutex());
    {   // Room for it: hold it back. This is the ONLY branch a buffered handle
        // takes, and it is the hot one.
        Value& buf = (*m)["buffer"];
        if (size < 0 || (size > 0 && (long long)(buf.s.size() + s.size()) <= size)) {
            buf = Value::str(buf.s + s);
            return;
        }
    }
    std::string pending = (*m)["buffer"].s;      // copied for the same reason as above
    if (!pending.empty()) { (*m)["buffer"] = Value::str(""); fhAppendToFile(m, pending); }
    // A write at least a bufferful on its own has nothing to gain from the
    // buffer and goes straight out; a smaller remainder starts the next one.
    if (size == 0 || (long long)s.size() >= size) { if (!s.empty()) fhAppendToFile(m, s); }
    else (*m)["buffer"] = Value::str(s);
}

Value Interpreter::fhSetOutBuffer(const Value& h, const Value& n) {
    long long size = outBufferSize(n);
    if (h.t != VT::Hash || !h.hash()) return Value::integer(size);
    auto st = h.hash()->find("std");
    if (st != h.hash()->end()) {
        const std::string which = st->second.toStr();
        if (which == "in") { g_stdInOutBuffer = size; return Value::integer(size); }
        bool err = which == "err";
        rtStdOutBuffer(err) = size;
        // Whatever is already sitting in the stream's own buffer belongs to the
        // OLD size — the point of switching to 0 is that it comes out now.
        std::lock_guard<std::mutex> lk(rtOutMutex());
        if (err) std::cerr.flush(); else std::cout.flush();
        return Value::integer(size);
    }
    fhFlush(h);                                  // the resize itself flushes
    std::lock_guard<std::mutex> lk(rtOutMutex());
    (*h.hash())["out-buffer"] = Value::integer(size);
    return Value::integer(size);
}

Value Interpreter::ioEmit(const std::string& s, const char* dynVar, bool toErr) {
    // Dynamic ($*) lookup: the current lexical scope, then the caller chain.
    Value* h = nullptr;
    if (tctx_.cur) {
        h = tctx_.cur->find(dynVar);
        if (!h)
            for (auto it = tctx_.dynStack.rbegin(); it != tctx_.dynStack.rend(); ++it)
                if (*it && (h = (*it)->find(dynVar))) break;
    }
    // Route to the handle both for a user OBJECT (a custom IO class) and for a
    // real FileHandle — `my $*OUT = open(...); say "x"` writes to the file, as
    // in Rakudo. (The FileHandle arm was missing: rebinding $*OUT silently
    // leaked say/print to stdout — found building -i in-place editing.)
    // For an object, route by what the class DEFINES: its own print method,
    // else its WRITE(Blob) sink (the IO::Handle protocol — Test::Output's
    // capture classes override only WRITE). Routing print at an object that
    // defines neither used to re-enter the global print through the
    // native-parent delegation and recurse to a stack overflow.
    // …and a TYPE OBJECT is a sink too: `$*OUT = class { method print(*@a) {…} }`
    // hands over the class itself, which is the shortest way to capture output
    // and is exactly what IO::Capture::Simple does. Only instances were routed,
    // so every `say` walked past the capture to the real stream.
    if (h && h->t == VT::Type && !h->s.empty()) {
        auto ci = classes_.find(h->s);
        if (ci != classes_.end() && ci->second) {
            if (ci->second->findMethod("print")) {
                ValueList pa{Value::str(s)};
                return methodCall(*h, "print", pa);
            }
            if (Value* wm = ci->second->findMethod("WRITE")) {
                Value blob = Value::str(s);
                blob.hashKind = "Blob";
                return invokeMethod(*wm, *h, ValueList{blob}, nullptr);
            }
        }
    }
    if (h && h->t == VT::Object && h->obj() && h->obj()->cls) {
        // Outside the lock below on purpose: this re-enters the interpreter to
        // run a user method, which may itself print. Holding a non-recursive
        // mutex across that would deadlock.
        if (h->obj()->cls->findMethod("print")) {
            ValueList pa{Value::str(s)};
            return methodCall(*h, "print", pa);
        }
        if (Value* wm = h->obj()->cls->findMethod("WRITE")) {
            Value blob = Value::str(s);
            blob.hashKind = "Blob";
            return invokeMethod(*wm, *h, ValueList{blob}, nullptr);
        }
        // neither print nor WRITE: not a sink — fall through to the real stream
    }
    else if (h && h->hashKind == "FileHandle") {
        ValueList pa{Value::str(s)};
        return methodCall(*h, "print", pa);
    }
    // ONE writer at a time. std::cout guarantees the BYTES of a single `<<` are
    // not interleaved (libc++ writes through the FILE*, which locks), but the
    // stream's own state word is touched by every sentry without
    // synchronisation — ThreadSanitizer reports a data race on any program that
    // prints from two threads, which since v3.0.0 means any program using the
    // default parallelism. A real lock rather than a suppression: it is what
    // makes the report go away honestly, it keeps TSan quiet enough for the
    // NEXT race to be visible, and it guarantees whole-write atomicity on
    // platforms whose streams are not FILE-backed (the WASM build).
    //
    // One mutex for both streams, not one each: stdout and stderr are usually
    // the same terminal or the same redirected file, so interleaving BETWEEN
    // them matters as much as within one.
    {
        std::lock_guard<std::mutex> lk(rtOutMutex());
        std::ostream& os = toErr ? std::cerr : std::cout;
        os << s;
        // out-buffer 0: the bytes are due NOW. Without this a `say` whose output
        // is a pipe rather than a terminal sat in std::cout's block buffer until
        // the program ended, so anything watching the pipe live saw nothing
        // (issue #51 — a runner streaming a child's output through rakupp).
        if (rtStdOutBuffer(toErr) == 0) os.flush();
    }
    return Value::boolean(true);
}

// ---------------- method dispatch ----------------
// quanthash value types: Set weighs on Bool, Bag on UInt, Mix on Real
const char* quantValueType(const std::string& kind) {
    if (kind == "Set" || kind == "SetHash") return "Bool";
    if (kind == "Bag" || kind == "BagHash") return "UInt";
    if (kind == "Mix" || kind == "MixHash") return "Real";
    return nullptr;
}
// `$repo.precomp-repository`: a precompilation repository over a store at
// `dir` — the shape Test::Compile hands straight back to `.need`. Raku++
// compiles on demand, so nothing reads the store; the objects are the API.
static Value precompRepoObj(std::unordered_map<std::string, std::shared_ptr<ClassInfo>>& classes, const std::string& dir) {
    auto so = makePayload<ObjectData>(); so->cls = classes["CompUnit::PrecompilationStore::FileSystem"];
    Value pfx = Value::str(dir); pfx.hashKind = "IO";
    so->attrs["prefix"] = pfx;
    auto ro = makePayload<ObjectData>(); ro->cls = classes["CompUnit::PrecompilationRepository::Default"];
    ro->attrs["store"] = Value::object(so);
    return Value::object(ro);
}
void jsonSkipWs(const std::string& s, size_t& i, const JsonCfg& cfg) {
    for (;;) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
        if (!cfg.jsonc || i + 1 >= s.size() || s[i] != '/') return;
        if (s[i + 1] == '/') { i += 2; while (i < s.size() && s[i] != '\n') i++; }
        else if (s[i + 1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i = i + 1 < s.size() ? i + 2 : s.size();
        }
        else return; // a lone '/' is the next token's parse error, not whitespace
    }
}
static bool jsonParseString(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20) return false; // raw control characters must arrive \u-escaped
        i++;
        if (c != '\\') { out += (char)c; continue; }
        if (i >= s.size()) return false;
        char e = s[i++];
        switch (e) {
            case 'n': out += '\n'; break;  case 't': out += '\t'; break;
            case 'r': out += '\r'; break;  case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;  case '/': out += '/';  break;
            case '"': out += '"';  break;  case '\\': out += '\\'; break;
            case 'u': {
                auto hex4 = [&](size_t p, unsigned& v) {
                    if (p + 4 > s.size()) return false;
                    v = 0;
                    for (int k = 0; k < 4; k++) {
                        char h = s[p + k]; v <<= 4;
                        if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                        else return false;
                    }
                    return true;
                };
                unsigned cp;
                if (!hex4(i, cp)) return false;
                i += 4;
                // a high surrogate followed by \u-escaped low surrogate is ONE
                // astral character (03-unicode.t round-trips flag emoji this way)
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() &&
                    s[i] == '\\' && s[i + 1] == 'u') {
                    unsigned lo;
                    if (hex4(i + 2, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                }
                if (cp >= 0xD800 && cp <= 0xDFFF) return false; // a LONE surrogate is not a character — die, like chr() would
                out += cpToU8(cp);
                break;
            }
            default: return false; // JSON::Fast dies on unknown escapes; so do we
        }
    }
    if (i >= s.size()) return false;
    i++; // closing quote
    return true;
}
bool jsonParseValue(const std::string& s, size_t& i, Value& out, JsonCfg cfg) {
    if (++cfg.depth > 20000) return false; // ~3 MB of C++ frames; no real document nests this deep
    jsonSkipWs(s, i, cfg);
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '"') { std::string str; if (!jsonParseString(s, i, str)) return false; out = Value::str(str); return true; }
    if (c == '{') {
        i++; out = Value::makeHash();
        if (cfg.immutable) out.hashKind = "Map";
        jsonSkipWs(s, i, cfg);
        if (i < s.size() && s[i] == '}') { i++; return true; }
        for (;;) {
            jsonSkipWs(s, i, cfg);
            std::string key; if (!jsonParseString(s, i, key)) return false;
            jsonSkipWs(s, i, cfg);
            if (i >= s.size() || s[i] != ':') return false; i++;
            Value v; if (!jsonParseValue(s, i, v, cfg)) return false;
            (*out.hash())[key] = v;
            jsonSkipWs(s, i, cfg);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; return true; }
            return false;
        }
    }
    if (c == '[') {
        i++; out = Value::array();
        out.isList = cfg.immutable; // List under :immutable, Array otherwise — is-deeply tells them apart
        jsonSkipWs(s, i, cfg);
        if (i < s.size() && s[i] == ']') { i++; return true; }
        for (;;) {
            Value v; if (!jsonParseValue(s, i, v, cfg)) return false;
            out.arr()->push_back(v);
            jsonSkipWs(s, i, cfg);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; return true; }
            return false;
        }
    }
    if (s.compare(i, 4, "true") == 0)  { i += 4; out = Value::boolean(true);  return true; }
    if (s.compare(i, 5, "false") == 0) { i += 5; out = Value::boolean(false); return true; }
    if (s.compare(i, 4, "null") == 0)  { i += 4; out = Value::any();           return true; }
    // number: scan the same loose token JSON::Fast does, then type it exactly
    // like Str.Numeric does — that is what the real module calls on the token.
    if (c != '-' && !ascii::isdigit((unsigned char)c)) return false; // JSON has no leading '+'
    size_t st = i;
    if (c == '-') i++;
    while (i < s.size() && (ascii::isdigit((unsigned char)s[i]) || s[i] == '.' ||
                            s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) i++;
    if (i == st) return false;
    out = numifyStr(s.substr(st, i - st));
    return out.t != VT::Any && out.t != VT::Nil; // "5." scans but does not numify — die like .Numeric
}

static void jfEscape(const std::string& s, std::string& out); // defined below (JSON::Fast codec)
std::string jsonEncode(const Value& v) {
    switch (v.t) {
        case VT::Nil: case VT::Any: case VT::Type: return "null";
        case VT::Bool: return v.b ? "true" : "false";
        case VT::Int:  return v.big() ? v.big()->toString() : std::to_string(v.i);
        case VT::Num: case VT::Rat: {
            // round-trip doubles (default 6 digits silently truncated); cnum so a
            // host's locale can never put a comma into JSON
            char b[40];
            cnum::snprintf(b, sizeof b, "%.17g", v.toNum());
            return b;
        }
        case VT::Array: {
            std::string r = "[";
            if (v.arr()) for (size_t k = 0; k < v.arr()->size(); k++) { if (k) r += ","; r += jsonEncode((*v.arr())[k]); }
            return r + "]";
        }
        case VT::Hash: {
            // keys go through the SAME escaper as string values — a quote or a
            // C0 control in a key used to be concatenated raw, emitting invalid
            // JSON from Rakudo::Internals::JSON.to-json and the dist-META writer
            std::string r = "{"; bool first = true;
            if (v.hash()) for (auto& kv : *v.hash()) {
                if (!first) r += ","; first = false;
                r += "\""; jfEscape(kv.first, r); r += "\":" + jsonEncode(kv.second);
            }
            return r + "}";
        }
        default: { // string (and anything stringy)
            std::string r = "\"";
            jfEscape(v.toStr(), r);
            return r + "\"";
        }
    }
}

// ---- JSON::Fast, natively --------------------------------------------------
// Sparrow6's check engine writes its whole context through JSON::Fast's
// to-json — 10,000 captured lines meant ~700 ms of interpreted jsonify/
// str-escape per task. The module itself stays exactly what the user
// installed (the v3.0.1 unvendoring stands: no pinned source in the binary);
// what changes is the CALL: loadModule wraps the loaded &to-json/&from-json,
// and a call whose arguments this replica covers runs the native codec.
// Anything it does not cover — an unknown adverb, a callable :sorted-keys, a
// NaN/Inf Num (whose rendering hangs on the $*JSON_NAN_INF_SUPPORT dynamic),
// or a type outside the ladder (DateTime, Version, objects) — falls through to
// the module's own sub, so behaviour is the module's in every uncovered case.
// The reason travels with it now: the WRAPPER discards it and hands the call
// back to JSON::Fast, but the PRIMITIVE has no module behind it and has to say
// what it refused.
struct JsonFastUnsupported { const char* why; };

static void jfEscape(const std::string& s, std::string& out) {
    // Measured against the module (the unicode block in the regression file
    // pins the bytes): \t \n \r, quote and backslash by name; other controls
    // as lower-case \u%04x; BMP text RAW — the module walks NFD codepoints,
    // but a Raku Str is NFG, so rebuilding those codepoints composes them
    // straight back to the bytes held here; and an ASTRAL codepoint as an
    // upper-case-hex surrogate PAIR — the one place the module and a raw
    // byte copy genuinely disagree. Falling back on any byte >= 0x80, the
    // old rule, sent Sparrow6-shaped writes to the interpreted module:
    // ~327 ms instead of ~4 ms on the 278 KB diagnose corpus.
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c <= 31) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
            else out += (char)c;
            i++;
            continue;
        }
        int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3
                : (c & 0xF8) == 0xF0 ? 4 : 1;
        if (len == 1 || i + (size_t)len > s.size()) { out += (char)c; i++; continue; } // not UTF-8: the byte, raw
        if (len < 4) { out.append(s, i, (size_t)len); i += (size_t)len; continue; }    // BMP: raw
        unsigned cp = (unsigned)(c & 0x07);
        for (int k = 1; k < 4; k++) cp = (cp << 6) | (unsigned)((unsigned char)s[i + k] & 0x3F);
        unsigned v = cp - 0x10000;
        char b[16];
        snprintf(b, sizeof b, "\\u%04X\\u%04X", 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF));
        out += b;
        i += 4;
    }
}

// `nanInf` is the $*JSON_NAN_INF_SUPPORT dynamic, read once by the caller: it
// is invariant across the recursion, like every option here except `level`.
static void jfEncode(const Value& v, bool pretty, int spacing, bool sortedKeys,
                     bool enumsAsValue, bool nanInf, int level, std::string& out) {
    // jsonify is one big `with obj` — anything undefined lands in its else and
    // renders "null". rtIsDefined is the one rule for that, and it knows what
    // the switch below cannot see: an enum TYPE object materializes as a
    // tagged pair-ARRAY here, not as VT::Type (to-json(Squee) is null, not
    // the member list). Mu is the exception the module itself makes: its
    // to-json takes Any, so to-json(Mu) dies in the BINDER — fall back and
    // let it (the fast-path regression file pins exactly that).
    if (!rtIsDefined(v)) {
        if (v.t == VT::Type && v.s == "Mu") throw JsonFastUnsupported{"cannot serialise Mu, whose type is not Any"};
        out += "null";
        return;
    }
    // the ladder is jsonify's, in jsonify's order
    if (!v.enumName.empty()) {           // enum value: its KEY, or with
        if (!enumsAsValue) {             // :enums-as-value its underlying value
            out += '"'; jfEscape(v.enumName, out); out += '"';
            return;
        }
        // :enums-as-value wants .value, and this Value carries only the
        // ordinal — a string-valued enum (Blerp (One => "Eins")) would come
        // out as its position. The module reads the real .value; let it.
        throw JsonFastUnsupported{"cannot serialise a string-valued enum under :enums-as-value"};
    }
    // an allomorph prints its NUMERIC half — jsonify re-dispatches IntStr /
    // RatStr / NumStr through .Int/.Rat/.Num before formatting, so the Str
    // face must not leak into the number: RatStr.new(0.0, '') is "0.0", not
    // "" plus the ".0" suffix (the module's own roundtrip suite feeds exactly
    // that). Any OTHER kinded numeric (Duration bridges to Num, say) is the
    // module's business.
    if ((v.t == VT::Int || v.t == VT::Rat || v.t == VT::Num) && !v.hashKind.empty()) {
        if (v.hashKind == "IntStr" || v.hashKind == "RatStr" || v.hashKind == "NumStr") {
            Value plain = v;
            plain.hashKind.clear();
            plain.s.clear();
            jfEncode(plain, pretty, spacing, sortedKeys, enumsAsValue, nanInf, level, out);
            return;
        }
        throw JsonFastUnsupported{"cannot serialise a numeric carrying a type tag"};
    }
    switch (v.t) {
        case VT::Nil: case VT::Any: out += "null"; return;
        case VT::Type:
            // jsonify's parameter is Any-constrained at EVERY level, so the one
            // type object it does not render as null is Mu itself — that call
            // dies in the module's own binder, and the module owns that error.
            if (v.s == "Mu") throw JsonFastUnsupported{"cannot serialise Mu, whose type is not Any"};
            out += "null";
            return;
        case VT::Bool: out += v.b ? "true" : "false"; return;
        case VT::Int:
            out += v.big() ? v.big()->toString() : std::to_string(v.i);
            return;
        case VT::Rat: {                  // Rat.Str, plus ".0" when integral
            std::string r = v.toStr();
            out += r;
            if (r.find('.') == std::string::npos) out += ".0";
            return;
        }
        case VT::Num: {                  // Num.Str, plus "e0" when it lacks one
            double d = v.toNum();
            // NaN and Inf are not JSON. The module writes `null` for them, and
            // bare NaN / Inf / -Inf when $*JSON_NAN_INF_SUPPORT is set — output
            // its own parser then refuses, which is the module's business and
            // not ours to improve on. Probed both ways 2026-09-05.
            if (std::isnan(d) || std::isinf(d)) {
                if (!nanInf) { out += "null"; return; }
                out += std::isnan(d) ? "NaN" : (d < 0 ? "-Inf" : "Inf");
                return;
            }
            std::string r = v.toStr();
            out += r;
            if (r.find('e') == std::string::npos && r.find('E') == std::string::npos)
                out += "e0";
            return;
        }
        case VT::Str:
            if (!v.hashKind.empty()) throw JsonFastUnsupported{"cannot serialise a Str carrying a type tag (Buf, IO::Path, …)"};
            out += '"'; jfEscape(v.s, out); out += '"';
            return;
        case VT::Pair: {                 // a Pair is Associative: one-entry object
            if (v.pairKey()) throw JsonFastUnsupported{"cannot serialise a Pair whose key is not a Str"};
            std::string open = "{", close = "}";
            if (pretty) {
                std::string ind((size_t)(spacing * (level + 1)), ' ');
                std::string outd((size_t)(spacing * level), ' ');
                out += "{\n" + ind + '"'; jfEscape(v.s, out); out += "\": ";
                jfEncode(v.pairVal() ? *v.pairVal() : Value::any(), pretty, spacing, sortedKeys, enumsAsValue, nanInf, level + 1, out);
                out += "\n" + outd + "}";
            } else {
                out += "{\""; jfEscape(v.s, out); out += "\":";
                jfEncode(v.pairVal() ? *v.pairVal() : Value::any(), pretty, spacing, sortedKeys, enumsAsValue, nanInf, level, out);
                out += "}";
            }
            return;
        }
        case VT::Range: {
            Value a = Value::array(); *a.arr() = v.flatten();
            jfEncode(a, pretty, spacing, sortedKeys, enumsAsValue, nanInf, level, out);
            return;
        }
        case VT::Array: {
            if (!v.arr()) { out += pretty ? "[\n]" : "[]"; return; }
            auto& xs = *v.arr();
            if (pretty) {
                // JSON::Fast's exact shape: "[\n<ind>… ,\n<ind>… \n<outd>]",
                // and an EMPTY array renders as "[\n<outd>]"
                std::string ind((size_t)(spacing * (level + 1)), ' ');
                std::string outd((size_t)(spacing * level), ' ');
                out += "[";
                if (xs.empty()) { out += "\n" + outd + "]"; return; }
                for (size_t i = 0; i < xs.size(); i++) {
                    out += (i ? ",\n" + ind : "\n" + ind);
                    jfEncode(xs[i], pretty, spacing, sortedKeys, enumsAsValue, nanInf, level + 1, out);
                }
                out += "\n" + outd + "]";
            } else {
                out += "[";
                for (size_t i = 0; i < xs.size(); i++) {
                    if (i) out += ",";
                    jfEncode(xs[i], pretty, spacing, sortedKeys, enumsAsValue, nanInf, level, out);
                }
                out += "]";
            }
            return;
        }
        case VT::Hash: {
            // allomorphs re-dispatch on their numeric half; DateTime, Version,
            // Supply and every other kinded hash is outside the ladder
            if (v.hashKind == "IntStr" || v.hashKind == "RatStr" || v.hashKind == "NumStr")
                throw JsonFastUnsupported{"cannot serialise a Hash carrying an allomorph tag"};
            if (!v.hashKind.empty()) throw JsonFastUnsupported{"cannot serialise a Hash carrying a type tag (Date, DateTime, …)"};
            // an object hash (`:{ 1 => 1 }`) stores each key under its WHICH
            // ("Int|1"); the module writes `.key.Str`, which only it can reach
            if (v.objKeyed) throw JsonFastUnsupported{"cannot serialise an object hash"};
            if (!v.hash()) { out += pretty ? "{\n}" : "{}"; return; }
            auto& h = *v.hash();
            // hashes iterate in INSERTION order here; :sorted-keys sorts by
            // key, exactly like the module's `.sort(*.key)`
            std::vector<std::pair<const std::string*, const Value*>> kvs;
            for (auto& kv : h) kvs.emplace_back(&kv.first, &kv.second);
            if (sortedKeys)
                std::sort(kvs.begin(), kvs.end(),
                          [](auto& a, auto& b) { return *a.first < *b.first; });
            if (pretty) {
                std::string ind((size_t)(spacing * (level + 1)), ' ');
                std::string outd((size_t)(spacing * level), ' ');
                out += "{";
                if (kvs.empty()) { out += "\n" + outd + "}"; return; }
                bool first = true;
                for (auto& kv : kvs) {
                    out += (first ? "\n" + ind : ",\n" + ind); first = false;
                    out += '"'; jfEscape(*kv.first, out); out += "\": ";
                    jfEncode(*kv.second, pretty, spacing, sortedKeys, enumsAsValue, nanInf, level + 1, out);
                }
                out += "\n" + outd + "}";
            } else {
                out += "{"; bool first = true;
                for (auto& kv : kvs) {
                    if (!first) out += ",";
                    first = false;
                    out += '"'; jfEscape(*kv.first, out); out += "\":";
                    jfEncode(*kv.second, pretty, spacing, sortedKeys, enumsAsValue, nanInf, level, out);
                }
                out += "}";
            }
            return;
        }
        default: throw JsonFastUnsupported{"cannot serialise a value outside the JSON ladder (an object, a Code, …)"};
    }
}

// $*JSON_NAN_INF_SUPPORT, read once per call. Absent or false means the module's
// default, which is to write `null`.
static bool jsonNanInfWanted(Interpreter&) {
    // findDynamicLenient, not a lexical find: $* variables are DYNAMICALLY
    // scoped, so the binding lives in a caller's frame rather than in whatever
    // scope this codec happens to be running in. A lexical lookup found it when
    // the program set it beside the call and missed it the moment a module sat
    // in between — which is every real use of the dynamic, and is what
    // JSON::Native's own suite caught.
    Value* d = Interpreter::findDynamicLenient("$*JSON_NAN_INF_SUPPORT");
    return d && d->truthy();
}
Value jsonToJsonBody(Interpreter& I, ValueList& a, const JsonUncovered& uncovered) {
    const Value* obj = nullptr;
    bool pretty = true; long long level = 0, spacing = 2; bool enumsAsValue = false;
    bool sortedKeys = false;
    for (auto& x : a) {
        if (x.t == VT::Pair && x.namedArg) {
            bool tv = x.pairVal() && x.pairVal()->truthy();
            if (x.s == "pretty") pretty = tv;
            else if (x.s == "level") level = x.pairVal() ? x.pairVal()->toInt() : 0;
            else if (x.s == "spacing") spacing = x.pairVal() ? x.pairVal()->toInt() : 2;
            else if (x.s == "enums-as-value") enumsAsValue = tv;
            else if (x.s == "sorted-keys") {
                // A CALLABLE comparator is not covered by either yet — the
                // wrapper delegates, the primitive raises. DATA-PLAN P1 lists
                // it as the primitive's to implement, and it needs the key sort
                // inside jfEncode to be able to call back into Raku.
                if (x.pairVal() && x.pairVal()->t == VT::Code)
                    return uncovered("a Callable :sorted-keys comparator is not supported");
                sortedKeys = tv;
            }
            else return uncovered("no adverb of that name");
        }
        else if (!obj) obj = &x;
        else return uncovered("more than one positional argument");
    }
    if (!obj) return uncovered("no value to serialise");
    try {
        std::string out;
        jfEncode(*obj, pretty, (int)spacing, sortedKeys, enumsAsValue,
                 jsonNanInfWanted(I), (int)level, out);
        return Value::str(out);
    } catch (JsonFastUnsupported& u) {
        return uncovered(u.why);
    }
}

// Holds the composed "where" text for the duration of the uncovered() call —
// the callback takes a const char*, and the wrapper discards it anyway.
static thread_local std::string jsonParseWhere;
Value jsonFromJsonBody(Interpreter& I, ValueList& a, const JsonUncovered& uncovered) {
    (void)I;
    const Value* text = nullptr;
    JsonCfg cfg;
    for (auto& x : a) {
        if (x.t == VT::Pair && x.namedArg) {
            bool tv = x.pairVal() && x.pairVal()->truthy();
            if (x.s == "immutable") cfg.immutable = tv;
            else if (x.s == "allow-jsonc") cfg.jsonc = tv;
            else return uncovered("no adverb of that name");
        }
        else if (!text) text = &x;
        else return uncovered("more than one positional argument");
    }
    if (!text || !(text->t == VT::Str && text->hashKind.empty()))
        return uncovered("expected a Str");   // Str() coercion is the module's
    size_t i = 0; Value out;
    // Where it went wrong, which JSON::Fast's own message does not say. `i` is
    // the byte the parser stopped at; line and column are counted from there.
    auto whereAt = [&](size_t at) {
        size_t line = 1, col = 1;
        for (size_t k = 0; k < at && k < text->s.size(); k++) {
            if (text->s[k] == '\n') { line++; col = 1; } else col++;
        }
        return " at byte " + std::to_string(at) +
               " (line " + std::to_string(line) + ", column " + std::to_string(col) + ")";
    };
    if (!jsonParseValue(text->s, i, out, cfg)) {
        jsonParseWhere = "malformed JSON" + whereAt(i);
        return uncovered(jsonParseWhere.c_str());
    }
    jsonSkipWs(text->s, i, cfg);
    if (i != text->s.size()) {
        jsonParseWhere = "trailing content after the value" + whereAt(i);
        return uncovered(jsonParseWhere.c_str());
    }
    return out;
}

Value jsonFastToJsonCall(Interpreter& I, ValueList& a, const Value& orig) {
    return jsonToJsonBody(I, a, [&](const char*) { return I.callCallable(orig, a); });
}

Value jsonFastFromJsonCall(Interpreter& I, ValueList& a, const Value& orig) {
    return jsonFromJsonBody(I, a, [&](const char*) { return I.callCallable(orig, a); });
}

// Called by loadModule right after JSON::Fast's tree has executed: wrap the
// module's own subs so both the EXPORT protocol (which hands out
// &JSON::Fast::to-json) and qualified calls resolve to the wrapped ones.
void Interpreter::wrapJsonFastExports(Env& moduleEnv) {
    auto wrap = [&](const char* name, Value (*fn)(Interpreter&, ValueList&, const Value&)) {
        // The subs live inside `module JSON::Fast { ... }`, so by the time the
        // tree has executed they exist as the QUALIFIED globals (and that
        // qualified spelling is what the module's own EXPORT sub hands out);
        // a bare moduleEnv entry is wrapped too when present.
        std::string qual = std::string("&JSON::Fast::") + (name + 1);
        Value* slot = nullptr;
        auto it = moduleEnv.vars.find(name);
        if (it != moduleEnv.vars.end() && it->second.t == VT::Code) slot = &it->second;
        Value* gslot = global_ ? global_->find(qual) : nullptr;
        if (!slot && !(gslot && gslot->t == VT::Code)) return;
        Value orig = slot ? *slot : *gslot;
        if (orig.code() && orig.code()->builtin) return;   // already wrapped
        Value w; w.t = VT::Code; w.setCode(std::make_shared<Callable>());
        w.code()->name = name + 1;                          // drop the '&'
        w.code()->builtin = [orig, fn](Interpreter& I2, ValueList& args) -> Value {
            return fn(I2, args, orig);
        };
        if (slot) *slot = w;
        if (gslot && gslot->t == VT::Code) *gslot = w;
        else if (global_) global_->define(qual, w);
    };
    wrap("&to-json", jsonFastToJsonCall);
    wrap("&from-json", jsonFastFromJsonCall);
}


// Does the user-declared method `m` on this invocant take a Junction WHOLE at
// argument position `ai`? A parameter typed `Mu` or `Junction` (or a slurpy)
// accepts one, and Rakudo hands the junction over intact instead of
// autothreading — `method name(Mu $name)` is exactly how Path::Finder takes
// `"*.pm" | "*.pod"` and threads it itself, one layer down. Same rule
// callCallable already applies to plain subs.
bool Interpreter::methodTakesJunction(const Value& inv, const std::string& m, size_t ai) {
    ClassInfo* cls = nullptr;
    if (inv.t == VT::Object && inv.obj()) cls = inv.obj()->cls.get();
    else if (inv.t == VT::Type) {
        auto it = classes_.find(inv.s.str());
        if (it != classes_.end()) cls = it->second.get();
    }
    if (!cls) return false;
    Value* mv = cls->findMethod(m);
    if (!mv || mv->t != VT::Code || !mv->code()) return false;
    std::vector<const Value*> cands;
    if (mv->code()->isMultiDispatcher) for (auto& c : mv->code()->candidates) cands.push_back(&c);
    else cands.push_back(mv);
    for (const Value* cv : cands) {
        if (!cv->code() || !cv->code()->params) continue;
        size_t seen = 0;
        for (const Param& p : *cv->code()->params) {
            if (p.named || p.invocant) continue;
            if (p.slurpy) return true;
            if (seen == ai) { if (p.type == "Mu" || p.type == "Junction") return true; break; }
            seen++;
        }
    }
    return false;
}

// `.kv`/`.keys`/`.values`/`.pairs`/`.antipairs` answer a Seq on EVERY container in
// Rakudo — Hash, Array, List, Pair, Match alike. Marking them at the one dispatch
// point keeps that uniform instead of tagging a dozen construction sites.
// The key/value family answers a LIST, not a Seq, whenever the invocant is not
// a collection: `42.values` is `(42,)` and `Any.keys` is `()` — both Lists,
// while `(1, 2).values` and `42.keys` are Seqs (Nil-Any sheet NA-04, NA-16,
// NA-18). Rakudo reaches the first through `Any.list`, which is a List, and the
// second through an iterator. An ENUM is a collection of its table and keeps
// its Seq, so Bool is excluded.
static bool kvFamilyAnswersList(const Value& inv, const std::string& m) {
    // An enum VALUE's `.kv` is its own (name, number) List — see the arm that
    // builds it. Only the enum TABLE views (keys/values/pairs) are Seqs.
    if (m == "kv" && !inv.enumName.empty() && inv.t != VT::Array) return true;
    const bool enumish = !inv.enumName.empty() || inv.t == VT::Bool ||
                         (inv.t == VT::Type && inv.s == "Bool");
    if (enumish) return false;
    // `.values` is the narrowest of the family: it is a Seq only where the type
    // OVERRIDES it with an iterator — List/Array/Slip, a Capture, a real Hash or
    // QuantHash, a Pair, a Match. Everything ELSE reaches `Any.values`, which is
    // `self.list`, so it is a List — and that includes three kinds that look like
    // collections and are not: a SEQ (Seq is not a List subclass, so it inherits
    // Any's), a RANGE, and any object carrying a hash inside (Date, DateTime,
    // Supply, Promise) or none at all (a plain class instance, whose `.values` is
    // the one-element list of itself).
    if (m == "values") {
        if (inv.t == VT::Array) return inv.s == "Seq";
        if (inv.t == VT::Hash) {
            static const std::set<std::string> hashy = {
                "", "Hash", "Map", "Stash", "Set", "SetHash",
                "Bag", "BagHash", "Mix", "MixHash"};
            return !hashy.count(inv.hashKind);
        }
        return inv.t != VT::Pair && inv.t != VT::Match;
    }
    if (m == "keys" || m == "kv" || m == "pairs" || m == "antipairs" || m == "invert")
        return inv.t == VT::Type || inv.t == VT::Any || inv.t == VT::Nil;
    return false;
}

Value Interpreter::methodCall(const Value& inv, const std::string& m, ValueList args, const std::vector<ExprPtr>* rwArgs,
                              bool skipOwn) {
    // A construction whose BUILD/TWEAK answered a Failure answers that Failure
    // (BuildFailureEx, from the hook runner). The catch is armed once per
    // `.new`/`.bless` — a nested construction inside a BUILD arms its own —
    // and every other method pays one short string compare, no TLS access.
    if (m.size() >= 3 && m.size() <= 5 && (opEq(m, "new") || opEq(m, "bless"))) {
        ExecContext& t = tctx_;
        if (!t.ctorCatchSkip) {
            t.ctorCatchSkip = true;
            t.ctorCatchDepth++;
            struct G { ExecContext& t; ~G() { t.ctorCatchSkip = false; t.ctorCatchDepth--; } } g{t};
            try { return methodCall(inv, m, std::move(args), rwArgs, skipOwn); }
            catch (BuildFailureEx& bf) { return bf.failure; }
        }
        t.ctorCatchSkip = false;
    }
    // An ENUM's type object is its tagged pair list here, but it renders as the
    // TYPE it is: `day.gist` is (day), `.raku` day, and `.Str` the empty string
    // with the uninitialized warning any type object gives (Rakudo)
    if (inv.t == VT::Array && !inv.enumType.empty() && inv.enumName.empty() &&
        (opEq(m, "gist") || opEq(m, "raku") || opEq(m, "perl") || opEq(m, "Str")) && args.empty() && isEnumTypeObject(inv)) {
        const std::string tn = inv.enumType.str();
        if (opEq(m, "gist")) return Value::str("(" + tn + ")");
        if (opEq(m, "raku") || opEq(m, "perl")) return Value::str(tn);
        warnUninit("Use of uninitialized value of type " + tn + " in string context.\n"
                   "Methods .^name, .raku, .gist, or .say can be used to stringify it to something meaningful.");
        return Value::str("");
    }
    // `$cool.printf` / `.sprintf` on a user class that `is Cool`: the object's
    // OWN .Str is the format, as Cool's methods are written (`self.Str`)
    if ((opEq(m, "printf") || opEq(m, "sprintf")) && inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
        !inv.obj()->cls->findMethodForCall(m)) {
        bool cool = false;
        for (ClassInfo* c = inv.obj()->cls.get(); c && !cool; c = c->parent.get()) cool = c->nativeParent == "Cool";
        auto bit = builtins_.find(m);
        if (cool && bit != builtins_.end()) {
            ValueList a2; a2.push_back(Value::str(strOf(inv)));
            for (auto& x : args) a2.push_back(x);
            return bit->second(*this, a2);
        }
    }
    // A list that holds CONTAINERS is read by its VALUES: every built-in method
    // walks elements raw. The mutators are the exception — they change the
    // list itself, and a copy would swallow the change.
    if (inv.t == VT::Array && inv.holdsContainers() && inv.arr()) {
        static const std::set<std::string> kMutators = {
            "push", "pop", "shift", "unshift", "append", "prepend", "splice",
            "ASSIGN-POS", "BIND-POS", "DELETE-POS", "STORE", "VAR", "WHERE", "WHICH"};
        if (!kMutators.count(m))
            return methodCall(decontList(inv), m, std::move(args), rwArgs, skipOwn);
    }
    // `.^roles` of a CORE numeric or string type: the roles its class does
    // (`42.2.^roles.grep(Rational)`, Rakudo's lists, most specific first)
    if (opEq(m, "^roles") && args.empty()) {
        std::string tn = inv.t == VT::Type ? inv.s.str() : inv.typeName();
        const std::vector<const char*>* rl = nullptr;
        static const std::vector<const char*> kRat{"Rational", "Real", "Numeric"};
        static const std::vector<const char*> kReal{"Real", "Numeric"};
        static const std::vector<const char*> kNum{"Numeric"};
        static const std::vector<const char*> kStr{"Stringy"};
        if (tn == "Rat" || tn == "FatRat") rl = &kRat;
        else if (tn == "Int" || tn == "Num") rl = &kReal;
        else if (tn == "Complex") rl = &kNum;
        else if (tn == "Str") rl = &kStr;
        if (rl && !classes_.count(tn)) {
            Value out = Value::array(); out.isList = true;
            for (const char* r : *rl) out.arr()->push_back(Value::typeObj(r));
            return out;
        }
    }
    // `.squish` of an ENDLESS source stays lazy: it is a grep that remembers
    // the previous key (`squish 1..Inf` is lazy, as Rakudo's is)
    if (opEq(m, "squish") &&
        ((inv.t == VT::Range && inv.rTo() >= 9000000000000000000LL) ||
         (inv.t == VT::Array && inv.arr() && inv.ext() &&
          std::static_pointer_cast<LazySeqState>(inv.ext())->infinite))) {
        Value asF, withF;
        for (auto& a : args)
            if (a.t == VT::Pair && a.pairVal() && a.pairVal()->t == VT::Code) {
                if (a.s == "as") asF = *a.pairVal(); else if (a.s == "with") withF = *a.pairVal();
            }
        auto st = std::make_shared<std::pair<bool, Value>>(true, Value());
        Value filt; filt.t = VT::Code; filt.setCode(std::make_shared<Callable>());
        filt.code()->builtin = [st, asF, withF](Interpreter& I, ValueList& a) -> Value {
            Value v = a.empty() ? Value::any() : a[0];
            Value k = asF.t == VT::Code ? I.callCallable(asF, ValueList{v}) : v;
            bool keep = st->first ||
                !(withF.t == VT::Code ? I.callCallable(withF, ValueList{st->second, k}).truthy()
                                      : applyArith("===", k, st->second).truthy());
            st->first = false; st->second = k;
            return Value::boolean(keep);
        };
        ValueList ga{filt};
        return methodCall(inv, "grep", std::move(ga));
    }
    // a type made by a user metaobject answers through it (CustomHow.cpp)
    if (haveCustomHows_.load(std::memory_order_relaxed) ||
        (inv.t == VT::Type && inv.s == "Metamodel::Primitives")) {
        Value out;
        if (customHowMethod(inv, m, args, out)) return out;
    }
    // Built-in methods called with arguments no candidate takes: Rakudo's
    // dispatcher answers X::Multi::NoMatch (roast APPENDICES multi-no-match.t)
    {
        auto noMatch = [&](const std::string& what) -> Value {
            throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                "Cannot resolve caller " + m + "(" + what + "); none of these signatures matches"};
        };
        size_t npos = 0, nnamed = 0;
        for (auto& x : args) { if (x.t == VT::Pair && x.namedArg) nnamed++; else npos++; }
        auto intish = [](const Value& v) {
            return v.t == VT::Int || v.t == VT::Whatever || v.t == VT::Code || v.t == VT::Bool ||
                   (v.t == VT::Type && (v.s == "Int" || v.s == "Whatever"));
        };
        if (opEq(m, "splice") && inv.t == VT::Array && !inv.isList && npos >= 1) {
            ValueList pa; for (auto& x : args) if (!(x.t == VT::Pair && x.namedArg)) pa.push_back(x);
            if (!intish(pa[0]) || (pa.size() >= 2 && !intish(pa[1]))) noMatch(inv.typeName() + ", …");
        }
        else if (opEq(m, "protect") && inv.t == VT::Type && (inv.s == "Lock" || inv.s == "Lock::Async"))
            noMatch(inv.s.str() + ", …");
        else if (opEq(m, "new") && inv.t == VT::Type) {
            const std::string tn = inv.s.str();
            if (tn == "Proc::Async" && npos == 0 && !classes_.count(tn)) noMatch("Proc::Async");
            else if (tn == "Junction" && !classes_.count(tn)) {
                // Junction.new(@values, :type) or Junction.new("any", @values)
                ValueList pa; bool hasType = false;
                for (auto& x : args) { if (x.t == VT::Pair && x.namedArg) { if (x.s == "type") hasType = true; } else pa.push_back(x); }
                bool ok = (pa.size() == 1 && (pa[0].t == VT::Array || pa[0].t == VT::Range) && hasType) ||
                          (pa.size() == 2 && pa[0].t == VT::Str);
                if (!ok) noMatch("Junction");
            }
            else if (tn == "Pair" && !classes_.count(tn) && (npos > 2 || (npos == 0 && nnamed > 0 &&
                      !(nnamed == 2)))) noMatch("Pair");
            else if (tn == "Int" && !classes_.count(tn) && npos > 1) noMatch("Int");
        }
        else if ((opEq(m, "subst") || opEq(m, "match")) && inv.t == VT::Str && inv.hashKind.empty()) {
            if (npos == 0 || (opEq(m, "match") && (args[0].t == VT::Nil || args[0].t == VT::Any))) noMatch("Str, …");
        }
        else if ((opEq(m, "words") || opEq(m, "lines")) && (inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Rat) && npos > 1)
            noMatch(inv.typeName() + ", …");
        else if (opEq(m, "printf") && inv.t == VT::Hash && inv.hash() && inv.hash()->count("std") && npos == 0)
            noMatch("IO::Handle");
    }
    // an ENUM type's `.^language-revision` — the revision it was declared under
    if (inv.t == VT::Array && !inv.enumType.empty() && opEq(m, "language-revision") && args.empty()) {
        auto it = enumLangRev_.find(inv.enumType);
        int r = it != enumLangRev_.end() ? it->second : langRev_;
        return Value::str(r == 0 ? "c" : r == 1 ? "d" : "e");
    }
    // `X::NYI.die` — throwing wants an exception INSTANCE, not its type object
    if (inv.t == VT::Type && (opEq(m, "fail") || opEq(m, "die") || opEq(m, "throw") || opEq(m, "rethrow") || opEq(m, "resume")) &&
        (inv.s == "Exception" || inv.s.rfind("X::", 0) == 0))
        throwTypedV("X::Parameter::InvalidConcreteness",
            {{"expected", Value::typeObj(std::string(inv.s.c_str()))}, {"got", inv},
             {"routine", Value::str(m)}, {"param", Value::str("self")},
             {"should-be-concrete", Value::boolean(true)}, {"param-is-invocant", Value::boolean(true)}},
            "Invocant of method '" + m + "' must be an object instance of type '" + std::string(inv.s.c_str()) +
            "', not a type object of type '" + std::string(inv.s.c_str()) + "'.  Did you forget a '.new'?");
    // `Mu.new(1)` — the default constructor takes named arguments only
    if (opEq(m, "new") && inv.t == VT::Type && inv.s == "Mu")
        for (auto& a : args)
            if (!(a.t == VT::Pair && a.namedArg))
                throwTypedV("X::Constructor::Positional", {{"type", Value::typeObj("Mu")}},
                            "Default constructor for 'Mu' only takes named arguments");
    // A handle's lazy `.lines` is read in full before a list method works on
    // it (`$fh.lines.grep(…)`): only `for` and subscripts walk it line by line
    // (…but not for its INDEX views: `.kv`/`.pairs`/`.antipairs` pull as they
    // are read, so `for $fh.lines.kv -> \k, \v { last }` leaves the rest unread)
    if (inv.t == VT::Array && inv.ext() && inv.arr() &&
        std::static_pointer_cast<LazySeqState>(inv.ext())->finiteSource &&
        m != "kv" && m != "pairs" && m != "antipairs")
        forceLazy(inv);
    // A JUNCTION argument autothreads: `$s.contains(none "01")` is a junction of
    // the per-eigenstate answers, which collapses later. This used to live only in
    // the MethodCall eval arm, so every internal caller lost it — the one that
    // reported it was a curried `*.contains(none "01")`, whose WhateverCode calls
    // methodCall directly and got a plain (wrong) Bool, so a sequence using it as
    // its endpoint never terminated (issue #22).
    //
    // MATCHER positions are exempt: a junction handed to grep/first/match is a
    // smartmatch target, not a value to thread over.
    for (size_t ai = 0; ai < args.size(); ai++) {
        if (!isJunction(args[ai])) continue;            // the common path stops here
        // Exactly the list the eval arm carried before this moved here — no
        // additions: this rule's job is to run everywhere, not to change.
        static const std::set<std::string> junctionMatcherMethods = {
            "grep", "first", "classify", "categorize", "index-of", "split", "comb", "match", "subst"};
        if (m.empty() || m[0] == '^' || junctionMatcherMethods.count(m)) break;
        // a HANDLE's output methods take `**@text`: the junction is printed
        // whole, as its gist (`$*OUT.say: (1, 2).all` is one line, `all(1, 2)`),
        // not once per eigenstate (roast S16-io/print.t)
        if (inv.t == VT::Hash && inv.hashKind == "FileHandle" &&
            (opEq(m, "say") || opEq(m, "print") || opEq(m, "note") || opEq(m, "put"))) break;
        if (methodTakesJunction(inv, m, ai)) continue;  // the signature asked for it whole
        Value jr = Value::array(); jr.enumName = args[ai].enumName; jr.isList = true;
        for (auto& e : *args[ai].arr()) {
            ValueList a2 = args; a2[ai] = e;
            jr.arr()->push_back(methodCall(inv, m, a2));
        }
        return jr;
    }
    // `.sort(:k)` is the one member of this family that answers a LIST — it
    // reports positions, not a re-ordered sequence (List-Array sheet LA-35).
    bool sortK = false;
    if (!args.empty() && opEq(m, "sort"))
        for (auto& av : args)
            if (av.t == VT::Pair && av.namedArg && av.s == "k")
                sortK = !av.pairVal() || av.pairVal()->truthy();
    // `Empty` short-circuits the list methods that keep their invocant's type:
    // `Empty.map({…})` is Empty, not `().Seq` (sheet LA-06). Measured against
    // Rakudo 2026.08 — map/grep/sort/list/unique answer Empty, while
    // reverse/flat/values/kv/Seq answer a plain empty Seq and .List answers ().
    if (inv.t == VT::Array && inv.arr() && inv.arr()->empty() && inv.s == "Slip" &&
        (opEq(m, "map") || opEq(m, "grep") || opEq(m, "sort") || opEq(m, "list") || opEq(m, "unique")))
        return emptySlipSingleton();
    Value r = methodCallInner(inv, m, std::move(args), rwArgs, skipOwn);
    if (r.t == VT::Array && r.isList && r.s.empty() && !sortK &&
        (opEq(m, "kv") || opEq(m, "keys") || opEq(m, "values") || opEq(m, "pairs") ||
         opEq(m, "antipairs") || opEq(m, "invert") ||
         opEq(m, "reverse") || opEq(m, "rotate") || opEq(m, "sort") || opEq(m, "unique") || opEq(m, "squish") ||
         opEq(m, "head") || opEq(m, "tail") || opEq(m, "skip") || opEq(m, "rotor") || opEq(m, "batch") ||
         opEq(m, "toggle") || opEq(m, "collate") || opEq(m, "repeated")) &&
        !kvFamilyAnswersList(inv, m))
        r.s = "Seq";
    // …and when the answer is a List, say so even if an inner delegation
    // already marked it: `42.values` reaches List.values, which IS a Seq, but
    // the answer to `42.values` is the one-element List (Nil-Any sheet NA-18).
    else if (r.t == VT::Array && r.isList && r.s == "Seq" && kvFamilyAnswersList(inv, m))
        r.s.clear();
    return r;
}

// A method `augment`-ed onto a BUILT-IN type: they are parked in builtinExt_ (keyed
// by type name), and the native ancestry is walked so augmenting Cool/Any reaches
// Int/Str too. Native values and type objects consult this ahead of the built-in
// method table.
Value jsonParseDoc(const std::string& text) {
    size_t i = 0; Value out;
    if (!jsonParseValue(text, i, out, JsonCfg{})) return Value::any();
    return out;
}

Value* Interpreter::builtinExtMethod(const Value& inv, const std::string& m) {
    if (builtinExt_.empty() || inv.t == VT::Object) return nullptr;
    std::string tn = inv.t == VT::Type ? inv.s : inv.typeName();
    auto lookup = [&](const std::string& t) -> Value* {
        auto ti = builtinExt_.find(t);
        if (ti == builtinExt_.end()) return nullptr;
        auto mi = ti->second.find(m);
        return mi == ti->second.end() ? nullptr : &mi->second;
    };
    if (Value* f = lookup(tn)) return f;
    for (const std::string& anc : typeAncestry(tn))
        if (anc != tn) if (Value* f = lookup(anc)) return f;
    return nullptr;
}

// How a constructed Parameter renders. A nameless one is its TYPE inside a
// signature — `(uint32, Str --> Int)` — and `Type $` on its own, which is the
// difference between Signature.gist and Parameter.gist; both spellings are what
// a reader sees, so both are produced from the one place.
static std::string ctorParamStr(const Value& p, bool inSignature) {
    if (!p.hash()) return "$";
    auto S = [&](const char* k) {
        auto it = p.hash()->find(k); return it == p.hash()->end() ? std::string() : it->second.toStr();
    };
    auto B = [&](const char* k) {
        auto it = p.hash()->find(k); return it != p.hash()->end() && it->second.truthy();
    };
    std::string type = S("type"), name = S("name"), out;
    if (name.empty()) {
        if (inSignature) return type.empty() ? "Any" : type;
        return (type.empty() || type == "Any" ? "" : type + " ") + "$";
    }
    if (!type.empty() && type != "Any") out = type + " ";
    if (B("slurpy")) out += "*";
    if (B("named")) out += ":";
    out += name;
    if (B("optional") && !B("named")) out += "?";
    return out;
}
extern const char* const kCatHandleSrc;                                          // CatHandleSrc.cpp

Value Interpreter::methodCallInner(const Value& invIn, const std::string& mName, ValueList args, const std::vector<ExprPtr>* rwArgs,
                                   bool skipOwn) {
    // The invocant arrives BY REFERENCE. It used to be by value, which cost a
    // 376-byte copy and up to eleven atomic refcount bumps on every method call —
    // to serve the handful of arms that actually rewrite it (the class-alias
    // rewrite just below, a Supply drain, one numeric coercion). Those few take a
    // copy on demand through `mutInv()`; everything else reads `inv` and pays
    // nothing. The compiler enforces it: `inv` is a const reference, so a write
    // that forgets to go through mutInv() will not compile.
    // Exactly one rewrite happens before dispatch — the package-relative alias
    // below — so it is done here, into a copy, and `inv` is bound afterwards.
    // std::optional, not a plain Value: a default-constructed Value is 376 bytes,
    // five std::strings and eleven shared_ptrs, and this runs on EVERY method call.
    // The optional's empty state is a flag — the Value is only built on the rare
    // path below that actually needs one.
    // A PseudoStash answers its key protocol live (its class methods) and
    // every other Hash method from the snapshot it carries
    if (invIn.t == VT::Object && invIn.obj() && invIn.obj()->cls &&
        invIn.obj()->cls->name == "PseudoStash" && !invIn.obj()->cls->methods.count(mName) &&
        !mName.empty() && mName[0] != '^' && mName != "WHAT" && mName != "HOW" &&
        mName != "raku" && mName != "gist" && mName != "Str") {
        auto it = invIn.obj()->attrs.find("snap");
        if (it != invIn.obj()->attrs.end()) return methodCall(it->second, mName, std::move(args));
    }
    // …and a path is a NUMBER by its basename: `$dir.add('3.5').Numeric` is 3.5
    // (S32-io/io-path.t; IO::Path.Numeric numifies .basename)
    if ((mName == "Numeric" || mName == "Rat" || mName == "Num" || mName == "Int" || mName == "FatRat" ||
         mName == "Real") && args.empty() && invIn.t == VT::Str && invIn.hashKind == "IO") {
        ValueList none;
        Value base = methodCall(invIn, "basename", none);
        return methodCall(Value::str(base.toStr()), mName, none);
    }
    // .succ / .pred step the BASENAME only (`foo/()`.succ is still `foo/()`)
    if ((mName == "succ" || mName == "pred") && invIn.t == VT::Str && invIn.hashKind == "IO") {
        std::string full = invIn.toStr();
        auto sl = full.find_last_of(invIn.enumName == "Win32" ? "/\\" : "/");
        std::string head = sl == std::string::npos ? "" : full.substr(0, sl + 1);
        std::string base = sl == std::string::npos ? full : full.substr(sl + 1);
        ValueList none;
        Value nb = methodCall(Value::str(base), mName, none);
        Value p = Value::str(head + nb.toStr()); p.hashKind = "IO"; p.enumName = invIn.enumName;
        if (!invIn.ofType().empty()) p.ofTypeM() = invIn.ofType();
        return p;
    }
    // `GLOBALish.WHO.merge-symbols($cu.handle.globalish-package)`: the names a
    // `$*REPO.need` kept to its unit become the program's
    if (invIn.t == VT::Hash && invIn.hashKind == "Stash" && mName == "merge-symbols") {
        for (auto& a : args) {
            if (a.t != VT::Hash || !a.hash()) continue;
            auto gu = globalishUnits_.find((const void*)a.hash());
            if (gu != globalishUnits_.end()) stashImport(stashUnitHere(), gu->second);
            for (auto& kv : *a.hash()) {
                std::string k = kv.first;
                if (kv.second.t == VT::Type && !kv.second.s.empty()) k = kv.second.s;
                for (auto it = requireScoped_.begin(); it != requireScoped_.end(); ) {
                    if (it->first == k || it->first.rfind(k + "::", 0) == 0) it = requireScoped_.erase(it);
                    else ++it;
                }
                for (auto it = needHidden_.begin(); it != needHidden_.end(); ) {
                    if (*it == k || it->rfind(k + "::", 0) == 0) it = needHidden_.erase(it);
                    else ++it;
                }
            }
        }
        return invIn;
    }
    // a package's stash (`Foo::Bar.WHO`) gists and stringifies as the package's
    // long name
    if (invIn.t == VT::Hash && invIn.hashKind == "Stash" && !invIn.s.empty() &&
        (mName == "gist" || mName == "Str"))
        return Value::str(invIn.s.str());
    std::optional<Value> invCopy;
    const Value* invp = &invIn;
    // package-relative short name: a bare `Frog` type invocant answers as its
    // qualified nested class (`Forest::Frog`) when no real class claims the
    // short name — covers `.new`, `.= new` on typed decls, and user methods
    if (invIn.t == VT::Type && !invIn.s.empty() && !classes_.count(invIn.s)) {
        auto ai = classAliases_.find(invIn.s);
        if (ai != classAliases_.end()) { invCopy = invIn; invCopy->s = ai->second; invp = &*invCopy; }
    }
    // A Proxy stamped with a SUBCLASS identity (`class AttrProxy is Proxy`,
    // constructed via callwith in its own .new) dispatches that class's methods
    // — public and private — with the proxy itself as self, BEFORE the FETCH
    // rule (AttrX::Mooish drives its whole lazy machinery this way).
    if (invIn.t == VT::Hash && invIn.hashKind == "Proxy" && invIn.hash()) {
        auto ki = invIn.hash()->find("\x01cls");
        if (ki != invIn.hash()->end()) {
            auto cit = classes_.find(ki->second.s);
            if (cit != classes_.end()) {
                if (Value* um = cit->second->findMethod(mName))
                    return invokeMethod(*um, invIn, args, rwArgs);
                // …and a public attribute's accessor reads the key it lives under
                if (args.empty())
                    for (ClassInfo* c = cit->second.get(); c; c = c->parent.get())
                        for (auto& at : c->attrs)
                            if (at.pub && at.name == mName) {
                                auto ai = invIn.hash()->find(std::string("\x01" "a") + at.sigil + "!" + at.name);
                                if (ai != invIn.hash()->end()) return ai->second;
                            }
            }
        }
    }
    // A method called ON a Proxy is a READ of what it stands for: FETCH first.
    // (`$doc.root[0].name` — AT-POS hands back a Proxy, and every method after it
    // was landing on the container.) `.VAR` deliberately still sees the Proxy.
    if (invIn.t == VT::Hash && invIn.hashKind == "Proxy" && invIn.hash() &&
        mName != "VAR" && mName != "FETCH" && mName != "STORE" && mName != "WHERE") {
        if (invIn.hash()->count("FETCH")) {
            Value fetched = deproxy(invIn);
            if (!(fetched.t == VT::Hash && fetched.hashKind == "Proxy"))
                return methodCallInner(fetched, mName, std::move(args), rwArgs, skipOwn);
        }
    }
    const Value& inv = *invp;
    // `.perl` IS `.raku` — the old name for the same method. Aliasing once, here,
    // replaces sixteen `|| m == "perl"` clauses scattered down the ladder, each of
    // which had to be remembered by whoever added the next `.raku` arm. It has to
    // happen at construction: MName holds a REFERENCE to the string, so binding it
    // to a ternary temporary would dangle — hence the static lvalue.
    // …but a class that defines its OWN `method perl` keeps it: Rakudo's `.perl`
    // is a real method on Mu, so a user override wins over the forward to `.raku`.
    // The lookup only runs for the literal name "perl", which is rare.
    static const std::string kRaku = "raku";
    const bool userPerl = mName.size() == 4 && mName == "perl" &&
                          inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
                          inv.obj()->cls->findMethod("perl");
    // `.Stringy` is Mu's string coercion — `self.Str` — and was missing entirely,
    // so it died on every type including the enum values LWP::Simple builds its
    // request line from. Forwarded the same way `.perl` forwards to `.raku`, with
    // the same escape: a class that defines its own `method Stringy` keeps it.
    static const std::string kStr = "Str";
    const bool userStringy = mName == "Stringy" &&
                             inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
                             inv.obj()->cls->findMethod("Stringy");
    MName m{(mName == "perl" && !userPerl)      ? kRaku
          : (mName == "Stringy" && !userStringy) ? kStr
                                                 : mName};
    m.skipOwn = skipOwn;
    auto a0 = [&]() -> Value { return args.empty() ? Value::any() : args[0]; };
    // A USER OBJECT whose class defines the method dispatches HERE. The arm that
    // does it lives in methodCallPart2, which is reached ~3000 lines down this
    // function, so every call to a user method first walked the entire built-in
    // ladder — thousands of `m == "..."` string compares. That is why a plain
    // method call measured 5.6x Rakudo and a private one 10x, while rakupp's own
    // loop is 4x FASTER: the cost was dispatch, not the work.
    //
    // Only the plain case is taken. `new` (whose multis ADD to the default
    // constructor), a role STUB satisfied by an attribute, and a `handles`-
    // delegated name all have rules the full arm knows, so they fall through to
    // it unchanged. Everything in the ladder that touches a user object already
    // guards itself with `!cls->findMethod(m)`, so nothing there wanted this call.
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls && m != "new" && !m.skipOwn) {
        auto ci = inv.obj()->cls;
        if (ci->delegatedNames.empty() || !ci->delegatedNames.count(m)) {
            ClassInfo* owner = nullptr;
            if (Value* um = ci->findMethodForCall(m, langRev_ < 2, &owner))
                if (!(um->t == VT::Code && um->code() && um->code()->isStub))
                    // hand the resolution on — invokeMethodChain would otherwise
                    // hash and walk for the same name all over again
                    return invokeMethodChain(m, ci.get(), inv, std::move(args), rwArgs, um, owner);
        }
    }
    // A grammar CURSOR — the `self` of a method reached through `<.method>`
    // (issue #64) — answers its grammar's RULES as method calls (`self.b`,
    // `self.expr($x)`: matched in the running parse, at the cursor's position),
    // then the grammar's own methods (`self.panic(…)`), and is otherwise the
    // Match it is: `.pos`, `.target`, `.orig` fall through to the Match arms.
    if (inv.t == VT::Match && inv.md() && inv.md()->cursor && !m.skipOwn) {
        auto cur = std::static_pointer_cast<GrammarCursor>(inv.md()->cursor);
        RxCursorCall* call = cur->call();
        if (call && call->hasRule(m)) {
            // arguments travel as `<rule(…)>` call text: quoted strings, which is
            // what a rule parameter is once it is bound
            std::string argText;
            for (auto& a : args) {
                if (a.t == VT::Pair) continue;
                if (!argText.empty()) argText += ", ";
                argText += '\'';
                for (char c : a.toStr()) { if (c == '\\' || c == '\'') argText += '\\'; argText += c; }
                argText += '\'';
            }
            ParseNode node;
            if (!call->callRule(m, argText, (long)inv.rTo(), node)) return Value::nil();
            Value r = matchFromNode(node, *cur->input, cur->input);
            auto next = std::make_shared<GrammarCursor>(*cur); // shares `live`
            next->rule = m;
            r.mdW().cursor = next;
            return r;
        }
        if (call && cur->grammar) {
            ClassInfo* owner = nullptr;
            if (Value* um = cur->grammar->findMethodForCall(m, langRev_ < 2, &owner))
                return invokeMethodChain(m, cur->grammar, inv, std::move(args), rwArgs, um, owner);
        }
        else if (!call && cur->grammar && (cur->grammar->findRule(m) || cur->grammar->findMethodForCall(m)))
            throw RakuError{Value::typeObj("X::AdHoc"),
                "Cannot call '" + std::string(m) + "' on a grammar cursor outside the parse it belongs to"};
    }
    // `@a.BIND-POS($i, $container)` is answered in methodCallTail, which is the
    // LAST of the three dispatch parts — so every call walked the whole built-in
    // ladder to reach it. BinaryHeap's sift-down makes about eight per call and
    // 300k for one `Graph.diameter`, which put `std::string == const char*` at the
    // top of that profile. The arm itself still lives in methodCallTail (with the
    // immutable-List check and the rest); this is only the shortcut to it.
    if (inv.t == VT::Array && inv.arr() && args.size() == 2 && m == "BIND-POS" && !inv.isList && !inv.shape() &&
        !isNativeElemType(inv.ofType())) {
        long long i = writeIndexInt(args[0]);
        if (i < 0) i += (long long)inv.arr()->size();
        if (i >= 0) {
            while ((long long)inv.arr()->size() <= i) inv.arr()->push_back(Value::any());
            (*inv.arr())[(size_t)i] = args[1];
            return args[1];
        }
    }
    // read the environment ONCE, not on every method call — getenv walks environ
    static const bool kTrace = std::getenv("RAKUPP_TRACE") != nullptr;
    if (kTrace) std::cerr << "[M] ." << m << " on type=" << (int)inv.t << " s=[" << inv.s << "]" << (inv.t==VT::Object && inv.obj() && inv.obj()->cls ? " ("+inv.obj()->cls->name+")" : "") << "\n";
    // ---- CompUnit repository machinery (what zef drives to query/install dists) ----
    {
        auto homeDir = []() -> std::string { return platHomeDir(); };
        auto mkCURI = [&](const std::string& name, const std::string& prefix) -> Value {
            auto od = makePayload<ObjectData>();
            od->cls = classes_["CompUnit::Repository::Installation"];
            od->attrs["name"] = Value::str(name);
            Value p = Value::str(prefix); p.hashKind = "IO"; // IO::Path
            od->attrs["prefix"] = p;
            return Value::object(od);
        };
        if (inv.t == VT::Type && inv.s == "CompUnit::RepositoryRegistry") {
            // run-script("zef"): what an installed bin wrapper calls. Find the
            // installed dist whose files map carries "bin/<name>", load the stored
            // script from <repo>/resources/<id>, and run it AS THE PROGRAM (its
            // `use Zef::CLI` mainline + MAIN dispatch; run-script never returns).
            if (m == "run-script") {
                std::string script = args.empty() ? "" : args[0].toStr();
                // (an `-I inst#…` store first, then the default repositories)
                for (auto& repo : repoPrefixesForPath(libPaths_)) {
                    std::string distDir = repo + "/dist";
                    DIR* d = opendir(distDir.c_str());
                    if (!d) continue;
                    // among all dists providing this bin (stale versions linger
                    // after upgrades), pick the highest "ver"
                    auto verKey = [](const std::string& meta) {
                        std::vector<long long> key;
                        auto vp = meta.find("\"ver\"");
                        if (vp == std::string::npos) return key;
                        auto q1 = meta.find('"', meta.find(':', vp) + 1);
                        auto q2 = meta.find('"', q1 + 1);
                        if (q1 == std::string::npos || q2 == std::string::npos) return key;
                        std::string v = meta.substr(q1 + 1, q2 - q1 - 1);
                        long long cur = 0; bool any = false;
                        for (char c : v) {
                            if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); any = true; }
                            else if (any) { key.push_back(cur); cur = 0; any = false; }
                        }
                        if (any) key.push_back(cur);
                        return key;
                    };
                    std::string content, distId; std::vector<long long> bestVer;
                    while (struct dirent* e = readdir(d)) {
                        std::string n = e->d_name; if (n == "." || n == "..") continue;
                        std::ifstream mf(distDir + "/" + n);
                        if (!mf) continue;
                        std::ostringstream ms; ms << mf.rdbuf(); std::string meta = ms.str();
                        std::string tag = "\"bin/" + script + "\"";
                        auto p = meta.find(tag);
                        if (p == std::string::npos) continue;
                        p = meta.find(':', p + tag.size());
                        auto q1 = meta.find('"', p), q2 = q1 == std::string::npos ? q1 : meta.find('"', q1 + 1);
                        if (p == std::string::npos || q2 == std::string::npos) continue;
                        std::string blobId = meta.substr(q1 + 1, q2 - q1 - 1);
                        std::ifstream sf(repo + "/resources/" + blobId);
                        // older rakupp installs put bin blobs in bin/<sha>
                        if (!sf) sf.open(repo + "/bin/" + blobId);
                        if (!sf) continue;
                        auto vk = verKey(meta);
                        if (!distId.empty() && vk <= bestVer) continue;
                        std::ostringstream sc; sc << sf.rdbuf();
                        content = sc.str(); distId = n; bestVer = vk;
                    }
                    closedir(d);
                    if (content.empty()) continue;
                    resourceStack_.push_back(buildResourceMap(repo, distId));
                    distStack_.push_back(buildInstalledDistribution(repo, distId));
                    struct RG { ValueList& s; ~RG() { s.pop_back(); } } rg{resourceStack_};
                    struct DG { ValueList& s; ~DG() { s.pop_back(); } } dg{distStack_};
                    // the caller (an installed bin wrapper) has a &MAIN of its
                    // own in scope; the nested run() must not auto-invoke it
                    inheritedMainBarrier_ = tctx_.cur ? tctx_.cur->find("&MAIN") : nullptr;
                    int code = 0;
                    try {
                        Lexer lx(content);
                        Parser ps(lx.tokenize());
                        Program prog = ps.parseProgram();
                        code = run(prog);
                    } catch (const ParseError& pe) {
                        std::cerr << "===SORRY!=== Parse error at line " << pe.line
                                  << " in installed script '" << script << "': " << pe.what() << "\n";
                        code = 2;
                    }
                    throw ExitEx{code};
                }
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Could not find an installed script named '" + script + "'"};
            }
            if (m == "repository-for-name") {
                std::string nm = args.empty() ? "" : args[0].toStr();
                // rakupp resolves `use` from ~/.raku, so every writable name maps there;
                // 'core'/'perl' get the same prefix but hold no CORE dist, so their
                // candidates come back empty (zef's ignore list ends up empty).
                Value r = mkCURI(nm, homeDir() + "/.raku");
                r.obj()->attrs["\x01chain"] = Value::boolean(true);
                return r;
            }
            if (m == "repository-for-spec") {
                std::string spec = args.empty() ? "" : args[0].toStr();
                // `inst#/path` / `file#/path` / a bare name; take the path after '#'
                std::string prefix = homeDir() + "/.raku";
                auto hash = spec.find('#');
                if (hash != std::string::npos && hash + 1 < spec.size()) prefix = spec.substr(hash + 1);
                Value r = mkCURI(spec, prefix);
                for (auto& a : args)
                    if (a.t == VT::Pair && a.s == "next-repo" && a.pairVal()) r.obj()->attrs["next-repo"] = *a.pairVal();
                return r;
            }
            if (m == "head") {
                Value r = mkCURI("home", homeDir() + "/.raku");
                r.obj()->attrs["\x01chain"] = Value::boolean(true);
                return r;
            }
            if (m == "name-for-repository") return Value::str("home");
        }
        // `CompUnit::Repository::Installation.new(:prefix, :next-repo, :name)` — a
        // store at that directory, which is made if it is not there yet
        if (inv.t == VT::Type && inv.s == "CompUnit::Repository::Installation" && m == "new") {
            std::string pfx, nm; Value next = Value::any();
            for (auto& a : args)
                if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "prefix") pfx = a.pairVal()->toStr();
                    else if (a.s == "next-repo") next = *a.pairVal();
                    else if (a.s == "name") nm = a.pairVal()->toStr();
                }
            if (!pfx.empty()) {
                std::string acc;
                for (size_t i = 0; i <= pfx.size(); i++) {
                    if (i == pfx.size() || pfx[i] == '/') { if (!acc.empty() && acc != "/") ::mkdir(acc.c_str(), 0777); }
                    if (i < pfx.size()) acc += pfx[i];
                }
            }
            return makeCuri(nm, pfx, next);
        }
        // A FileSystem repo is the non-installed sibling: it serves a source tree
        // directly. rakupp does not enumerate its dists either, so `.files` answers
        // the same empty list rather than being absent.
        // ONE repository per prefix: `CURFS.new(:prefix($p)) === CURFS.new(:prefix($p))`
        if (inv.t == VT::Type && inv.s == "CompUnit::Repository::FileSystem" && m == "new" &&
            classes_.count("CompUnit::Repository::FileSystem")) {
            std::string pfx; Value next = Value::any();
            for (auto& a : args)
                if (a.t == VT::Pair && a.pairVal()) {
                    if (a.s == "prefix") pfx = a.pairVal()->toStr();
                    else if (a.s == "next-repo") next = *a.pairVal();
                }
            static std::mutex curfsMu;
            static std::map<std::string, Value> curfsCache;
            std::lock_guard<std::mutex> lk(curfsMu);
            auto hit = curfsCache.find(pfx);
            if (hit != curfsCache.end()) return hit->second;
            auto od = makePayload<ObjectData>();
            od->cls = classes_["CompUnit::Repository::FileSystem"];
            od->attrs["prefix"] = Value::str(pfx);
            od->attrs["next-repo"] = next;
            Value o = Value::object(od);
            curfsCache[pfx] = o;
            return o;
        }
        if (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
            inv.obj()->cls->name == "CompUnit::Repository::FileSystem") {
            auto& at = inv.obj()->attrs;
            if (m == "prefix") {
                Value p = Value::str(at.count("prefix") ? at["prefix"].toStr() : std::string());
                p.hashKind = "IO"; return p;
            }
            if (m == "short-id") return Value::str("file");
            if (m == "id") {
                std::string nextId;
                auto nx = at.find("next-repo");
                if (nx != at.end() && nx->second.t == VT::Object) nextId = methodCall(nx->second, "id", ValueList{}).toStr();
                return Value::str(sha1hex("file#" + (at.count("prefix") ? at["prefix"].toStr() : std::string()) + nextId));
            }
            if (m == "can-install") return Value::boolean(false);
            if (m == "precomp-repository") {
                auto it = at.find("\x01precomp");
                if (it != at.end()) return it->second;
                const std::string pfx = at.count("prefix") ? at["prefix"].toStr() : std::string(".");
                return at["\x01precomp"] = precompRepoObj(classes_, pfx + "/.precomp");
            }
            if (m == "install")
                throwTyped("X::AdHoc", {}, "Cannot install on CompUnit::Repository::FileSystem");
        }
        // `.candidates($spec)` / `.files($name)`: the tree's one distribution,
        // when it provides the module (or holds the file) and fits the spec's
        // matchers — a tree without a META6.json has a wildcard version, API
        // and auth
        if (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
            inv.obj()->cls->name == "CompUnit::Repository::FileSystem" &&
            (m == "files" || m == "candidates") && !args.empty()) {
            Value e = Value::array(); e.isList = true; e.s = "Seq";
            Value dist = curfsDistribution(*inv.obj());
            Value meta = methodCall(dist, "meta", ValueList{});
            const Value& spec = args[0];
            std::string want = spec.t == VT::Hash && spec.hash() && spec.hash()->count("short-name")
                             ? spec.hash()->at("short-name").toStr() : spec.toStr();
            auto has = [&](const char* k) {
                auto it = meta.hash() ? meta.hash()->find(k) : ValueMap::iterator();
                return meta.hash() && it != meta.hash()->end() && it->second.t == VT::Hash && it->second.hash() &&
                       it->second.hash()->count(want);
            };
            bool hit = m == "files" ? has("files") : (has("provides") || has("files"));
            if (hit && spec.t == VT::Hash && spec.hash()) {
                auto field = [&](const char* k) { auto it = meta.hash()->find(k); return it != meta.hash()->end() ? it->second.toStr() : std::string(); };
                auto matcher = [&](const char* k) { auto it = spec.hash()->find(k); return it != spec.hash()->end() ? it->second : Value::boolean(true); };
                hit = distFieldMatches(field("auth"), matcher("auth-matcher"), true, true) &&
                      distFieldMatches(field("ver"), matcher("version-matcher"), false, true) &&
                      distFieldMatches(field("api"), matcher("api-matcher"), false, true);
            }
            if (hit) e.arr()->push_back(dist);
            return e;
        }
        if (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
            inv.obj()->cls->name == "CompUnit::Repository::FileSystem" &&
            (m == "files" || m == "candidates" || m == "installed")) {
            Value e = Value::array(); e.isList = true; e.s = "Seq"; return e;
        }
        // …and it RESOLVES like any repository: a short-name asked for by a
        // DependencySpecification answers a CompUnit when this tree holds the
        // module, and Nil when it does not. Without an arm of its own the call
        // fell through to IO::Path's `.resolve` and came back as a PATH, so
        // `isa-ok compunit($class, $dir), CompUnit` failed for Identity::Utils
        // and the five identity modules beside it. The tree is searched the two
        // ways a source repository is laid out — `<prefix>/Foo/Bar.rakumod` and
        // `<prefix>/lib/Foo/Bar.rakumod` — plus the dist's own META6 provides.
        if (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
            inv.obj()->cls->name == "CompUnit::Repository::FileSystem" &&
            (m == "resolve" || m == "need")) {
            std::string want;
            if (!args.empty()) {
                if (args[0].t == VT::Str) want = args[0].s;
                else if (args[0].t == VT::Hash && args[0].hash()) {
                    auto it = args[0].hash()->find("short-name");
                    if (it != args[0].hash()->end()) want = it->second.toStr();
                }
            }
            if (want.empty()) return Value::nil();
            auto& at = inv.obj()->attrs;
            std::string prefix = at.count("prefix") ? at["prefix"].toStr() : "";
            std::string rel = want;
            for (size_t p = rel.find("::"); p != std::string::npos; p = rel.find("::"))
                rel.replace(p, 2, "/");
            auto exists = [](const std::string& path) {
                struct stat st;
                return ::stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
            };
            // Rakudo looks under the prefix ITSELF (`<prefix>/Foo/Bar.rakumod`),
            // and a prefix that is a distribution root — with a META6.json —
            // goes through that file's `provides` instead. A `lib/` guess is
            // NOT part of it: a bare dist root with no META6 resolves nothing
            // there, and this answers the same.
            bool found = false;
            std::string foundPath;
            for (const char* ext : {".rakumod", ".pm6", ".pm"})
                if (exists(prefix + "/" + rel + ext)) { found = true; foundPath = prefix + "/" + rel + ext; break; }
            if (!found && exists(prefix + "/META6.json")) {
                std::ifstream mf(prefix + "/META6.json");
                std::string meta((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
                // "Foo::Bar" : "lib/Foo/Bar.rakumod" — take the path it names and
                // check the file is really there, so a stale provides entry does
                // not answer for a module the tree no longer holds.
                size_t kp = meta.find("\"" + want + "\"");
                if (kp != std::string::npos) {
                    size_t c = meta.find(':', kp + want.size() + 2);
                    size_t q1 = c == std::string::npos ? std::string::npos : meta.find('"', c);
                    size_t q2 = q1 == std::string::npos ? std::string::npos : meta.find('"', q1 + 1);
                    if (q2 != std::string::npos)
                        found = exists(prefix + "/" + meta.substr(q1 + 1, q2 - q1 - 1));
                }
            }
            if (!found) {
                if (m == "need") {   // (resolve answers Nil; need, like any repository's, dies)
                    auto nx = at.find("next-repo");
                    if (nx != at.end() && nx->second.t == VT::Object) return methodCall(nx->second, m, args);
                    throwTypedV("X::CompUnit::UnsatisfiedDependency", {{"specification", args[0]}},
                                "Could not find " + want + " in:\n    file#" + prefix);
                }
                return Value::nil();
            }
            // the SAME CompUnit each time the repository is asked (Rakudo caches
            // what it loaded): `$curlf.need(…) === $curlf.need(…)`
            {
                auto cit = inv.obj()->attrs.find("\x01cu:" + want);
                if (cit != inv.obj()->attrs.end()) return cit->second;
            }
            if (m == "need") {
                // the repository's OWN tree is where the module is: search it first
                libPaths_.insert(libPaths_.begin(), prefix);
                struct RtLoad { int& d; RtLoad(int& x) : d(x) { d++; } ~RtLoad() { d--; } } rtLoad{runtimeLoadDepth_};
                try { loadModule(want, {}, /*doImport=*/false, false, "", false, /*mergeGlobals=*/false); }
                catch (...) { libPaths_.erase(libPaths_.begin()); throw; }
                libPaths_.erase(libPaths_.begin());
            }
            Value cu = Value::makeHash(); cu.hashKind = "CompUnit";
            (*cu.hash())["from"] = Value::str("Raku");
            (*cu.hash())["short-name"] = Value::str(want);
            (*cu.hash())["repo"] = inv;
            // A repository made on the spot — not one on the `$*REPO` chain —
            // loads its unit from SOURCE: Rakudo answers False here whatever the
            // module says (S22-package-format/local.t), where `$*REPO.need`
            // below precompiles it
            (*cu.hash())["precompiled"] = Value::boolean(false);
            inv.obj()->attrs["\x01cu:" + want] = cu;
            return cu;
        }
        if (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
            inv.obj()->cls->name == "CompUnit::Repository::Installation") {
            auto& at = inv.obj()->attrs;
            std::string prefix = at.count("prefix") ? at["prefix"].toStr() : "";
            std::string name = at.count("name") ? at["name"].toStr() : "";
            if (m == "prefix") { Value p = Value::str(prefix); p.hashKind = "IO"; return p; }
            if (m == "name") return Value::str(name);
            if (m == "loaded") {
                auto it = at.find("\x01loaded");
                if (it != at.end() && it->second.t == VT::Array) return it->second;
                Value e = Value::array(); e.isList = true; return e;
            }
            // A repository the program named (`inst#…`, `.new(:prefix)`), as
            // against one of the default chain zef drives (home, site, vendor):
            // only a named store answers from its own contents below.
            const bool chain = at.count("\x01chain") && at["\x01chain"].truthy();
            if (m == "short-id") return Value::str("inst");
            if (m == "id") {
                auto it = at.find("\x01id");
                if (it != at.end()) return it->second;
                std::string nextId;
                auto nx = at.find("next-repo");
                if (nx != at.end() && nx->second.t == VT::Object) nextId = methodCall(nx->second, "id", ValueList{}).toStr();
                return at["\x01id"] = Value::str(sha1hex("inst#" + prefix + nextId));
            }
            if (m == "next-repo") {
                auto nx = at.find("next-repo");
                return nx != at.end() && nx->second.t == VT::Object ? nx->second : Value::nil();
            }
            if (!chain && (m == "candidates" || m == "resolve" || m == "need" || m == "files" || m == "uninstall")) {
                if (m == "candidates") {
                    Value e = Value::array(); e.isList = true; e.s = "Seq";
                    if (!args.empty()) *e.arr() = curiCandidates(prefix, args[0]);
                    return e;
                }
                if (m == "files") {
                    Value e = Value::array(); e.isList = true; e.s = "Seq";
                    if (args.empty()) return e;
                    const std::string want = args[0].toStr();
                    for (auto& d : curiCandidates(prefix, args[0])) {
                        Value files = methodCall(methodCall(d, "meta", ValueList{}), "AT-KEY", ValueList{Value::str("files")});
                        if (files.t != VT::Hash || !files.hash() || !files.hash()->count(want)) continue;
                        struct stat st;
                        if (::stat((prefix + "/resources/" + files.hash()->at(want).toStr()).c_str(), &st) == 0) e.arr()->push_back(d);
                    }
                    return e;
                }
                if (m == "uninstall") {
                    Value dist = args.empty() ? Value::any() : args[0];
                    std::string distId;
                    if (dist.t == VT::Hash && dist.hash() && dist.hash()->count("dist-id")) distId = dist.hash()->at("dist-id").toStr();
                    if (distId.empty()) {
                        Value mv = methodCall(dist, "meta", ValueList{});
                        auto f = [&](const char* k) { return mv.hash() && mv.hash()->count(k) ? mv.hash()->at(k).toStr() : std::string(); };
                        std::string ver = mv.hash() && mv.hash()->count("version") ? mv.hash()->at("version").toStr() : f("ver");
                        distId = sha1hex(f("name") + ver + f("auth") + f("api"));
                    }
                    std::ifstream in(prefix + "/dist/" + distId);
                    if (!in) {   // (a dist another installer recorded: find it by its fields)
                        Value mv = methodCall(dist, "meta", ValueList{});
                        auto f = [&](const Value& h, const char* k) { return h.hash() && h.hash()->count(k) ? h.hash()->at(k).toStr() : std::string(); };
                        auto verOf = [&](const Value& h) { std::string v = f(h, "ver"); return v.empty() ? f(h, "version") : v; };
                        if (DIR* dd = opendir((prefix + "/dist").c_str())) {
                            while (struct dirent* de = readdir(dd)) {
                                std::string n = de->d_name;
                                if (n == "." || n == "..") continue;
                                std::ifstream r(prefix + "/dist/" + n);
                                std::ostringstream rs; rs << r.rdbuf();
                                Value rm = jsonParseDoc(rs.str());
                                if (rm.t != VT::Hash) continue;
                                if (f(rm, "name") == f(mv, "name") && verOf(rm) == verOf(mv) &&
                                    f(rm, "auth") == f(mv, "auth") && f(rm, "api") == f(mv, "api")) { distId = n; break; }
                            }
                            closedir(dd);
                        }
                        in.open(prefix + "/dist/" + distId);
                        if (!in) return Value::boolean(false);
                    }
                    std::ostringstream ss; ss << in.rdbuf(); in.close();
                    Value rec = jsonParseDoc(ss.str());
                    // (a name taken from a record must stay inside the store)
                    auto safeName = [](const std::string& n) {
                        return !n.empty() && n[0] != '/' && n.find("..") == std::string::npos && n.find('\\') == std::string::npos;
                    };
                    auto recHash = [](const Value& r, const char* k) -> std::shared_ptr<ValueMap> {
                        if (r.t != VT::Hash || !r.hash()) return nullptr;
                        auto it = r.hash()->find(k);
                        return it != r.hash()->end() && it->second.t == VT::Hash ? it->second.hashS() : nullptr;
                    };
                    auto dropShort = [&](const std::string& key) {
                        std::string dir = prefix + "/short/" + sha1hex(key);
                        ::unlink((dir + "/" + distId).c_str());
                        ::rmdir(dir.c_str());   // only when nothing else lives there
                    };
                    std::set<std::string> mine;   // this dist's blobs
                    if (auto pv = recHash(rec, "provides"))
                        for (auto& kv : *pv) {
                            dropShort(kv.first);
                            if (kv.second.t == VT::Hash && kv.second.hash())
                                for (auto& pk : *kv.second.hash())
                                    if (pk.second.t == VT::Hash && pk.second.hash() && pk.second.hash()->count("file") &&
                                        safeName(pk.second.hash()->at("file").toStr()) &&
                                        pk.second.hash()->at("file").toStr().find('/') == std::string::npos)
                                        mine.insert("sources/" + pk.second.hash()->at("file").toStr());
                        }
                    std::vector<std::string> bins;
                    if (auto fv = recHash(rec, "files"))
                        for (auto& kv : *fv) {
                            dropShort(kv.first);
                            const std::string id = kv.second.toStr();
                            if (safeName(id) && id.find('/') == std::string::npos) {
                                mine.insert("resources/" + id);
                                // …and a library's platform twin beside it
                                if (kv.first.rfind("resources/libraries/", 0) == 0) {
#if defined(_WIN32)
                                    mine.insert("resources/" + id + ".dll");
#elif defined(__APPLE__)
                                    mine.insert("resources/lib" + id + ".dylib");
#else
                                    mine.insert("resources/lib" + id + ".so");
#endif
                                }
                            }
                            if (kv.first.rfind("bin/", 0) == 0 && safeName(kv.first.substr(4)) &&
                                kv.first.find('/', 4) == std::string::npos)
                                bins.push_back(kv.first.substr(4));
                        }
                    ::unlink((prefix + "/dist/" + distId).c_str());
                    // a blob another installed dist still references stays
                    // (sources and resources are stored by content)
                    std::string others;
                    if (DIR* dd = opendir((prefix + "/dist").c_str())) {
                        while (struct dirent* de = readdir(dd)) {
                            std::string n = de->d_name;
                            if (n == "." || n == "..") continue;
                            std::ifstream o(prefix + "/dist/" + n);
                            std::ostringstream os; os << o.rdbuf(); others += os.str();
                        }
                        closedir(dd);
                    }
                    for (auto& b : mine) {
                        std::string id = b.substr(b.find('/') + 1);
                        std::string bare = id.rfind("lib", 0) == 0 ? id.substr(3) : id;
                        bare = bare.substr(0, bare.find('.'));
                        if (others.find("\"" + id + "\"") != std::string::npos ||
                            others.find("\"" + bare + "\"") != std::string::npos) continue;
                        ::unlink((prefix + "/" + b).c_str());
                    }
                    for (auto& b : bins)
                        if (others.find("\"bin/" + b + "\"") == std::string::npos) ::unlink((prefix + "/bin/" + b).c_str());
                    return Value::boolean(true);
                }
                // resolve / need
                ValueList cands = args.empty() ? ValueList{} : curiCandidates(prefix, args[0]);
                std::string want = args.empty() ? std::string()
                                 : args[0].t == VT::Hash && args[0].hash() && args[0].hash()->count("short-name")
                                 ? args[0].hash()->at("short-name").toStr() : args[0].toStr();
                if (cands.empty()) {
                    auto nx = at.find("next-repo");
                    if (nx != at.end() && nx->second.t == VT::Object) return methodCall(nx->second, m, args);
                    if (m == "resolve") return Value::nil();
                    throwTypedV("X::CompUnit::UnsatisfiedDependency", {{"specification", args.empty() ? Value::any() : args[0]}},
                                "Could not find " + want + " in:\n    inst#" + prefix);
                }
                const Value& dist = cands.front();
                const std::string cuKey = "\x01cu:" + dist.hash()->at("dist-id").toStr() + ":" + want;
                if (m == "need") {   // the same unit each time it is asked for
                    auto hit = at.find(cuKey);
                    if (hit != at.end()) return hit->second;
                }
                Value cu = Value::makeHash(); cu.hashKind = "CompUnit";
                (*cu.hash())["from"] = Value::str("Raku");
                (*cu.hash())["short-name"] = Value::str(want);
                (*cu.hash())["repo"] = inv;
                (*cu.hash())["distribution"] = dist;
                (*cu.hash())["version"] = methodCall(Value::typeObj("Version"), "new",
                                                     ValueList{Value::str(dist.hash()->at("\x01ver").toStr())});
                (*cu.hash())["precompiled"] = Value::boolean(true);
                if (m == "resolve") return cu;
                // Load THIS dist's source, and keep what it declares out of the
                // program's names: `$*REPO.need` merges no globals (the unit's
                // own are in `$cu.handle.globalish-package`, for merge-symbols)
                std::unordered_set<std::string> before, firstSegs;
                for (auto& kv : classes_) { before.insert(kv.first); firstSegs.insert(kv.first.substr(0, kv.first.find("::"))); }
                pinnedInstall_ = PinnedInstall{want, prefix, dist.hash()->at("dist-id").toStr(), dist.hash()->at("\x01src").toStr()};
                {
                    struct Unpin { std::optional<PinnedInstall>& p; ~Unpin() { p.reset(); } } unpin{pinnedInstall_};
                    struct RtLoad { int& d; RtLoad(int& x) : d(x) { d++; } ~RtLoad() { d--; } } rtLoad{runtimeLoadDepth_};
                    loadModule(want, {}, /*doImport=*/false, false, "", false, /*mergeGlobals=*/false);
                }
                for (auto& kv : classes_)
                    if (!before.count(kv.first) && !firstSegs.count(kv.first.substr(0, kv.first.find("::"))))
                        needHidden_.insert(kv.first);
                auto& lst = inv.obj()->attrs["\x01loaded"];
                if (lst.t != VT::Array) { lst = Value::array(); lst.isList = true; }
                lst.arrRef().push_back(cu);
                inv.obj()->attrs[cuKey] = cu;
                return cu;
            }
            if (m == "id" || m == "short-id") return Value::str(name.empty() ? std::string("inst") : name);
            if (m == "Str" || m == "gist" || m == "raku") return Value::str("inst#" + prefix);
            if (m == "path-spec") return Value::str("inst#" + prefix);
            if (m == "can-install") return Value::boolean(true);
            if (m == "precomp-repository") {
                auto it = at.find("\x01precomp");
                if (it != at.end()) return it->second;
                return at["\x01precomp"] = precompRepoObj(classes_, prefix + "/precomp");
            }
            if (m == "repo-chain") {
                // The whole chain the loader searches — home, then every site and
                // vendor prefix — not just this one link. A program that walks the
                // chain to enumerate installed distributions (rather than assuming
                // ~/.raku) saw one repository and missed everything zef installed
                // system-wide.
                Value e = Value::array(); e.isList = true; e.s = "Seq";
                std::string homeRepo = platHomeDir() + "/.raku";
                if (!chain) {   // a store the program named: itself, then its next-repo's chain
                    e.arr()->push_back(inv);
                    auto nx = at.find("next-repo");
                    if (nx != at.end() && nx->second.t == VT::Object && nx->second.obj()) {
                        Value rest = nx->second.obj()->cls && nx->second.obj()->cls->name == "CompUnit::Repository::Installation"
                                   ? methodCall(nx->second, "repo-chain", ValueList{}) : Value::any();
                        if (rest.t == VT::Array && rest.arr()) for (auto& r : *rest.arr()) e.arr()->push_back(r);
                        else e.arr()->push_back(nx->second);
                    }
                    return e;
                }
                for (const std::string& pre : rakuRepoPrefixes()) {
                    auto od = makePayload<ObjectData>();
                    od->cls = inv.obj()->cls;   // the Installation class, already in hand
                    std::string nm = pre == homeRepo ? "home"
                                   : pre.size() > 5 && pre.compare(pre.size()-5,5,"/site") == 0 ? "site"
                                   : pre.size() > 7 && pre.compare(pre.size()-7,7,"/vendor") == 0 ? "vendor"
                                   : "inst";
                    od->attrs["name"] = Value::str(nm);
                    od->attrs["\x01chain"] = Value::boolean(true);
                    Value p2 = Value::str(pre); p2.hashKind = "IO";
                    od->attrs["prefix"] = p2;
                    e.arr()->push_back(Value::object(od));
                }
                // …and the `core` repository, which sits beside each `site`. It is
                // REPORTED but never searched: rakupp answers the core types from
                // its own builtins rather than from Rakudo's sources, so it stays
                // out of rakuRepoPrefixes(). Listing it keeps the chain an honest
                // description of the installation.
                for (const std::string& pre : rakuRepoPrefixes()) {
                    if (!(pre.size() > 5 && pre.compare(pre.size()-5,5,"/site") == 0)) continue;
                    std::string core = pre.substr(0, pre.size()-5) + "/core";
                    struct stat st;
                    if (stat(core.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
                    auto od = makePayload<ObjectData>();
                    od->cls = inv.obj()->cls;
                    od->attrs["name"] = Value::str("core");
                    od->attrs["\x01chain"] = Value::boolean(true);
                    Value p3 = Value::str(core); p3.hashKind = "IO";
                    od->attrs["prefix"] = p3;
                    e.arr()->push_back(Value::object(od));
                }
                if (e.arr()->empty()) e.arr()->push_back(inv);
                return e;
            }
            if (m == "candidates") {
                // Phase 1: no dist enumeration yet — an empty candidate list is correct
                // for 'core' (rakupp has no CORE dist) and keeps zef's ignore list empty.
                Value e = Value::array(); e.isList = true; e.s = "Seq"; return e;
            }
            // `$curi.installed` — the distributions written under this repo. Each
            // `dist/<id>` file is the dist's META as JSON (that is what .install
            // writes), so the listing is those, parsed, as Distribution objects.
            // zef's `list --installed` walks this; answering an empty list made it
            // print nothing at all.
            if (m == "installed") {
                Value e = Value::array(); e.isList = true; e.s = "Seq";
                if (DIR* dd = opendir((prefix + "/dist").c_str())) {
                    while (struct dirent* de = readdir(dd)) {
                        std::string n = de->d_name;
                        if (n == "." || n == "..") continue;
                        std::ifstream in(prefix + "/dist/" + n);
                        if (!in) continue;
                        std::ostringstream ss; ss << in.rdbuf();
                        Value meta = jsonParseDoc(ss.str());
                        if (meta.t != VT::Hash) continue;
                        Value d = Value::makeHash(); d.hashKind = "Distribution";
                        (*d.hash())["meta"] = meta;
                        Value p2 = Value::str(prefix); p2.hashKind = "IO";
                        (*d.hash())["prefix"] = p2;
                        (*d.hash())["dist-id"] = Value::str(n);
                        e.arr()->push_back(d);
                    }
                    closedir(dd);
                }
                return e;
            }
            // `.files($name, :$ver, :$auth, :$api)` looks up an INSTALLED file (a
            // `bin/` script or a `resources/` entry) across the repo's distributions.
            // rakupp does not enumerate dists yet — the same Phase-1 gap as
            // `.candidates` — so the honest answer is the empty list every caller
            // already handles with `// "Nada"`, not a missing method.
            if (m == "files") {
                Value e = Value::array(); e.isList = true; e.s = "Seq"; return e;
            }
            // `$*REPO.need($dep-spec)` — LOAD the named module, the way `use` does,
            // and answer a CompUnit for it (Nil if it will not load, so zef's
            // `unless try $*REPO.need($spec)` skips the plugin). zef 1.x loads every
            // one of its own backends through this instead of `require ::($name)`.
            if (m == "need" || m == "resolve") {
                std::string want;
                if (!args.empty()) {
                    if (args[0].t == VT::Str) want = args[0].s;
                    else if (args[0].t == VT::Hash && args[0].hash()) {
                        auto it = args[0].hash()->find("short-name");
                        if (it != args[0].hash()->end()) want = it->second.toStr();
                    }
                }
                if (want.empty()) return Value::nil();
                if (m == "need") {                            // throws if it cannot load
                    struct RtLoad { int& d; RtLoad(int& x) : d(x) { d++; } ~RtLoad() { d--; } } rtLoad{runtimeLoadDepth_};
                    loadModule(want, {}, true, false, "", false, /*mergeGlobals=*/false);
                }
                Value cu = Value::makeHash(); cu.hashKind = "CompUnit";
                (*cu.hash())["short-name"] = Value::str(want);
                (*cu.hash())["repo"] = inv;
                (*cu.hash())["precompiled"] = Value::boolean(unitPrecompilable(want, libPaths_));
                // `.loaded` lists what this repository has handed out
                if (m == "need" && inv.t == VT::Object && inv.obj()) {
                    auto& lst = inv.obj()->attrs["\x01loaded"];
                    if (lst.t != VT::Array) { lst = Value::array(); lst.isList = true; }
                    lst.arrRef().push_back(cu);
                }
                return cu;
            }
            if (m == "install") {
                // $cur.install($dist, :$force) — write the CURI layout under `prefix`
                // (sources/<sha>, short/<sha1(name)>/<dist-id>, dist/<dist-id> JSON,
                // resources/, bin/). rakupp reads exactly this to resolve `use`.
                // A prefix-less repository object would write into "/sources" and
                // fail SILENTLY on every file — refuse loudly instead (the
                // .new(prefix=>) spelling does not thread the prefix through;
                // repository-for-spec("inst#/path") does).
                if (prefix.empty())
                    throw RakuError{Value::typeObj("X::AdHoc"),
                        "install: this repository object carries no prefix — construct it with "
                        "CompUnit::Repository::Installation.new(:prefix(…)) or "
                        "CompUnit::RepositoryRegistry.repository-for-spec('inst#/path')"};
                Value dist = args.empty() ? Value::any() : args[0];
                bool force = false;
                for (auto& a : args)
                    if (a.t == VT::Pair && a.s == "force") force = a.pairVal() && a.pairVal()->truthy();
                Value metaV = methodCall(dist, "meta", ValueList{});
                if (metaV.t != VT::Hash || !metaV.hash())
                    throw RakuError{Value::typeObj("X::AdHoc"), "install: distribution has no meta"};
                auto& meta = *metaV.hash();
                auto mstr = [&](const char* k) -> std::string {
                    auto it = meta.find(k); return it != meta.end() ? it->second.toStr() : "";
                };
                std::string name = mstr("name");
                std::string ver  = meta.count("version") ? meta["version"].toStr() : mstr("ver");
                std::string auth = mstr("auth");
                std::string api  = mstr("api");
                // NB the "\0"s append NOTHING — std::string + const char* stops at
                // the terminator — so the id is the four fields CONCATENATED, and
                // every dist-id on every store on disk was computed that way.
                // Adding the separators would rename every record and orphan every
                // installed distribution: this line is a format, not a hash call.
                std::string distId = sha1hex(name + "\0" + ver + "\0" + auth + "\0" + api);
                // (a Distribution::Path / ::Hash / a repository's dist says where
                // its files are; any other distribution object answers .IO)
                std::string distRoot = dist.t == VT::Hash && dist.hashKind == "Distribution" && dist.hash() &&
                                       dist.hash()->count("prefix")
                                     ? dist.hash()->at("prefix").toStr()
                                     : methodCall(dist, "IO", ValueList{}).toStr();
                auto mkdirp = [](const std::string& p) {
                    std::string acc;
                    for (size_t i = 0; i <= p.size(); i++) {
                        if (i == p.size() || p[i] == '/') { if (!acc.empty() && acc != "/") ::mkdir(acc.c_str(), 0777); }
                        if (i < p.size()) acc += p[i];
                    }
                };
                auto slurp = [](const std::string& path) -> std::string {
                    std::ifstream in(path, std::ios::binary);
                    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
                };
                // already installed? (a short entry for a provided module under this dist-id)
                Value provV = meta.count("provides") ? meta["provides"] : Value::makeHash();
                // (a meta built in code may give `provides` as one Pair, or a
                // list of them: `:provides(:Foo<lib/Foo.rakumod>)`)
                if (provV.t == VT::Pair || (provV.t == VT::Array && provV.arr())) {
                    Value h = Value::makeHash();
                    if (provV.t == VT::Pair) (*h.hash())[provV.s.str()] = provV.pairVal() ? *provV.pairVal() : Value::any();
                    else for (auto& e : *provV.arr())
                        if (e.t == VT::Pair) (*h.hash())[e.s.str()] = e.pairVal() ? *e.pairVal() : Value::any();
                    provV = h;
                }
                if (!force && provV.t == VT::Hash && provV.hash() && !provV.hash()->empty()) {
                    std::string firstMod = provV.hash()->begin()->first;
                    std::string sentinel = prefix + "/short/" + sha1hex(firstMod) + "/" + distId;
                    if (std::ifstream(sentinel).good())
                        throw RakuError{Value::typeObj("X::AdHoc"),
                            name + ":ver<" + ver + ">:auth<" + auth + "> is already installed"};
                }
                mkdirp(prefix + "/sources"); mkdirp(prefix + "/short"); mkdirp(prefix + "/dist");
                Value filesOut = Value::makeHash();
                // The dist RECORD's provides must be the NESTED store shape
                // Rakudo writes — {mod => {relpath => {file => <source-id>,
                // time => Nil}}} — not META6's flat mod=>path. zef's uninstall
                // walks provides{$mod}{$path}<file>, and a flat record died
                // there with "Type Str does not support associative indexing";
                // loading never noticed, since resolution reads short/ entries.
                Value provOut = Value::makeHash();
                if (provV.t == VT::Hash && provV.hash())
                    for (auto& kv : *provV.hash()) {
                        std::string mod = kv.first, srcRel;
                        // provides value is either the source path, or {path => {file,…}}
                        if (kv.second.t == VT::Hash && kv.second.hash() && !kv.second.hash()->empty())
                            srcRel = kv.second.hash()->begin()->first;
                        else srcRel = kv.second.toStr();
                        std::string content = slurp(distRoot + "/" + srcRel);
                        std::string srcSha = sha1hex(content);
                        { std::ofstream o(prefix + "/sources/" + srcSha, std::ios::binary); o << content; }
                        std::string sdir = prefix + "/short/" + sha1hex(mod);
                        mkdirp(sdir);
                        std::ofstream o(sdir + "/" + distId);
                        o << ver << "\n" << auth << "\n" << api << "\n" << srcSha << "\n" << distId << "\n";
                        (*filesOut.hash())[srcRel] = Value::str(srcSha);
                        Value leaf = Value::makeHash();
                        (*leaf.hash())["file"] = Value::str(srcSha);
                        (*leaf.hash())["time"] = Value::nil();
                        Value byPath = Value::makeHash();
                        (*byPath.hash())[srcRel] = leaf;
                        (*provOut.hash())[mod] = byPath;
                    }
                // resources/ and bin/ — zef injects these into meta<files> (rel-path => src)
                if (meta.count("files") && meta["files"].t == VT::Hash && meta["files"].hash()) {
                    mkdirp(prefix + "/resources"); mkdirp(prefix + "/bin");
                    for (auto& kv : *meta["files"].hash()) {
                        std::string rel = kv.first, src = kv.second.toStr();
                        // the file's source: the dist's own copy at the same
                        // relative path, a path relative to the dist, or an
                        // absolute one (zef hands those in)
                        struct stat sst;
                        std::string from = src.empty() ? distRoot + "/" + rel
                                         : (src[0] == '/' && ::stat(src.c_str(), &sst) == 0) ? src
                                         : distRoot + "/" + src;
                        std::string content = slurp(from);
                        std::string sha = sha1hex(content);
                        // a resource keeps its extension (config.txt is
                        // resources/<SHA>.txt, as Rakudo stores it); a library
                        // keeps the bare id beside its platform twin below
                        if (rel.rfind("resources/", 0) == 0 && rel.rfind("resources/libraries/", 0) != 0) {
                            size_t dot = from.rfind('.'), sl = from.rfind('/');
                            if (dot != std::string::npos && (sl == std::string::npos || dot > sl + 1))
                                sha += from.substr(dot);
                        }
                        // EVERY file blob lands in resources/ — bin/ scripts too.
                        // That is Rakudo's layout (bin/ holds only the named
                        // wrappers below), and it is what run-script reads. This
                        // repo's first cut put bin blobs in bin/<sha>, which left
                        // run-script blind to them.
                        { std::ofstream o(prefix + "/resources/" + sha, std::ios::binary); o << content; }
                        // A compiled library rides in under a SECOND name as well:
                        // Rakudo resolves `%?RESOURCES<libraries/x>` by decorating
                        // the blob id the way its own installer names the file —
                        // resources/lib<id>.dylib (.so; <id>.dll) — and never
                        // looks at the bare id. Digest::SHA256::Native built its
                        // libsha256.dylib, this store held it as the bare sha, and
                        // under Rakudo the module died "Cannot locate native
                        // library"; the same bytes under both names leave the
                        // dist usable by either engine, which is the store's promise.
                        if (rel.rfind("resources/libraries/", 0) == 0) {
#if defined(_WIN32)
                            std::string twin = sha + ".dll";
#elif defined(__APPLE__)
                            std::string twin = "lib" + sha + ".dylib";
#else
                            std::string twin = "lib" + sha + ".so";
#endif
                            std::ofstream o(prefix + "/resources/" + twin, std::ios::binary); o << content;
                        }
                        (*filesOut.hash())[rel] = Value::str(sha);
                        // ...and its short/ index entry: Rakudo's `.files("bin/x")`
                        // — the lookup its run-script resolves a bare script name
                        // through — reads short/<sha1(rel-path)>/<dist-id>, the
                        // same 5-line format the provides entries use. Without it
                        // a wrapper runs under rakupp but dies under Rakudo with
                        // "No candidate found".
                        {
                            std::string fdir = prefix + "/short/" + sha1hex(rel);
                            mkdirp(fdir);
                            std::ofstream o(fdir + "/" + distId);
                            o << ver << "\n" << auth << "\n" << api << "\n" << sha << "\n" << distId << "\n";
                        }
                        // bin/<name> also gets a NAMED, executable wrapper —
                        // Rakudo's own template, but with a `rakupp` shebang: what
                        // rakupp installed runs under rakupp. `env raku` handed the
                        // command to Rakudo wherever that name was Rakudo's, and the
                        // store (~/.raku) is one Rakudo reads too, so it ran there
                        // without a word. (Rakudo adds -m/-j/-js backend variants;
                        // the bare name is the one people run, and the only one
                        // written here.)
                        if (rel.rfind("bin/", 0) == 0 && rel.find('/', 4) == std::string::npos && rel.size() > 4) {
                            std::string script = rel.substr(4);
                            std::string wpath = prefix + "/bin/" + script;
                            { std::ofstream w(wpath, std::ios::binary);
                              w << "#!/usr/bin/env rakupp\n"
                                   "sub MAIN(:$name is copy, :$auth, :$ver, *@, *%) {\n"
                                   "    CompUnit::RepositoryRegistry.run-script(\"" << script
                                << "\", :dist-name<" << name << ">, :$name, :$auth, :$ver);\n"
                                   "}\n"; }
#ifndef _WIN32
                            ::chmod(wpath.c_str(), 0755);
#endif
                        }
                    }
                }
                // dist/<id> — the meta index (list-installed reads it; buildResourceMap
                // scans it for `resources/…` → the on-disk resource copy).
                Value distMeta = metaV; distMeta.setHash(makePayload<ValueMap>(meta));
                (*distMeta.hash())["files"] = filesOut;
                if (provOut.hash() && !provOut.hash()->empty())
                    (*distMeta.hash())["provides"] = provOut;
                // Rakudo's records carry `ver` beside META6's `version`; zef's
                // list/upgrade logic reads the short spelling (ver<0> without it)
                if (!meta.count("ver") && !ver.empty())
                    (*distMeta.hash())["ver"] = Value::str(ver);
                { std::ofstream o(prefix + "/dist/" + distId); o << jsonEncode(distMeta); }
                // the dist-id, so the CALLER can record provenance ("what did
                // rakupp install" is the question uninstall refuses without)
                return Value::str(distId);
            }
        }
    }
    if (m == "WHY") {
        // declarator pod: `#| text` above / `#= text` beside a declaration —
        // answered as the Pod::Block::Declarator it is, knowing its declarand
        // (WHEREFORE) and which part came before and which after; the `$=pod`
        // entry for the same text learns its WHEREFORE here too
        auto declarator = [&](const std::string& pod, const std::string& trail, int line) -> Value {
            std::string lead = trail.empty() ? pod
                             : pod == trail ? std::string()
                             : pod.substr(0, pod.size() > trail.size() + 1 ? pod.size() - trail.size() - 1 : 0);
            Value d = Value::makeHash(); d.hashKind = "Pod";
            (*d.hash())["podclass"] = Value::str("Pod::Block::Declarator");
            Value pc = Value::array(); pc.arr()->push_back(Value::str(pod));
            (*d.hash())["contents"] = pc;
            (*d.hash())["leading"] = lead.empty() ? Value::any() : Value::str(lead);
            (*d.hash())["trailing"] = trail.empty() ? Value::any() : Value::str(trail);
            (*d.hash())["WHEREFORE"] = inv;
            bool linked = false;
            for (auto& e : podDom_)
                if (e.t == VT::Hash && e.hashKind == "Pod" && e.hash() && e.hash()->count("declLine")) {
                    long long dl = (*e.hash())["declLine"].toInt();
                    bool tr = e.hash()->count("trailingPod");
                    if (line > 0 && (dl == line || (tr && dl == line + 1))) { (*e.hash())["WHEREFORE"] = inv; linked = true; }
                }
            // no line to go by (a package's doc): the first unlinked entry with this text
            if (!linked)
                for (auto& e : podDom_)
                    if (e.t == VT::Hash && e.hashKind == "Pod" && e.hash() && e.hash()->count("declLine") &&
                        !e.hash()->count("WHEREFORE") && e.toStr() == pod) {
                        (*e.hash())["WHEREFORE"] = inv; break;
                    }
            return d;
        };
        if (inv.t == VT::Code && inv.code() && !inv.code()->pod.empty())
            return declarator(inv.code()->pod, inv.code()->podTrail, inv.code()->declLine);
        // a dispatch group is documented by its PROTO (a bare multi group has none)
        if (inv.t == VT::Code && inv.code() && inv.code()->isMultiDispatcher)
            for (auto& c : inv.code()->candidates)
                if (c.code() && (c.code()->isProto || c.code()->isProtoBody) && !c.code()->pod.empty())
                    return declarator(c.code()->pod, c.code()->podTrail, c.code()->declLine);
        // an Attribute's doc, carried on its meta-object
        if (inv.t == VT::Hash && inv.hashKind == "Attribute" && inv.hash() && inv.hash()->count("why")) {
            auto& h = *inv.hash();
            return declarator(h["why"].toStr(), h.count("whyTrail") ? h["whyTrail"].toStr() : std::string(),
                              h.count("whyLine") ? (int)h["whyLine"].toInt() : 0);
        }
        // a Parameter's own doc, plumbed from Param.pod at reflection time
        if (inv.t == VT::Hash && inv.hashKind == "Parameter" && inv.hash() && inv.hash()->count("why")) {
            const Value& w = (*inv.hash())["why"];
            if (w.t == VT::Str && !w.s.empty())
                return declarator(w.s.str(), inv.hash()->count("whyTrail") ? (*inv.hash())["whyTrail"].toStr()
                                                                           : std::string(), 0);
            return w;
        }
        if (inv.t == VT::Type) {
            // A type's declarator is ONE object, however it is reached: the
            // group's `.WHY` is its default candidate's (`Boxer.WHY =:=
            // Boxer.^candidates[0].WHY`, S26-documentation/why-both.t), and
            // asking twice gives the same thing twice. Kept per ClassInfo, and
            // only while that type's doc is the one it was built from.
            static std::mutex whyMu;
            static std::unordered_map<const ClassInfo*, std::pair<std::string, Value>> whyOfType;
            auto typeWhy = [&](ClassInfo* ci) -> Value {
                std::lock_guard<std::mutex> lk(whyMu);
                auto hit = whyOfType.find(ci);
                if (hit != whyOfType.end() && hit->second.first == ci->pod) return hit->second.second;
                Value d = declarator(ci->pod, ci->podTrail, ci->decl ? ci->decl->line : 0);
                whyOfType[ci] = {ci->pod, d};
                return d;
            };
            // a role-group CANDIDATE is documented by its own declaration, and
            // the group by its default candidate's
            if (inv.ext()) {
                auto rc = std::static_pointer_cast<RoleCandidateRef>(inv.ext());
                if (rc->ci && !rc->ci->pod.empty()) return typeWhy(rc->ci.get());
            }
            auto it = classes_.find(inv.s);
            ClassInfo* dc = it != classes_.end() && it->second && it->second->isRole ? it->second->roleGroupDefault()
                          : it != classes_.end() ? it->second.get() : nullptr;
            if (dc && !dc->pod.empty()) return typeWhy(dc);
            auto pi = pkgPod_.find(inv.s); // a module/package keeps its own
            if (pi != pkgPod_.end()) {
                auto pt = pkgPodTrail_.find(inv.s);
                return declarator(pi->second, pt != pkgPodTrail_.end() ? pt->second : std::string(), 0);
            }
        }
        if (inv.t == VT::Object && inv.obj() && inv.obj()->cls && !inv.obj()->cls->pod.empty())
            return Value::str(inv.obj()->cls->pod);
        // an enum's type object is carried as its value list, tagged by enumType
        if (inv.t == VT::Array && !inv.enumType.str().empty()) {
            auto pi = pkgPod_.find(inv.enumType.str());
            if (pi != pkgPod_.end()) {
                auto pt = pkgPodTrail_.find(inv.enumType.str());
                return declarator(pi->second, pt != pkgPodTrail_.end() ? pt->second : std::string(), 0);
            }
        }
        return Value::nil();
    }
    // Any.hash is an empty Hash (Rakudo: `my $x; $x.hash` → {}) — zef reads
    // `$dist.meta<files>.hash.keys` where <files> may be absent.
    if (m == "hash" && (inv.t == VT::Any || (inv.t == VT::Type && inv.s == "Any")))
        return Value::makeHash();
    if (m == "pairup" && (inv.t == VT::Any || inv.t == VT::Type || inv.t == VT::Nil)) {
        Value e = Value::array(); e.isList = true; e.s = "Seq"; return e; // :U invocant
    }
    // `.Str` / `.gist` / `~` on a LIST goes through each element's own .Str, so a
    // list of objects with a user `method Str` renders as those strings (see strOf).
    if (inv.t == VT::Array && inv.arr() && inv.enumName.empty() &&
        (m == "Str" || m == "Stringy")) {
        bool anyObj = false;
        for (auto& e : *inv.arr()) if (e.t == VT::Object) { anyObj = true; break; }
        if (anyObj) return Value::str(strOf(inv));
    }
    // A Blob's numeric value is its element count; it has no characters
    // (unless it is an encoded string's blob, handled by .Str below)
    if (inv.t == VT::Str && (inv.hashKind == "Buf" || inv.hashKind == "Blob")) {
        if (m == "Numeric" || m == "Int") return Value::integer(inv.blobElems());
        if (m == "chars" && inv.enumName.empty())
            throwTyped("X::Buf::AsStr", {{"method", "chars"}},
                       "Cannot use a Buf as a string, but you called the chars method on it");
        if (m == "reverse") {   // a Blob of the same type, back to front
            std::string r(inv.s.str().rbegin(), inv.s.str().rend());
            if (inv.blobElems() == (long long)inv.s.size()) {
                Value o = inv; o.s = IStr(r); o.s.promote(); return o;
            }
        }
    }
    // a binary buffer has no string semantics: .Str is an error (use .decode)
    // — except an ENCODING-typed blob, which knows how to read itself: Rakudo's
    // `utf8.Str` is `.decode`, and PSGI stringifies a `Str.encode` body exactly
    // so (`$output ~= $segment.Str`).
    if (inv.t == VT::Str && (inv.hashKind == "Buf" || inv.hashKind == "Blob") &&
        m == "Str") {
        if (inv.enumName == "utf8" || inv.enumName == "utf16" || inv.enumName == "utf32") {
            ValueList none; return methodCall(inv, "decode", none);
        }
        throwTyped("X::Buf::AsStr", {{"method", "Str"}},
                   "Cannot use a Buf as a string, but you called the Str method on it");
    }
    // reverse/rotate are illegal only on a MULTI-dimensional fixed array; a 1-dim
    // shaped array reverses/rotates fine (returns a reordered list, no resize).
    if (inv.t == VT::Array && inv.shape() && inv.shape()->size() >= 2 && (m == "reverse" || m == "rotate"))
        throwTypedV("X::IllegalOnFixedDimensionArray", {{"operation", Value::str(m)}},
                    "Cannot " + m + " a fixed-dimension array");
    // …and nothing changes the SIZE of a fixed array, whatever its dimensions
    if (inv.t == VT::Array && inv.shape() && !inv.shape()->empty() &&
        (m == "push" || m == "append" || m == "pop" || m == "unshift" || m == "prepend" ||
         m == "shift" || m == "splice"))
        throwTypedV("X::IllegalOnFixedDimensionArray", {{"operation", Value::str(m)}},
                    "Cannot " + m + " a fixed-dimension array");
    // Multi-dim shaped array (`my @a[2;2]`) — keys/values/kv/pairs/antipairs/flat/
    // iterator walk the LEAVES, keyed by index tuples. (A 1-dim shaped array uses
    // the ordinary Array handlers: keys are plain indices, .flat is a Seq, etc.)
    if (inv.t == VT::Array && inv.shape() && inv.shape()->size() >= 2 &&
        (m == "keys" || m == "values" || m == "kv" || m == "pairs" ||
         m == "antipairs" || m == "flat" || m == "iterator")) {
        size_t ndim = inv.shape()->size();
        std::vector<std::pair<Value, Value>> ents; // (index key, leaf value)
        std::vector<long long> idx;
        std::function<void(const Value&)> walk = [&](const Value& node) {
            if (idx.size() == ndim) {
                Value key;
                if (ndim == 1) key = Value::integer(idx[0]);
                else { key = Value::array(); key.isList = true;
                       for (auto ix : idx) key.arr()->push_back(Value::integer(ix)); }
                ents.push_back({key, node});
                return;
            }
            if (node.t == VT::Array && node.arr())
                for (size_t i = 0; i < node.arr()->size(); i++) {
                    idx.push_back((long long)i); walk((*node.arr())[i]); idx.pop_back();
                }
        };
        walk(inv);
        if (m == "iterator") {
            Value it = Value::makeHash(); it.hashKind = "Iterator";
            Value items = Value::array();
            for (auto& e : ents) items.arr()->push_back(e.second);
            (*it.hash())["items"] = items; (*it.hash())["pos"] = Value::integer(0);
            return it;
        }
        Value o = Value::array(); o.isList = true;
        for (auto& e : ents) {
            if (m == "keys") o.arr()->push_back(e.first);
            else if (m == "values" || m == "flat") o.arr()->push_back(e.second);
            else if (m == "kv") { o.arr()->push_back(e.first); o.arr()->push_back(e.second); }
            else if (m == "pairs") { Value p = Value::pair(e.first.toStr(), e.second); p.pairKeyM() = std::make_shared<Value>(e.first); o.arr()->push_back(std::move(p)); }
            else { Value p = Value::pair(e.second.toStr(), e.first); p.pairKeyM() = std::make_shared<Value>(e.second); o.arr()->push_back(std::move(p)); } // antipairs
        }
        return o;
    }
    // A multi-dim shaped array renders its structure: rows on their own lines for
    // .gist, and a `Array.new(:shape(…), row, …)` constructor for .raku.
    if (inv.t == VT::Array && inv.shape() && !inv.shape()->empty() && inv.arr() &&
        (m == "gist" || m == "raku")) {
        // A ONE-dimensional shaped array has no rows to lay out, but it still
        // round-trips through the shaped constructor: `my @a[2]` is
        // `Array.new(:shape(2,), [1, Any])` (sheet LA-20).
        if (inv.shape()->size() == 1) {
            if (m == "gist") return Value::str(gistOf(inv));
            std::string ctor1;
            if (inv.ofType().empty() || inv.ofType() == "Any" || inv.ofType() == "Mu") ctor1 = "Array";
            else if (ascii::islower((unsigned char)inv.ofType()[0])) ctor1 = "array[" + inv.ofType() + "]";
            else ctor1 = "Array[" + inv.ofType() + "]";
            std::string o1 = ctor1 + ".new(:shape(" + std::to_string((*inv.shape())[0]) + ",), [";
            for (size_t i = 0; i < inv.arr()->size(); i++) {
                if (i) o1 += ", ";
                ValueList none;
                o1 += methodCall((*inv.arr())[i], "raku", none).toStr();
            }
            return Value::str(o1 + "])");
        }
        if (m == "gist") {
            std::string out = "[";
            for (size_t i = 0; i < inv.arr()->size(); i++) { if (i) out += "\n "; out += gistOf((*inv.arr())[i]); }
            return Value::str(out + "]");
        }
        std::string ctor;
        if (inv.ofType().empty() || inv.ofType() == "Any" || inv.ofType() == "Mu") ctor = "Array";
        else if (ascii::islower((unsigned char)inv.ofType()[0])) ctor = "array[" + inv.ofType() + "]";
        else ctor = "Array[" + inv.ofType() + "]";
        std::string out = ctor + ".new(:shape(";
        for (size_t i = 0; i < inv.shape()->size(); i++) { if (i) out += ", "; out += std::to_string((*inv.shape())[i]); }
        out += ")";
        // The ROWS render as plain brackets: the element type is named ONCE, by
        // the constructor. (A row is an Array carrying the same ofType, and
        // letting it answer for itself printed a nested `array[int].new(…)`
        // inside the shaped one.)
        std::function<void(const Value&)> strip = [&](const Value& n) {
            const_cast<Value&>(n).ofTypeM().clear();
            if (n.t == VT::Array && n.arr()) for (auto& e : *n.arr()) strip(e);
        };
        for (auto& row : *inv.arr()) {
            Value r = row;
            if (r.t == VT::Array && r.arr()) { r.setArr(makePayload<ValueList>(*r.arr())); strip(r); }
            ValueList none;
            out += ", " + methodCall(r, "raku", none).toStr();
        }
        return Value::str(out + ")");
    }
    if (inv.t == VT::Array && inv.shape() && !inv.shape()->empty() && inv.arr() && m == "clone") {
        Value c = inv; // deep-copy the nested storage so containers are independent
        std::function<Value(const Value&)> deep = [&](const Value& n) -> Value {
            if (n.t == VT::Array && n.arr()) { Value a = n; a.setArr(makePayload<ValueList>());
                for (auto& e : *n.arr()) a.arr()->push_back(deep(e)); return a; }
            return n;
        };
        c = deep(inv);
        c.shapeM() = std::make_shared<std::vector<long long>>(*inv.shape());
        return c;
    }
    // Most list operations on a multi-dim shaped array run over its LEAVES — flatten
    // the fixed structure to a plain list and delegate.
    if (inv.t == VT::Array && inv.shape() && inv.shape()->size() >= 2 && inv.arr() &&
        (m == "join" || m == "map" || m == "grep" || m == "combinations" ||
         m == "permutations" || m == "rotor" || m == "pick" || m == "roll" ||
         m == "first" || m == "reduce" || m == "sum" || m == "min" || m == "max" ||
         m == "sort" || m == "reverse" || m == "List" || m == "Seq" || m == "Slip" ||
         m == "Bag")) {
        Value flat = Value::array(); flat.isList = true;
        std::function<void(const Value&)> collect = [&](const Value& n) {
            if (n.t == VT::Array && n.arr()) for (auto& e : *n.arr()) collect(e);
            else flat.arr()->push_back(n);
        };
        for (auto& e : *inv.arr()) collect(e);
        return methodCall(flat, m, args, rwArgs);
    }
    // (`$j.printf` / `$j.sprintf` AUTOTHREAD like the other methods below: the
    // format is a Cool, and a Junction is not one — each eigenstate is a format)
    // any other method on a junction AUTOTHREADS: call it on each eigenstate,
    // return a junction of the results (`($a & $b).finish`, `$j.defined`, …)
    // (a METAMODEL call `.^name`/`.^WHAT`/… answers for the Junction ITSELF and is
    //  excluded here — `(1 & 2).^name` is "Junction", not a junction of "Int")
    if (!inv.enumName.empty() && inv.t == VT::Array && inv.arr() && !(m.size() && m[0] == '^') &&
        (inv.enumName == "any" || inv.enumName == "all" || inv.enumName == "one" || inv.enumName == "none")) {
        static const std::set<std::string> junctionOwn = {
            "Bool", "so", "not", "gist", "raku", "perl", "WHAT", "WHO", "HOW",
            "return", "return-rw", // control flow acts on the junction, not each state
            "WHICH", "WHY", "item", "new", "defined-or", "THREAD",
            "DEFINITE",             // a Junction IS a defined object — but `.defined` is not that
                                    // question: it autothreads and collapses (see below)
            "say", "note"};         // .say/.note gist the junction ("all(1, 2)"); .print autothreads
        // `.defined` autothreads over the eigenstates and COLLAPSES to a plain Bool —
        // `(none 3, Str).defined` is False. It is not an alias for `.Bool`:
        // `(any 0, "").defined` is True while `.Bool` is False.
        if (m == "defined") {
            Value j = Value::array(); j.enumName = inv.enumName;
            j.setArr(makePayload<ValueList>());
            for (auto& el : *inv.arr()) j.arr()->push_back(Value::boolean(defined(el)));
            return Value::boolean(j.truthy());
        }
        if (m == "THREAD" && !args.empty()) {
            // shallow map: the block sees each eigenstate whole (junctions included)
            Value out = Value::array(); out.enumName = inv.enumName;
            out.setArr(makePayload<ValueList>());
            for (auto& el : *inv.arr()) {
                ValueList one{el};
                tctx_.noAutothread = true;
                out.arr()->push_back(callCallable(args[0], one));
            }
            return out;
        }
        // A SLICE-derived junction (`%h{any ^2}` / `@a[any(0,1)]`; s carries the
        // provenance) takes the resizing mutators on its backing array instead of
        // autothreading them: each thread would die on an Any eigenstate, where
        // Rakudo's slice junction threads over CONTAINERS that autovivify —
        // machinery rakupp does not have. Mutating the temporary keeps it the
        // soft no-op it always was, not a file-killing death.
        static const std::set<std::string> sliceResizers = {
            "push", "append", "pop", "unshift", "prepend", "shift", "splice"};
        if (!junctionOwn.count(m) && !(inv.s == "slice" && sliceResizers.count(m))) {
            Value out = Value::array(); out.enumName = inv.enumName;
            out.setArr(makePayload<ValueList>());
            for (auto& el : *inv.arr()) out.arr()->push_back(methodCall(el, m, args, rwArgs));
            return out;
        }
    }
    // `augment class Int {…}`: methods added to a built-in type are parked in
    // builtinExt_ (keyed by type name). Consult it — walking the native ancestry,
    // so augmenting Cool/Any reaches Int/Str too — for native values and type
    // objects, ahead of the built-in method table.
    if (Value* f = builtinExtMethod(inv, m)) {
        // An augment ADDS candidates; it does not hide the built-in ones. A multi
        // whose candidates all reject the args falls through to the built-in
        // method instead of dying — `augment class DateTime { multi method
        // new(Any:U) {…} }` must leave `DateTime.new($str)` working (JSON::Fast).
        if (f->code() && f->code()->isMultiDispatcher) {
            bool anyFits = false;
            ValueList withInv; withInv.push_back(inv);
            for (auto& a : args) withInv.push_back(a);
            for (auto& c : f->code()->candidates) {
                if (c.code() && c.code()->isProto) continue;
                if (scoreCandidate(c, withInv) >= 0 || scoreCandidate(c, args) >= 0) { anyFits = true; break; }
            }
            if (!anyFits) goto builtinExtFallthrough;
        }
        return invokeMethod(*f, inv, std::move(args), rwArgs);
    }
    builtinExtFallthrough:;
    // Any is not Cool: string methods on an UNDEFINED invocant die in Rakudo
    // ("Cannot resolve caller split(Any:U: …)"), typically after `prompt`/`get`
    // hit EOF. Everything else on Any stays lenient.
    if (inv.t == VT::Any) {
        static const std::set<std::string> strOnUndef = {
            "split", "comb", "words", "chars", "codes", "lc", "uc", "tc", "fc",
            "tclc", "wordcase", "flip", "substr", "subst", "trans", "index",
            "rindex", "starts-with", "ends-with", "contains", "match", "base",
            "ord", "ords", "encode", "parse-base"};
        if (strOnUndef.count(m))
            throw RakuError{Value::typeObj("X::Method::NotFound"),
                "Cannot resolve caller " + m + "(Any:U); the invocant is a type object, not an instance"};
    }
    // CompUnit — what `$*REPO.need(…)` answers. A module loaded from source is
    // held in the precomp cache (a serialized tree) once compiled, so it
    // reports itself precompiled.
    if (inv.t == VT::Hash && inv.hashKind == "CompUnit" && inv.hash()) {
        auto& h = *inv.hash();
        if (m == "precompiled") {
            auto it = h.find("precompiled");
            return it != h.end() ? it->second : Value::boolean(true);
        }
        if (m == "from") return Value::str("Raku");
        // the unit's handle: its GLOBALish package is the one every loaded
        // unit merges into here
        if (m == "handle") {
            Value hd = Value::makeHash(); hd.hashKind = "CompUnit::Handle";
            auto it = h.find("short-name");
            if (it != h.end()) (*hd.hash())["short-name"] = it->second;
            return hd;
        }
        if (m == "short-name" || m == "repo" || m == "version" || m == "distribution") {
            auto it = h.find(m);
            return it != h.end() ? it->second : Value::any();
        }
        if (m == "Str" || m == "gist") {
            auto it = h.find("short-name");
            return it != h.end() ? it->second : Value::str("");
        }
    }
    // (the unit's OWN GLOBALish: the packages it declared, and what they hold)
    if (inv.t == VT::Hash && inv.hashKind == "CompUnit::Handle" && m == "globalish-package" && inv.hash()->count("short-name")) {
        auto us = unitStash_.find((*inv.hash())["short-name"].toStr());
        if (us != unitStash_.end()) {
            Value st = Value::makeHash(); st.hashKind = "Stash"; st.s = "GLOBAL";
            for (auto& kv : us->second.globalish)
                if (kv.second) (*st.hash())[kv.first] = Value::typeObj(kv.second->fq);
            globalishUnits_[(const void*)st.hash()] = us->first;   // (merge-symbols joins this unit's view)
            return st;
        }
    }
    if (inv.t == VT::Hash && inv.hashKind == "CompUnit::Handle" &&
        (m == "globalish-package" || m == "unit" || m == "export-package"))
        return evalString(m == "globalish-package" ? "GLOBAL" : m == "unit" ? "UNIT::" : "EXPORT");
    // IterationBuffer — a low-level mutable element buffer (the iterator protocol's
    // scratch space), a growable list under the hood. Handled up front so its
    // `.elems`/`.List`/… win over the generic Hash methods (it is a hashKind Hash).
    if (inv.t == VT::Hash && inv.hashKind == "IterationBuffer") {
        auto& items = *(*inv.hash())["items"].arr();
        auto asList = [&]() { Value o = Value::array(); o.isList = true; *o.arr() = items; return o; };
        if (m == "elems" || m == "Numeric" || m == "Int") return Value::integer((long long)items.size());
        if (m == "AT-POS") { long i = a0().toInt(); return (i >= 0 && i < (long)items.size()) ? items[i] : Value::typeObj("Mu"); }
        if (m == "push")    { items.push_back(a0()); return a0(); }
        if (m == "unshift") { items.insert(items.begin(), a0()); return a0(); }
        if (m == "BIND-POS") {
            long i = args.empty() ? 0 : args[0].toInt();
            Value val = args.size() > 1 ? args[1] : Value::any();
            if ((long)items.size() <= i) items.resize(i + 1);
            if (i >= 0) items[i] = val;
            return val;
        }
        if (m == "List" || m == "Seq" || m == "Slip" || m == "list") {
            Value r = asList();
            if (m == "Slip") r.s = "Slip"; // Slips splice into list-building contexts
            return r;
        }
        if (m == "append" || m == "prepend") {
            ValueList add;
            for (auto& a : args) {
                if (a.t == VT::Hash && a.hashKind == "IterationBuffer") for (auto& x : *(*a.hash())["items"].arr()) add.push_back(x);
                else for (auto& x : toList(a)) add.push_back(x);
            }
            if (m == "append") items.insert(items.end(), add.begin(), add.end());
            else items.insert(items.begin(), add.begin(), add.end());
            return inv;
        }
        if (m == "clear") { items.clear(); return Value::nil(); }
        if (m == "raku" || m == "gist" || m == "Str") return Value::str("IterationBuffer.new(...)");
    }
    // A class inheriting a built-in type answers that type's identity coercion with
    // itself: `class D is Str {}` → D.new.Str === the D object (Str.Str is identity).
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls && !inv.obj()->cls->findMethod(m)) {
        static const std::set<std::string> idTypes = {"Str", "Int", "Num", "Rat", "Bool", "Real", "Numeric"};
        if (idTypes.count(m))
            for (ClassInfo* ci = inv.obj()->cls.get(); ci; ci = ci->parent.get())
                if (ci->nativeParent == m) return inv;
    }
    // `.resume` inside a CATCH: unwind to the enclosing block, which continues
    // execution at the statement after the one that threw. With NO handler on
    // the stack a ResumeEx would escape to std::terminate — die catchably.
    if (m == "resume") {
        if (catchDepth_ == 0)
            throw RakuError{Value::typeObj("X::Parameter::InvalidConcreteness"),
                            "Cannot resume without an active exception handler"};
        throw ResumeEx{};
    }
    // 6.e `.Callable($name)`: the named method as a callable, or a Failure when
    // there is none — a findable method reference, next to .can's list.
    if (m == "Callable" && sixE() && !args.empty() && args[0].t == VT::Str) {
        std::string want = args[0].toStr();
        Value found = methodCall(inv, "can", ValueList{Value::str(want)});
        if (found.t == VT::Array && found.arr() && !found.arr()->empty()) return (*found.arr())[0];
        Value f = rakuppNewFailure();
        (*f.hash())["exception"] = Value::typeObj("X::Method::NotFound");
        (*f.hash())["message"]   = Value::str("No such method '" + want + "' for invocant of type '" +
                                            inv.typeName() + "'");
        return f;
    }
    // 6.e `.snitch`: run a tap (default: note the value) and return self — for
    // sticking a peek into a method chain. Universal, so handle it up front.
    if (m == "snitch" && sixE()) {
        if (!args.empty() && args[0].t == VT::Code) callCallable(args[0], {inv});
        else std::cerr << gistOf(inv) << "\n";
        return inv;
    }
    // `.are`/`.snip` on a type object or lone scalar treat it as a 1-element list
    // (so `Int.are` → Int, `42.are` → Int).
    // …and a Date/DateTime is a VALUE, not a collection: it only happens to be
    // stored as a tagged Hash, so `$date.are` walked its FIELDS and answered
    // Pair. (Data::TypeSystem deduced Pair for every date column.)
    auto dateish = [](const Value& v) {
        return v.t == VT::Hash && (v.hashKind == "Date" || v.hashKind == "DateTime" ||
                                   v.hashKind == "Instant" || v.hashKind == "Duration");
    };
    // …but a Supply TYPE object is not one item to snip: `Supply.snip` is a
    // method that wants an instance, and Roast asks for the error (snip.t).
    if ((m == "are" || m == "snip") && inv.t != VT::Array && inv.t != VT::Range &&
        !(inv.t == VT::Type && inv.s == "Supply") &&
        (inv.t != VT::Hash || dateish(inv))) {
        // …except that `.are(T)` on a TYPE OBJECT asks about the invocant
        // itself, not about a one-element list around it, and says so: Rakudo's
        // `Any:U:` candidate reports "Expected 'Str' but got 'Int'" with no
        // element index, where the `Any:D:` one appends " in element 0"
        // (roast S29-any/are.t asserts both wordings side by side).
        if (m == "are" && !args.empty() && inv.t == VT::Type) {
            if (applyArith("~~", inv, args[0]).truthy()) return Value::boolean(true);
            Value f = rakuppNewFailure();
            (*f.hash())["exception"] = Value::typeObj("X::AdHoc");
            (*f.hash())["message"]   = Value::str("Expected '" + typeOfVal(args[0]) +
                                                  "' but got '" + typeOfVal(inv) + "'");
            return f;
        }
        Value one = Value::array(); one.isList = true; one.arr()->push_back(inv);
        return methodCall(one, m, args, rwArgs);
    }

    // Nil REFUSES to be mutated, and says so with the type Rakudo names
    // (Nil-Any sheet NA-06; roast S02-types/nil.t asserts that each throws).
    // `.STORE` and the assigning subscripts are read-only targets; the
    // list mutators are an outright misuse; the BINDING subscripts hand back a
    // Failure carrying X::Bind, so they only detonate when the value is used.
    if (inv.t == VT::Nil) {
        // Nil is a Cool, so the STRING methods it inherits run on the empty
        // string — and every one of them answers a Str, `chars` and `contains`
        // included (Nil-Any sheet NA-07). Before the list rules below, or
        // `.comb`/`.words` would answer the one-element list instead.
        static const std::set<std::string> kStrOnNil = {
            "chars", "chomp", "chop", "codes", "comb", "contains", "ends-with",
            "flip", "indent", "index", "indices", "lc", "lines", "tc", "tclc",
            "rindex", "starts-with", "trans", "substr", "subst", "substr-eq",
            "substr-rw", "wordcase", "words", "uc"};
        if (kStrOnNil.count(m)) {
            const std::string msg = "Use of Nil." + (const std::string&)m +
                                    " coerced to empty string";
            if (quietDepth_ == 0 && !runControlWarn(msg)) std::cerr << msg << "\n";
            return Value::str("");
        }
        // Nil numifies to the INT zero, as `.Int` already did — `.Numeric` went
        // through the generic Num path and answered `0e0` (Nil-Any sheet NA-08).
        if (m == "Numeric" || m == "Real") return Value::integer(0);
        // `.ACCEPTS` is the one question Nil answers rather than absorbs: it
        // is what `$x ~~ Nil` asks, and returning Nil for it made the
        // smartmatch neither true nor false. Only Nil itself — and a Failure,
        // which is a Nil — matches (Nil-Any sheet NA-10).
        if (m == "ACCEPTS")
            return Value::boolean(!args.empty() &&
                                  (args[0].t == VT::Nil ||
                                   (args[0].t == VT::Hash && args[0].hashKind == "Failure")));
        // Reading through Nil is Nil, however many indices are handed over:
        // `Nil.AT-POS(0, 1, 2)` is one Nil, not a slice (NA-15).
        if (m == "AT-POS" || m == "AT-KEY" || m == "DELETE-POS" || m == "DELETE-KEY")
            return Value::nil();
        if (m == "STORE" || m == "ASSIGN-POS" || m == "ASSIGN-KEY")
            throw RakuError{Value::typeObj("X::Assignment::RO"),
                            "Cannot modify an immutable Nil"};
        if (m == "push" || m == "append" || m == "unshift" || m == "prepend")
            throw RakuError{Value::typeObj("X::AdHoc"),
                            "Use of Nil." + (const std::string&)m + " not allowed"};
        if (m == "BIND-POS" || m == "BIND-KEY") {
            Value f = rakuppNewFailure();
            const std::string msg = "Cannot use bind operator with this left-hand side";
            (*f.hash())["exception"] =
                // X::Bind's .target NAMES what was bound to, as a phrase
                // ("a call", "Nil") — it is not the value itself.
                makeTypedEx("X::Bind", {{"target", Value::str("Nil")}}, msg);
            (*f.hash())["message"] = Value::str(msg);
            return f;
        }
    }
    // An undefined invocant — Any, or Nil — IS a list: the ONE-element list
    // holding itself. `Any.map({$_})` is `((Any),)` and `Nil.sort` is `(Nil,)`,
    // because Rakudo reaches these through Any's iterable methods and an
    // undefined invocant iterates as one element (Nil-Any sheet NA-03, NA-04,
    // NA-16). The key/value family is the one exception: a value with no
    // ELEMENTS has nothing to pair up, so those six answer the empty list
    // (NA-04, NA-18) — which is also what an unmatched named capture used as
    // `@<x>.kv` wants.
    if ((inv.t == VT::Any || inv.t == VT::Nil) &&
        (m == "keys" || m == "values" || m == "kv" || m == "pairs" ||
         m == "antipairs" || m == "invert")) {
        Value o = Value::array(); o.isList = true; return o;
    }
    if ((inv.t == VT::Any || inv.t == VT::Nil) &&
        (m == "map" || m == "grep" || m == "list" || m == "flat" || m == "reverse" ||
         m == "sort" || m == "first" || m == "head" || m == "tail" || m == "join" ||
         m == "skip" || m == "unique")) { // sum/min/max/minmax want a DEFINED
                                          // invocant — see NA-05, NA-23
        Value o = Value::array(); o.isList = true; o.arr()->push_back(inv);
        return methodCall(o, m, args, rwArgs);
    }
    // `.ast`/`.made` on an undefined capture (e.g. `$<optional><tag>.ast`) degrades to Nil.
    if ((inv.t == VT::Any || inv.t == VT::Nil) && (m == "ast" || m == "made")) return Value::nil();

    // metamodel call .^method — .^name/.^WHAT answer the type; others dispatch by bare name
    // a Scalar container record (from `.VAR` on a $-variable): its own name/default,
    // .^name = Scalar via typeName; anything else answers from the held value.
    if (inv.t == VT::Hash && inv.hashKind == "Scalar" && inv.hash() &&
        m != "^name" && m != "WHAT" && m != "WHICH" && m != "raku") {
        if (m == "name")    { auto it = inv.hash()->find("name");    return it != inv.hash()->end() ? it->second : Value::any(); }
        if (m == "dynamic") { // a $*twigil variable is dynamic
            auto it = inv.hash()->find("name");
            std::string n = it != inv.hash()->end() ? it->second.toStr() : "";
            // …and so are the match and error variables (`$/.VAR.dynamic`)
            return Value::boolean((n.size() > 1 && n[1] == '*') || n == "$/" || n == "$!");
        }
        if (m == "default") { auto it = inv.hash()->find("default"); return it != inv.hash()->end() ? it->second : Value::any(); }
        if (m == "of")      { auto it = inv.hash()->find("default"); return (it != inv.hash()->end() && it->second.t == VT::Type) ? it->second : Value::typeObj("Mu"); }
        auto vi = inv.hash()->find("value");
        if (vi != inv.hash()->end()) return methodCall(vi->second, m, std::move(args), rwArgs);
    }
    if (!m.empty() && m[0] == '^') {
        std::string mm = m.substr(1);
        // A COERCION type object (`Foo(Str)`) carries both halves in its name,
        // which is the only place they fit — a type object IS a name here.
        // Getopt::Long picks an option's conversion through exactly this
        // surface: parse the argument as the CONSTRAINT type, and unless the
        // result already is the TARGET type, hand it to the target's COERCE.
        if (mm == "constraint_type" || mm == "target_type" || mm == "coerce") {
            size_t o = inv.t == VT::Type ? inv.s.find('(') : std::string::npos;
            if (o != std::string::npos && o > 0 && !inv.s.empty() && inv.s.back() == ')') {
                std::string target = inv.s.substr(0, o);
                std::string from = inv.s.substr(o + 1, inv.s.size() - o - 2);
                if (mm == "constraint_type") return Value::typeObj(from.empty() ? "Any" : from);
                if (mm == "target_type") return Value::typeObj(target);
                if (args.empty()) return Value::any();
                // a value the CONSTRAINT refuses cannot be coerced at all
                // (`Int(Str).^coerce(pi)`), and one already the target stays
                if (!from.empty() && from != "Any" && from != "Mu" && !typeOrSubsetMatches(args[0], from) &&
                    !typeOrSubsetMatches(args[0], target))
                    throwTypedV("X::Coerce::Impossible",
                        {{"target-type", Value::typeObj(target)}, {"from-type", Value::typeObj(args[0].typeName())}},
                        "Impossible coercion from '" + args[0].typeName() + "' into '" + target +
                        "': no acceptable coercion method found");
                if (typeOrSubsetMatches(args[0], target) && rtIsDefined(args[0])) return args[0];
                {   // a built-in target has no COERCE method: its own coercer
                    auto uc = classes_.find(target);
                    if (uc == classes_.end() || !uc->second->findMethod("COERCE"))
                        return coerceToType(args[0], target);
                }
                return coerceThroughType(args[0], target, inv.s);
            }
        }
        if (mm == "name") {
            if (inv.t == VT::Type && inv.s.rfind("Metamodel::", 0) == 0)
                return Value::str("Perl6::" + inv.s.str()); // Rakudo's full metaclass name
            // …and the same for a metaobject that is a real OBJECT rather than a
            // type: a user class's `.HOW` is one, and `class C { }.HOW.^name`
            // answered the bare `Metamodel::ClassHOW` where Rakudo prefixes it.
            if (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
                inv.obj()->cls->name.rfind("Metamodel::", 0) == 0)
                return Value::str("Perl6::" + inv.obj()->cls->name);
            // An OBJECT hash is a PARAMETERIZED Hash: `my Int %h{Str}` is a
            // Hash[Int,Str]. Only the name carries the parameters — typeName()
            // stays "Hash", because dispatch and error messages key on it.
            if (!objHashKeyType(inv).empty()) return Value::str("Hash[" + inv.ofType() + "]");
            // …and so is a typed Array or Hash: `my Int @a; @a.^name` is
            // `Array[Int]` (sheet LA-21). Same rule — the parameter shows in
            // the NAME only, and a parameterized TYPE OBJECT already carries it
            // in typeName().
            if ((inv.t == VT::Array || inv.t == VT::Hash) && !inv.isList &&
                !inv.ofType().empty() && inv.ofType() != "Mu" &&
                inv.typeName().find('[') == std::string::npos)
                return Value::str(inv.typeName() + "[" + inv.ofType() + "]");
            // a DEFINITENESS-constrained type reports its smiley: `Any:D.^name`
            if (inv.t == VT::Type && inv.i)
                return Value::str(inv.typeName() + (inv.i == 1 ? ":D" : ":U"));
            // …and a class RENAMED through `.^set_name` answers its current name
            // even through a handle that still carries the old one — the name
            // lives on the metaobject, which is what set_name mutates.
            if (inv.t == VT::Type && inv.ofType().empty()) {
                auto cn = classes_.find(inv.s);
                if (cn != classes_.end() && cn->second && !cn->second->name.empty())
                    // a ROLE PUN answers what it was WRITTEN as — `Foo[Int]` —
                    // not the registry key that keeps two of them one type
                    return Value::str(cn->second->dispName.empty() ? cn->second->name
                                                                   : cn->second->dispName);
            }
            {   // …and so does an INSTANCE of one: `Foo[Int].new.^name`
                auto cn = classes_.find(inv.typeName());
                if (cn != classes_.end() && cn->second && !cn->second->dispName.empty())
                    return Value::str(cn->second->dispName);
            }
            return Value::str(inv.typeName());
        }
        // `.^base_type` — the same type without its definiteness constraint
        if (mm == "base_type" && inv.t == VT::Type) {
            Value b = Value::typeObj(inv.s); b.ofTypeM() = inv.ofType(); return b;
        }
        // `.^array_type` — the ELEMENT type of a buffer, which is how
        // NativeHelpers::Blob decides what to allocate. A plain Blob/Buf is
        // uint8; the sized spellings carry theirs in ofType, and utf8 is uint8
        // under its own name. (Rakudo answers this for a buffer VALUE and for
        // utf8; a bare `blob8` type object has no such method there either.)
        if (mm == "array_type" && inv.t == VT::Str &&
            (inv.hashKind == "Buf" || inv.hashKind == "Blob"))
            return Value::typeObj(inv.ofType().empty() ? "uint8" : inv.ofType());
        // The utf* TYPE OBJECTS answer it as well — and only those. Measured:
        // Rakudo gives utf8/utf16/utf32 their element type and refuses blob8,
        // blob32, Buf and Blob, which are aliases rather than classes there.
        if (mm == "array_type" && inv.t == VT::Type) {
            if (inv.s == "utf8")  return Value::typeObj("uint8");
            if (inv.s == "utf16") return Value::typeObj("uint16");
            if (inv.s == "utf32") return Value::typeObj("uint32");
        }
        if (mm == "shortname") { // type name without its package qualifier
            std::string n = inv.typeName();
            {   // a role pun is known by its display name, `Foo::Bar[Int]`
                auto cn = classes_.find(n);
                if (cn != classes_.end() && cn->second && !cn->second->dispName.empty())
                    n = cn->second->dispName;
            }
            // EVERY name in the rendering is shortened, not just the head:
            // Rakudo answers `Baz[Foo[Int],Bar[Int]]` for
            // `Foo::Bar::Baz[Foo[Int],Foo::Bar[Int]]`, so the parameters lose
            // their qualifiers too. (For an unparameterized name this is the
            // same single-segment strip it always did.)
            std::string o;
            size_t seg = 0;
            for (size_t i = 0; i <= n.size(); i++) {
                if (i != n.size() && n[i] != '[' && n[i] != ']' && n[i] != ',') continue;
                std::string part = n.substr(seg, i - seg);
                size_t cut = part.rfind("::");
                o += cut == std::string::npos ? part : part.substr(cut + 2);
                if (i < n.size()) o += n[i];
                seg = i + 1;
            }
            return Value::str(o);
        }
        // `.^mixin(Role)` mixes IN PLACE — it reblesses the object itself, so every
        // reference to it sees the role, which is what separates it from `but`.
        // (It answers here, ahead of the `tobj` collapse below, which would replace
        // an object invocant with its bare type object and lose the thing to mix
        // into.) As a copy it was a silent no-op for its callers: PDF::COS::Tie's
        // `method mixin($role) { $.^mixin($role); $.tie-init }` is how every PDF
        // dictionary takes on its entry type, and the object came back unchanged.
        if (mm == "mixin") {
            Value r = inv;
            for (auto& a : args) r = mixinValue(std::move(r), a, /*copy=*/false);
            return r;
        }
        if (mm == "WHAT") return Value::typeObj(inv.typeName());
        // A user-declared META-METHOD wins over the builtin one: `method
        // ^parameterize(Mu:U \obj, **@pos)` is how a class makes ITSELF
        // parameterizable, and the builtin below would quietly answer `X[Int]`
        // instead of running it (Parameterizable).
        {
            std::string tn = inv.t == VT::Type ? inv.s : inv.typeName();
            auto cit2 = classes_.find(tn);
            if (cit2 != classes_.end() && cit2->second)
                if (Value* umm = cit2->second->findMethod("^" + mm)) {
                    Value tobj2 = Value::typeObj(tn);
                    ValueList a2; a2.reserve(args.size() + 1);
                    a2.push_back(tobj2);
                    for (auto& a : args) a2.push_back(a);
                    return invokeMethod(*umm, tobj2, a2, nullptr);
                }
        }
        // `X.^parameterize(T)` yields the parameterized type `X[T]` (same as `X[T]`)
        if (mm == "parameterize") {
            // …and a parametric ROLE's is its pun, exactly as `R[T]` makes it
            if (inv.t == VT::Type) {
                auto rit = classes_.find(inv.s);
                if (rit != classes_.end() && rit->second && rit->second->isRole && rit->second->decl &&
                    (!rit->second->decl->roleParams.empty() || !rit->second->roleVariants.empty())) {
                    ValueList av = args;
                    ClassInfo* role = rit->second->roleVariants.empty()
                                    ? rit->second.get() : pickRoleVariantValues(rit->second, av).get();
                    return makeRolePun(role, inv.s, av);
                }
            }
            Value ty = Value::typeObj(inv.t == VT::Type ? inv.s : inv.typeName());
            for (auto& a : args) {
                std::string pn = a.t == VT::Type ? a.s : a.typeName();
                ty.ofTypeM() = ty.ofType().empty() ? pn : ty.ofType() + "," + pn;
            }
            return ty;
        }
        // meta-methods (.^methods/.^attributes/.^parents/…) resolve against the
        // type (HOW), even when called on an instance.
        Value tobj = (inv.t == VT::Object && inv.obj() && inv.obj()->cls) ? Value::typeObj(inv.obj()->cls->name) : inv;
        // (an object of one candidate of a role group answers for THAT
        // candidate, which the group's name alone does not say)
        if (mm == "language-revision" && args.empty() && inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
            inv.obj()->cls->langRev >= 0) {
            const int r = inv.obj()->cls->langRev;
            return Value::str(r == 0 ? "c" : r == 1 ? "d" : "e");
        }
        // an instance of a ROLE is its pun, and the pun DOES the role:
        // `B.new.^roles` is (B, A…) where `B.^roles` is only what B does
        if (mm == "roles" && inv.t == VT::Object && inv.obj() && inv.obj()->cls && inv.obj()->cls->isRole) {
            Value rest = methodCall(tobj, m, args, rwArgs);
            Value out = Value::array(); out.isList = true;
            out.arr()->push_back(tobj);
            bool transitive = true;
            for (auto& a : args) if (a.t == VT::Pair && a.s == "transitive") transitive = !a.pairVal() || a.pairVal()->truthy();
            // (a pun's own roles already name the pun: `VR[Int].new.^roles`
            // is VR[Int] once)
            const auto& own = inv.obj()->cls;
            if (transitive && rest.t == VT::Array && rest.arr())
                for (auto& r : *rest.arr())
                    if (!(r.t == VT::Type && (r.s == own->name || (!own->dispName.empty() && r.s == own->dispName))))
                        out.arr()->push_back(r);
            return out;
        }
        // …and the LINEARISATION questions resolve against the type object for a
        // plain value too: `42.^mro` is `(Int, Cool, Any, Mu)` and `Nil.^mro` is
        // `(Nil, Cool, Any, Mu)`. They used to be "no such method" on anything
        // that was not already a type object (Nil-Any sheet NA-02).
        if ((mm == "mro" || mm == "parents") && inv.t != VT::Type && inv.t != VT::Object)
            return methodCall(Value::typeObj(inv.typeName()), m, std::move(args), rwArgs);
        if ((mm == "lookup" || mm == "find_method") &&
            !(tobj.t == VT::Type && classes_.count(tobj.s))) {
            // builtin-type invocant (`().^lookup('elems')`): a "method object" —
            // a Callable that dispatches the named method on its first argument
            std::string mn = args.empty() ? "" : args[0].toStr();
            // …but Rakudo answers Mu when the type does NOT have the method, and
            // modules gate on exactly that: HTTP::Tiny tells an IO::Path form
            // field from a plain string with `$value.^lookup('slurp')`, and an
            // always-yes answer made it try to slurp the string. rakupp has no
            // table of builtin methods — dispatch is an if-chain over string
            // literals — so the only oracle is to TRY it and read the answer off
            // X::Method::NotFound. The probe runs on a sentinel of the invocant's
            // type (a real IO::Path is swapped for one that cannot exist, so a
            // reader throws "failed to open" rather than reading anything), and
            // names whose dispatch would WRITE are not probed at all: they keep
            // the old permissive answer rather than risk the side effect.
            // A TYPE object is not probeable — `Str.^lookup('parse-base')` is the
            // idiomatic way to reach a method object, and the instance dispatch it
            // names cannot run on the type. Only an INSTANCE invocant is probed.
            // (probeMethodExists carries the shared unsafe-names list and the
            // NotFound dance — one copy, shared with .can's fallback.)
            // A TYPE OBJECT of a builtin is probed through a SENTINEL instance,
            // the way `.can` already does: `Int.^find_method('type')` has to be
            // falsy, and answering a method object for every name made a module
            // walking `while $obj.^find_method('type')` loop past the leaf type
            // and call `.type` on an Int (Data::TypeSystem's is-full-array).
            Value probeInv = inv;
            // A SUBSET probes as its base type — `UInt` is an Int and has exactly
            // Int's methods. Without this it had no sentinel at all, so every name
            // came back as a method object: Red asks
            // `$attr.type.^find_method("red-type-db-methods")` of each column's
            // declared type and then CALLS what it is handed, which died on a UInt
            // column for eleven of its test files (issue #77).
            std::string tn = inv.t == VT::Type ? inv.s : std::string();
            for (int hop = 0; hop < 4 && !tn.empty(); hop++) {
                auto sit = subsets_.find(tn);
                if (sit == subsets_.end() || sit->second.base.empty() || sit->second.base == tn) break;
                tn = sit->second.base;
            }
            // …and the ones the LANGUAGE defines, which are not in `subsets_`
            // because no Raku `subset` statement declared them here
            if (tn == "UInt" || tn == "IntStr" || tn == "int" || tn == "uint" ||
                tn == "int8" || tn == "int16" || tn == "int32" || tn == "int64" ||
                tn == "uint8" || tn == "uint16" || tn == "uint32" || tn == "uint64" ||
                tn == "byte" || tn == "atomicint" || tn == "Priority") tn = "Int";
            else if (tn == "NumStr" || tn == "num" || tn == "num32" || tn == "num64") tn = "Num";
            else if (tn == "StrStr") tn = "Str";
            if (inv.t == VT::Type && !classes_.count(inv.s)) {
                if (tn == "Str") probeInv = Value::str("");
                else if (tn == "Int") probeInv = Value::integer(0);
                else if (tn == "Num") probeInv = Value::number(0);
                else if (tn == "Bool") probeInv = Value::boolean(false);
                else if (tn == "Array" || tn == "List" || tn == "Positional") probeInv = Value::array();
                else if (tn == "Hash" || tn == "Map" || tn == "Associative") probeInv = Value::makeHash();
                // Date/DateTime answer their own methods and nothing else's, so the
                // sentinel has to be one of them — built through the ordinary
                // constructor, which reads a clock and touches nothing else.
                else if (tn == "DateTime" || tn == "Date") {
                    try { ValueList none; probeInv = methodCall(Value::typeObj(tn), "now", none); }
                    catch (...) { probeInv = inv; }
                }
            }
            if (!mn.empty() && probeInv.t != VT::Object && probeInv.t != VT::Type &&
                probeInv.t != VT::Any && probeInv.t != VT::Nil &&
                probeMethodExists(probeInv, mn, "/nonexistent/rakupp-lookup-probe") == -1)
                return Value::typeObj("Mu");
            // `Mu` and `Any` are the ROOT types: anything they have, every other
            // type has too. So a name that is not even on an Int cannot be on
            // them, and this probe only ever turns a wrong YES into a right NO —
            // never the reverse, which is why the answer is not read positively.
            if (!mn.empty() && inv.t == VT::Type && (inv.s == "Mu" || inv.s == "Any") &&
                probeMethodExists(Value::integer(0), mn, "/nonexistent/rakupp-lookup-probe") == -1)
                return Value::typeObj("Mu");
            Value code; code.t = VT::Code; code.setCode(std::make_shared<Callable>());
            code.code()->name = mn; code.code()->isMethod = true;
            code.code()->builtin = [mn](Interpreter& I, ValueList& a) -> Value {
                if (a.empty()) return Value::any();
                Value in2 = a[0]; ValueList rest(a.begin() + 1, a.end());
                return I.methodCall(in2, mn, rest);
            };
            return code;
        }
        // `.^methods` / `.^attributes` on a BUILTIN type: rakupp has no table to
        // enumerate (dispatch is an if-chain), so the honest answer is the empty
        // list — Data::Dump walks `.^mro[1..*]».^methods` to EXCLUDE inherited
        // methods, and dying on Any took the whole dump down.
        // Date/DateTime fall through: they DO answer .^attributes (the
        // synthesized Rakudo-shaped list JSON::Unmarshal rebuilds from).
        // .^method_names rides along with .^methods for the same reason.
        // …except the CORE types' own method surface, which is known: a
        // table per type, walked along the built-in MRO (Any and Mu only
        // under :all, as Rakudo's .^methods does)
        if ((mm == "methods" || mm == "method_names") &&
            !(tobj.t == VT::Type && classes_.count(resolveClassAlias(tobj.s)))) {
            static const std::map<std::string, std::vector<const char*>> kCoreMethods = {
                {"Mu", {"ACCEPTS", "WHICH", "WHERE", "WHY", "Bool", "Str", "Stringy", "gist", "raku",
                        "perl", "defined", "isa", "does", "can", "so", "not", "clone", "new", "bless",
                        "print", "put", "say", "note", "item", "self", "return", "return-rw", "emit",
                        "take", "dispatcher", "Capture", "sink", "iterator", "BUILDALL"}},
                {"Any", {"list", "flat", "eager", "elems", "end", "keys", "values", "pairs", "kv",
                         "antipairs", "grep", "map", "first", "head", "tail", "sort", "unique", "squish",
                         "repeated", "reverse", "join", "min", "max", "minmax", "sum", "reduce", "produce",
                         "classify", "categorize", "combinations", "permutations", "rotor", "batch",
                         "pick", "roll", "Array", "List", "Hash", "Set", "Bag", "Mix", "SetHash",
                         "BagHash", "MixHash", "Seq", "Slip", "hash", "array", "push", "append",
                         "unshift", "prepend", "splice", "deepmap", "duckmap", "nodemap", "cache",
                         "lazy", "skip", "toggle", "snip", "are", "invert", "Map", "Supply"}},
                {"Cool", {"abs", "chars", "chop", "chomp", "comb", "contains", "ends-with", "fc",
                          "flip", "index", "indices", "lc", "lines", "match", "ords", "parse-base",
                          "sprintf", "split", "starts-with", "subst", "substr", "tc", "tclc", "trans",
                          "trim", "trim-leading", "trim-trailing", "uc", "uniname", "uninames", "words",
                          "Int", "Num", "Rat", "FatRat", "Numeric", "Real", "sqrt", "sin", "cos", "tan",
                          "exp", "log", "log10", "round", "floor", "ceiling", "truncate", "sign", "chr",
                          "chrs", "ord", "codes", "fmt", "IO", "EVAL", "samecase", "wordcase", "uniprop",
                          "is-prime", "base", "Complex", "UInt"}},
                {"List", {"ACCEPTS", "Bool", "Capture", "Int", "Numeric", "Str", "Supply", "elems",
                          "end", "fmt", "gist", "join", "keys", "values", "kv", "pairs", "antipairs",
                          "raku", "reverse", "rotate", "sum", "is-lazy", "iterator", "eager", "hyper",
                          "race", "pick", "roll", "combinations", "permutations", "to", "from"}},
                {"Array", {"ASSIGN-POS", "AT-POS", "BIND-POS", "DELETE-POS", "EXISTS-POS", "STORE",
                           "append", "clone", "default", "dynamic", "name", "of", "pop", "prepend",
                           "push", "shape", "shift", "splice", "unshift", "grab"}},
                {"Str", {"ACCEPTS", "Bool", "Int", "Num", "Numeric", "Str", "WHICH", "chars", "chomp",
                         "chop", "codes", "comb", "contains", "ends-with", "fc", "flip", "gist", "index",
                         "indices", "lc", "lines", "match", "ord", "ords", "parse-base", "raku", "split",
                         "starts-with", "subst", "subst-mutate", "substr", "substr-eq", "tc", "tclc",
                         "trans", "trim", "uc", "words", "encode", "NFC", "NFD", "NFKC", "NFKD",
                         "samemark", "wordcase", "succ", "pred", "Date", "DateTime", "IO"}},
                {"Int", {"Bool", "Int", "Num", "Rat", "Str", "Range", "chr", "expmod", "is-prime",
                         "lsb", "msb", "pred", "succ", "polymod", "sqrt-rem"}},
                {"Num", {"Bool", "Int", "Num", "Rat", "Str", "Range", "pred", "succ", "rand"}},
                {"Rat", {"Bool", "Int", "Num", "Rat", "Str", "nude", "numerator", "denominator",
                         "norm", "base-repeating", "round", "floor", "ceiling", "log", "succ", "pred",
                         "isNaN", "raku", "FatRat"}},
                {"Map", {"AT-KEY", "EXISTS-KEY", "keys", "values", "kv", "pairs", "antipairs",
                         "elems", "Bool", "Str", "gist", "raku", "Hash", "Map", "invert"}},
                {"Hash", {"ASSIGN-KEY", "BIND-KEY", "DELETE-KEY", "STORE", "classify-list",
                          "categorize-list", "default", "dynamic", "keyof", "of", "push", "append"}},
                {"Range", {"min", "max", "bounds", "excludes-min", "excludes-max", "infinite",
                           "is-int", "elems", "list", "minmax", "rand", "reverse"}},
                {"Seq", {"iterator", "cache", "is-lazy", "eager", "sink", "List", "Slip", "list"}},
            };
            static const std::map<std::string, std::vector<const char*>> kCoreMro = {
                {"Mu", {"Mu"}}, {"Any", {"Any", "Mu"}}, {"Cool", {"Cool", "Any", "Mu"}},
                {"List", {"List", "Cool", "Any", "Mu"}}, {"Array", {"Array", "List", "Cool", "Any", "Mu"}},
                {"Str", {"Str", "Cool", "Any", "Mu"}}, {"Int", {"Int", "Cool", "Any", "Mu"}},
                {"Num", {"Num", "Cool", "Any", "Mu"}}, {"Rat", {"Rat", "Cool", "Any", "Mu"}},
                {"Map", {"Map", "Cool", "Any", "Mu"}}, {"Hash", {"Hash", "Map", "Cool", "Any", "Mu"}},
                {"Range", {"Range", "Cool", "Any", "Mu"}}, {"Seq", {"Seq", "Cool", "Any", "Mu"}},
            };
            std::string tn = tobj.t == VT::Type ? tobj.s.str() : inv.typeName();
            auto mro = kCoreMro.find(tn);
            if (mro != kCoreMro.end()) {
                bool all = false, local = mm == "method_names";
                for (auto& a : args) if (a.t == VT::Pair) {
                    if (a.s == "all") all = a.pairVal() ? a.pairVal()->truthy() : true;
                    if (a.s == "local") local = a.pairVal() ? a.pairVal()->truthy() : true;
                }
                Value o = Value::array(); o.isList = true;
                std::set<std::string> seen;
                for (const char* cls : mro->second) {
                    std::string c = cls;
                    if (!all && c != tn && (c == "Any" || c == "Mu")) break;
                    auto tm = kCoreMethods.find(c);
                    if (tm != kCoreMethods.end())
                        for (const char* mn : tm->second) {
                            if (!seen.insert(mn).second) continue;
                            if (mm == "method_names") { o.arr()->push_back(Value::str(mn)); continue; }
                            Value code; code.t = VT::Code; code.setCode(std::make_shared<Callable>());
                            std::string mname = mn;
                            code.code()->name = mname; code.code()->isMethod = true;
                            code.code()->builtin = [mname](Interpreter& I, ValueList& av) -> Value {
                                if (av.empty()) return Value::any();
                                Value in2 = av[0]; ValueList rest(av.begin() + 1, av.end());
                                return I.methodCall(in2, mname, rest);
                            };
                            o.arr()->push_back(code);
                        }
                    if (local) break;
                }
                return o;
            }
        }
        if ((mm == "methods" || mm == "method_names" || mm == "attributes") &&
            !(tobj.t == VT::Type && classes_.count(resolveClassAlias(tobj.s))) &&
            !(mm == "attributes" && tobj.t == VT::Type &&
              (tobj.s == "DateTime" || tobj.s == "Date" || tobj.s == "Attribute" ||
               tobj.s == "Rat" || tobj.s == "FatRat"))) {
            Value o = Value::array(); o.isList = true; return o;
        }
        // `T.^foo(…)` IS `T.HOW.foo(T, …)`, and nothing above answered — so ask
        // the metaobject itself. It matters once the .HOW is more than rakupp's
        // own: a role mixed into it (`Target.HOW does Extra`, Method::Also's
        // AliasableClassHOW) or a class declared under a module-supplied HOW put
        // their methods there and nowhere else. Last, so every built-in
        // meta-method above keeps its answer.
        if (tobj.t == VT::Type && !tctx_.metaForwarding.count(mm)) {
            auto hit = classes_.find(tobj.s);
            if (hit != classes_.end() && hit->second &&
                hit->second->howObj.t == VT::Object && hit->second->howObj.obj() &&
                hit->second->howObj.obj()->cls &&
                hit->second->howObj.obj()->cls->findMethod(mm)) {
                ValueList ha; ha.reserve(args.size() + 1);
                // `$obj.^m(…)` is `$obj.HOW.m($obj, …)` — the INSTANCE, not its
                // type: Red's `self.^set-dirty(…)` in a model's TWEAK reads the
                // row's own attributes through it
                ha.push_back(inv.t == VT::Object ? inv : tobj);
                for (auto& a : args) ha.push_back(a);
                return methodCall(hit->second->howObj, mm, ha, rwArgs);
            }
        }
        return methodCall(tobj, mm, args, rwArgs);
    }

    // ---- Iterator protocol (S07). An iterator over a materialized list:
    // hashKind "Iterator", (*hash)["items"] = the values, (*hash)["pos"] = position.
    // Every copy of the Value shares the same hash map, so advancing `pos` through
    // one copy is visible through all of them (iterators are stateful objects).
    if (inv.t == VT::Hash && inv.hashKind == "Iterator" && inv.hash()) {
        Value& itemsV = (*inv.hash())["items"];
        Value& posV = (*inv.hash())["pos"];
        ValueList& items = itemsV.arrRef();
        long long n = (long long)items.size();
        // A LAZY source: the items Value is the sequence itself (see the
        // .iterator arm), sharing its materialised prefix and its LazySeqState —
        // grow the prefix on demand instead of stopping at whatever happened to
        // be cached when the iterator was made (issue #30: `(1 xx *).iterator`
        // answered one element and then IterationEnd forever).
        auto lazySrc = itemsV.t == VT::Array && itemsV.ext()
                           ? std::static_pointer_cast<LazySeqState>(itemsV.ext())
                           : std::shared_ptr<LazySeqState>();
        auto ensure = [&](long long want) { // grow the cache to `want` elements while the source can
            if (!lazySrc || !lazySrc->appendNext) return;
            while (n < want && lazySrc->appendNext(items)) n = (long long)items.size();
        };
        auto drainFinite = [&] { // materialise ALL of a source that is known to end
            if (!lazySrc || !lazySrc->appendNext || lazySrc->infinite) return;
            while (lazySrc->appendNext(items)) {}
            n = (long long)items.size();
        };
        auto iterEnd = [] { return Value::typeObj("IterationEnd"); };
        auto pushInto = [&](const Value& tgt, long long count) -> long long {
            long long pushed = 0;
            // the target is an Array, or an IterationBuffer — its own list of
            // items (highlighter drains `columns(…).iterator.push-all(my $ib :=
            // IterationBuffer.new)`; a buffer target was silently ignored)
            ValueList* dst = nullptr;
            if (tgt.t == VT::Array && tgt.arr()) dst = tgt.arr();
            else if (tgt.t == VT::Hash && tgt.hashKind == "IterationBuffer" && tgt.hash()) {
                Value& iv = (*tgt.hash())["items"];
                if (iv.t != VT::Array || !iv.arr()) iv = Value::array();
                dst = iv.arr();
            }
            if (dst)
                while (posV.i < n && pushed < count) { dst->push_back(items[posV.i++]); pushed++; }
            return pushed;
        };
        if (m == "pull-one") { ensure(posV.i + 1); return posV.i < n ? items[posV.i++] : iterEnd(); }
        if (m == "push-all" || m == "push-until-lazy" || m == "push-exactly" || m == "push-at-least") {
            if (m == "push-all" || m == "push-until-lazy") {
                drainFinite(); // an endless source pushes only its materialised prefix
                if (!args.empty()) pushInto(args[0], n);
                // push-until-lazy stopping FOR laziness answers the iterator, not
                // IterationEnd: there is more, just not now
                if (m == "push-until-lazy" && lazySrc && lazySrc->infinite) return inv;
                return iterEnd();
            }
            long long want = args.size() > 1 ? args[1].toInt() : 0;
            ensure(posV.i + want);
            long long pushed = args.empty() ? 0 : pushInto(args[0], want);
            return pushed < want ? iterEnd() : Value::integer(pushed);
        }
        // A `42 xx 2**62` repeat carries its length and is flagged endless, so
        // drainFinite leaves it alone and sinking it generates nothing — which
        // is the whole of Rakudo's "sunk, plain value `xx` sink cheaply".
        if (m == "sink-all") { drainFinite(); posV.i = n; return iterEnd(); }
        // the skip methods answer an INT (1/0), not a Bool
        if (m == "skip-one") { ensure(posV.i + 1); bool ok = posV.i < n; if (ok) posV.i++; return Value::integer(ok ? 1 : 0); }
        if (m == "skip-at-least") {
            long long want = args.empty() ? 0 : args[0].toInt();
            ensure(posV.i + want);
            long long skipped = std::min(want, n - posV.i); if (skipped < 0) skipped = 0;
            posV.i += skipped;
            return Value::integer(skipped >= want ? 1 : 0);
        }
        if (m == "skip-at-least-pull-one") {
            long long want = args.empty() ? 0 : args[0].toInt();
            ensure(posV.i + std::max(0LL, want) + 1);
            posV.i = std::min(n, posV.i + std::max(0LL, want));
            return posV.i < n ? items[posV.i++] : iterEnd();
        }
        if (m == "count-only") { // remaining, no advance
            if (lazySrc && lazySrc->hasCount)
                return applyArith("-", lazySrc->countVal, Value::integer(posV.i));
            drainFinite(); return Value::integer(n - posV.i);
        }
        if (m == "bool-only") { ensure(posV.i + 1); return Value::boolean(posV.i < n); }
        if (m == "is-lazy") { auto it = inv.hash()->find("lazy"); return Value::boolean(it != inv.hash()->end() && it->second.truthy()); }
        // an iterator over a RANDOMISED or unordered source promises neither a
        // stable order nor an increasing one; the flag rides on the iterator
        if (m == "is-deterministic" || m == "is-monotonically-increasing") {
            auto it = inv.hash()->find("nondeterministic");
            return Value::boolean(it == inv.hash()->end() || !it->second.truthy());
        }
        if (m == "can") { // introspection: which protocol methods this iterator supports
            static const std::set<std::string> ms = {
                "pull-one", "push-all", "push-until-lazy", "push-exactly", "push-at-least",
                "sink-all", "skip-one", "skip-at-least", "skip-at-least-pull-one",
                "count-only", "bool-only", "is-lazy", "iterator",
            };
            Value out = Value::array(); out.isList = true;
            std::string mn = args.empty() ? "" : args[0].toStr();
            if (ms.count(mn)) out.arr()->push_back(Value::str(mn));
            return out;
        }
        if (m == "iterator") return inv; // an Iterator is its own .iterator
        if (m == "WHAT") return Value::typeObj("Iterator");
    }

    // Signature introspection value (from &routine.signature).
    if (inv.t == VT::Hash && inv.hashKind == "Signature") {
        if (m == "raku" || m == "gist" || m == "Str") {
            std::string body = inv.hash()->count("str") ? (*inv.hash())["str"].toStr() : "()";
            // .raku is the signature literal; .gist/.Str are the bare parens —
            // and .raku keeps an anonymous typed parameter's sigil (`:(Int $)`)
            if (m == "raku" && inv.hash()->count("rakustr")) body = (*inv.hash())["rakustr"].toStr();
            return Value::str(m == "raku" ? ":" + body : body);
        }
        if (m == "returns" || m == "of") {
            auto it = inv.hash()->find("returns");
            return it != inv.hash()->end() ? it->second : Value::typeObj("Mu");
        }
        if (m == "arity") return inv.hash()->count("arity") ? (*inv.hash())["arity"] : Value::integer(0);
        if (m == "count") return inv.hash()->count("count") ? (*inv.hash())["count"] : Value::integer(0);
        if (m == "params" || m == "parameters") { Value p = inv.hash()->count("params") ? (*inv.hash())["params"] : Value::array(); p.isList = true; return p; }
        if (m == "ACCEPTS") {
            if (args.empty()) return Value::boolean(false);
            // Signature ~~ Signature is a different question from Signature ~~
            // Capture: it asks whether EVERY call that binds the left also binds
            // the right, i.e. whether the left's accepted call-set is contained in
            // the right's. Arity/count windows answer most of it; a slurpy NAMED
            // does not widen the positional window, so `:(*%) ~~ :()` needs its own
            // test (both are [0,0] positionally, but only one takes nameds).
            if (args[0].t == VT::Hash && args[0].hashKind == "Signature" && args[0].hash() &&
                args[0].hash()->count("\x01sigptr") && inv.hash()->count("\x01sigptr")) {
                auto* T = (const std::vector<Param>*)(intptr_t)(*args[0].hash())["\x01sigptr"].toInt();
                auto* S = (const std::vector<Param>*)(intptr_t)(*inv.hash())["\x01sigptr"].toInt();
                return Value::boolean(sigAcceptsSig(*this, *S, (*inv.hash())["\x01ret"].toStr(),
                                                    *T, (*args[0].hash())["\x01ret"].toStr()));
            }
            if (args[0].t == VT::Hash && args[0].hashKind == "Signature" && args[0].hash()) {
                const Value& lhs = args[0];
                auto num = [](const Value& sg, const char* k) {
                    auto it = sg.hash()->find(k);
                    return it == sg.hash()->end() ? 0.0 : it->second.toNum();
                };
                auto str = [](const Value& sg) {
                    auto it = sg.hash()->find("str");
                    return it == sg.hash()->end() ? std::string() : it->second.s;
                };
                if (!(num(lhs, "arity") >= num(inv, "arity") &&
                      num(lhs, "count") <= num(inv, "count"))) return Value::boolean(false);
                bool lAny = str(lhs).find("*%") != std::string::npos;
                // (a `|` capture takes the nameds as well as the positionals)
                bool rAny = str(inv).find("*%") != std::string::npos || str(inv).find('|') != std::string::npos;
                if (lAny && !rAny) return Value::boolean(false);
                return Value::boolean(true);
            }
            // otherwise: would the value BIND? A Capture binds as it is; any
            // other value through its `.Capture` — a Hash's pairs, a List's
            // Pairs and the rest, a Rat's numerator and denominator, a Set's
            // keys — and a value with no Capture (`42 ~~ :(Int)`) binds
            // nothing. With the declared parameters to hand, the binder's own
            // trial (types, `where`s, nameds, sub-signatures) decides, as a
            // multi's dispatch would.
            if (inv.hash()->count("\x01sigptr")) {
                auto* S = (std::vector<Param>*)(intptr_t)(*inv.hash())["\x01sigptr"].toInt();
                Value cap = args[0];
                if (!(cap.t == VT::Array && cap.hashKind == "Capture")) {
                    try { cap = methodCall(cap, "Capture", ValueList{}); }
                    catch (RakuError&) { return Value::boolean(false); }
                }
                ValueList callArgs;
                // a METHOD's signature binds the invocant first: the capture's
                // first positional is that, and the rest are the parameters
                const bool isMeth = inv.hash()->count("\x01method") > 0;
                bool invocantTaken = !isMeth;
                if (cap.t == VT::Array && cap.arr())
                    for (auto& e : *cap.arr()) {
                        if (e.t == VT::Pair && !e.itemized) { Value n = e; n.namedArg = true; callArgs.push_back(n); }
                        else if (!invocantTaken) invocantTaken = true;
                        else callArgs.push_back(e);
                    }
                if (!invocantTaken) return Value::boolean(false);
                else if (cap.t == VT::Hash && cap.hash())
                    for (auto& kv : *cap.hash()) {
                        Value n = Value::pair(kv.first, kv.second); n.namedArg = true; callArgs.push_back(n);
                    }
                auto tmp = std::make_shared<Callable>();
                tmp->params = S;
                tmp->isMethod = isMeth;   // (and its implicit *%_)
                tmp->closure = tctx_.cur;
                Value tv; tv.t = VT::Code; tv.setCode(tmp);
                try { return Value::boolean(scoreCandidate(tv, callArgs) >= 0); }
                catch (RakuError&) { return Value::boolean(false); }
            }
            // (no parameters to bind against: the approximation)
            // would this CAPTURE bind? — arity window, literal
            // constraints, positional types, required nameds (Cro's router check)
            const Value& cap = args[0];
            ValueList pos; std::map<std::string, Value> named;
            if (cap.t == VT::Array && cap.arr())
                for (auto& e : *cap.arr()) {
                    if (e.t == VT::Pair) named[e.s] = e.pairVal() ? *e.pairVal() : Value::any();
                    else pos.push_back(e);
                }
            long long arity = inv.hash()->count("arity") ? (*inv.hash())["arity"].toInt() : 0;
            double cnt = inv.hash()->count("count") ? (*inv.hash())["count"].toNum() : 0;
            if ((long long)pos.size() < arity) return Value::boolean(false);
            if (std::isfinite(cnt) && (double)pos.size() > cnt) return Value::boolean(false);
            size_t pi2 = 0;
            bool ok = true;
            if (inv.hash()->count("params") && (*inv.hash())["params"].arr())
                for (auto& pv : *(*inv.hash())["params"].arr()) {
                    if (pv.t != VT::Hash) continue;
                    auto& ph = *pv.hash();
                    bool isNamed = ph.count("named") && ph["named"].truthy();
                    bool isSlurpy = ph.count("slurpy") && ph["slurpy"].truthy();
                    if (isSlurpy) continue;
                    if (isNamed) {
                        bool opt = ph.count("optional") && ph["optional"].truthy();
                        std::string key;
                        if (ph.count("named_names") && ph["named_names"].arr() && !ph["named_names"].arr()->empty())
                            key = (*ph["named_names"].arr())[0].toStr();
                        else if (ph.count("name") && ph["name"].s.size() > 1)
                            key = ph["name"].s.substr(1);
                        auto nit = key.empty() ? named.end() : named.find(key);
                        if (!opt && !key.empty() && nit == named.end()) { ok = false; break; }
                        // a SUPPLIED named must bind too: its type, and the
                        // Positional/Associative an `@`/`%` sigil implies —
                        // `\(…, :to(Str)) ~~ :(Str $v, :@to!)` is False (Red
                        // picks its inflater by asking exactly that)
                        if (nit != named.end()) {
                            const Value& nv = nit->second;
                            char sg = ph.count("name") && !ph["name"].s.empty() ? ph["name"].s[0] : '$';
                            if (sg == '@' && !(nv.t == VT::Array || nv.t == VT::Range ||
                                               typeOrSubsetMatches(nv, "Positional"))) { ok = false; break; }
                            if (sg == '%' && !(nv.t == VT::Hash || typeOrSubsetMatches(nv, "Associative"))) { ok = false; break; }
                            if (sg == '$' && ph.count("type") && !ph["type"].s.empty() &&
                                ph["type"].s != "Mu" && ph["type"].s != "Any" &&
                                !typeOrSubsetMatches(nv, ph["type"].s)) { ok = false; break; }
                        }
                        continue;
                    }
                    if (pi2 >= pos.size()) break; // optional tail
                    const Value& a2 = pos[pi2++];
                    if (ph.count("constraints")) {
                        // constraints is now the all(…) junction — an empty one
                        // constrains nothing; a literal eigenstate must match
                        const Value& cjv = ph["constraints"];
                        if (cjv.t == VT::Array && cjv.enumName == "all" && cjv.arr() && !cjv.arr()->empty()) {
                            const Value& cv = (*cjv.arr())[0];
                            bool eq = (a2.isNumeric() && cv.isNumeric()) ? a2.toNum() == cv.toNum()
                                                                         : a2.toStr() == cv.toStr();
                            if (!eq) { ok = false; break; }
                        }
                    }
                    if (ph.count("type") && !ph["type"].s.empty() &&
                        !typeOrSubsetMatches(a2, ph["type"].s)) { ok = false; break; }
                }
            return Value::boolean(ok);
        }
    }
    // a Parameter's introspection (.name/.type/.named/.optional/.slurpy)
    // `.dynamic` — was this container declared with a `*` twigil? `.default` —
    // its `is default(…)` element value (Any when it has none).
    // `.self` is the invocant itself — the identity method every type has
    if (m == "self" && args.empty()) return inv;

    // a value reached any other way is not a dynamic variable (the `*`-twigil
    // case is answered from the NAME, in the MethodCall evaluator)
    if (m == "dynamic" && (inv.t == VT::Array || inv.t == VT::Hash || inv.t == VT::Str ||
                           inv.t == VT::Int || inv.t == VT::Num || inv.t == VT::Any))
        return Value::boolean(false);
    if (m == "default" && (inv.t == VT::Array ||
                          (inv.t == VT::Hash && inv.hashKind != "Parameter"))) {
        // A Map has NO `.default` — a missing key reads as Nil and there is no
        // knob to change that, so asking for one is a missing method, not Any
        // (sheet HM-14).
        if (inv.t == VT::Hash && inv.hashKind == "Map")
            throw RakuError{Value::typeObj("X::Method::NotFound"),
                "No such method 'default' for invocant of type 'Map'"};
        if (inv.elemDefault()) return *inv.elemDefault();
        // a QuantHash has a TYPED default, not Any: False for the Set family,
        // 0 for the weighted ones. Only those — every other tagged Hash
        // (Parameter, Failure, …) has its own `.default`, or none.
        static const std::set<std::string> quant = {"Set", "SetHash", "Bag", "BagHash", "Mix", "MixHash"};
        if (inv.t == VT::Hash && quant.count(inv.hashKind))
            return inv.hashKind.rfind("Set", 0) == 0 ? Value::boolean(false) : Value::integer(0);
        if (inv.t == VT::Hash && !inv.hashKind.empty() && inv.hashKind != "Map")
            return Value::any(); // not a container — fall through below
        // A TYPED container with no `is default` defaults to its own type
        // object: `my Int @a; @a.default` is Int, not Any (sheet LA-21).
        if (!inv.ofType().empty() && inv.ofType() != "Mu" &&
            inv.ofType().find(',') == std::string::npos &&
            !ascii::islower((unsigned char)inv.ofType()[0]))
            return Value::typeObj(inv.ofType());
        return Value::any();
    }

    if (inv.t == VT::Hash && inv.hashKind == "Parameter") {
        // `.raku` is how the parameter is written, typed: `Int $a`, `Mu $_? …`
        if (m == "raku" && inv.hash()->count("str")) {
            std::string str = (*inv.hash())["str"].toStr();
            auto ti = inv.hash()->find("type-obj");
            std::string ty = ti != inv.hash()->end() && ti->second.t == VT::Type ? std::string(ti->second.s.c_str()) : std::string();
            // an unwritten type shows only when it is Mu (a block's or a
            // signature literal's `$a`): Any and the sigil-implied
            // Positional/Associative/Callable stay silent, as in Rakudo
            if (ty == "Mu" && !str.empty() && std::strchr("$@%&\\|*", str[0]))
                str = "Mu " + str;
            return Value::str(str);
        }
        if ((m == "name" || m == "named" || m == "optional" || m == "slurpy" ||
             m == "constraints" || m == "named_names" || m == "usage-name" ||
             m == "raw" || m == "copy" || m == "rw" || m == "capture" ||
             m == "invocant" || m == "multi-invocant" ||
             m == "prefix" || m == "suffix" || m == "modifier" ||
             m == "default" || m == "readonly") && inv.hash()->count(m))
            return (*inv.hash())[m];
        // `.type` answers the TYPE OBJECT (Cro's router compares `=:= Str`);
        // the plain string form stays under the "type" key for legacy callers
        if (m == "type" && inv.hash()->count("type-obj")) return (*inv.hash())["type-obj"];
        if (m == "type" && inv.hash()->count(m)) return (*inv.hash())[m];
        if (m == "positional") return Value::boolean(!(*inv.hash())["named"].truthy() && !(*inv.hash())["slurpy"].truthy());
        if (m == "sigil") { const std::string& n = (*inv.hash())["name"].s; return Value::str(n.empty() ? "$" : n.substr(0, 1)); }
        // An accessor of a role a parameter trait mixed in (`$param does
        // Formatted::Named(:$argument)` put the role's attributes into this same
        // map): `$param.argument`. Last, so it can never shadow a real
        // Parameter method — the Attribute meta-object ends the same way.
        {
            auto ai = inv.hash()->find(m);
            if (ai != inv.hash()->end()) return ai->second;
        }
    }
    // a Capture's .list is its POSITIONAL args, .hash/.Map its NAMED (Pair) args
    // Capture.new(:list(...), :hash(...)) — build the \(…)-style capture value
    // Attribute.new(:name<$!x>, :type(Int), :package(Foo)) — build an Attribute
    // meta-object (for .^add_attribute and dynamic class construction).
    if (inv.t == VT::Type && inv.s == "IO::Path::Parts" && m == "new") {
        Value pp = Value::makeHash(); pp.hashKind = "IO::Path::Parts";
        auto pos = [&](size_t i) { return args.size() > i && args[i].t != VT::Pair ? args[i].toStr() : std::string(); };
        (*pp.hash())["volume"] = Value::str(pos(0));
        (*pp.hash())["dirname"] = Value::str(pos(1));
        (*pp.hash())["basename"] = Value::str(pos(2));
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal() &&
            (a.s == "volume" || a.s == "dirname" || a.s == "basename"))
            (*pp.hash())[a.s] = Value::str(a.pairVal()->toStr());
        return pp;
    }
    if (inv.t == VT::Hash && inv.hashKind == "IO::Path::Parts") {
        if (m == "volume" || m == "dirname" || m == "basename") return (*inv.hash())[m];
        // It is Positional as well as Associative: `$parts[0]` is the volume PAIR
        // and `$parts[]` lists all three. The order is the declaration order —
        // volume, dirname, basename — not the map's sorted order, so this cannot
        // just walk the hash.
        if (m == "AT-POS" || m == "list" || m == "List" || m == "elems" || m == "Numeric") {
            static const char* kOrder[3] = {"volume", "dirname", "basename"};
            if (m == "elems" || m == "Numeric") return Value::integer(3);
            auto pairAt = [&](int i) {
                return Value::pair(kOrder[i], (*inv.hash())[kOrder[i]]);
            };
            if (m == "AT-POS") {
                long long i = args.empty() ? 0 : args[0].toInt();
                if (i < 0) i += 3;
                return (i >= 0 && i < 3) ? pairAt((int)i) : Value::any();
            }
            Value out = Value::array(); out.isList = true;
            for (int i = 0; i < 3; i++) out.arr()->push_back(pairAt(i));
            return out;
        }
        if (m == "gist" || m == "raku" || m == "Str") {
            // the parts are STRING LITERALS, so a backslash in a Windows path has to
            // be escaped like any other Str.raku (`"\\a"`, not `"\a"`)
            auto q = [&](const char* k) {
                std::string v = (*inv.hash())[k].toStr(), o = "\"";
                for (char c : v) { if (c == '\\' || c == '"') o += '\\'; o += c; }
                return o + "\"";
            };
            return Value::str("IO::Path::Parts.new(" + q("volume") + "," + q("dirname") + "," + q("basename") + ")");
        }
        if (m == "elems") return Value::integer(3);
    }
    if (inv.t == VT::Type && inv.s == "Attribute" && m == "new") {
        Value at = Value::makeHash(); at.hashKind = "Attribute";
        (*at.hash())["name"] = Value::str(""); (*at.hash())["type"] = Value::typeObj("Mu");
        (*at.hash())["readonly"] = Value::boolean(true); (*at.hash())["has_accessor"] = Value::boolean(false);
        for (auto& a : args) if (a.t == VT::Pair && a.pairVal()) {
            if (a.s == "name") (*at.hash())["name"] = *a.pairVal();
            else if (a.s == "type" || a.s == "of") (*at.hash())["type"] = *a.pairVal();
            else if (a.s == "rw") (*at.hash())["readonly"] = Value::boolean(!a.pairVal()->truthy());
            else if (a.s == "has_accessor") (*at.hash())["has_accessor"] = *a.pairVal();
            else if (a.s == "package") (*at.hash())["package"] = *a.pairVal();
        }
        return at;
    }
    // Parameter.new / Signature.new — a signature composed at RUNTIME out of type
    // objects instead of parsed from a signature literal. Issue #84: a program
    // that builds one this way could not start, because neither constructor
    // existed. Both build the SAME hash shape the introspection path builds for a
    // parsed signature, so every accessor already written reads a constructed one
    // without knowing the difference — and `nativecast($signature, $ptr)` below
    // takes either.
    if (inv.t == VT::Type && inv.s == "Parameter" && m == "new") {
        Value pv = Value::makeHash(); pv.hashKind = "Parameter";
        auto& h = *pv.hash();
        h["name"] = Value::str("");            h["usage-name"] = Value::str("");
        h["type"] = Value::str("Any");         h["type-obj"] = Value::typeObj("Any");
        h["named"] = Value::boolean(false);    h["optional"] = Value::boolean(false);
        h["slurpy"] = Value::boolean(false);   h["raw"] = Value::boolean(false);
        h["readonly"] = Value::boolean(true);  h["rw"] = Value::boolean(false);
        h["copy"] = Value::boolean(false);     h["capture"] = Value::boolean(false);
        h["invocant"] = Value::boolean(false); h["multi-invocant"] = Value::boolean(true);
        h["prefix"] = Value::str("");          h["suffix"] = Value::str("");
        h["modifier"] = Value::str("");        h["default"] = Value::typeObj("Code");
        h["constraints"] = Value::typeObj("Mu");
        Value nn = Value::array(); nn.isList = true; h["named_names"] = nn;
        bool sawOptional = false;
        for (auto& a : args) {
            if (a.t != VT::Pair || !a.pairVal()) continue;
            const Value& v = *a.pairVal();
            if (a.s == "type" || a.s == "of") {
                h["type-obj"] = v;
                h["type"] = Value::str(v.t == VT::Type ? std::string(v.s.str()) : std::string("Any"));
            }
            else if (a.s == "name") {
                std::string n = v.toStr();
                h["name"] = Value::str(n);
                h["usage-name"] = Value::str(n.size() > 1 ? n.substr(1) : n);
            }
            else if (a.s == "named")    h["named"] = Value::boolean(v.truthy());
            else if (a.s == "optional") { h["optional"] = Value::boolean(v.truthy()); sawOptional = true; }
            else if (a.s == "slurpy")   h["slurpy"] = Value::boolean(v.truthy());
            else if (a.s == "is-rw" || a.s == "rw")     h["rw"] = Value::boolean(v.truthy());
            else if (a.s == "is-copy" || a.s == "copy") h["copy"] = Value::boolean(v.truthy());
            else if (a.s == "is-raw" || a.s == "raw")   h["raw"] = Value::boolean(v.truthy());
            else if (a.s == "default")  h["default"] = v;
            else if (a.s == "invocant") h["invocant"] = Value::boolean(v.truthy());
            else if (a.s == "multi-invocant") h["multi-invocant"] = Value::boolean(v.truthy());
            else h[a.s] = v;   // anything else is kept, as a mixed-in trait already is
        }
        // a NAMED parameter is optional unless it was asked to be required — `:$x`
        // binds or does not, where `$x` must
        if (h["named"].truthy() && !sawOptional) h["optional"] = Value::boolean(true);
        h["str"] = Value::str(ctorParamStr(pv, /*inSignature=*/false));
        return pv;
    }
    if (inv.t == VT::Type && inv.s == "Signature" && m == "new") {
        Value sv = Value::makeHash(); sv.hashKind = "Signature";
        Value params = Value::array(); params.isList = true;
        Value returns = Value::typeObj("Mu");
        for (auto& a : args) {
            if (a.t != VT::Pair || !a.pairVal()) continue;
            const Value& v = *a.pairVal();
            if (a.s == "params" || a.s == "parameters") {
                // `:params(|@list)` slips a list; `:params($p)` is one parameter.
                if (v.t == VT::Array && v.arr() && v.hashKind != "Parameter")
                    for (auto& e : *v.arr()) params.arr()->push_back(e);
                else params.arr()->push_back(v);
            }
            else if (a.s == "returns" || a.s == "of") returns = v;
        }
        // arity counts the REQUIRED positionals, count every positional — a
        // slurpy takes as many as it is given, so it makes the count Inf
        long long arity = 0, count = 0; bool slurpy = false;
        std::string body; bool first = true;
        for (auto& e : *params.arr()) {
            if (e.t != VT::Hash || !e.hash()) continue;
            if (!first) body += ", ";
            first = false;
            body += ctorParamStr(e, /*inSignature=*/true);
            bool nm = (*e.hash())["named"].truthy(), sl = (*e.hash())["slurpy"].truthy();
            if (nm) continue;
            if (sl) { slurpy = true; continue; }
            count++;
            if (!(*e.hash())["optional"].truthy()) arity++;
        }
        std::string retName = returns.t == VT::Type ? std::string(returns.s.str()) : std::string("Mu");
        (*sv.hash())["str"] = Value::str("(" + body + " --> " + retName + ")");
        (*sv.hash())["arity"] = Value::integer(arity);
        (*sv.hash())["count"] = slurpy ? Value::number(std::numeric_limits<double>::infinity())
                                       : Value::integer(count);
        (*sv.hash())["params"] = std::move(params);
        (*sv.hash())["returns"] = std::move(returns);
        return sv;
    }
    // Junction.new("any", (1, 2)) — the constructor spelling of any(1, 2)
    // …and Junction.new(@values, :type<all>)
    if (inv.t == VT::Type && inv.s == "Junction" && m == "new") {
        Value j = Value::array(); j.isList = true;
        std::string type = "any";
        ValueList pos;
        for (auto& a : args) {
            if (a.t == VT::Pair && a.namedArg) {
                if (a.s == "type" && a.pairVal()) type = a.pairVal()->toStr();
                continue;
            }
            pos.push_back(a);
        }
        if (pos.size() >= 2) {                       // ("any", @values)
            type = pos[0].toStr();
            for (auto& e : pos[1].flatten()) j.arr()->push_back(e);
        }
        else if (pos.size() == 1) {                  // (@values, :type)
            if (pos[0].t == VT::Array || pos[0].t == VT::Range)
                for (auto& e : pos[0].flatten()) j.arr()->push_back(e);
            else type = pos[0].toStr();
        }
        j.enumName = type;
        return j;
    }
    // Format.new("%s|%s") — a reusable sprintf: calling it formats its arguments
    if (inv.t == VT::Type && (inv.s == "Format" || inv.s == "Formatter") && m == "new") {
        std::string fmt = args.empty() ? "" : args[0].toStr();
        // `.arity` is the number of directives the format consumes
        long long ar = 0;
        for (size_t k = 0; k + 1 < fmt.size(); k++)
            if (fmt[k] == '%') { if (fmt[k + 1] == '%') k++; else ar++; }
        Value code; code.t = VT::Code; code.setCode(std::make_shared<Callable>());
        code.code()->name = "Format";
        code.code()->builtin = [fmt](Interpreter& I, ValueList& a) -> Value {
            ValueList sa; sa.push_back(Value::str(fmt));
            for (auto& x : a) sa.push_back(x);
            return I.callBuiltin("sprintf", sa);
        };
        Value f = Value::makeHash(); f.hashKind = "Format";
        (*f.hash())["fmt"] = Value::str(fmt);
        (*f.hash())["arity"] = Value::integer(ar);
        (*f.hash())["code"] = code;
        return f;
    }
    // `.WALK` — the methods of one name along the class hierarchy, as a
    // WalkList: a List of the candidates that is itself callable, invoking each
    // on the invocant in turn. `:name<m>` takes the 6.c order options
    // (:canonical — the MRO, the default —, :super, :breadth, :descendant,
    // :ascendant/:preorder) and the :include/:omit class filters; `WALK("m",
    // :roles)` also visits, after each class, the roles it composes, breadth
    // first, for their SUBMETHODS only (a role's methods are the class's).
    if (m == "WALK" && (inv.t == VT::Object || inv.t == VT::Type) && !args.empty()) {
        std::string name; bool withRoles = false, super_ = false, breadth = false, desc = false, asc = false;
        Value include, omit;
        for (auto& a : args) {
            if (a.t == VT::Pair) {
                bool on = !a.pairVal() || a.pairVal()->truthy();
                if (a.s == "name") name = a.pairVal() ? a.pairVal()->toStr() : "";
                else if (a.s == "roles") withRoles = on;
                else if (a.s == "super") super_ = on;
                else if (a.s == "breadth") breadth = on;
                else if (a.s == "descendant") desc = on;
                else if (a.s == "ascendant" || a.s == "preorder") asc = asc || on;
                else if (a.s == "include" && a.pairVal()) include = *a.pairVal();
                else if (a.s == "omit" && a.pairVal()) omit = *a.pairVal();
            }
            else if (name.empty()) name = a.toStr();
        }
        ClassInfo* ci0 = nullptr;
        if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) ci0 = inv.obj()->cls.get();
        else if (inv.t == VT::Type) {
            auto it = classes_.find(inv.s.str());
            if (it != classes_.end() && it->second && !it->second->isRole) ci0 = it->second.get();
        }
        Value wl = Value::array(); wl.isList = true; wl.s = "WalkList";
        wl.xw().pairKey = std::make_shared<Value>(inv);
        if (!ci0) {   // a built-in type: the one method it answers to
            Value fm = methodCall(inv, "^find_method", ValueList{Value::str(name)});
            if (fm.t == VT::Code) wl.arr()->push_back(fm);
            return wl;
        }
        auto localParents = [](ClassInfo* c) {
            std::vector<ClassInfo*> r;
            if (c->parent && !c->parent->isRole) r.push_back(c->parent.get());
            for (auto& p : c->extraParents) if (p && !p->isRole) r.push_back(p.get());
            return r;
        };
        std::vector<ClassInfo*> classes;
        auto have = [&](ClassInfo* c) { return std::find(classes.begin(), classes.end(), c) != classes.end(); };
        if (super_) classes = localParents(ci0);
        else if (breadth) {
            std::vector<ClassInfo*> level{ci0};
            while (!level.empty()) {
                std::vector<ClassInfo*> next;
                for (ClassInfo* c : level) {
                    if (!have(c)) classes.push_back(c);
                    for (ClassInfo* p : localParents(c))
                        if (std::find(next.begin(), next.end(), p) == next.end()) next.push_back(p);
                }
                level = std::move(next);
            }
        }
        else if (asc) {
            std::function<void(ClassInfo*)> pre = [&](ClassInfo* c) {
                if (have(c)) return;
                classes.push_back(c);
                for (ClassInfo* p : localParents(c)) pre(p);
            };
            pre(ci0);
        }
        else if (desc) {
            std::function<void(ClassInfo*)> post = [&](ClassInfo* c) {
                if (have(c)) return;
                for (ClassInfo* p : localParents(c)) post(p);
                if (!have(c)) classes.push_back(c);
            };
            post(ci0);
        }
        else classes = c3ClassMro(ci0);
        auto accepts = [&](const Value& filter, ClassInfo* c) {
            return boolify(methodCall(filter, "ACCEPTS", ValueList{Value::typeObj(c->name)}));
        };
        // a declaration's DIRECT roles, in the order it names them
        auto direct = [](ClassInfo* ci) {
            std::vector<std::string> rs;
            if (ci->decl) {
                if (ci->decl->parentIsDoes && !ci->decl->parent.empty()) rs.push_back(ci->decl->parent);
                for (auto& r : ci->decl->roles) rs.push_back(r);
            }
            else rs.assign(ci->doneRoles.begin(), ci->doneRoles.end());
            return rs;
        };
        std::set<ClassInfo*> seenRole;
        for (ClassInfo* c : classes) {
            if (include.t != VT::Nil && include.t != VT::Any && !accepts(include, c)) continue;
            if (omit.t != VT::Nil && omit.t != VT::Any && accepts(omit, c)) continue;
            auto mit = c->methods.find(name);
            if (mit != c->methods.end() && mit->second.t == VT::Code &&
                !(withRoles && c->roleSubmethods.count(name)))
                wl.arr()->push_back(mit->second);
            if (!withRoles) continue;
            auto d0 = direct(c);
            std::deque<std::string> q(d0.begin(), d0.end());
            while (!q.empty()) {
                auto it = classes_.find(q.front()); q.pop_front();
                if (it == classes_.end() || !it->second || !seenRole.insert(it->second.get()).second) continue;
                ClassInfo* r = it->second.get();
                auto rm = r->methods.find(name);
                if (rm != r->methods.end() && rm->second.t == VT::Code && rm->second.code() &&
                    rm->second.code()->isSubmethod)
                    wl.arr()->push_back(rm->second);
                for (auto& rn : direct(r)) q.push_back(rn);
            }
        }
        return wl;
    }
    // A WalkList (from `.WALK`): calling it — `.invoke(…)` or `(…)` — runs
    // each method on the invocant as the result list is read, so a `for` over
    // it calls one method per iteration. An exception propagates, unless
    // `.quiet` made the list turn it into that candidate's Failure; a Slip a
    // method returns stays one value, so each result keeps its own place.
    if (inv.t == VT::Array && inv.s == "WalkList" && inv.arr()) {
        if (m == "invoke" || m == "CALL-ME") {
            auto meths = std::make_shared<ValueList>(*inv.arr());
            Value self = inv.pairKey() ? *inv.pairKey() : Value::any();
            bool quiet = inv.xr().rExFrom;
            auto idx = std::make_shared<size_t>(0);
            ValueList cargs = args;
            Interpreter* ip = this;
            auto st = std::make_shared<LazySeqState>();
            st->streaming = st->finiteSource = true;   // a `for` pulls one call per turn
            st->appendNext = [=](ValueList& cache) -> bool {
                if (*idx >= meths->size()) return false;
                Value mm = (*meths)[(*idx)++];
                ValueList a = cargs;
                Value r;
                if (quiet) {
                    try { r = ip->invokeMethod(mm, self, std::move(a)); }
                    catch (RakuError& e) {
                        r = Value::makeHash(); r.hashKind = "Failure";
                        (*r.hash())["exception"] = ip->exceptionFor(e);
                        (*r.hash())["message"] = Value::str(e.message);
                    }
                }
                else r = ip->invokeMethod(mm, self, std::move(a));
                if (r.t == VT::Array && r.s == "Slip") r.itemized = true;
                cache.push_back(r);
                return true;
            };
            Value out = Value::array(); out.isList = true; out.s = "Seq";
            out.extM() = st;
            return out;
        }
        if (m == "quiet" || m == "reverse") {
            Value c = Value::array(); c.isList = true; c.s = "WalkList";
            *c.arr() = *inv.arr();
            if (m == "reverse") std::reverse(c.arr()->begin(), c.arr()->end());
            c.xw().pairKey = inv.pairKey();
            c.xw().rExFrom = m == "quiet" ? (args.empty() || boolify(args[0])) : inv.xr().rExFrom;
            return c;
        }
        if (m == "invocant") return inv.pairKey() ? *inv.pairKey() : Value::any();
    }
    // `Pair.Pair` — a Pair type object coerces to itself
    if (inv.t == VT::Type && inv.s == "Pair" && m == "Pair") return inv;
    // `T.^add_fallback(-> $obj, $name { … }, -> $obj, $name { method })`
    if ((m == "^add_fallback" || m == "add_fallback") && inv.t == VT::Type && args.size() == 2 &&
        args[0].t == VT::Code && args[1].t == VT::Code) {
        addedFallbacks_[std::string(inv.s.c_str())].emplace_back(args[0], args[1]);
        return Value::nil();
    }
    // `.^enum_value_list` / `.^enum_values` of a CORE enum, its members in
    // declaration order (IO-Socket-INET.t picks an Int no ProtocolFamily
    // member has with `(0...*).first(ProtocolFamily.^enum_value_list.none)`)
    // (reached with the `^` already stripped on some routes, so both spellings)
    if (inv.t == VT::Type && (m == "^enum_value_list" || m == "^enum_values" ||
                              m == "enum_value_list" || m == "enum_values")) {
        static const std::map<std::string, std::vector<const char*>> kCoreEnums = {
            {"Bool", {"False", "True"}},
            {"Order", {"Less", "Same", "More"}},
            {"PromiseStatus", {"Planned", "Kept", "Broken"}},
            {"Endian", {"NativeEndian", "LittleEndian", "BigEndian"}},
            {"SeekType", {"SeekFromBeginning", "SeekFromCurrent", "SeekFromEnd"}},
            {"ProtocolType", {"PROTO_TCP", "PROTO_UDP"}},
            {"ProtocolFamily", {"PF_UNSPEC", "PF_INET", "PF_INET6", "PF_LOCAL", "PF_UNIX", "PF_MAX"}},
        };
        auto ce = kCoreEnums.find(std::string(inv.s.c_str()));
        if (ce != kCoreEnums.end()) {
            Value out = Value::array(); out.isList = true;
            Value mp = Value::makeHash(); mp.hashKind = "Map";
            for (const char* nm : ce->second) {
                Value ev;
                if (!coreEnumValue(nm, ev)) continue;
                out.arr()->push_back(ev);
                (*mp.hash())[nm] = Value::integer(ev.t == VT::Bool ? (ev.b ? 1 : 0) : ev.toInt());
            }
            return (m == "^enum_values" || m == "enum_values") ? mp : out;
        }
    }
    // `Bool.enums` — the built-in enum's Map
    if (inv.t == VT::Type && inv.s == "Bool" && m == "enums") {
        Value mp = Value::makeHash(); mp.hashKind = "Map";
        (*mp.hash())["False"] = Value::integer(0);
        (*mp.hash())["True"] = Value::integer(1);
        return mp;
    }
    // `Signal.enums` — this platform's signals by name — and `Signal(2)`,
    // the coercion from a number to the enum value
    if (inv.t == VT::Type && inv.s == "Signal" && m == "enums") {
        Value mp = Value::makeHash(); mp.hashKind = "Map";
        for (auto& kv : signalNameMapFwd()) (*mp.hash())[kv.first] = Value::integer(kv.second);
        return mp;
    }
    if (m == "Signal" && (inv.t == VT::Int || (inv.t == VT::Str && inv.isAllomorph())))
        return makeSignalEnumValueFwd((int)inv.toInt());
    // `Blob.encoding` — a plain Blob/Buf type object carries none
    if (inv.t == VT::Type && (inv.s == "Blob" || inv.s == "Buf") && m == "encoding") return Value::any();
    // A QuantHash holds no containers to bind to, and the immutable three
    // hold no slot to delete from or assign to either
    if (inv.t == VT::Hash && (m == "BIND-KEY" || m == "DELETE-KEY" || m == "ASSIGN-KEY")) {
        const std::string hk = inv.hashKind;
        const bool quant = hk == "Set" || hk == "Bag" || hk == "Mix" ||
                           hk == "SetHash" || hk == "BagHash" || hk == "MixHash";
        if (quant && m == "BIND-KEY")
            throwTypedV("X::Bind", {{"target", Value::str(hk)}}, "Cannot bind to a " + hk);
        if ((hk == "Set" || hk == "Bag" || hk == "Mix") && m != "BIND-KEY")
            throwTypedV("X::Assignment::RO", {{"typename", Value::str(hk)}, {"value", inv}},
                        "Cannot modify an immutable " + hk);
    }
    // `%h.new` — a fresh, EMPTY hash of the invocant's own type: an object
    // hash (`my %h{Any}`) makes another object hash, a typed one keeps its type
    if (inv.t == VT::Hash && inv.hash() && m == "new" && args.empty() &&
        (inv.hashKind.empty() || inv.hashKind == "Hash")) {
        Value h = methodCall(inv, "clone", ValueList{});
        if (h.t == VT::Hash && h.hash()) { h.hashRef().clear(); return h; }
    }
    // Formatter's compile steps, as far as a program can see them: the format
    // grammar parses the whole string, CODE is the formatting routine, AST
    // the RakuAST it would be built from (here: the format as a literal)
    if (inv.t == VT::Type && inv.s == "Formatter::Syntax" && m == "parse" && !args.empty())
        return regexMatch(args[0].toStr(), "^ .* $", nullptr, "regex");
    if (inv.t == VT::Type && inv.s == "Formatter" && (m == "CODE" || m == "AST") && !args.empty()) {
        if (m == "CODE") {
            ValueList a1{args[0]};
            Value f = methodCall(Value::typeObj("Format"), "new", a1);
            return (*f.hash())["code"];
        }
        ValueList a1{Value::str(args[0].toStr())};
        return methodCall(Value::typeObj("RakuAST::StrLiteral"), "new", a1);
    }
    if (inv.t == VT::Hash && inv.hashKind == "Format") {
        if (m == "Str" || m == "gist" || m == "raku") return (*inv.hash())["fmt"];
        if (m == "arity" || m == "count") return (*inv.hash())["arity"];
        if (m == "Callable") return (*inv.hash())["code"];   // the formatter as a routine
        if (m == "signature") return methodCall((*inv.hash())["code"], "signature", args, rwArgs);
        // `.directives` names the conversion letter of each `%…` in order:
        // "%05d%3x:%s" is (d x s). Flags, width and precision are skipped —
        // the directive is the first ALPHABETIC character after the percent.
        if (m == "directives") {
            const std::string fmt = (*inv.hash())["fmt"].toStr();
            Value out = Value::array(); out.isList = true;
            for (size_t k = 0; k + 1 < fmt.size(); k++) {
                if (fmt[k] != '%') continue;
                if (fmt[k + 1] == '%') { k++; continue; } // a literal percent
                size_t j = k + 1;
                while (j < fmt.size() && !ascii::isalpha((unsigned char)fmt[j])) j++;
                if (j < fmt.size()) { out.arr()->push_back(Value::str(std::string(1, fmt[j]))); k = j; }
            }
            return out;
        }
        if (m == "CALL-ME" || m == "()") return methodCall((*inv.hash())["code"], "CALL-ME", args, rwArgs);
    }
    if (inv.t == VT::Type && inv.s == "Capture" && m == "new") {
        Value c = Value::array(); c.hashKind = "Capture"; c.itemized = true;
        for (auto& a : args) {
            if (a.t != VT::Pair || !a.pairVal()) continue;
            if (a.s == "list") {
                const Value& lv = *a.pairVal();
                if (lv.t == VT::Array && lv.arr()) for (auto& e : *lv.arr()) c.arr()->push_back(e);
                else if (lv.t == VT::Range) for (auto& e : lv.flatten()) c.arr()->push_back(e);
                else if (lv.t != VT::Nil && lv.t != VT::Any) c.arr()->push_back(lv);
            }
            else if (a.s == "hash") {
                const Value& hv = *a.pairVal();
                if (hv.t == VT::Hash && hv.hash())
                    for (auto& kv : *hv.hash()) { Value np = Value::pair(kv.first, kv.second); np.namedArg = true; c.arr()->push_back(std::move(np)); }
            }
        }
        return c;
    }
    // A Capture is TWO collections, not one flat list: positionals indexed 0..n
    // and nameds keyed by name. Every accessor partitions accordingly —
    // `\(1, 2, :x(3)).elems` is 2 (the positionals), and .keys/.values/.pairs
    // run the positionals first, then the nameds.
    if (inv.t == VT::Array && inv.hashKind == "Capture" &&
        (m == "list" || m == "hash" || m == "Map" || m == "elems" || m == "Numeric" ||
         m == "keys" || m == "values" || m == "pairs" || m == "antipairs" || m == "kv" ||
         m == "Bool")) {
        ValueList pos; std::map<std::string, Value> named;
        // A Pair is a named part only if it WENT IN as one: `\(:a(1))` is named,
        // `\(('a' => 1))` and a Pair slurped from a positional argument are not.
        if (inv.arr()) for (auto& e : *inv.arr()) {
            if (e.t == VT::Pair && e.namedArg) named[e.s] = e.pairVal() ? *e.pairVal() : Value::any();
            else pos.push_back(e);
        }
        if (m == "list") { Value o = Value::array(); o.isList = true; *o.arr() = pos; return o; }
        // `.Numeric` (and so prefix `+`) is the POSITIONAL count, like .elems — the
        // named parts do not add to it
        if (m == "elems" || m == "Numeric") return Value::integer((long long)pos.size());
        if (m == "Bool")  return Value::boolean(!pos.empty() || !named.empty());
        // `.kv` is key-then-value flattened: the positional INDEX then its value,
        // then each named's NAME then its value. Falling through to Array.kv
        // yielded the named part as (index, Pair) instead.
        if (m == "kv") {
            Value o = Value::array(); o.isList = true; o.s = "Seq";
            for (size_t i = 0; i < pos.size(); i++) {
                o.arr()->push_back(Value::integer((long long)i));
                o.arr()->push_back(pos[i]);
            }
            for (auto& kv : named) { o.arr()->push_back(Value::str(kv.first)); o.arr()->push_back(kv.second); }
            return o;
        }
        if (m == "keys" || m == "values" || m == "pairs" || m == "antipairs") {
            Value o = Value::array(); o.isList = true; o.s = "Seq";
            for (size_t i = 0; i < pos.size(); i++) {
                Value k = Value::integer((long long)i);
                if (m == "keys")      o.arr()->push_back(k);
                else if (m == "values") o.arr()->push_back(pos[i]);
                else if (m == "pairs")  { Value p = Value::pair(std::to_string(i), pos[i]);
                                          p.pairKeyM() = std::make_shared<Value>(k); o.arr()->push_back(p); }
                else { Value p = Value::pair(pos[i].toStr(), k); // antipairs: value => key
                       p.pairKeyM() = std::make_shared<Value>(pos[i]); o.arr()->push_back(p); }
            }
            for (auto& kv : named) {
                if (m == "keys")        o.arr()->push_back(Value::str(kv.first));
                else if (m == "values") o.arr()->push_back(kv.second);
                else if (m == "pairs")  { Value p = Value::pair(kv.first, kv.second);
                                          p.namedArg = true; o.arr()->push_back(p); }
                else { Value p = Value::pair(kv.second.toStr(), Value::str(kv.first));
                       p.pairKeyM() = std::make_shared<Value>(kv.second); o.arr()->push_back(p); }
            }
            return o;
        }
        Value o = Value::makeHash(); o.hashKind = "Map";
        for (auto& kv : named) (*o.hash())[kv.first] = kv.second;
        return o;
    }

    // A `but`/`does` mixin over a non-object base: a composed role/class method wins,
    // object-identity/introspection methods stay on the object, and every other
    // method (coercions, arithmetic-ish, base-type methods) delegates to the box.
    // …and a QUALIFIED call past the object's own method (`self.IO::Path::slurp`
    // from inside a `slurp` override, or from a role mixed in over it —
    // IO::Path::AutoDecompress's Proccer) goes to the box whatever the class
    // defines: skipOwn is exactly "not mine, the built-in's".
    // …but `.new` — and its two other spellings — on an INSTANCE means "another
    // one of these". Rakudo inherits them from Mu, so the invocant's TYPE does
    // the constructing. Forwarding them to the box instead asked a Hash VALUE
    // for a `new` only the Hash TYPE has, and `self.new!open-file: $spec` — how
    // a PDF re-opens a document from one of its own instances — died with "No
    // such method 'new' for invocant of type 'Hash'".
    if (inv.t == VT::Object && inv.obj() && inv.obj()->hasBoxed && inv.obj()->cls &&
        !m.skipOwn && (m == "new" || m == "bless" || m == "CREATE") &&
        !inv.obj()->cls->findMethod(m) && classes_.count(inv.obj()->cls->name))
        return methodCall(Value::typeObj(inv.obj()->cls->name), m, args, rwArgs);
    if (inv.t == VT::Object && inv.obj() && inv.obj()->hasBoxed && inv.obj()->cls &&
        (m.skipOwn || (!inv.obj()->cls->findMethod(m) && !inv.obj()->cls->findAttr(m)))) {
        static const std::set<std::string> keepOnObj = {
            // `.can` must see the MIXIN's methods — forwarding it to the boxed
            // value hides them (`(Any but $failure).can('Failure')`)
            "does", "HOW", "WHAT", "WHICH", "defined", "DEFINITE", "isa", "WHERE", "can",
            // …and `.VAR` of a mixin is the mixin itself: `(5 but R).VAR` is an Int+{R}
            "VAR"};
        if (!keepOnObj.count(m)) {
            Value r = methodCall(inv.obj()->boxed, m, args, rwArgs);
            // …and a subclassed Promise's `.then` is one of the subclass too
            // (S17-promise/basic.t: `class Meows is Promise`)
            if (m == "then" && r.t == VT::Hash && r.hashKind == "Promise" &&
                inv.obj()->boxed.t == VT::Hash && inv.obj()->boxed.hashKind == "Promise") {
                auto od = makePayload<ObjectData>();
                od->cls = inv.obj()->cls;
                od->hasBoxed = true;
                od->boxed = r;
                return Value::object(od);
            }
            return r;
        }
    }

    // `IO::Socket::INET.listen($host, $port, …)` / `.connect($host, $port, …)`:
    // the positional spellings of `.new(:listen, :localhost, :localport)` and
    // `.new(:host, :port)`
    if (inv.t == VT::Type && inv.s == "IO::Socket::INET" && (m == "listen" || m == "connect")) {
        ValueList na;
        size_t pos = 0;
        for (auto& a : args) {
            if (a.t == VT::Pair) { na.push_back(a); continue; }
            const char* key = pos == 0 ? (m == "listen" ? "localhost" : "host")
                                       : (m == "listen" ? "localport" : "port");
            if (pos++ < 2) { Value p = Value::pair(key, a); p.namedArg = true; na.push_back(p); }
        }
        if (m == "listen") { Value p = Value::pair("listen", Value::boolean(true)); p.namedArg = true; na.push_back(p); }
        return methodCall(inv, "new", na);
    }
    // Pair.new($key, $value) or Pair.new(:key(...), :value(...)) — same shape as `=>`.
    // IO::Socket::INET.new — a TCP client (:host/:port) or a listener (:listen).
    if (inv.t == VT::Type && inv.s == "IO::Socket::INET" && m == "new") {
        std::string host = "localhost", localhost; long port = 0, localport = 0; bool listen = false;
        long family = -2; // -2 = unspecified
        for (auto& a : args) {
            if (a.t != VT::Pair) continue;
            Value pv = a.pairVal() ? *a.pairVal() : Value::any();
            if (a.s == "host") host = pv.toStr();
            else if (a.s == "port") port = pv.toInt();
            else if (a.s == "localhost") localhost = pv.toStr();
            else if (a.s == "localport") localport = pv.toInt();
            else if (a.s == "listen") listen = pv.truthy();
            // the ProtocolFamily enum carries Rakudo's ordinals (PF_INET is 1,
            // PF_INET6 2), not the OS numbers the socket calls want
            else if (a.s == "family")
                family = pv.enumType == "ProtocolFamily"
                    ? (pv.enumName == "PF_INET" ? PF_INET : pv.enumName == "PF_INET6" ? PF_INET6
                       : pv.enumName == "PF_UNSPEC" ? 0 : (long)pv.toInt())
                    : pv.toInt();
        }
        // `:host<name:port>` — the port may ride along in the host string, and an
        // explicit `:port` wins over it. That is how HTTP::UserAgent connects:
        // a request's `.host` is its Host HEADER, which carries the port
        // whenever it is not the scheme's default, so a request to
        // http://localhost:3137/ asked for the host "localhost:3137" and we
        // tried to resolve that as a NAME.
        // Only a SINGLE colon: an IPv6 literal (`::1`, `fe80::1`) has several
        // and is a host in its own right.
        auto splitHostPort = [](std::string& h, long& p) {
            size_t c = h.find(':');
            if (c == std::string::npos || h.find(':', c + 1) != std::string::npos) return;
            if (c + 1 >= h.size()) return;
            std::string tail = h.substr(c + 1);
            h = h.substr(0, c);
            if (p) return;                       // an explicit port wins
            char* end = nullptr;
            long v = std::strtol(tail.c_str(), &end, 10);
            if (end && *end == '\0') p = v;      // …and a non-numeric tail is simply dropped
        };
        // PF_UNIX / PF_LOCAL: a UNIX-domain stream socket at the PATH the host names
        bool unixFam = false;
        for (auto& a : args)
            if (a.t == VT::Pair && a.s == "family" && a.pairVal() &&
                (a.pairVal()->enumName == "PF_UNIX" || a.pairVal()->enumName == "PF_LOCAL")) unixFam = true;
        if (unixFam) {
            const std::string path = listen ? localhost : host;
#ifdef _WIN32
            // Winsock has no <sys/un.h>; its AF_UNIX (afunix.h, Windows 10 1803+)
            // is not wired up, so the family is refused rather than mis-dialled
            throw RakuError{Value::typeObj("X::AdHoc"), std::string(listen ? "Cannot listen on " : "Cannot connect to ") +
                            path + ": UNIX-domain sockets are not supported on Windows"};
#else
            int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd < 0) throw RakuError{Value::typeObj("X::AdHoc"), "Cannot create a socket: " + std::string(std::strerror(errno))};
            sockaddr_un ua{}; ua.sun_family = AF_UNIX;
            std::strncpy(ua.sun_path, path.c_str(), sizeof(ua.sun_path) - 1);
            int rc;
            if (listen) rc = (::bind(fd, (sockaddr*)&ua, sizeof(ua)) < 0 || ::listen(fd, 128) < 0) ? -1 : 0;
            else { bool pk = gilPark(); rc = ::connect(fd, (sockaddr*)&ua, sizeof(ua)); gilUnpark(pk); }
            if (rc < 0) {
                int err = errno; ::close(fd);
                throw RakuError{Value::typeObj("X::AdHoc"), std::string(listen ? "Cannot listen on " : "Cannot connect to ") +
                                path + ": " + std::strerror(err)};
            }
            Value s = Value::makeHash(); s.hashKind = "Socket"; (*s.hash())["fd"] = Value::integer(fd);
            (*s.hash())["unix"] = Value::boolean(true);
            (*s.hash())[listen ? "localhost" : "peerhost"] = Value::str(path);
            return s;
#endif
        }
        if (listen) splitHostPort(localhost, localport);
        else        splitHostPort(host, port);
        // Validate before touching the OS: port 0..65535, family a sane small value.
        long usePort = listen ? localport : port;
        if (usePort < 0 || usePort > 65535)
            throw RakuError{Value::typeObj("X::AdHoc"), "Invalid port: " + std::to_string(usePort)};
        if (family != -2 && family != 0 && family != PF_INET) // validated, then IGNORED before: AF_INET was opened for :family(PF_INET6)
            throw RakuError{Value::typeObj("X::AdHoc"), "Socket family " + std::to_string(family) + " is not supported: only PF_INET (" + std::to_string(PF_INET) + ")"};
        auto resolve = [](const std::string& h, sockaddr_in& addr) {
            addr.sin_addr.s_addr = inet_addr(h.c_str());
            if (addr.sin_addr.s_addr == INADDR_NONE) {
                if (hostent* he = gethostbyname(h.c_str())) memcpy(&addr.sin_addr, he->h_addr, he->h_length);
            }
        };
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) throw RakuError{Value::typeObj("X::AdHoc"), "Cannot create a socket: " + std::string(std::strerror(errno))};
        sockaddr_in addr{}; addr.sin_family = AF_INET;
        if (listen) {
            int yes = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
            addr.sin_port = htons((uint16_t)localport);
            // A NAME is as valid here as a dotted quad — `:localhost<localhost>`
            // is how a test spins up a server on the loopback. Only the client
            // path resolved, so the listener bound to INADDR_NONE and failed,
            // handing back Nil with nothing to say why.
            if (localhost.empty() || localhost == "0.0.0.0") addr.sin_addr.s_addr = INADDR_ANY;
            else resolve(localhost, addr);
            if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0 || ::listen(fd, 128) < 0) { // a Nil here let the program run on with nobody listening
                int err = errno; ::close(fd);
                throw RakuError{Value::typeObj("X::AdHoc"), "Cannot listen on " + (localhost.empty() ? std::string("0.0.0.0") : localhost) +
                                ":" + std::to_string(localport) + ": " + std::strerror(err)};
            }
        } else {
            addr.sin_port = htons((uint16_t)port);
            resolve(host, addr);
            bool p = gilPark(); int rc = ::connect(fd, (sockaddr*)&addr, sizeof(addr)); gilUnpark(p);
            if (rc < 0) { // a Nil here let `$s.print` on Any pass silently
                int err = errno; ::close(fd);
                throw RakuError{Value::typeObj("X::AdHoc"), "Cannot connect to " + host + ":" + std::to_string(port) + ": " + std::strerror(err)};
            }
        }
        Value s = Value::makeHash(); s.hashKind = "Socket"; (*s.hash())["fd"] = Value::integer(fd);
        // Keep the name as GIVEN: `.localhost` answers what was asked for, not
        // what it resolved to, which is what Rakudo reports.
        if (listen) { if (!localhost.empty()) (*s.hash())["localhost"] = Value::str(localhost); }
        else (*s.hash())["peerhost"] = Value::str(host);
        return s;
    }
    // CArray[T].new(vals…) — a packed native array (NativeCall). Stored as raw
    // bytes in .s (like Blob); callNative passes a pointer to the bytes.
    // NativeCall CStruct field read: `$s.field` on a native-backed struct reads
    // native memory at the field's computed offset. (Writes go through the
    // assignment path.) Only for a repr('CStruct') class the accessor doesn't
    // otherwise define a real method for.
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls &&
        (inv.obj()->cls->repr == "CStruct" || inv.obj()->cls->repr == "CPPStruct" ||
         inv.obj()->cls->repr == "CUnion") &&
        inv.obj()->attrs.count("__native_ptr") && !inv.obj()->cls->findMethod(m)) {
        std::string type; long long off = Interpreter::ncFieldOffset(inv.obj()->cls.get(), m, type);
        if (off >= 0) {
            long long base = inv.obj()->attrs["__native_ptr"].toInt();
            long long fa = base + off;
            // scalar field: read directly; pointer/Str/class field: read the 8-byte
            // pointer and box it appropriately.
            // An INLINE member (`HAS Inner $.in`) IS these bytes — hand back a
            // view onto them, so `$o.in.x` reads and `$o.in.x = 1` writes the
            // outer struct's own memory. ncFieldOffset marks it; a plain `has`
            // of the same type stores a POINTER and is dereferenced below.
            if (type.rfind("HAS ", 0) == 0) {
                std::string inner = type.substr(4);
                auto ict = classes_.find(inner);
                if (ict == classes_.end()) ict = classes_.find(resolveClassAlias(inner));
                if (ict != classes_.end()) {
                    Value o; o.t = VT::Object; o.setObj(makePayload<ObjectData>());
                    o.obj()->cls = ict->second;
                    o.obj()->attrs["__native_ptr"] = Value::integer(fa);
                    return o;   // borrowed: the OUTER struct owns the memory
                }
                type = inner;
            }
            std::string bt = type.substr(0, type.find('['));
            if (bt == "Str") { long long p; std::memcpy(&p, (void*)(intptr_t)fa, 8); return Value::str(p ? std::string((const char*)(intptr_t)p) : ""); }
            if (bt == "Pointer") { long long p; std::memcpy(&p, (void*)(intptr_t)fa, 8); return ncMakePointer(type, (void*)(intptr_t)p); }
            if (bt == "CArray")  { long long p; std::memcpy(&p, (void*)(intptr_t)fa, 8); return ncMakeLiveCArray(type, (void*)(intptr_t)p); }
            auto cit = classes_.find(type);
            if (cit != classes_.end()) { // nested CStruct/CPointer field → box the pointer
                long long p; std::memcpy(&p, (void*)(intptr_t)fa, 8);
                Value o; o.t = VT::Object; o.setObj(makePayload<ObjectData>());
                o.obj()->cls = cit->second; o.obj()->attrs["__native_ptr"] = Value::integer(p);
                return o;
            }
            return Interpreter::ncReadElem(fa, type, 0);
        }
    }
    // Segments continue in methodCallPart1b and methodCallPart1c (MethodCallPart1b.cpp, MethodCallPart1c.cpp) — same ordered chain.
    if (auto r = methodCallPart1b(inv, m, args, rwArgs)) return std::move(*r);
    if (auto r = methodCallPart1c(inv, m, args, rwArgs)) return std::move(*r);
    // Segment continues in MethodCallPart2.cpp — same ordered chain.
    if (auto r = methodCallPart2(inv, m, args, rwArgs)) return std::move(*r);
    // Segment continues in MethodCallPart3.cpp — same ordered chain.
    if (auto r = methodCallPart3(inv, m, args, rwArgs)) return std::move(*r);
    if (auto r = methodCallTail(inv, m, args, rwArgs)) return std::move(*r);
    // fallthrough: unknown method — but any method call on Nil returns Nil
    if (inv.t == VT::Nil) return Value::nil();
    // FALLBACK: a class may catch every unresolved method itself, receiving the
    // NAME first and then the original arguments (Terminal::ANSI::OO turns each
    // colour name into a method this way). Looked up last, so it never shadows
    // a real method, and skipped for FALLBACK itself to avoid a loop.
    if (m != "FALLBACK") {
        ClassInfo* fci = nullptr;
        if (inv.t == VT::Object && inv.obj()) fci = inv.obj()->cls.get();
        else if (inv.t == VT::Type) { auto it = classes_.find(inv.s); if (it != classes_.end()) fci = it->second.get(); }
        if (fci)
            if (Value* fb = fci->findMethod("FALLBACK")) {
                ValueList fargs; fargs.reserve(args.size() + 1);
                fargs.push_back(Value::str(m));
                for (auto& a : args) fargs.push_back(a);
                return invokeMethod(*fb, inv, fargs);
            }
    }
    // …and the fallbacks `.^add_fallback` registered, for the invocant's type
    // or any class it inherits from: the first whose condition takes the name
    // computes the method, which is then called like any other
    if (!addedFallbacks_.empty()) {
        std::vector<std::string> names;
        if (inv.t == VT::Object && inv.obj() && inv.obj()->cls)
            for (ClassInfo* c = inv.obj()->cls.get(); c; c = c->parent.get()) names.push_back(c->name);
        else names.push_back(inv.t == VT::Type ? std::string(inv.s.c_str()) : inv.typeName());
        for (auto& tn : names) {
            auto fit = addedFallbacks_.find(tn);
            if (fit == addedFallbacks_.end()) continue;
            auto fbs = fit->second;   // a copy: a fallback may add another
            for (auto& fb : fbs) {
                ValueList ca{inv, Value::str(m)};
                if (!callCallable(fb.first, ca).truthy()) continue;
                ValueList ka{inv, Value::str(m)};
                Value meth = callCallable(fb.second, ka);
                ValueList margs; margs.reserve(args.size() + 1);
                margs.push_back(inv);
                for (auto& a : args) margs.push_back(a);
                return callCallable(meth, margs);
            }
        }
    }
    // A grammar's RULE is also a method: `G.new.some-rule` is legal Raku. With no
    // string to match it starts on an empty cursor and fails, so Rakudo hands back
    // a falsy Cursor. Answer an undefined Match, which is falsy and matches
    // nothing — URI's suite asserts exactly that with
    // `nok 'foo' ~~ IETF::RFC_Grammar::URI.new().TOP-non-empty`.
    {
        ClassInfo* gci = inv.t == VT::Object && inv.obj() ? inv.obj()->cls.get()
                       : inv.t == VT::Type ? (classes_.count(inv.s) ? classes_[inv.s].get() : nullptr)
                       : nullptr;
        for (ClassInfo* c = gci; c; c = c->parent.get())
            if (c->isGrammar && c->findRule(m)) {
                // With no argument the rule runs on an EMPTY cursor, and the result
                // is that failed-or-successful Cursor. Smart-matching a string
                // against it yields the cursor's own truthiness, whatever the
                // string — which is why `'#foo' ~~ G.new.URI-reference` is True
                // (URI-reference matches "") and `'foo' ~~ G.new.absolute-URI` is
                // False (absolute-URI does not).
                return grammarParse(c, "", /*subparse=*/false, m, Value());
            }
    }
    // An ITERABLE object answers the list methods that Rakudo's Iterable role
    // supplies, by running its own `.iterator`: `glob('*.md').sort` works because
    // IO::Glob `does Iterable`. Deliberately NOT the whole list surface — Rakudo
    // splits it, and `.list`/`.elems`/`.reverse`/`.join`/`.kv` on an Iterable
    // object mean the invocant AS ONE ITEM (checked against Rakudo, one by one).
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) {
        static const std::set<std::string> kIterableMethods = {
            "sort", "map", "grep", "first", "head", "tail", "unique", "squish",
            "Seq", "flat" };
        if (kIterableMethods.count(m)) {
            bool iterable = inv.obj()->cls->findMethod("iterator") != nullptr;
            for (ClassInfo* c = inv.obj()->cls.get(); c && !iterable; c = c->parent.get())
                if (c->doesRole("Iterable")) iterable = true;
            ValueList items;
            if (iterable && objListItems(inv, items)) {
                Value lst = Value::array(); lst.isList = true; *lst.arr() = std::move(items);
                return methodCall(lst, m, args, rwArgs);
            }
        }
    }
    // A user class deriving the BUILTIN IO::Handle (no ClassInfo behind it)
    // still owes the handle protocol's small setup surface — Test::Output's
    // capture classes call self.encoding('utf8') from their constructors.
    // Attr-backed, added per real need, measured by the battery.
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) {
        std::string nb;
        for (ClassInfo* c = inv.obj()->cls.get(); c && nb.empty(); c = c->parent.get())
            nb = c->nativeParent;
        if (nb == "IO::Handle") {
            // `.chomp` / `.nl-in` / `.nl-out` are handle STATE with defaults, and
            // a derived class inherits them. Without this they fell through to
            // Str.chomp on the object's own stringification, so Text::CSV's
            // `my Bool $chomped = $io.chomp` type-checked a Str against Bool.
            if ((m == "chomp" || m == "nl-in" || m == "nl-out") && args.empty()) {
                auto it = inv.obj()->attrs.find(m);
                if (it != inv.obj()->attrs.end() && it->second.t != VT::Any) return it->second;
                if (m == "chomp") return Value::boolean(true);
                if (m == "nl-out") return Value::str("\n");
                Value d = Value::array();               // nl-in: Rakudo's default pair
                d.arr()->push_back(Value::str("\n"));
                d.arr()->push_back(Value::str("\r\n"));
                d.isList = d.itemized = true;           // $("\n", "\r\n"), a List, as the base answers
                return d;
            }
            if (m == "encoding") {
                if (!args.empty() && args[0].t != VT::Pair) {
                    inv.obj()->attrs["__io-encoding"] = args[0];
                    return args[0];
                }
                auto it = inv.obj()->attrs.find("__io-encoding");
                return it != inv.obj()->attrs.end() ? it->second : Value::str("utf8");
            }
            // The handle WRITE protocol: print/say/put on a derived handle
            // encode and funnel into the class's own WRITE(Blob) — exactly
            // how Rakudo's IO::Handle routes them, and how Test::Output's
            // capture classes receive everything they capture.
            if (m == "print" || m == "say" || m == "put") {
                if (Value* wm = inv.obj()->cls->findMethod("WRITE")) {
                    std::string s;
                    for (auto& a : args) {
                        if (a.t == VT::Pair) continue;
                        s += m == "say" ? methodCall(a, "gist", ValueList{}, nullptr).toStr()
                                        : a.toStr();
                    }
                    if (m != "print") s += "\n";
                    Value blob = Value::str(s);
                    blob.hashKind = "Blob";
                    return invokeMethod(*wm, inv, ValueList{blob}, nullptr);
                }
            }
            // …and the rest of the writers funnel there too: `.printf` formats,
            // `.print-nl` sends the handle's nl-out, `.write` hands its Blob on
            // as is, and `.spurt` either (a Str is encoded, a Blob is not)
            if (m == "printf" || m == "print-nl" || m == "write" || m == "spurt") {
                if (Value* wm = inv.obj()->cls->findMethod("WRITE")) {
                    ValueList pos;
                    for (auto& a : args) if (!(a.t == VT::Pair && a.namedArg)) pos.push_back(a);
                    Value blob;
                    if ((m == "write" || m == "spurt") && !pos.empty() && pos[0].t == VT::Str &&
                        (pos[0].hashKind == "Blob" || pos[0].hashKind == "Buf"))
                        blob = pos[0];
                    else {
                        std::string s;
                        if (m == "printf" && !pos.empty()) {
                            ValueList fa(pos.begin() + 1, pos.end());
                            s = doSprintf(pos[0].toStr(), fa, langRev_);
                        }
                        else if (m == "print-nl") s = methodCall(inv, "nl-out", ValueList{}).toStr();
                        else if (!pos.empty()) s = pos[0].toStr();
                        blob = Value::str(s);
                        blob.hashKind = "Blob";
                    }
                    return invokeMethod(*wm, inv, ValueList{blob}, nullptr);
                }
            }
            if (m == "flush" || m == "close") return Value::boolean(true);
        }
    }
    // The invocant may be a type whose `class`/`grammar` declaration is further
    // down the file — those are compile-time in Rakudo, so `say f("x"); grammar
    // G {…}` works there. Create it now and try once more.
    if (inv.t == VT::Type && !inv.s.empty() && materializePendingType(inv.s))
        return methodCall(inv, m, args);
    // Rakudo's Any gives EVERY object the one-element-list interface:
    // `Foo.new.elems` is 1, `.list` is `(Foo.new)`, `.head` is the object
    // itself, `.keys` is `(0)`. rakupp had this for the simple scalars only
    // (42.elems is 1), so a module asking `$.type.elems` about an object of one
    // of its own classes died where Rakudo answers 1 — Data::TypeSystem's
    // Examiner, and the six dists queued behind it. Last resort, after every
    // user method and builtin path has already declined.
    // …and a CODE object is an Any like any other: `rx/a/.cache` is `(rx/a/,)`,
    // `(sub {}).keys` is `(0)`. Only objects reached this arm, so a Regex — the
    // Code a module is most likely to hand around as a value — died on the
    // one-element interface. Testo caches the regex it was given before
    // matching with it, and stopped on its first assertion.
    // A user class that `is Cool` gets Cool's numeric methods, which work on
    // `self.Numeric`: `class NotComplex is Cool { method Numeric { $magic } }`
    // answers `.conj`, `.exp`, `.log`, `.sqrt`, `.roots`… as $magic does.
    if (inv.t == VT::Object && inv.obj() && inv.obj()->cls) {
        static const std::set<std::string> kCoolNumeric = {
            "abs", "conj", "exp", "log", "log10", "log2", "sqrt", "roots", "sign",
            "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sec", "cosec", "cotan",
            "asec", "acosec", "acotan", "sinh", "cosh", "tanh", "sech", "cosech", "cotanh",
            "asinh", "acosh", "atanh", "asech", "acosech", "acotanh", "floor", "ceiling",
            "round", "truncate", "cis", "unpolar", "polar", "is-prime", "expmod", "rand",
            "Int", "Num", "Rat", "FatRat", "Real", "Bridge", "Complex", "UInt", "narrow",
            "base", "sqrt", "succ", "pred"};
        if (kCoolNumeric.count(m)) {
            bool isCool = false;
            for (ClassInfo* c = inv.obj()->cls.get(); c && !isCool; c = c->parent.get())
                if (c->name == "Cool" || c->nativeParent == "Cool") isCool = true;
            if (isCool && inv.obj()->cls->findMethod("Numeric")) {
                Value num = methodCall(inv, "Numeric", {});
                if (!(num.t == VT::Object)) return methodCall(num, m, args);
            }
        }
    }
    if (inv.t == VT::Object || inv.t == VT::Code || inv.t == VT::Regex) {
        static const std::set<std::string> kOneElem = {
            "elems", "end", "list", "List", "Array", "flat", "cache", "eager",
            "values", "keys", "pairs", "antipairs", "kv", "head", "tail",
            "first", "map", "grep", "sort", "reverse", "reduce", "unique",
            "squish", "skip", "batch", "rotor", "combinations", "permutations"};
        if (kOneElem.count(m)) {
            Value one = Value::array(); one.arr()->push_back(inv); one.isList = true;
            return methodCall(one, m, args);
        }
    }
    // `.new` on an INSTANCE of a built-in type is its type's constructor:
    // `$pair.new(:key<a>, :value<b>)`, `5.new` — and a PARAMETERIZED type
    // object spelled out as a name (`Array[Numeric]`, as an attribute's `.=`
    // seed) constructs the base type with that element type
    if (m == "new") {
        if (inv.t == VT::Type && inv.s.str().find('[') != std::string::npos && inv.s.str().back() == ']') {
            std::string full = inv.s.str();
            size_t br = full.find('[');
            Value base = Value::typeObj(full.substr(0, br));
            base.ofTypeM() = full.substr(br + 1, full.size() - br - 2);
            if (base.s != inv.s) return methodCall(base, "new", args);
        }
        if (inv.t != VT::Type && inv.t != VT::Object && inv.t != VT::Code && inv.t != VT::Any &&
            inv.t != VT::Nil && rtIsDefined(inv)) {
            std::string tn = inv.typeName();
            if (!tn.empty() && isKnownTypeName(tn)) return methodCall(Value::typeObj(tn), "new", args);
        }
    }
    if (std::getenv("RAKUPP_TRACE"))
        std::cerr << "[NoMethod] ." << m << " on " << inv.typeName()
                  << " at " << (srcFile_.empty() ? "?" : srcFile_) << ":" << curLine_ << "\n";
    // A private call arrives as its DISPATCH KEY — `!wrong`, which is what the
    // class method tables are keyed by (`md->isPrivate ? "!" + name : name`).
    // The exception reports the method as it was WRITTEN, and says which kind
    // of call it was: roast's S12-methods/private.t asks for
    // `method => 'wrong', private => &so`, and Rakudo's message names the kind
    // too. Only the key is prefixed — a Raku method name cannot begin with `!`.
    const bool priv = m.size() > 1 && m[0] == '!';
    const std::string name = priv ? m.substr(1) : (const std::string&)m;
    throwTypedV("X::Method::NotFound",
                {{"method",   Value::str(name)},
                 {"typename", Value::str(inv.typeName())},
                 {"private",  Value::boolean(priv)}},
                std::string("No such ") + (priv ? "private " : "") + "method '" +
                    (const std::string&)m + "' for invocant of type '" + inv.typeName() + "'");
}

} // namespace rakupp
