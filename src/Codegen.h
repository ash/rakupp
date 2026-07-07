#pragma once
#include "Ast.h"
#include <string>

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
std::string transpileToCpp(Program& prog, bool optimize = false);

}
