#pragma once
#include "Ast.h"
#include "Token.h"
#include <stdexcept>
#include <vector>
#include <set>

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
        if (kind == "infix") userInfix_.insert(name);
        else if (kind == "prefix") userPrefix_.insert(name);
        else if (kind == "postfix") userPostfix_.insert(name);
    }

private:
    std::vector<Token> toks_;
    size_t pos_ = 0;
    std::set<std::string> userInfix_, userPrefix_, userPostfix_; // user-declared operators (sub infix:<…>)
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
    void skipTraits();
    ExprPtr parseColonPair();                     // :name / :!name / :name(x) / :$var
    std::vector<ExprPtr> parseCallArgs();         // after '('
    ExprPtr parseInterpString(const std::string& raw);
    std::vector<std::string> readAngleWords(const std::string& close); // <...>/«...» word list (opening delim already consumed)
};

} // namespace rakupp
