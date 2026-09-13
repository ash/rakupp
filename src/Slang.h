#pragma once
// A slang, activated — what `use Slang::X` registered, in the shape the Lexer
// and the Parser consume. The slang module itself runs verbatim in a scratch
// Interpreter with a compile-time `$*LANG`; what comes back is the set of
// grammar productions its roles override, sorted into SEAMS (a production the
// lexer runs the slang's own token for) and MODES (a production whose body
// references Rakudo's grammar and is re-stated as a parser switch). The whole
// story, tier by tier: docs/dev/plans/SLANG-PLAN.md.
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rakupp {

struct SlangSeams {
    std::string module;             // the slang module, for diagnostics
    std::shared_ptr<void> host;     // the scratch Interpreter the tokens live in (kept alive with the seams)
    // Seams — the slang declared a candidate for this production, and its
    // token is RUN at the position the built-in lexer would start one.
    bool number = false;            // number:sym<X>          (Slang::Roman, ::NumberBase, ::Kazu)
    bool value = false;             // value:sym<X>           (Slang::Date)
    bool identifier = false;        // identifier / name      (Slang::Piersing, ::Subscripts)
    bool sigilless = false;         // sigilless-variable     (Slang::Emoji, ::Nogil)
    bool pointy = false;            // pointy-block-starter   (Slang::Lambda)
    bool declarator = false;        // routine-declarator:sym<sub> / <method> (Slang::Mosdef, ::Tuxic)
    // Modes — tier 3: keyed on the rule NAME a real registration produced.
    bool spacedCall = false;        // term:sym<identifier>   (Slang::Tuxic: `foo (1, 2)` is a two-argument call)
    bool spacedMethodop = false;    // methodop               (Slang::Tuxic: `$x.foo (1, 2)`, `self!foo (1)`)
    // Run the candidates of production `which` at byte offset `pos` of `src`,
    // longest match wins. On a match `endOut` is the byte offset just past it
    // and `replOut` the source text to lex in its place: the action's RakuAST
    // node deparsed, the matched text when there is no action, the declarator
    // keyword for `routine-declarator`. Throws ParseError (line 0) when the
    // slang's own code dies mid-match.
    std::function<bool(const std::string& which, const std::string& src, size_t pos,
                       size_t& endOut, std::string& replOut)> tryMatch;
};

// Is this module source a slang? The registration line is what makes one:
// `use Slangify …` (thirteen of the sixteen published) or a direct
// `define_slang`. Slangify itself matches the second and is excluded by NAME
// where this is consulted — it is the interface, not a slang; otherwise a
// module's name says nothing (Qwiratry's slang is `Qwiratry::Mold::Slang`,
// Dawa's is `Dawa`).
inline bool rakuppIsSlangSource(const std::string& src) {
    return src.find("use Slangify") != std::string::npos ||
           src.find("define_slang") != std::string::npos;
}

} // namespace rakupp
