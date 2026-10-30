// A binary's compile-time values (#190).
//
// Rakudo runs a unit's constant initializers and BEGIN blocks while it compiles
// the unit, and a precompiled module keeps what they came to: loading it runs
// none of that code again. A binary is the program precompiled, so `--exe`,
// `--aot` and `--bundle` do the same. They run the program's compile-time part
// in an interpreter while BUILDING — BEGIN, CHECK, `use`, the declarations, the
// constants, and no mainline — and substitute each value for the code that
// computed it: a constant's initializer becomes its value written as Raku
// (`.raku`), and so does a `BEGIN` used for its value. The top-level BEGIN and
// CHECK blocks are dropped, and what they left in the unit's variables is
// restored by one generated BEGIN of plain assignments. So
// `constant DATA = slurp 'data.txt'` reads the file where the binary is built,
// and the binary carries the text.
//
// A value is embedded only when it is plain data whose `.raku` evaluates back
// to a value `eqv` to it, of the same type: strings, numbers, Bool, enums, type
// objects, and lists, arrays, hashes, maps, pairs, ranges, sets, bags, mixes
// and blobs of them. Anything else (an object, a closure, a handle, a lazy
// list) is left to run when the binary starts, as before, and the build says
// which in a note. So is a constant inside a routine or block, which the pass
// does not compute. A pass that fails — a BEGIN that dies at build time —
// changes nothing.
//
// A program carries its values as a table: a bundled binary applies it to the
// tree it parses at run time, the other two to the tree they compile. Sites are
// numbered in the order one walk (CtWalk) meets them, and each entry carries
// its site's kind and line, so a tree that parses otherwise is left alone
// rather than patched in the wrong place.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace rakupp {

struct Program;
struct Stmt;
class Interpreter;

struct CtEntry {
    int site = 0;      // the walk's numbering
    char kind = 0;     // 'C' a constant's initializer, 'B' a BEGIN or CHECK value
    int line = 0;
    std::string lit;   // the value, as Raku source
};

struct CtTable {
    int nSites = 0, nTopBegins = 0;   // what the tree the values belong to holds
    std::vector<CtEntry> subs;
    // the top-level BEGIN and CHECK blocks are dropped, and these unit
    // variables (name, value as Raku source) restored in their place
    bool dropBegins = false;
    std::vector<std::pair<std::string, std::string>> restore;
    bool empty() const { return subs.empty() && !dropBegins; }
};

// Build time: run SRC's compile-time part and say what it came to. Prints the
// notes for what stays at run time; an empty table changes nothing.
CtTable computeCompileTimeValues(const std::string& src, const std::string& srcName,
                                 const std::vector<std::string>& libPaths);
void applyCompileTimeValues(Program& prog, const CtTable& t);
std::string encodeCtTable(const CtTable& t);

// The pass's side of Interpreter::run (I.ctValues_ set): what the mainline
// runs of statement S, and what BEGIN and CHECK left in the unit's variables.
bool ctMainlineStmt(Interpreter& I, Stmt* s, bool boundEarly);
void ctAfterCheck(Interpreter& I, Program& prog);

// A bundled binary's stub hands its table to rakuppSetCtTable, which sets the
// hook rakuppRunOn applies to the program once parsed; null in every other.
extern void (*g_ctApply)(Program&);
void rakuppSetCtTable(const char* data, std::size_t len);
// …and the pass's interpreter, set up as rakuppRunOn sets one up (Runtime.cpp)
void rakuppPrepareInterp(Interpreter& interp, const std::string& src, const std::string& fileName,
                         const std::vector<std::string>& libPaths, const std::string& finish);

} // namespace rakupp
