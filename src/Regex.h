#pragma once
#include <functional>
#include <type_traits>
#include <map>
#include <set>
#include <cstdint>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>

namespace rakupp {

// Non-owning callable reference for match continuations: two words, no heap, one
// indirect call. Continuations only live for the duration of the match call chain
// (they are never stored), so borrowing the callable from the caller's stack is safe.
struct FnRef {
    void* ctx;
    bool (*fn)(void*, long);
    template <class F, class = std::enable_if_t<!std::is_same_v<std::decay_t<F>, FnRef>>>
    FnRef(F&& f) : ctx((void*)&f), fn([](void* c, long v) { return (*(std::remove_reference_t<F>*)c)(v); }) {}
    bool operator()(long v) const { return fn(ctx, v); }
};

// Interpreter callbacks that let the grammar matcher evaluate embedded Raku at
// match time — code assertions, `:my`/`{…}` side-effects, runtime `$var` atoms,
// and `** { … }` quantifier bounds — all against the interpreter's live scope.
struct GrammarHooks {
    using NamedMap = std::map<std::string, std::pair<long, long>>; // named-capture byte spans, for $/ / $<x>
    using ParamMap = std::map<std::string, std::string>;           // current rule params, e.g. $indent
    std::function<bool(const std::string&, long, long, const NamedMap&, const ParamMap&)> assertPass; // <?{…}>
    std::function<void(const std::string&, long, long, const NamedMap&, const ParamMap&)> run;        // :my / {…}
    std::function<std::string(const std::string&, const NamedMap&, const ParamMap&)> str;             // $var atom
    std::function<std::pair<long,long>(const std::string&, const NamedMap&, const ParamMap&)> range;  // ** {…}
    // Save/restore interpreter side-effect state (`:my` vars, deferred makes) so an LTM
    // collect pass can measure branch lengths without polluting the commit pass.
    std::function<std::shared_ptr<void>()> saveState;
    std::function<void(std::shared_ptr<void>)> restoreState;
};

// A node of the parse tree recorded by the backtracking GrammarMatcher: which rule
// matched which span, its positional captures ($0..) and named/subrule children.
// A leaf with an empty `name` records a plain named capture ($<x>=[…]), not a rule.
// The child map is FROZEN behind a shared_ptr when the node is recorded, so
// re-recording a memoized sub-match is a refcount bump, not a subtree copy.
struct ParseNode;
using ChildMap = std::map<std::string, std::vector<ParseNode>>;
struct ParseNode {
    std::string name;
    long from = 0, to = 0;
    std::vector<std::pair<long, long>> caps;              // positional captures ($0,$1,…)
    std::map<std::string, std::pair<long, long>> named;   // named-capture spans ($<x>)
    std::shared_ptr<const ChildMap> kids;                 // frozen sub-trees (null = leaf); a vector collates repeated captures
};

// Result of a regex match against a subject string (byte offsets).
struct RxMatch {
    bool matched = false;
    long from = 0, to = 0;
    std::vector<std::pair<long, long>> caps;            // positional captures ($0,$1,..); {-1,-1} = unset
    std::map<int, std::vector<std::pair<long, long>>> capReps; // occurrences per list-valued positional capture
    std::set<int> listCaps;                             // which positional indices are list-valued ($n under */+/**)
    std::map<std::string, std::pair<long, long>> named; // named captures ($<name>) byte ranges
    std::map<std::string, std::pair<long, long>> subs;  // subrule names matched (for $<name> tree access)
    std::map<std::string, std::vector<ParseNode>> children; // per-name occurrence list; repeated captures collate here
};

// Resolver for grammar subrule calls <name>: match rule `name` against `subj`
// anchored at `pos`; on success fill `out` (with out.to = end offset) and return true.
using SubResolver = std::function<bool(const std::string& name, const std::string& subj, long pos, RxMatch& out)>;

// Per-rule-name metadata the GrammarMatcher caches once and subrule call sites
// reuse on every call (via a pointer cached on the compiled AST node), so the
// hot path never re-resolves the name through string-keyed maps.
class Regex;
struct GrammarRuleMeta {
    bool ratchet = false; int id = 0;
    Regex* singleChar = nullptr;   // !=null: body is one char matcher (inlined at call sites)
    const void* rule = nullptr;    // the GrammarMatcher::Rule* this name resolves to (null = unknown)
    Regex* noArg = nullptr;        // pre-compiled body for a parameterless rule
    const std::vector<std::string>* proto = nullptr; // protoregex candidate names (if this name is a proto)
    bool isWs = false;             // built-in <ws>
    std::string builtinClass;      // unknown name: built-in char-class flags ("d","a",…), else empty
};

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
    // Optional interpreter callbacks for standalone (non-grammar) matches — lets a
    // plain `/ … { make 1 } … /` execute its code blocks. Null = lenient no-op.
    const GrammarHooks* runHooks = nullptr;

private:
    enum class K { Lit, Any, Class, Seq, Alt, Rep, Group, AnchorStart, AnchorEnd, WBLeft, WBRight, Nop, Subrule, Look, Code, VarMatch, CapStart };
    struct Node {
        K k;
        std::string lit;                 // Lit
        std::vector<std::unique_ptr<Node>> kids;
        // Class
        std::vector<std::pair<unsigned char, unsigned char>> ranges;
        std::vector<std::pair<uint32_t, uint32_t>> cpRanges; // codepoint ranges (>0xFF chars, named/hex escapes in classes)
        std::string classFlags;          // subset of "dws" (positive), uppercase = negated
        std::string negClassFlags;       // `-rule` difference members: char must NOT match these
        std::string uprop;               // Unicode property for <:Nd>/<:L>/… (Class node, codepoint-aware)
        bool negate = false;
        bool icase = false;              // case-insensitive at THIS node (scoped inline :i)
        bool multiline = false;          // AnchorStart/AnchorEnd: `^^`/`$$` (line) vs `^`/`$` (string)
        mutable uint32_t byteset[8];     // per-byte match result (incl. icase+negate), built on first use
        mutable bool bytesetReady = false;
        // Rep
        long min = 0, max = -1;          // max = -1 => unbounded
        bool greedy = true;
        std::unique_ptr<Node> sep;       // `X+ % Y` / `X+ %% Y` separator (null if none)
        std::string repCode;             // `** { … }` — evaluate this at match time for (min,max)
        // Code / VarMatch: `lit` holds the code / variable expression; `runOnly` = `:my`/`{…}` (execute, always pass)
        bool runOnly = false;
        bool ltmStop = false; // a bare `{…}` code block — ends the LTM declarative prefix (`:my`/assertions do not)
        // Alt
        bool firstMatch = false;         // `||` (sequential first-match) vs `|` (LTM, longest wins)
        // Group
        int capIndex = -1;               // -1 => non-capturing
        std::string capName;
        bool listCap = false;            // capture is under a repetition quantifier (*/+/**) → $n is a list
        // Subrule
        std::string ruleName;
        std::string ruleArgs;            // raw args of a parameterised call <name($x, '')>
        std::string ruleAlias;           // capture key for <alias=rule> (else = ruleName)
        bool ruleCapture = true;         // <name> captures as $<name>; <.name> does not
        mutable const GrammarRuleMeta* metaCache = nullptr; // per-node name resolution (grammar path)
        // Look: zero-width assertion — kids[0] is the inner pattern; `negate` = <!…>,
        // `behind` = lookbehind (<?after…>) vs lookahead (<?before…>/<?…>).
        bool behind = false;
        // lookbehind scan window = the inner pattern's possible match width, computed
        // once — bounds the start-position scan to O(width) instead of O(pos)
        mutable long lookMin = 0, lookMax = -1;
        mutable bool lookWidthReady = false;
    };
    using NodePtr = std::unique_ptr<Node>;

