#pragma once
#include "AsciiCtype.h"
#include "Token.h"
#include "Slang.h"
#include <memory>
#include <cctype>
#include <cstdint>
#include <map>
#include <functional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace rakupp {

struct ParseError; // Parser.h; only the definition and the callers need it whole

// What may appear in an identifier, and the ONE rule about `-`/`'` inside a
// name: they continue the name only when a LETTER or `_` follows — never a
// digit. So `elems-1` is `elems - 1` and `$x-1` is `$x - 1`.
//
// This lives in the header because the LEXER and the string-interpolation
// scanner in the PARSER both have to answer it and must agree. They did not:
// the interpolation scanners tested isalnum, glued the digit into the name, and
// handed `"$x-1"` to the expression parser — which re-split it correctly and
// interpolated the ARITHMETIC RESULT, so `my $x = 5; say "$x-1"` printed 4
// where Rakudo prints 5-1. Six sites implemented this rule three different ways.
inline bool rakuIdentStart(char c) { return ascii::isalpha((unsigned char)c) || c == '_'; }
inline bool rakuIdentCont(char c)  { return ascii::isalnum((unsigned char)c) || c == '_'; }
inline bool rakuIdentJoins(char sep, char next) {
    return (sep == '-' || sep == '\'') && rakuIdentStart(next);
}

class Lexer {
public:
    explicit Lexer(std::string src);
    std::vector<Token> tokenize();
    const std::string& finishData() const { return finishData_; } // text after =finish ($=finish)
    const std::string& podData() const { return podData_; } // rendered =pod content (for --doc)
    // SLANG-PLAN §B: the seams a `use Slang::X` armed, in force from byte offset
    // slangFrom_ on. The Parser re-lexes the unit with these set; an ordinary
    // lex has slang_ null and pays one test per token start.
    std::shared_ptr<SlangSeams> slang_;
    size_t slangFrom_ = 0;
    // A parse site sets this: a lex that dies mid-file (a construct only a slang
    // below a `use` can read — `₃₆123`, 十二) then ends the token stream with an
    // End token carrying the error (flag set, ival = its index) instead of
    // throwing, so the parser can reach the `use` first. Parser::error rethrows.
    bool tolerant_ = false;
    // Quote keywords this unit declares as SUBS — `sub tr`, `sub q`, `sub s`.
    // A declared routine beats the quote construct, as it does on Rakudo, so
    // `tr { td 'a' }` is a call and not a transliteration. Filled by a pre-scan
    // of the source for the unit's own declarations; the Parser adds the ones an
    // imported module declares and re-lexes what is left of the unit.
    std::set<std::string> notQuoteWords_;
    static ParseError storedLexError(size_t idx);
    // Is `w` a quote-form keyword a sub may also be named? (q, qq, Q, m, s, tr, …)
    static bool isQuoteKeyword(const std::string& w);
    // Add every `sub <quote-keyword>` declared in `src` to `into`.
    static void scanQuoteWordSubs(const std::string& src, std::set<std::string>& into);
    // Report every `sub NAME` declaration in `src`. Used before a lex, so it is
    // textual; comments are skipped and the name must be followed by a signature,
    // a body or a trait.
    static void scanDeclaredSubNames(const std::string& src,
                                     const std::function<void(const std::string&)>& cb);

private:
    std::string src_;
    std::string finishData_; // captured =finish data block
    std::string podData_;    // rendered content of =begin pod blocks
    size_t pos_ = 0;
    std::vector<std::string> userOps_; // `sub infix:<…>` spellings declared in THIS file, longest first
    size_t unspaceEnd_ = (size_t)-1;  // pos right after an unspace (`\` + whitespace/comment): not whitespace
    std::set<std::string> termNames_; // names this file declares as TERMS (`constant X`, `my \x`): a `/` after one divides
    size_t termScan_ = 0;             // how far `out` has been scanned for those declarations
    int angleWords_ = 0; // depth inside a bare `< … >` word list: quote/regex lexing is off (content is words)
    int angleLine_ = 0;  // line the OUTERMOST `<` of that word list opened on
    int line_ = 1;
public:
    std::map<int, std::string> declPod_; // `#= text` trailing declarator pod, by line
    std::map<int, std::string> leadPod_; // `#| text` leading declarator pod, by line
private:
    bool slangArmed() const { return slang_ && pos_ >= slangFrom_ && angleWords_ == 0; }
    void tokenizeImpl(std::vector<Token>& out);
    bool probing_ = false;   // inside slangNumberExtent: lexing to MEASURE, not to report
    bool trySlangLiteral(std::vector<Token>& out, bool spaced); // number/value/sigilless/pointy where no bareword starts
    bool trySlangWord(std::vector<Token>& out, bool spaced);    // number/value/pointy/declarator/identifier where a bareword starts
    bool slangTry(std::vector<Token>& out, bool spaced, const char* which, size_t builtinEnd);
    void slangExtendVarName(std::string& name, size_t nameStart); // `$x₁`: the identifier seam inside a variable name
    size_t slangIdentExtent();  // where the built-in bareword would end (no side effects; 0 = none)
    size_t slangNumberExtent(); // where lexNumber would end (0 = it would not lex one)
    void slangEmitSource(std::vector<Token>& out, bool spaced, size_t end, const std::string& repl);
    void slangEmitToken(std::vector<Token>& out, bool spaced, size_t end, Tok kind, const std::string& text, bool flag);
    int col_ = 1;

