#pragma once
#include <functional>
#include <string>
#include <vector>

namespace rakupp {

struct Program;

// One call the compiler can prove will never bind: Rakudo's compile-time
// X::TypeCheck::Argument, "Calling f(Int) will never work with declared
// signature (Str $x)".
struct DoomedCall {
    int line = 0;
    std::string name;                     // the routine, as the call names it
    std::vector<std::string> arguments;   // the argument types, as the message lists them
    // One gist for a sub, or for a proto that refused the call itself; one per
    // candidate, in declaration order, when the multi candidates refused it.
    std::vector<std::string> signatures;
    bool multi = false;
    bool protoguilt = false;
    std::string message() const;
};

// Whole-unit "can this call ever bind?" check, run before the program does.
//
// A call to a routine whose signature cannot accept the arguments is refused by
// Rakudo's optimizer when everything it needs is known at compile time: the
// routine is a sub this unit declares (lexically visible from the call, wherever
// below it is declared), and every argument is positional, unflattened and of a
// type the compiler knows — a literal, a type object, or a variable declared
// with a nominal type. The rakupp binder reports the same calls, but only when
// one RUNS: `try f(42)` succeeds quietly there, and a file Rakudo will not
// compile prints half its output first.
//
// The verdicts are Rakudo's own rules, read off its trial binder and its
// compile-time multi analysis and probed shape by shape against Rakudo 2026.09
// (t/regression/will-never-work-compile-time.raku keeps the cases): a parameter
// it does not analyse — a literal default, `is rw`, a `:D`/`:U` smiley, a
// coercion, a slurpy, a `&` sigil — makes it unsure, and unsure is never a
// finding. Over that sits one rule of rakupp's: a call is reported only
// if it truly can never bind. Rakudo types `my @a` as Positional and so refuses
// `f(@a)` against an `Iterable $x` that an Array satisfies at run time; this
// pass judges `my @a` as the Array it is, and leaves such a call to run.
//
// Like findUndeclaredVars it must never refuse a working program, so it stands
// down — reports nothing — wherever it cannot see enough: a name an imported
// module might supply, a type it cannot place, a candidate set it cannot sort.
//
// `searchPath` is where an imported module's source is looked for (only once a
// finding exists). `isCoreRoutine` answers whether the setting has a routine of
// that name: a `multi` the unit declares without a proto of its own joins the
// setting's candidates, which this pass cannot see, so such a name is left be.
std::vector<DoomedCall> findDoomedCalls(const Program& prog, const std::vector<std::string>& searchPath,
                                        const std::function<bool(const std::string&)>& isCoreRoutine);

// Print `findings` as Rakudo's compile-time report on stderr and answer the exit
// code to leave with (1).
//
//   ===SORRY!=== Error while compiling t.raku
//   Calling f(Int) will never work with declared signature (Str $x)
//   at t.raku:3
//   ------> try ⏏f(42);
int reportDoomedCalls(const std::vector<DoomedCall>& findings, const std::string& fileName,
                      const std::string& src);

// RAKUPP_NO_CALLCHECK=1 turns the check off, for the day it is wrong about a
// program that runs perfectly well.
bool callCheckEnabled();

} // namespace rakupp