    std::string pat_;
    size_t pos_ = 0;
    int ncaps_ = 0;
    std::set<int> listCaps_;             // positional capture indices under a repetition quantifier
    bool ok_ = true;
    bool icase_ = false;
    bool curIcase_ = false; // parse-time adverb state: :i/:!i scoped to the enclosing group
    bool sigspace_ = false;
    bool ratchet_ = false; // `token`/`rule`: quantifiers are possessive, matches commit (no backtracking)
    int assertDepth_ = 0; // >0 while parsing an assertion inner (so parseSeq stops at `>`)
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

public:
    // matcher state — captures for the rule currently being matched
    struct MState {
        const std::string& s;
        std::vector<std::pair<long, long>> caps;
        std::map<std::string, std::pair<long, long>> named;
        std::map<std::string, std::pair<long, long>> subs; // subrule names matched
        std::map<std::string, std::vector<ParseNode>> children; // named subrule sub-trees (grammar path)
        const SubResolver* resolver = nullptr;             // plain-regex subrule path (atomic)
        class GrammarMatcher* grammar = nullptr;           // grammar path (backtrackable)
        std::map<int, std::vector<std::pair<long, long>>> capReps; // list-valued positional capture occurrences
        long startPos = 0;                                 // where this frame's match began (for $/ in code assertions)
        long capFrom = -1;                                 // `<(` capture-start position (overall match .from), -1 = none
        const GrammarHooks* hooks = nullptr;               // interpreter callbacks (null = lenient/no runtime eval)
        const std::string* curSym = nullptr;               // proto candidate's sym value, so `<sym>` matches it
        long firstCode = -1;                               // string pos at the first bare `{…}` block (ends the LTM declarative prefix)
        long steps = 0;                                    // backtracking step budget (guards catastrophic patterns)
    };
    // Thrown when a match exceeds the step budget — a pathological pattern
    // (nested-quantifier backtracking) would otherwise hang or overflow the C++
    // stack. Caught at the match entry points and reported as a no-match/error.
    struct StepLimitExceeded {};
    bool matchNode(const Node* n, MState& st, long pos, const FnRef& k) const;
    // {min,max} byte width the pattern can match; max = -1 means unbounded/unknown.
    std::pair<long, long> nodeWidth(const Node* n, MState& st) const;
    const Node* root() const { return root_.get(); }
    int ncaps() const { return ncaps_; }
    // Fast path for a rule whose whole body is a single character matcher (e.g.
    // `token space { <[\ \t]> }`): returns true if this regex is exactly that.
    bool rootIsSingleChar() const;
    // If rootIsSingleChar(), test it at `pos`: returns pos+1 on match, -1 on no match.
    long trySingleChar(const std::string& s, long pos) const;
private:
    bool classMatch(const Node* n, char c) const;
};

