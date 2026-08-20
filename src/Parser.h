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
    // The parse died ON the end-of-input token, i.e. the source ran out rather
    // than said something wrong — `sub f {` with no `}`. Only the REPL reads it,
    // to tell "give me a continuation line" from "this is a syntax error".
    bool atEof = false;
    ParseError(const std::string& msg, int line) : std::runtime_error(msg), line(line) {}
    ParseError(const std::string& msg, int line, bool atEof)
        : std::runtime_error(msg), line(line), atEof(atEof) {}
    ParseError(const std::string& msg, int line, std::string type,
               std::vector<std::pair<std::string, std::string>> attrs)
        : std::runtime_error(msg), line(line), exType(std::move(type)), exAttrs(std::move(attrs)) {}
};

// Module SOURCES compiled into a binary. `--bundle` parses the main program at
// run time, and that parse has to scan each `use`d module for the operators it
// declares — otherwise a program using an imported operator does not parse once
// the module tree is gone. Registered at startup by generated code; read-only
// afterwards. (`--exe`/`--aot` parse at BUILD time and never need this.)
void rakuppRegisterModuleSource(const std::string& name, const char* src, size_t len);
const std::string* rakuppEmbeddedModuleSource(const std::string& name);
// Resolve a module's SOURCE exactly as the loader does — the lib search path
// first, then the installed CompUnit repositories. Defined in Interpreter.cpp;
// the parser needs it to scan a `use`d module for operators and for the
// sigilless constants it exports.
// sixE: from 6.e the `.pm` extension is no longer looked for.
bool rakuppFindModuleSource(const std::string& name,
                            const std::vector<std::string>& searchPath,
                            std::string& pathOut, std::string& srcOut, bool sixE);

