#pragma once
#include <string>
#include <vector>

namespace rakupp {

struct Program;

// A single lint diagnostic. `rule` is the stable machine-readable rule id
// (e.g. "unused-variable"); `severity` is 'W' (warning) or 'N' (note).
struct LintFinding {
    int line = 0;
    char severity = 'W';
    std::string rule;
    std::string message;
};

// Static analysis over an already-parsed program. Runs a set of deliberately
// conservative rules — a missed warning is acceptable, a false one is not,
// because Raku's dynamism (interpolation, dynamic vars, EVAL, symbolic refs,
// introspection) makes over-eager analysis untrustworthy. Findings come back
// sorted by (line, rule). Does not execute the program.
std::vector<LintFinding> lintProgram(Program& prog);

} // namespace rakupp
