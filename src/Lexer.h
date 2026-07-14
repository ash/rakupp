#pragma once
#include "Token.h"
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace rakupp {

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
    int line_ = 1;
public:
    std::map<int, std::string> declPod_; // `#= text` trailing declarator pod, by line
private:
    int col_ = 1;

    char peek(size_t off = 0) const;
    char advance();
    bool eof() const { return pos_ >= src_.size(); }
    bool match(char c);
    uint32_t codepointHere() const;     // decode UTF-8 codepoint at pos_ (0 at eof)
    bool unicodeLetterHere() const;     // is the codepoint at pos_ an identifier letter
    void consumeIdentChars(std::string& name); // append ASCII-cont + Unicode-letter chars
    bool tryReadSuperscript(std::string& digits); // ⁰¹²³… run -> ASCII digits (for ** N)

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
    std::string heredocMarker_;  // set by tryQuoteForm when a :to form is seen
    bool heredocInterp_ = false;
    bool warnedLeadingZero_ = false; // emit the leading-0-isn't-octal warning once
    Token lexIdentOrVar();
    Token lexOperator();

    Token make(Tok k, const std::string& t);
};

} // namespace rakupp
