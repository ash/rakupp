// --sandbox: the policy a program runs under (docs/dev/plans/SANDBOX-PLAN.md).
//
// One switch per process. It is set before the interpreter is built — by the
// CLI's --sandbox, or by an embedding host's RkConfig.sandbox — and never
// relaxed afterwards, so threads read it without a lock: nothing writes it once
// Raku code can run. One interpreter per process (EMBEDDING.md) is what makes a
// process-wide switch the same thing as a per-interpreter one.
//
// The checks sit where a Raku-visible operation is about to reach the OS — a
// file, a process, a socket, native code, the environment — and refuse with
// X::SecurityPolicy::Sandbox before it does. Pure path arithmetic (.basename,
// .parent, .add …) touches nothing and is never checked.
#pragma once
#include <string>
#include "Value.h"

namespace rakupp {

class Interpreter;

// What a refused operation needed. Named in the exception's .capability.
enum class SandboxCap { Read, Write, Run, Net, Ffi };

extern bool g_sandboxed;
inline bool sandboxed() { return g_sandboxed; }

// The interpreter's own checks — every refusal below tests this one, while
// what the sandbox HIDES (%*ENV, the precomp cache, the cwd's module dirs)
// tests g_sandboxed. The two differ only in the OS layer's self-test
// (RAKUPP_SANDBOX_SELFTEST=os-only, SandboxOs.h), which turns the checks off
// so the gate can see what the kernel alone refuses.
extern bool g_sandboxChecks;

// One-way: there is no call that turns it off.
void sandboxEnable();

// The self-test's half: checks off, the sandbox itself still on. main() calls
// it only after the OS layer is in force.
void sandboxChecksOffForSelftest();

const char* sandboxCapName(SandboxCap cap);

// Throws X::SecurityPolicy::Sandbox (operation, capability). The message is
// "<operation> is not allowed in the sandbox: it needs <capability> access".
[[noreturn]] void sandboxRefuse(Interpreter& I, const std::string& operation, SandboxCap cap);

// One builtin sub, by name: replaced with a refusal if it reaches the OS. The
// constructor hands it every entry right after registering them, when the
// sandbox is on — before anything has looked one up, so a call by name,
// `&open` taken as a value and a compiled program's resolved pointer all find
// the refusal. The methods behind the same operations are checked where they
// are implemented.
void sandboxWrapBuiltin(const std::string& name, BuiltinFn& fn);

// An IO::Path, Str or IO::Handle method about to run: refuses the ones that
// would reach the file system. Called only when the sandbox is on.
void sandboxMethodGate(Interpreter& I, const Value& inv, const std::string& m, const ValueList& args);

// What `open`'s adverbs ask for: Write when any of them may create, truncate
// or write, Read otherwise.
SandboxCap sandboxOpenCap(const ValueList& args);

// The same refusal from code with no interpreter at hand (the spawn and socket
// primitives): the exception is the type object alone, without the two
// attributes. Those sites are backstops behind a check that has them.
[[noreturn]] void sandboxRefuseBare(const std::string& operation, SandboxCap cap);

// The check every gated operation makes: free when the sandbox is off.
inline void sandboxCheck(Interpreter& I, const char* operation, SandboxCap cap) {
    if (g_sandboxChecks) sandboxRefuse(I, operation, cap);
}

}  // namespace rakupp
