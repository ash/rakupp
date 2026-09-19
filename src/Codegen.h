#pragma once
#include "Ast.h"
#include <set>
#include <string>
#include <vector>

namespace rakupp {

// Thrown when the program uses a construct the native codegen backend does not
// (yet) support. The caller reports it and suggests --compile (AOT bundling),
// which handles the whole language.
struct CodegenError { std::string msg; };

// Transpile a whole program into a self-contained C++ source string that
// implements the program natively (calling the runtime for Value semantics).
// Throws CodegenError on any unsupported construct.
// With optimize=true, fixed-arity positional subs get direct `Value` parameters
// (skipping the per-call ValueList heap allocation) — the `-O` codegen pass.
// `moduleExports` are the `is export` sub names of the modules the program
// `use`s (collectModuleGraph fills them). A call to one of those names is
// resolved through the run-time environment instead of the builtin table, so an
// exported sub shadows a same-named built-in here as it does in the interpreter.
// `srcText` is the unit's source. It is consulted only through DeclCheck's
// findLaxVars, to learn which names a `no strict` region auto-vivifies — those
// have no declaration to emit a C++ local from, so they are compiled as runtime
// slots instead. Passing "" answers "no name is declared anywhere", which is
// safe (a lax unit then falls back to bundling) but coarse.
std::string transpileToCpp(Program& prog, bool optimize = false, const std::string& srcPath = "",
                           const std::set<std::string>& moduleExports = {},
                           const std::string& srcText = "");

// ---- the tier-up JIT's entry point (docs/dev/plans/JIT-PLAN.md) -------------
//
// Emit ONE loop statement as a standalone C++ translation unit holding a single
// `extern "C"` function. This is the same emitter `--exe` uses, with `-O` on,
// pointed at a subtree instead of a whole program: the kernel a running
// interpreter compiles, dlopens and enters at an iteration boundary.
//
// `slots` are the Raku names the loop reads or writes but does not declare —
// bound inside the kernel as `Value&` references into the live frame, in the
// order given. The caller (src/Jit.cpp) computes them with a scope-aware walk
// and has already refused anything outside the JIT whitelist; this function
// still throws CodegenError on whatever the emitter itself cannot handle, which
// is the second of the two independent gates on what may tier up.
std::string emitJitKernel(Stmt* loop, const std::string& fnName,
                          const std::vector<std::string>& slots);

}
