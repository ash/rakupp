#pragma once
#include "Ast.h"
#include "Token.h"
#include <stdexcept>
#include <vector>

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

private:
    std::vector<Token> toks_;
    size_t pos_ = 0;

    const Token& cur() const { return toks_[pos_]; }
    const Token& peek(int off = 1) const;
    bool isKind(Tok k) const { return cur().kind == k; }
    bool isOp(const std::string& s) const;
    bool isIdent(const std::string& s) const;
    const Token& advance();
    bool matchOp(const std::string& s);
    bool matchKind(Tok k);
    void expectKind(Tok k, const char* what);
    [[noreturn]] void error(const std::string& msg);

    // statements
    StmtPtr parseStatement();
    StmtPtr applyModifiers(StmtPtr s);
    std::unique_ptr<Block> parseBlock();
    StmtPtr parseSub(bool isMulti);
    StmtPtr parseClass(bool isRole, bool isGrammar = false, bool isPackage = false);
    void skipToStatementEnd(); // advance to the next ; or class-body }, balancing ({[ ]})
    StmtPtr parseSubset();
    StmtPtr parseEnum();
    StmtPtr parseIf(bool isUnless);
    StmtPtr parseWhile(bool isUntil);
    StmtPtr parseFor();
    std::vector<Param> parseSignature();      // after '(' ... ')'
    std::vector<Param> parsePointyParams();   // -> $a, \b { ... }  (stops at '{')

    // expressions
    ExprPtr parseExpression();          // full expr incl. commas/and/or
    ExprPtr parseExpr(int minbp);
    ExprPtr parsePrefix();
    ExprPtr parsePostfix(ExprPtr base);
    ExprPtr parsePrimary();
    ExprPtr parseDeclarator(const std::string& scope);
    void skipTraits();
    ExprPtr parseColonPair();                     // :name / :!name / :name(x) / :$var
    std::vector<ExprPtr> parseCallArgs();         // after '('
    ExprPtr parseInterpString(const std::string& raw);
};

} // namespace rakupp
