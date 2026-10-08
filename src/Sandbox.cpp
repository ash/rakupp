// --sandbox: the process-wide switch, its exception, and the tables that say
// which spellings of an operation reach the OS (Sandbox.h).

#include "Sandbox.h"
#include "Interpreter.h"

#include <cstring>

namespace rakupp {

bool g_sandboxed = false;
bool g_sandboxChecks = false;

void sandboxEnable() { g_sandboxed = g_sandboxChecks = true; }

void sandboxChecksOffForSelftest() { g_sandboxChecks = false; }

const char* sandboxCapName(SandboxCap cap) {
    switch (cap) {
        case SandboxCap::Read:  return "read";
        case SandboxCap::Write: return "write";
        case SandboxCap::Run:   return "run";
        case SandboxCap::Net:   return "net";
        case SandboxCap::Ffi:   return "ffi";
    }
    return "?";
}

void sandboxRefuse(Interpreter& I, const std::string& operation, SandboxCap cap) {
    const char* c = sandboxCapName(cap);
    I.throwTyped("X::SecurityPolicy::Sandbox", {{"operation", operation}, {"capability", c}},
                 operation + " is not allowed in the sandbox: it needs " + c + " access");
}

void sandboxRefuseBare(const std::string& operation, SandboxCap cap) {
    const char* c = sandboxCapName(cap);
    throw RakuError{Value::typeObj("X::SecurityPolicy::Sandbox"),
                    operation + " is not allowed in the sandbox: it needs " + c + " access"};
}

// `open`'s adverbs: anything that may create, truncate or write is a write.
SandboxCap sandboxOpenCap(const ValueList& args) {
    for (auto& x : args) {
        if (x.t != VT::Pair) continue;
        if (x.pairVal() && !x.pairVal()->truthy()) continue;   // `:!w` asks for nothing
        const std::string k = x.s;
        if (k == "w" || k == "a" || k == "x" || k == "rw" || k == "ra" || k == "rx" ||
            k == "update" || k == "exclusive" || k == "create" || k == "truncate" || k == "append")
            return SandboxCap::Write;
        if (k == "mode" && x.pairVal() && x.pairVal()->toStr() != "ro") return SandboxCap::Write;
    }
    return SandboxCap::Read;
}

namespace {

using C = SandboxCap;
struct Row { const char* name; C cap; };

template <size_t N>
const Row* findRow(const Row (&rows)[N], const std::string& m) {
    for (auto& r : rows) if (m == r.name) return &r;
    return nullptr;
}

// IO::Path methods that reach the file system. Everything else an IO::Path
// answers — .basename, .parent, .add, .absolute, .Str … — is arithmetic on
// the string and is never refused.
const Row kPathMethods[] = {
    {"slurp", C::Read}, {"lines", C::Read}, {"words", C::Read}, {"comb", C::Read},
    {"split", C::Read}, {"Supply", C::Read}, {"open", C::Read},
    {"e", C::Read}, {"f", C::Read}, {"d", C::Read}, {"r", C::Read}, {"w", C::Read},
    {"x", C::Read}, {"rw", C::Read}, {"rwx", C::Read}, {"rx", C::Read}, {"wx", C::Read},
    {"l", C::Read}, {"s", C::Read}, {"z", C::Read}, {"mode", C::Read},
    {"user", C::Read}, {"group", C::Read},
    {"modified", C::Read}, {"accessed", C::Read}, {"changed", C::Read}, {"created", C::Read},
    {"dir", C::Read}, {"contents", C::Read}, {"resolve", C::Read}, {"readlink", C::Read},
    {"chdir", C::Read}, {"watch", C::Read},
    {"spurt", C::Write}, {"mkdir", C::Write}, {"rmdir", C::Write}, {"unlink", C::Write},
    {"symlink", C::Write}, {"link", C::Write}, {"copy", C::Write}, {"rename", C::Write},
    {"move", C::Write}, {"chmod", C::Write},
};

// …and the ones rakupp also answers on a plain Str (`"a.txt".copy("b.txt")`).
// Not .lines/.words/.comb/.split: on a Str those are about the string.
const Row kStrMethods[] = {
    {"open", C::Read},
    {"modified", C::Read}, {"accessed", C::Read}, {"changed", C::Read}, {"created", C::Read},
    {"dir", C::Read}, {"contents", C::Read}, {"resolve", C::Read}, {"readlink", C::Read},
    {"copy", C::Write}, {"rename", C::Write}, {"move", C::Write},
};

// IO::Handle methods that re-open the handle's path. Under the sandbox no file
// handle can have been opened, so one that HAS a path is an unopened
// `IO::Handle.new(:path)`, which these would open behind the program's back.
const Row kHandleMethods[] = {
    {"open", C::Read}, {"slurp", C::Read}, {"slurp-rest", C::Read}, {"lines", C::Read},
    {"words", C::Read}, {"get", C::Read}, {"getline", C::Read}, {"getc", C::Read},
    {"read", C::Read}, {"readchars", C::Read}, {"comb", C::Read}, {"split", C::Read},
    {"Supply", C::Read}, {"eof", C::Read}, {"seek", C::Read}, {"tell", C::Read},
    {"t", C::Read}, {"lock", C::Read}, {"unlock", C::Read}, {"native-descriptor", C::Read},
    {"print", C::Write}, {"print-nl", C::Write}, {"printf", C::Write}, {"put", C::Write},
    {"say", C::Write}, {"spurt", C::Write}, {"write", C::Write}, {"flush", C::Write},
};

// The builtin SUBS that reach the OS. `open` and `slurp` are handled apart,
// because each has a spelling that only reads standard input.
const Row kSubs[] = {
    {"dir", C::Read}, {"EVALFILE", C::Read}, {"chdir", C::Read}, {"indir", C::Read},
    {"rakupp-gzslurp", C::Read},
    {"spurt", C::Write}, {"mkdir", C::Write}, {"rmdir", C::Write}, {"unlink", C::Write},
    {"rename", C::Write}, {"move", C::Write}, {"copy", C::Write}, {"chmod", C::Write},
    {"link", C::Write}, {"symlink", C::Write}, {"make-temp-file", C::Write},
    {"make-temp-dir", C::Write}, {"rakupp-gzspurt", C::Write},
    {"rakupp-repo-lock", C::Write}, {"rakupp-repo-unlock", C::Write},
    {"run", C::Run}, {"shell", C::Run}, {"QX", C::Run}, {"__qx__", C::Run},
    {"rakupp-ext-load", C::Ffi}, {"nativecast", C::Ffi}, {"cglobal", C::Ffi},
    {"guess_library_name", C::Ffi},
};

// The name a refusal reports: the spelling the program wrote.
std::string subOpName(const char* name) {
    if (!std::strcmp(name, "__qx__") || !std::strcmp(name, "QX")) return "qx";
    return name;
}

// The first positional argument, or nullptr.
const Value* firstPositional(const ValueList& a) {
    for (auto& x : a) if (x.t != VT::Pair) return &x;
    return nullptr;
}

bool isDash(const Value& v) { return v.t == VT::Str && v.toStr() == "-"; }
bool isHandle(const Value& v) { return v.t == VT::Hash && v.hashKind == "FileHandle"; }

}  // namespace

void sandboxWrapBuiltin(const std::string& name, BuiltinFn& fn) {
    // `open('-')` is standard input (or output, with :w); any other path is
    // refused, as a write if an adverb asks to write.
    if (name == "open") {
        BuiltinFn orig = fn;
        fn = [orig](Interpreter& in, ValueList& a) -> Value {
            const Value* p = firstPositional(a);
            if (p && isDash(*p)) return orig(in, a);
            sandboxRefuse(in, "open", sandboxOpenCap(a));
        };
        return;
    }
    // `slurp()` reads $*ARGFILES (standard input unless @*ARGS names files,
    // which $*ARGFILES refuses itself), and `slurp($*IN)` an open handle; a
    // path is refused.
    if (name == "slurp") {
        BuiltinFn orig = fn;
        fn = [orig](Interpreter& in, ValueList& a) -> Value {
            const Value* p = firstPositional(a);
            if (!p || isDash(*p) || isHandle(*p)) return orig(in, a);
            sandboxRefuse(in, "slurp", SandboxCap::Read);
        };
        return;
    }
    const Row* r = findRow(kSubs, name);
    if (!r) return;
    std::string op = subOpName(r->name);
    C cap = r->cap;
    fn = [op, cap](Interpreter& in, ValueList&) -> Value { sandboxRefuse(in, op, cap); };
}

void sandboxMethodGate(Interpreter& I, const Value& inv, const std::string& m, const ValueList& args) {
    if (inv.t == VT::Str) {
        const bool io = inv.hashKind == "IO";
        if (!io && !inv.hashKind.empty()) return;            // a Buf, an enum … not a path
        const Row* r = io ? findRow(kPathMethods, m) : findRow(kStrMethods, m);
        if (!r) return;
        C cap = m == "open" ? sandboxOpenCap(args) : r->cap;
        // `'-'.IO` is standard input (and, opened to write, standard output)
        if (io && isDash(inv) && (cap == C::Read || m == "open")) return;
        sandboxRefuse(I, std::string(io ? "IO::Path." : "Str.") + m, cap);
    }
    if (!isHandle(inv) || !inv.hash()) return;
    const auto& h = *inv.hash();
    // the standard handles, a child's pipes and $*ARGFILES's captured buffer
    // are not files; only a handle with a path of its own would open one
    if (!h.count("path") || h.count("std") || h.count("proc-owner") || h.count("captured") ||
        h.count("live-tok"))
        return;
    const Row* r = findRow(kHandleMethods, m);
    if (!r) return;
    sandboxRefuse(I, "IO::Handle." + m, m == "open" ? sandboxOpenCap(args) : r->cap);
}

}  // namespace rakupp
