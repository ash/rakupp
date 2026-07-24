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
    std::string exType; // typed compile diagnostic (X::Parameter::Twigil, …); "" = generic
    std::vector<std::pair<std::string, std::string>> exAttrs;
    ParseError(const std::string& msg, int line) : std::runtime_error(msg), line(line) {}
    ParseError(const std::string& msg, int line, std::string type,
               std::vector<std::pair<std::string, std::string>> attrs)
        : std::runtime_error(msg), line(line), exType(std::move(type)), exAttrs(std::move(attrs)) {}
};

class Parser {
public:
    explicit Parser(std::vector<Token> toks);
    Program parseProgram();
    void checkRedeclarations(const std::vector<StmtPtr>& stmts); // same-scope dup subs/types
    ExprPtr parseExpressionPublic() { return parseExpression(); }
    // pre-declare a user-defined operator (so EVAL'd code can parse custom infixes)
    void declareUserOp(const std::string& kind, const std::string& name) {
        if (kind == "infix") userInfix_[name] = 120 /*BP_ADD default*/;
        else if (kind == "prefix") userPrefix_.insert(name);
        else if (kind == "postfix") userPostfix_.insert(name);
    }

private:
    std::vector<Token> toks_;
public:
    std::map<int, std::string> declPod_; // `#= text` by line (from the Lexer)
    std::map<int, std::string> leadPod_; // `#| text` by line (from the Lexer)
    // join the run of #= lines starting AT or just below `line`, "" if none
    std::string trailingPodFor(int line) const {
        std::string out;
        for (int l : {line, line + 1}) {
            if (!out.empty()) break;
            for (int k = l; ; k++) {
                auto it = declPod_.find(k);
                if (it == declPod_.end()) break;
                out = out.empty() ? it->second : out + " " + it->second;
            }
        }
        return out;
    }
    // join the run of #| lines ENDING just above `line` (blank-free), "" if none
    std::string leadingPodFor(int line) const {
        std::string out;
        int l = line - 1;
        while (true) {
            auto it = leadPod_.find(l);
            if (it == leadPod_.end()) break;
            out = out.empty() ? it->second : it->second + " " + out;
            l--;
        }
        return out;
    }
private:
    size_t pos_ = 0;
    std::map<std::string, int> userInfix_;   // user infix name → left binding power (from is tighter/looser/equiv)
    std::set<std::string> userInfixRight_;   // user infixes declared `is assoc<right>`
    std::set<std::string> userPrefix_, userPostfix_; // user-declared operators (sub prefix:<…> / postfix:<…>)
    std::set<std::string> sigilless_; // names declared sigilless (my \x, \a params, -> \d) — parse as terms, not listops
    bool stmtCond_ = false; // parsing a block-statement condition: `{` is the control block, not a listop arg
    std::string lastContainerIs_; // `is Set`-style container trait captured by skipTraits
    std::string lastContainerOf_; // its key-type parameter: `is Bag[Int]`
    int anonStateN_ = 0;          // unique ids for bare-`$` anonymous state vars
    bool useNqp_ = false;         // saw `use nqp` — enables the nqp:: op subset
    static bool nqpConstValue(const std::string& name, long long& out);
    ExprPtr makeNqpOp(const std::string& op, std::vector<ExprPtr>& args);
    std::map<std::string, std::string> userCircumfix_, userPostcircumfix_; // open-bracket -> close-bracket
    // Lexical scoping for user-declared operators: every registration is logged
    // and parseBlock rolls back to its entry mark, so `sub postfix:<!!>` inside
    // a block doesn't leak out (and eat every later ternary's `!!`).
    struct OpUndo { char table; std::string name; bool existed; int oldBp; std::string oldClose; };
    std::vector<OpUndo> opUndo_;
    void regInfix(const std::string& n, int bp) {
        auto it = userInfix_.find(n);
        opUndo_.push_back({'i', n, it != userInfix_.end(), it != userInfix_.end() ? it->second : 0, ""});
        userInfix_[n] = bp;
    }
    void regSet(char t, std::set<std::string>& s, const std::string& n) {
        opUndo_.push_back({t, n, s.count(n) > 0, 0, ""});
        s.insert(n);
    }
    void regMap(char t, std::map<std::string, std::string>& m, const std::string& n, const std::string& close) {
        auto it = m.find(n);
        opUndo_.push_back({t, n, it != m.end(), 0, it != m.end() ? it->second : ""});
        m[n] = close;
    }
    void opRollback(size_t mark) {
        while (opUndo_.size() > mark) {
            OpUndo& u = opUndo_.back();
            switch (u.table) {
                case 'i': if (u.existed) userInfix_[u.name] = u.oldBp; else userInfix_.erase(u.name); break;
                case 'p': if (!u.existed) userPrefix_.erase(u.name); break;
                case 'P': if (!u.existed) userPostfix_.erase(u.name); break;
                case 'r': if (!u.existed) userInfixRight_.erase(u.name); break;
                case 'c': if (u.existed) userCircumfix_[u.name] = u.oldClose; else userCircumfix_.erase(u.name); break;
                case 'C': if (u.existed) userPostcircumfix_[u.name] = u.oldClose; else userPostcircumfix_.erase(u.name); break;
            }
            opUndo_.pop_back();
        }
    }
    std::string pcfxClose_; // active postcircumfix close bracket (don't re-open it inside its own content)
    std::string sigRetType_; // return type from an in-signature `--> T` (read by parseSub)
    ExprPtr sigRetLiteral_;  // literal from an in-signature `--> 1` (read by parseSub)
    bool inReactBlock_ = false; // true while parsing a react/supply block (whenever must be inside one)
    bool unitDecl_ = false;     // true while dispatching a `unit …` declaration (allows a bodyless `unit sub foo;`)
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
    void enforceStmtSep(); // same-line statement juxtaposition is "two terms in a row"
public:
    bool strictSep_ = false; // set by EVAL: strict statement separation in snippets
    int routineDepth_ = 0;   // nesting of sub/method bodies (&?ROUTINE legality)
private:
    StmtPtr parseStatementImpl();
    StmtPtr applyModifiers(StmtPtr s);
    ExprPtr applyExprModifiers(ExprPtr e); // trailing stmt modifiers inside (…)/@(…)/…
    std::unique_ptr<Block> parseBlock();
    void checkVirtualCallInDefault(size_t defStart); // `has $.x = $.y` is illegal
    static void checkNullRegex(const std::string& pat, int line,
                               bool branches = true); // `/ /`; branches: `/a|/` too
    StmtPtr parseSub(bool isMulti, bool isProto = false);
    StmtPtr parseClass(bool isRole, bool isGrammar = false, bool isPackage = false, bool isUnit = false,
                       const std::string& kindKw = "");
    int classDepth_ = 0; // >0 while parsing inside a class/role/grammar body
    std::set<std::string> ourProtos_; // names with an our-scoped proto (our multi is then legal)
    // `use MONKEY-TYPING` is lexically scoped: one frame per block, program frame at [0]
    std::vector<char> monkeyScopes_ = {0};
    bool monkeyActive() const {
        for (char f : monkeyScopes_) if (f) return true;
        return false;
    }
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
