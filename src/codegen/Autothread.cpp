// Which positional parameters of a compiled routine take a Junction argument
// apart (Codegen: the rtAutothread prologue of a sub, a lexical sub, a block).
//
// The interpreter autothreads a call in callCallable: a Junction handed to a
// parameter that does not take it whole runs the routine once per eigenstate
// and answers a junction of the results. A natively compiled routine is a C++
// function the interpreter's call path never sees, so `sub f($v) { … }` got
// the junction itself — Cro's `sub extract($v --> Nil) { @c.push($v) }`,
// called with a parameter's `all(…)` constraints, pushed the junction, and no
// route with a literal segment ever matched. The rule is callCallable's: a
// parameter typed Mu or Junction, a native, a slurpy and (in a block, where an
// untyped parameter is Mu) an untyped one take the junction whole.
#include "../Ast.h"
#include <cstdint>
#include <string>
#include <vector>

namespace rakupp {

bool isNativeTypeName(const std::string& n);

// "" when no parameter autothreads, else the bit mask of the positional
// parameters that do, as a C++ literal
std::string autothreadMask(const std::vector<Param>& ps, bool block) {
    uint64_t mask = 0;
    unsigned pos = 0;
    for (const Param& p : ps) {
        if (p.named || p.invocant) continue;
        if (p.slurpy || pos >= 64) break;
        const bool whole = p.type == "Mu" || p.type == "Junction" || isNativeTypeName(p.type) ||
                           (p.type.empty() && block);
        if (!whole) mask |= uint64_t(1) << pos;
        pos++;
    }
    return mask ? std::to_string(mask) + "ULL" : std::string();
}

} // namespace rakupp
