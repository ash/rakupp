#pragma once
// The Arduino backend (`rakupp --target=arduino`, ARDUINO-PLAN.md): a Raku
// program becomes one Arduino sketch — C++ that names no runtime, built by
// the Arduino core's own compiler into a firmware image the board runs alone.
// The mainline before the top-level `loop { }` is setup(), one iteration of
// that loop is one call of loop(), and `use Arduino` names the core's API.
#include "../Ast.h"
#include "../Codegen.h"   // CodegenError: the refusal type main.cpp already catches
#include <string>

namespace rakupp {

struct ArduinoOptions {
    std::string srcName;   // for the header comment
    std::string version;   // RAKUPP_VERSION
};

// Transpile a whole program to a sketch. Throws CodegenError on anything
// outside the board subset — there is no interpreter on a board to hand it
// to — with the line and the construct in the message.
std::string transpileToArduino(Program& prog, const ArduinoOptions& opt);

}
