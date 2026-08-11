// SLIM-PLAN P3: the stub half of the `eval` seam — linked in place of
// librakupp_parse.a when --slim cuts the feature. The parser's entire
// out-of-line surface is four symbols (measured: linking rt without parse
// leaves exactly these undefined), so a cut binary carries these four throws
// instead of the Lexer and Parser, and EVAL / require / a runtime-compiled
// regex is the moment the program learns the feature is out.
//
// The constructors default-construct the members and then throw — nothing is
// ever used, the object never finishes constructing. parseProgram() has no
// return statement on purpose: featureMissing is [[noreturn]].

#include "../ucd_seam.h"
#include "../Lexer.h"
#include "../Parser.h"

namespace rakupp {

Lexer::Lexer(std::string) {
    featureMissing("eval", "EVAL/require/runtime-compiled regexes (the lexer)");
}
std::vector<Token> Lexer::tokenize() {
    featureMissing("eval", "EVAL/require/runtime-compiled regexes (the lexer)");
}
Parser::Parser(std::vector<Token>) {
    featureMissing("eval", "EVAL/require/runtime-compiled regexes (the parser)");
}
Program Parser::parseProgram() {
    featureMissing("eval", "EVAL/require/runtime-compiled regexes (the parser)");
}

}
