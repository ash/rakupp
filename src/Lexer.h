#pragma once
#include "AsciiCtype.h"
#include "Token.h"
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace rakupp {

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

private:
    std::string src_;
    std::string finishData_; // captured =finish data block
    std::string podData_;    // rendered content of =begin pod blocks
    size_t pos_ = 0;
    size_t atomDropEnd_ = (size_t)-1; // pos right after a dropped ⚛ marker (not whitespace)
    int angleWords_ = 0; // depth inside a bare `< … >` word list: quote/regex lexing is off (content is words)
    int angleLine_ = 0;  // line the OUTERMOST `<` of that word list opened on
    int line_ = 1;
public:
    std::map<int, std::string> declPod_; // `#= text` trailing declarator pod, by line
    std::map<int, std::string> leadPod_; // `#| text` leading declarator pod, by line
private:
    int col_ = 1;

    char peek(size_t off = 0) const;
    char advance();
    bool eof() const { return pos_ >= src_.size(); }
    bool match(char c);
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
    static bool regexContext(const std::vector<Token>& out); // is a bare / a regex here?
    bool tryRuleDecl(std::vector<Token>& out, bool spaced); // token/rule/regex NAME { ... }
    void processHeredocs(std::vector<Token>& out);          // fill q:to/MARKER/ bodies at line end
    // pending heredocs: (marker, token index in out, interpolating?)
    std::vector<std::tuple<std::string, size_t, bool>> pendingHeredocs_;
    std::string heredocFeats_; // interpolation features of a `qq:!c:to/…/` heredoc ("" = all)
    std::vector<std::string> pendingHeredocFeats_; // one per pendingHeredocs_ entry
    std::string heredocMarker_;  // set by tryQuoteForm when a :to form is seen
    bool heredocInterp_ = false;
    bool warnedLeadingZero_ = false; // emit the leading-0-isn't-octal warning once
    Token lexIdentOrVar();
    Token lexOperator(bool termBefore = false);
    bool p5AssignAhead(size_t off) const; // ws* then a plain `=` (not == => =~)

    Token make(Tok k, const std::string& t);
};

} // namespace rakupp
