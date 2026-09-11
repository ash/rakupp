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
#include "../RakuAstClasses.h"

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

// The RakuAST:: registry (RAKUAST-PLAN P0) rides the same archive, for the same
// reason: `.AST` IS the parser, so a cut binary that meets a RakuAST name fails
// exactly where it would have failed on an EVAL. SlimScan counts a `RakuAST::`
// name as an eval use, so a program that needs these keeps its parser and never
// links this file.
const std::shared_ptr<ClassInfo>* rakuAstClass(const std::string&) {
    featureMissing("eval", "the RakuAST:: classes");
}
const std::vector<std::string>& rakuAstAncestry(const std::string&) {
    featureMissing("eval", "the RakuAST:: classes");
}
// …but materializing is an OPTIMISATION (the unit said it will need them, so
// build on this thread rather than in a worker). A cut binary simply skips it
// and lets the refusal land on the name itself, where the message belongs.
void rakuAstMaterialize() {}
Value rakuAstNew(Interpreter&, const std::string&, ValueList&) {
    featureMissing("eval", "constructing a RakuAST:: node");
}
std::string rakuAstDeparse(Interpreter&, const Value&) {
    featureMissing("eval", "RakuAST .DEPARSE");
}
Value rakuAstNameFrom(Interpreter&, const ValueList&) {
    featureMissing("eval", "constructing a RakuAST::Name");
}

}
