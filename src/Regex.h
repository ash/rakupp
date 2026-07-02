#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rakupp {

// Result of a regex match against a subject string (byte offsets).
struct RxMatch {
    bool matched = false;
    long from = 0, to = 0;
    std::vector<std::pair<long, long>> caps;            // positional captures ($0,$1,..); {-1,-1} = unset
    std::map<std::string, std::pair<long, long>> named; // named captures ($<name>) byte ranges
    std::map<std::string, std::pair<long, long>> subs;  // subrule names matched (for $<name> tree access)
};

// Resolver for grammar subrule calls <name>: match rule `name` against `subj`
// anchored at `pos`; on success fill `out` (with out.to = end offset) and return true.
using SubResolver = std::function<bool(const std::string& name, const std::string& subj, long pos, RxMatch& out)>;

// A compiled Raku regex supporting a pragmatic core of the language:
//   literals, '.' , quoted '...', \d \w \s (+negated), \n \t \r escapes,
//   <[...]> / <-[...]> char classes with a..z ranges, named classes
//   (<digit> <alpha> <alnum> <space> <upper> <lower> <xdigit> <ws> <print>),
//   groups [ ] (non-capturing) and ( ) (capturing), alternation | and ||,
//   quantifiers * + ? and ** N / ** N..M (greedy, or frugal with trailing ?),
//   anchors ^ $ (and ^^ $$ treated the same), insignificant whitespace,
//   and the :i (ignorecase) / :s (sigspace) adverbs.
class Regex {
public:
    explicit Regex(const std::string& pattern, const std::string& flags = "");
    bool ok() const { return ok_; }
    int nCaps() const { return ncaps_; }
    // Find the first match whose start is >= startPos (unanchored search).
    bool search(const std::string& subject, long startPos, RxMatch& out) const;
    bool search(const std::string& subject, long startPos, RxMatch& out, const SubResolver& r) const;
    // Match anchored exactly at `pos` (used for grammar subrule calls).
    bool matchAt(const std::string& subject, long pos, RxMatch& out, const SubResolver& r) const;

private:
    enum class K { Lit, Any, Class, Seq, Alt, Rep, Group, AnchorStart, AnchorEnd, Nop, Subrule };
    struct Node {
        K k;
        std::string lit;                 // Lit
        std::vector<std::unique_ptr<Node>> kids;
        // Class
        std::vector<std::pair<unsigned char, unsigned char>> ranges;
        std::string classFlags;          // subset of "dws" (positive), uppercase = negated
        std::string uprop;               // Unicode property for <:Nd>/<:L>/… (Class node, codepoint-aware)
        bool negate = false;
        // Rep
        long min = 0, max = -1;          // max = -1 => unbounded
        bool greedy = true;
        // Group
        int capIndex = -1;               // -1 => non-capturing
        std::string capName;
        // Subrule
        std::string ruleName;
        bool ruleCapture = true;         // <name> captures as $<name>; <.name> does not
    };
    using NodePtr = std::unique_ptr<Node>;

    std::string pat_;
    size_t pos_ = 0;
    int ncaps_ = 0;
    bool ok_ = true;
    bool icase_ = false;
    bool sigspace_ = false;
    NodePtr root_;

    // parser
    NodePtr parseAlt();
    NodePtr parseSeq();
    NodePtr parseQuant();
    NodePtr parseAtom();
    void parseClassBodyMember(Node* node);
    void skipWs();
    char peek(size_t o = 0) const { return pos_ + o < pat_.size() ? pat_[pos_ + o] : '\0'; }
    bool eof() const { return pos_ >= pat_.size(); }

    // matcher
    struct MState {
        const std::string& s;
        std::vector<std::pair<long, long>> caps;
        std::map<std::string, std::pair<long, long>> named;
        std::map<std::string, std::pair<long, long>> subs; // subrule names matched
        const SubResolver* resolver = nullptr;
    };
    bool matchNode(const Node* n, MState& st, long pos, const std::function<bool(long)>& k) const;
    bool classMatch(const Node* n, char c) const;
};

} // namespace rakupp
