#pragma once
#include <string>
#include <vector>

namespace rakupp {

struct Program;

// A single lint diagnostic. `rule` is the stable machine-readable rule id
// (e.g. "unused-variable"); `severity` is 'E' (error — the program will not
// compile), 'W' (warning) or 'N' (note). Nothing in lintProgram raises an 'E':
// the linter only ever advises. The severity exists because `--lint` also
// reports the undeclared-variable check (DeclCheck.h), and a report that a file
// cannot compile must not be printed as though it were an ignorable warning.
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
