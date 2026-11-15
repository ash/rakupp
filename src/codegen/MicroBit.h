#pragma once
// The micro:bit backend (`rakupp --target=microbit`, MICROBIT-PLAN.md): a Raku
// program becomes one C++ file for the BBC micro:bit V2 on CODAL, its own
// runtime, which CODAL's build (microbit-v2-samples) turns into MICROBIT.hex.
// The mainline runs in main() after uBit.init(), the top-level `loop { }` is
// a C loop there, and `use MicroBit` names the board by MicroPython's names.
#include "../Ast.h"
#include "../Codegen.h"   // CodegenError: the refusal type main.cpp already catches
#include <string>

namespace rakupp {

struct MicroBitOptions {
    std::string srcName;   // for the header comment
    std::string version;   // RAKUPP_VERSION
};

// Transpile a whole program to CODAL C++. Throws CodegenError on anything
// outside the board subset, with the line and the construct in the message.
std::string transpileToMicroBit(Program& prog, const MicroBitOptions& opt);

}
