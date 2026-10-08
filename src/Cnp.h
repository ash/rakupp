#pragma once
// `--cnp` — the copy-and-patch backend. docs/dev/plans/CNP-PLAN.md.
//
// The same tier-up harness `--jit` built (Jit.cpp: the candidate walk, the
// counter, the slot binding, the guards), with a different way of turning a hot
// loop into machine code. `--jit` emits C++ and runs a compiler; this emits a
// small register IR and stitches together machine-code snippets that were
// compiled when rakupp itself was built. So:
//
//   * no C++ compiler has to exist on the machine running the program;
//   * nothing is written to disk, so there is no cache to warm, invalidate or
//     clean up, and the FIRST run of a program is the one that gets faster;
//   * a kernel costs microseconds to produce rather than half a second, which
//     is why the threshold can be two orders of magnitude lower.
//
// What it gives up is generality: `--jit`'s kernel is the `--exe -O` emission
// and can in principle do whatever that backend does, while this one can only
// do what the stencils in src/cnp/stencils.c cover. Where the lowering meets
// something it has no stencil for it refuses the loop, which costs nothing —
// the loop simply stays interpreted.
#include <cstddef>
#include <string>
#include <map>
#include <vector>

namespace rakupp {

struct Stmt;
struct Expr;
struct Value;
struct Callable;
struct Env;
class Interpreter;

namespace cnp {

struct Kernel;

// Is there a stencil table in this build, for this instruction set?
bool available();
// Why not, when there is not. Empty when there is.
const char* unavailableReason();
// What the table was extracted for, for `--cnp=verbose` and `--version`.
const char* arch();

// Does the kernel call out of itself (a routine, an interpolation's .Str)?
// A callee may start a thread (see runIfReady in Jit.cpp).
bool makesCalls(const Kernel* k);
// Lower a loop the eligibility walk has already accepted. Null on refusal,
// with `why` naming what the lowering could not express. `slots` is the
// harness's slot list, in order; registers 0..slots.size()-1 mirror it.
// `inl`: routines the kernel may compile in place instead of calling (see
// inlineBody), by name.
Kernel* compile(Stmt* loop, const std::vector<std::string>& slots, std::string& why,
                const std::map<std::string, const Callable*>* inl = nullptr);
// A sub a kernel can compile in place: untyped `$` parameters with no traits,
// defaults or constraints, and a body that is ONE expression over those
// parameters alone (literals, arithmetic, comparisons, `?? !!`), with no
// operator the program overloads in `env`. Its body expression, or null.
const Expr* inlineBody(const Callable& c, Env* env);

// Enter a kernel: bind the registers from the containers, run the loop to
// completion, write the written ones back. False means the kernel could not be
// entered here and the site should be retired, with `why` saying what stopped
// it; a Raku exception raised inside propagates as it would from interpreted
// code, after the write-back.
bool run(Kernel* k, Interpreter& I, Value** slots, const std::vector<bool>& written,
         std::string& why);

// For `--cnp=stats`.
size_t codeBytes(const Kernel* k);
size_t opCount(const Kernel* k);
size_t regCount(const Kernel* k);

}  // namespace cnp
}  // namespace rakupp
