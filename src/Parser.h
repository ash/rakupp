#pragma once
#include "Ast.h"
#include <memory>
#include "Token.h"
#include <stdexcept>
#include <vector>
#include <set>
#include <map>

namespace rakupp {

struct ParseError : std::runtime_error {
    int line;
    ParseError(const std::string& msg, int line) : std::runtime_error(msg), line(line) {}
};

class Parser {
public:
    explicit Parser(std::vector<Token> toks);
    Program parseProgram();
    ExprPtr parseExpressionPublic() { return parseExpression(); }
    // pre-declare a user-defined operator (so EVAL'd code can parse custom infixes)
    void declareUserOp(const std::string& kind, const std::string& name) {
        if (kind == "infix") userInfix_[name] = 120 /*BP_ADD default*/;
        else if (kind == "prefix") userPrefix_.insert(name);
        else if (kind == "postfix") userPostfix_.insert(name);
    }

private:
    std::vector<Token> toks_;
    size_t pos_ = 0;
    std::map<std::string, int> userInfix_;   // user infix name → left binding power (from is tighter/looser/equiv)
    std::set<std::string> userInfixRight_;   // user infixes declared `is assoc<right>`
    std::set<std::string> userPrefix_, userPostfix_; // user-declared operators (sub prefix:<…> / postfix:<…>)
    std::set<std::string> sigilless_; // names declared sigilless (my \x, \a params, -> \d) — parse as terms, not listops
    bool stmtCond_ = false; // parsing a block-statement condition: `{` is the control block, not a listop arg
    std::map<std::string, std::string> userCircumfix_, userPostcircumfix_; // open-bracket -> close-bracket
    std::string pcfxClose_; // active postcircumfix close bracket (don't re-open it inside its own content)
    std::string sigRetType_; // return type from an in-signature `--> T` (read by parseSub)
    ExprPtr sigRetLiteral_;  // literal from an in-signature `--> 1` (read by parseSub)
    bool inReactBlock_ = false; // true while parsing a react/supply block (whenever must be inside one)
    std::vector<std::string> typeStack_; // enclosing class/role/grammar names (for ::?CLASS)

    const Token& cur() const { return toks_[pos_]; }
    const Token& peek(int off = 1) const;
    bool isKind(Tok k) const { return cur().kind == k; }
    bool isOp(const std::string& s) const;
    bool isIdent(const std::string& s) const;
    const Token& advance();
    // token classifiers (member fns so they can recognise user-declared operators)
    bool startsTermToken(const Token& t) const;
    bool startsListopArg(const Token& t) const;
    int infixBpOf(const std::string& op) const;    // binding power of a named infix (builtin or user)
    bool matchOp(const std::string& s);
    bool matchKind(Tok k);
    void expectKind(Tok k, const char* what);
    [[noreturn]] void error(const std::string& msg);

    // statements
    StmtPtr parseStatement();
    StmtPtr parseStatementImpl();
    StmtPtr applyModifiers(StmtPtr s);
    std::unique_ptr<Block> parseBlock();
    StmtPtr parseSub(bool isMulti);
    StmtPtr parseClass(bool isRole, bool isGrammar = false, bool isPackage = false, bool isUnit = false);
    void skipToStatementEnd(); // advance to the next ; or class-body }, balancing ({[ ]})
    StmtPtr parseSubset();
    StmtPtr parseEnum();
    StmtPtr parseIf(bool isUnless);
    StmtPtr parseWhile(bool isUntil);
    StmtPtr parseFor();
    std::vector<Param> parseSignature(Tok closeTok = Tok::RParen); // after '(' … ')' (or '[' … ']' for a sub-signature)
    std::vector<Param> parsePointyParams();   // -> $a, \b { ... }  (stops at '{')

    // expressions
    ExprPtr parseExpression();          // full expr incl. commas/and/or
    ExprPtr parseExpr(int minbp);
    ExprPtr parsePrefix(bool tight = false);
    ExprPtr parsePostfix(ExprPtr base, bool stopAtSpaceDot = false);
    ExprPtr parsePrimary();
    ExprPtr parseDeclarator(const std::string& scope);
    void skipTraits(bool onVarDecl = false, ExprPtr* defaultOut = nullptr);
    ExprPtr parseColonPair();                     // :name / :!name / :name(x) / :$var
    std::vector<ExprPtr> parseCallArgs(ExprPtr* invocant = nullptr); // after '('; *invocant set for `f($obj: args)`
    ExprPtr parseInterpString(const std::string& raw);
    ExprPtr parseEmbeddedExpr(const std::string& src); // parse a `{…}`/`$()` interpolation, inheriting user operators
    std::vector<std::string> readAngleWords(const std::string& close); // <...>/«...» word list (opening delim already consumed)
};

} // namespace rakupp