    char peek(size_t off = 0) const;
    char advance();
    bool eof() const { return pos_ >= src_.size(); }
    bool match(char c);
    void skipRegexComment(std::string& out); // `#` in a regex: to end of line, or an embedded #`(…) to its closer
    uint32_t codepointHere() const;     // decode UTF-8 codepoint at pos_ (0 at eof)
    bool unicodeLetterAt(size_t off) const; // is the codepoint `off` bytes ahead a letter?
    bool unicodeLetterHere() const;     // is the codepoint at pos_ an identifier letter
    void consumeIdentChars(std::string& name); // append ASCII-cont + Unicode-letter chars
    bool tryReadSuperscript(std::string& digits); // ⁰¹²³… run -> ASCII digits (for ** N)

    // A delimited construct ran off the end of the file. Rakudo words this two
    // ways — the bare quote forms name the CONSTRUCT, the bracketed q/rx/comment
    // forms name the TERMINATOR — and both quote the line the construct STARTED
    // on, the only coordinate still worth reporting once the scan has eaten the
    // rest of the file. `atEof` is set so the REPL asks for another line instead
    // of erroring on a half-typed literal (as the runaway heredoc already does).
    [[noreturn]] void runawayQuote(const char* construct, const char* finalDelim,
                                   int startLine) const;
    [[noreturn]] void runawayTerm(const std::string& close, const std::string& open,
                                  int startLine) const;

    void skipWhitespaceAndComments();
    Token lexNumber();
    Token lexQuoted(char quote);
    bool tryQuoteForm(Token& out); // q// qq// Q// with bracketing/char delimiters
    bool trySetOp(Token& out);     // (|) (&) (elem) ... ASCII set operators
    bool regexContext(const std::vector<Token>& out); // is a bare / a regex here?
    bool tryRuleDecl(std::vector<Token>& out, bool spaced); // token/rule/regex NAME { ... }
    void processHeredocs(std::vector<Token>& out);          // fill q:to/MARKER/ bodies at line end
    // pending heredocs: (marker, token index in out, interpolating?)
    std::vector<std::tuple<std::string, size_t, bool>> pendingHeredocs_;
    std::string heredocFeats_; // interpolation features of a `qq:!c:to/…/` heredoc ("" = all)
    std::vector<std::string> pendingHeredocFeats_; // one per pendingHeredocs_ entry
    std::string heredocMarker_;  // set by tryQuoteForm when a :to form is seen
    // …and the marker is not the flag: `q :to '' ` names the EMPTY terminator (a
    // blank line ends that body), so an empty heredocMarker_ is a real heredoc.
    bool heredocPending_ = false;
    bool heredocInterp_ = false;
    // `q:to/…/` processes the same backslash escapes `q[…]` does (`\\` → `\`,
    // `\'` → `'`); `Q:to/…/` processes none, `qq:to/…/` processes all. Only the
    // middle case needed a flag — the body used to come through untouched, so
    // DBDish::Pg's test SQL kept a doubled backslash and Postgres stored one too
    // many.
    bool heredocEscapes_ = false;
    std::vector<bool> pendingHeredocEsc_;   // per pending heredoc: does q:to unescape it?
    bool warnedLeadingZero_ = false; // emit the leading-0-isn't-octal warning once
    Token lexIdentOrVar();
    Token lexOperator(bool termBefore = false);
    bool p5AssignAhead(size_t off) const; // ws* then a plain `=` (not == => =~)

    Token make(Tok k, const std::string& t);
};

} // namespace rakupp