class Parser {
public:
    explicit Parser(std::vector<Token> toks);
    Program parseProgram();
    void checkRedeclarations(const std::vector<StmtPtr>& stmts); // same-scope dup subs/types
    ExprPtr parseExpressionPublic() { return parseExpression(); }
    // Module search path, for finding the operators a `use`d module declares —
    // filled by the runtime from -I/RAKULIB before parsing. See scanModuleOps.
    std::vector<std::string> libPaths_{"lib", ".", "rakulib"};
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
    // `#=` lines already claimed by a PARAMETER (a multi-line signature puts
    // param docs on the lines just below the declaration line, which is where
    // the routine's own trailing-doc probe looks — the docs of `sub MAIN(\n
    // Str :$foo, #= …` used to double as the routine's `-- …` usage suffix;
    // issue #17)
    std::set<int> claimedPodLines_;
    // join the run of #= lines starting AT or just below `line`, "" if none;
    // a line a parameter owns ends the run without contributing
    std::string trailingPodFor(int line) const {
        std::string out;
        for (int l : {line, line + 1}) {
            if (!out.empty()) break;
            for (int k = l; ; k++) {
                auto it = declPod_.find(k);
                if (it == declPod_.end() || claimedPodLines_.count(k)) break;
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
    bool lastIsDynamic_ = false;  // `is dynamic` captured by skipTraits, same way
    // `my $x will leave {…}` — the phaser name + block captured by skipTraits;
    // the declarator turns them into a synthetic LEAVE/KEEP/UNDO phaser stmt
    // Type names this unit declares (see Program::declaredTypeNames) —
    // recorded as parseClass/parseSubset/parseEnum see them, handed to the
    // Program at the end of parseProgram.
    std::set<std::string> declTypeNames_;
    bool declTypesOpaque_ = false;
    // (pendingStmts_), flushed after the current statement by the block loops
    std::string lastWillPhaser_;
    ExprPtr lastWillBlock_;
    std::vector<StmtPtr> pendingStmts_;
    bool lastIsExport_ = false;   // `is export` on a variable declaration, same way
    std::string lastContainerOf_; // its key-type parameter: `is Bag[Int]`
    int sigOwnerLine_ = 0;        // decl line of the routine whose signature is being parsed:
                                  // its leading `#|` belongs to the routine, not to a parameter
    int anonStateN_ = 0;          // unique ids for bare-`$` anonymous state vars
    bool useNqp_ = false;         // saw `use nqp` — enables the nqp:: op subset
    // Language revision of the unit being parsed: 0=6.c, 1=6.d (the default),
    // 2=6.e. Set when `use v6.X` is parsed — which the language guarantees is
    // the first statement — so syntax that only exists from 6.e can be gated
    // here rather than parsed unconditionally and sorted out later. The
    // interpreter has its own copy for runtime behaviour; this one exists
    // because by the time that one is set, parsing is long over.
    int langRev_ = 1;
    // `<|w>` (word) and `<|c>` (codepoint) are the only regex boundaries. Any
    // other name is a typo that 6.c/6.d compile to a silent no-op and 6.e
    // refuses; checked on the pattern source because the regex engine is
    // deliberately standalone and knows nothing about revisions.
    void checkRegexBoundaries(const std::string& pattern, int line) const;
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
    std::vector<bool> typeIsRole_;       // parallel: is that enclosing type a ROLE?
                                         // (::?CLASS in a role is GENERIC — resolved
                                         // per-invocant at runtime, not baked here)

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
    // `use Foo` where Foo declares operators: find its source and register them,
    // so `$c ◐ 20` parses in the importing file. Defined in Parser.cpp.
    void scanModuleOps(const std::string& module);
    void scanOpsIn(const std::string& src, const std::string& srcPath); // the scan itself, shared by the disk and embedded paths
    std::set<std::string> scannedMods_;            // modules already scanned for operators
public:
    // Every module SOURCE scanModuleOps read, as (path, content). A cached parse
    // of this file is only valid while those still say what they said: an
    // imported module that gains or loses an operator changes how THIS file
    // parses, without this file changing at all. See Interpreter::loadModule.
    std::vector<std::pair<std::string, std::string>> opScanned_;
private:
    // Token index of the last `}` that closed a BLOCK. A block-closing brace at
    // end of line ENDS the statement (Rakudo's rule), so an infix on the next line
    // starts a new statement instead of continuing the expression:
    //   ($r,$g,$b) = (…).map: { … }      <- statement ends here
    //   %(r => $r, …)                    <- a new statement, not `} % (…)`
    // A subscript's `}` does not count (`%h{'a'}\n + 3` really is a continuation),
    // which is why this records the position rather than testing the token kind.
    size_t lastBlockClose_ = (size_t)-1;
    size_t stmtStart_ = 0; // first token of the statement being parsed (see lastBlockClose_)
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
    StmtPtr parseSub(bool isMulti, bool isProto = false, bool asMethod = false);
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
    // The shared tail of a sigilless capture parameter (`\\p`): optional paren
    // sub-signature, then is/where traits.
    void parseSigillessTail(Param& p);
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
    ExprPtr parseColonPair();
    // adverbs written after a call's `)`: `f($x):12size` passes `size => 12`
    void takeTrailingAdverbs(std::vector<ExprPtr>& args);                     // :name / :!name / :name(x) / :$var
    std::vector<ExprPtr> parseCallArgs(ExprPtr* invocant = nullptr); // after '('; *invocant set for `f($obj: args)`
    ExprPtr parseInterpString(const std::string& raw);
    ExprPtr parseEmbeddedExpr(const std::string& src); // parse a `{…}`/`$()` interpolation, inheriting user operators
    ExprPtr angleColonPair(const std::string& w); // `:name(expr)` word in a «…»/qww list → PairExpr (null if not pair-shaped)
    ExprPtr qqwwWordItem(const std::string& w);   // one «…»/<<…>>/qqww word with qq:ww:v semantics
    std::vector<std::string> readAngleWords(const std::string& close); // <...>/«...» word list (opening delim already consumed)
};

} // namespace rakupp