// Backtrackable grammar engine: matches a table of named rules with the
// continuation threaded THROUGH subrule calls, so `<a> <b>` can backtrack into
// <a> when <b> fails. Supports parameterised rules `<r($x)>`, per-rule capture
// frames, and records a ParseNode tree the interpreter turns into Match values.
class GrammarMatcher {
public:
    struct Rule { std::string pattern, kind; std::vector<std::string> params; };
    std::map<std::string, Rule> rules;
    std::map<std::string, std::vector<std::string>> protos; // proto name -> candidate rule names (`x:<sym>`)
    GrammarHooks hooks; // interpreter callbacks for match-time evaluation (set by grammarParse)

    // Parse `input` from rule `top`. On success fills `out` (the tree) and returns
    // the end offset in `endOut`; requires a full match unless `subparse`.
    bool parse(const std::string& input, const std::string& top, bool subparse, ParseNode& out, long& endOut);

    // Called by Regex::matchNode for a `<name(args)>` subrule; threads `k` through
    // the callee. `capKey` (empty for <.name>) is the parent-frame capture key.
    bool matchSub(const std::string& name, const std::string& args, const std::string& capKey,
                  Regex::MState& st, long pos, const FnRef& k);
    // Same, with the name already resolved (call sites cache the meta on the AST node).
    bool matchSubMeta(const GrammarRuleMeta& meta, const std::string& name, const std::string& args,
                      const std::string& capKey, Regex::MState& st, long pos, const FnRef& k);

    // The parameter bindings of the rule currently being matched (for code-block access).
    const std::map<std::string, std::string>& currentParams() const;

    // Packrat memo of a ratchet (token/rule) subrule's deterministic match at a given
    // (rule, params, pos): tokens don't backtrack, so their first complete match is THE
    // match — caching it collapses the exponential re-descent of recursive LTM probing.
    struct MemoEntry {
        bool matched = false;
        long end = 0;
        long declEnd = 0; // string pos where the LTM declarative prefix ends (first bare code block, else end)
        std::vector<std::pair<long, long>> caps;
        std::map<std::string, std::pair<long, long>> named;
        std::shared_ptr<const ChildMap> kids; // frozen once; replays share, never copy
    };
    long candDeclEnd_ = -1; // set by matchSubMeta after a candidate match: its declarative-prefix end (for proto LTM)
    void clearMemo() { memo_.clear(); }

    using NameMeta = GrammarRuleMeta;
    const NameMeta& nameMeta(const std::string& name);      // cached per-name metadata (see GrammarRuleMeta)

private:
    std::unordered_map<uint64_t, MemoEntry> memo_;          // ratchet-token packrat cache (per parse), integer-keyed
    std::unordered_map<std::string, NameMeta> nameMeta_;    // per-name metadata cache (avoids repeated rules.find)
    std::map<std::string, std::unique_ptr<Regex>> cache_;   // name(+arg values) → compiled
    std::vector<std::map<std::string, std::string>> scope_; // parameterised-rule param bindings
    Regex* compiled(const std::string& name, const std::string& argstr, std::map<std::string, std::string>& boundOut);
    Regex* compiledFor(const Rule& rule, const std::string& name, const std::string& argstr, std::map<std::string, std::string>& boundOut);
    std::string evalArg(const std::string& e) const;
    std::vector<std::string> splitArgs(const std::string& s) const;
    std::string interpParams(const std::string& pat, const std::map<std::string, std::string>& sc) const;
};

} // namespace rakupp
