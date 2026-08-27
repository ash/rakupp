#pragma once
#include <set>
#include <string>
#include <vector>

namespace rakupp {

struct Program;

// One variable reference with no declaration anywhere in its lexical chain.
struct UndeclaredVar {
    std::string name;   // spelled with its sigil, as written
    int line = 0;
};

// Whole-unit "is every variable declared?" check, run before the program does.
//
// The interpreter already throws X::Undeclared for an undeclared variable, but
// only when execution REACHES it — so `say $x; say $typo;` prints a line and
// then dies, where Rakudo refuses to compile the file at all. This is the same
// question asked of the whole AST up front (issue #32).
//
// The rule that governs every decision here: a finding must be a certainty.
// Over-reporting would refuse to run a working program, which is far worse than
// the status quo, so the pass over-approximates what is DECLARED at every turn
// and stands down entirely — returning nothing — as soon as the unit does
// anything that can conjure names it cannot see (EVAL, a symbolic reference,
// `no strict`, `require`, an import it cannot resolve). Scoping is lexical but
// position-INSENSITIVE: a declaration anywhere in the enclosing block counts,
// even below the use, because the interpreter is that lenient in places and a
// static check must never be stricter than the engine it guards.
//
// `searchPath` is the module search path; it is consulted only when the pass
// already has a candidate AND the unit imports something, to check the name is
// not one an imported module exports. That lookup reads files, so the common
// path — no candidates — never touches the disk.
// `src` is the unit's source text, the last word on whether a name is declared:
// a candidate the AST cannot resolve is dropped anyway if the text spells it as
// a declaration somewhere. That is what makes the pass safe against the binders
// rakupp's parser does not yet keep (see textDeclares in DeclCheck.cpp), at the
// cost of not reporting a name that IS declared, but in another scope.
std::vector<UndeclaredVar> findUndeclaredVars(const Program& prog, const std::string& src,
                                              const std::vector<std::string>& searchPath);

// The names a lexically lax region auto-vivifies — what `no strict` MAKES legal
// rather than what it hides. Same walk as findUndeclaredVars, opposite face of
// it: a name lands here when it is used where the pragma is in force and has no
// declaration the pass can see, and it survives the same source-text backstop,
// so a name in `names` is one the unit declares NOWHERE. That is what the
// native backend needs — it emits a C++ local for every variable, and a name
// with no declaration has no local to emit — and the backstop is what makes the
// answer safe to act on: a name it never declares cannot collide with one it
// does.
//
// `complete` is false when the pass stood down (EVAL, `require`, a symbolic
// reference) before finishing: `names` may then be short of what the unit
// actually auto-vivifies, so a caller that must be exhaustive has to refuse the
// unit rather than trust it.
struct LaxVars {
    std::set<std::string> names;
    bool complete = true;
};
LaxVars findLaxVars(const Program& prog, const std::string& src);

// Print `findings` as a compile-time report on stderr and answer the exit code
// to leave with (1). `src` is the unit's source, used only to quote the
// offending line; pass "" to leave the quote out.
//
//   ===SORRY!=== Error while compiling t.raku
//   Variable '$y' is not declared
//   at t.raku:3
//   ------> say ⏏$y;
int reportUndeclaredVars(const std::vector<UndeclaredVar>& findings,
                         const std::string& fileName, const std::string& src);

// Whether the check is switched on at all. RAKUPP_NO_DECLCHECK=1 turns it off,
// for the day it is wrong about a program that runs perfectly well.
bool declCheckEnabled();

} // namespace rakupp
