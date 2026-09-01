#pragma once
#include <algorithm>
#include <functional>
#include <type_traits>
#include <map>
#include <set>
#include <atomic>
#include "LtmNfa.h"
#include <cstdint>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>

namespace rakupp {

// A sorted flat map exposing the std::map subset the matcher uses. The parse
// tree's child/named maps are built, snapshot-copied, and torn down constantly
// during a grammar parse, and std::map pays one heap node per key where one
// contiguous buffer suffices — profiled at ~45% of a capturing parse in
// allocator time, 28% in memo teardown alone. Iteration stays key-sorted,
// exactly like std::map, because Match assembly walks these in key order.
// Unlike std::map, mutation invalidates references/iterators to OTHER
// entries — the matcher re-looks-up by key around its continuations, which
// is why the swap is safe here; don't hold a reference across an insert.
template <typename V>
class FlatMap {
    using Vec = std::vector<std::pair<std::string, V>>;
    Vec v_;
    static bool keyLess(const std::pair<std::string, V>& e, const std::string& k) { return e.first < k; }
public:
    using value_type = typename Vec::value_type;
    using iterator = typename Vec::iterator;
    using const_iterator = typename Vec::const_iterator;
    iterator begin() { return v_.begin(); }
    iterator end() { return v_.end(); }
    const_iterator begin() const { return v_.begin(); }
    const_iterator end() const { return v_.end(); }
    bool empty() const { return v_.empty(); }
    size_t size() const { return v_.size(); }
    void clear() { v_.clear(); }
    iterator find(const std::string& k) {
        auto it = std::lower_bound(v_.begin(), v_.end(), k, keyLess);
        return it != v_.end() && it->first == k ? it : v_.end();
    }
    const_iterator find(const std::string& k) const {
        auto it = std::lower_bound(v_.begin(), v_.end(), k, keyLess);
        return it != v_.end() && it->first == k ? it : v_.end();
    }
    size_t count(const std::string& k) const { return find(k) != v_.end() ? 1 : 0; }
    V& operator[](const std::string& k) {
        auto it = std::lower_bound(v_.begin(), v_.end(), k, keyLess);
        if (it == v_.end() || it->first != k) it = v_.insert(it, {k, V{}});
        return it->second;
    }
    size_t erase(const std::string& k) {
        auto it = find(k);
        if (it == v_.end()) return 0;
        v_.erase(it);
        return 1;
    }
    iterator erase(const_iterator it) { return v_.erase(it); }
};

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
struct ParseNode; // declared below — the hooks reference completed nodes
struct RxCursorCaps;   // defined below, once ChildMap is — the mid-match cursor's capture lists

struct GrammarHooks {
    using NamedMap = FlatMap<std::pair<long, long>>; // named-capture byte spans, for $/ / $<x>
    using ParamMap = std::map<std::string, std::string>;           // current rule params, e.g. $indent
    // Action firing DURING the match, Rakudo-style: a completed subrule fires its
    // action method immediately — and a later backtrack/overall failure does NOT
    // unfire it (HTTP::Header sets header fields from actions of a parse whose
    // TOP ultimately fails on a missing trailing newline). hasAction gates the
    // node assembly so rules with no method cost nothing; onRule fires it.
    // Fired on FRESH completions only — a memo replay reuses the first firing.
    std::function<bool(const std::string&)> hasAction;
    std::function<void(ParseNode)> onRule; // by value: fire sites move their freshly built node in
    std::function<bool(const std::string&, long, long, const NamedMap&, const ParamMap&)> assertPass; // <?{…}>
    // Same as `assertPass`, but carrying the POSITIONAL captures, so the
    // assertion's `$/` is the CURSOR — the match so far, with `$0` and friends.
    // Preferred over `assertPass` when set.
    std::function<bool(const std::string&, long, long, const NamedMap&,
                       const std::vector<std::pair<long, long>>&, const ParamMap&)> assertPassCaps;
    std::function<void(const std::string&, long, long, const NamedMap&, const ParamMap&)> run;        // :my / {…}
    // Same as `run`, but carrying the POSITIONAL captures too so the block's `$/`
    // can offer `$0`. Set by the plain-regex path; preferred over `run` when set.
    std::function<void(const std::string&, long, long, const NamedMap&,
                       const std::vector<std::pair<long, long>>&, const ParamMap&)> runCaps;
    // Same again, plus the cursor's per-name occurrence lists — so `$<n>` inside
    // the block has the SHAPE it will have when the match finishes, list and all.
    // Preferred over `runCaps` when set.
    std::function<void(const std::string&, long, long, const NamedMap&,
                       const std::vector<std::pair<long, long>>&,
                       const RxCursorCaps&, const ParamMap&)> runCursor;
    std::function<std::string(const std::string&, const NamedMap&, const ParamMap&)> str;             // $var atom
    std::function<std::pair<long,long>(const std::string&, const NamedMap&, const ParamMap&)> range;  // ** {…}
    // Save/restore interpreter side-effect state (`:my` vars, deferred makes) so an LTM
    // collect pass can measure branch lengths without polluting the commit pass.
    std::function<std::shared_ptr<void>()> saveState;
    std::function<void(std::shared_ptr<void>)> restoreState;
    // LTM subrule expansion (phase 3): hand the NFA builder a named rule's
    // PATTERN TEXT + compile flags, or answer false for anything it will not
    // vouch for (builtins unless lexically shadowed, qualified names, protos,
    // sigspace rules). The NFA compiles and OWNS the callee; the text also
    // serves as a staleness stamp, since named-regex registration is
    // last-wins and a re-declared token must invalidate cached expansions.
    std::function<bool(const std::string& name, std::string& text, std::string& flags)> namedRule;
};

// A node of the parse tree recorded by the backtracking GrammarMatcher: which rule
// matched which span, its positional captures ($0..) and named/subrule children.
// A leaf with an empty `name` records a plain named capture ($<x>=[…]), not a rule.
// The child map is FROZEN behind a shared_ptr when the node is recorded, so
// re-recording a memoized sub-match is a refcount bump, not a subtree copy.
struct ParseNode;
using ChildMap = FlatMap<std::vector<ParseNode>>;

// What a MID-MATCH code block needs beyond the flat capture spans: the per-name
// occurrence lists and which captures are list-valued. Without them the `$/` a
// `{…}` block sees collapses every repeated name to its last occurrence, so
// `<n> '+' <n> { $<n>.elems }` read 0 where the finished match reads 2.
struct RxCursorCaps {
    const ChildMap* children = nullptr;
    const std::map<int, std::vector<std::pair<long, long>>>* capReps = nullptr;
    std::shared_ptr<const std::set<int>> listCaps;
    std::shared_ptr<const std::set<std::string>> listNames;
};
struct ParseNode {
    std::string name;
    std::string actualRule; // proto entry: the winning `name:sym<…>` candidate (else empty)
    long from = 0, to = 0;
    std::vector<std::pair<long, long>> caps;              // positional captures ($0,$1,…)
    GrammarHooks::NamedMap named;                         // named-capture spans ($<x>)
    std::shared_ptr<const ChildMap> kids;                 // frozen sub-trees (null = leaf); a vector collates repeated captures
    // capture keys under a repetition quantifier in THIS rule's pattern (<pair>* etc.):
    // list-valued regardless of occurrence count (Rakudo: even 0 or 1 gives an Array).
    // Shared from the rule's compiled Regex; null = none.
    std::shared_ptr<const std::set<std::string>> listNames;
    // positional captures under a repetition quantifier (`(...)+` → $0 is an Array
    // of every occurrence, as in Rakudo): which indices are list-valued, and the
    // per-iteration spans. Shared/frozen like kids; null = none.
    std::shared_ptr<const std::set<int>> listCaps;
    std::shared_ptr<const std::map<int, std::vector<std::pair<long, long>>>> capReps;
};

// Result of a regex match against a subject string (byte offsets).
struct RxMatch {
    bool matched = false;
    long from = 0, to = 0;
    std::vector<std::pair<long, long>> caps;            // positional captures ($0,$1,..); {-1,-1} = unset
    std::map<int, std::vector<std::pair<long, long>>> capReps; // occurrences per list-valued positional capture
    std::set<int> listCaps;                             // which positional indices are list-valued ($n under */+/**)
    GrammarHooks::NamedMap named;                       // named captures ($<name>) byte ranges
    ChildMap children;                                  // per-name occurrence list; repeated captures collate here
    std::shared_ptr<const std::set<std::string>> listNames; // subrule keys under a quantifier → always list-valued
    std::shared_ptr<const std::set<std::string>> hashNames; // `%<name>=…` keys → built as a Hash of matched strings
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
    bool scoped = false;           // body declares `:my` — its dynamic vars are per-invocation
                                   // (save/restore interpreter scope around the call)
    bool dynDep = false;           // body declares `:my` or reads a dynamic var ($*/@*/%*): its match
                                   // can depend on caller state not in the packrat key, so don't memoise
    std::string builtinClass;      // unknown name: built-in char-class flags ("d","a",…), else empty
    // proto dispatch, RAKUPP_LTM=1: the union NFA over the candidates'
    // declarative prefixes, built once per (matcher, proto) — the matcher is
    // per-parse, so there is no cross-parse staleness to manage
    mutable std::shared_ptr<LtmNfa> protoNfa;
    mutable bool protoNfaTried = false; // a failed build is not retried per position
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
    const std::string& obsolete() const { return obsolete_; } // non-empty: retired P5 metachar
    // Find the first match whose start is >= startPos (unanchored search).
    bool search(const std::string& subject, long startPos, RxMatch& out) const;
    bool search(const std::string& subject, long startPos, RxMatch& out, const SubResolver& r,
                const std::set<std::string>* lexNames = nullptr,
                const GrammarHooks* hooks = nullptr) const;
    // Match anchored exactly at `pos` (used for grammar subrule calls).
    bool matchAt(const std::string& subject, long pos, RxMatch& out, const SubResolver& r,
                 const std::set<std::string>* lexNames = nullptr,
                 const GrammarHooks* hooks = nullptr) const;
    // `:exhaustive` — every match at every start position and every length.
    std::vector<RxMatch> searchExhaustive(const std::string& subject, const SubResolver& r,
                 const std::set<std::string>* lexNames = nullptr,
                 const GrammarHooks* hooks = nullptr) const;
    // Optional interpreter callbacks for standalone (non-grammar) matches — lets a
    // plain `/ … { make 1 } … /` execute its code blocks. Null = lenient no-op.
    // A SHARED (cached) Regex must get its hooks through the per-call parameter
    // above instead: this member is per-object state, and the interpreter's
    // pattern cache hands one object to nested and repeated matches whose hook
    // frames are on different stacks.
    const GrammarHooks* runHooks = nullptr;

private:
    friend class LtmNfa; // the declarative-prefix ranking NFA reads Node directly
    enum class K { Lit, Any, Class, Seq, Alt, Conj, Rep, Group, AnchorStart, AnchorEnd, WBLeft, WBRight, Nop, Subrule, Look, Code, VarMatch, CapStart, CapEnd, CondRef };
    struct Node {
        K k;
        std::string lit;                 // Lit
        std::vector<std::unique_ptr<Node>> kids;
        // Class
        std::vector<std::pair<unsigned char, unsigned char>> ranges;
        std::vector<std::pair<uint32_t, uint32_t>> cpRanges; // codepoint ranges (>0xFF chars, named/hex escapes in classes)
        std::vector<std::string> clusterMembers; // NFG class members that are multi-codepoint graphemes (\c[A, COMBINING…])
        std::string classFlags;          // subset of "dws" (positive), uppercase = negated
        std::string negClassFlags;       // `-rule` difference members: char must NOT match these
        std::string uprop;               // Unicode property for <:Nd>/<:L>/… (Class node, codepoint-aware)
        bool negate = false;
        // `<~~>` — recurse into the pattern this node was WRITTEN in. Null means
        // the whole regex being matched; a sub-pattern spliced in from a Regex
        // value points at its own root, so `my $re = rx/ '(' <~~>* ')' /` recurses
        // into `$re` and not into whatever host interpolated it.
        const Node* recTarget = nullptr;
        bool icase = false;              // case-insensitive at THIS node (scoped inline :i)
        bool imark = false;              // Lit: :ignoremark — compare base codepoints, consume the whole grapheme
        bool multiline = false;          // AnchorStart/AnchorEnd: `^^`/`$$` (line) vs `^`/`$` (string)
        bool absEnd = false;             // AnchorEnd: P5 `\z` — absolute end, not before a final newline
        bool p5Line = false;             // AnchorStart, P5 (?m) `^`: after a \n but NOT at the very end
        mutable uint32_t byteset[8];     // per-byte match result (incl. icase+negate), built on first use
        mutable std::atomic<bool> bytesetReady{false}; // release-published after the
                                          // byteset words are filled; readers acquire-load (the flag
                                          // gates a 32-byte array, so relaxed would not be enough)
        // Rep
        long min = 0, max = -1;          // max = -1 => unbounded
        bool greedy = true;
        bool possessive = false;         // `a*:` — grab greedily and never give any back
        std::unique_ptr<Node> sep;       // `X+ % Y` / `X+ %% Y` separator (null if none)
        bool sepTrail = false;           // `%%`: an optional TRAILING separator may follow the last item
        std::string repCode;             // `** { … }` — evaluate this at match time for (min,max)
        // Code / VarMatch: `lit` holds the code / variable expression; `runOnly` = `:my`/`{…}` (execute, always pass)
        bool runOnly = false;
        bool ltmStop = false; // a bare `{…}` code block — ends the LTM declarative prefix (`:my`/assertions do not)
        // Alt
        bool firstMatch = false;         // `||` (sequential first-match) vs `|` (LTM, longest wins)
        bool classCombo = false;         // SYNTHESIZED first-match Alt for a composed char class
                                         // (`<+a +b>` / `<-a +b>`): semantically a one-char UNION,
                                         // so the LTM prefix model must union it, not take kid 0
        // Group
        int capIndex = -1;               // -1 => non-capturing
        std::string capName;
        bool listCap = false;            // capture is under a repetition quantifier (*/+/**) → $n is a list
        // `$<a>=( … )`: the parens are a CAPTURE, and a capture is its own
        // capture scope — names matched inside belong to `$<a>`, not to the
        // enclosing match. `$<a>=[ … ]` only groups, so its contents stay where
        // they were written. Decided at parse time; only these groups pay for
        // the bookkeeping.
        bool nestNames = false;
        // Subrule
        std::string ruleName;
        std::string ruleArgs;            // raw args of a parameterised call <name($x, '')>
        std::string ruleAlias;           // capture key for <alias=rule> (else = ruleName)
        bool aliasDotted = false;        // <alias=.rule> — the alias captures, the rule name does NOT
        bool ruleCapture = true;         // <name> captures as $<name>; <.name> does not
        mutable const GrammarRuleMeta* metaCache = nullptr; // per-node name resolution (grammar path)
        // Look: zero-width assertion — kids[0] is the inner pattern; `negate` = <!…>,
        // `behind` = lookbehind (<?after…>) vs lookahead (<?before…>/<?…>).
        bool behind = false;
        // lookbehind scan window = the inner pattern's possible match width, computed
        // once — bounds the start-position scan to O(width) instead of O(pos)
        mutable long lookMin = 0, lookMax = -1;
        mutable bool lookWidthReady = false;
        // Alt: the declarative-prefix ranking NFA, built once on first use.
        // Cached ON the node (the byteset precedent) — a process compiles
        // many regexes, and a map keyed by Node* was poisoned when freed
        // node addresses were recycled (found by the phase-1 harness on
        // S05-mass/rx.t: stale NFAs produced empty or nonsense ranks).
        mutable std::unique_ptr<LtmNfa> ltmNfa;
    };
    using NodePtr = std::unique_ptr<Node>;

    std::string pat_;
    size_t pos_ = 0;
    int ncaps_ = 0;
    std::set<int> listCaps_;             // positional capture indices under a repetition quantifier
    mutable std::shared_ptr<const std::set<int>> listCapsFrozen_; // lazily frozen copy for ParseNode sharing
    std::shared_ptr<std::set<std::string>> listNames_; // subrule capture keys under a repetition quantifier
    std::shared_ptr<std::set<std::string>> hashNames_; // `%<name>=…` hash-valued capture keys
    void collectListNames(const Node* n); // walk a quantified atom, gathering capturing subrule keys
    void markRepeatedNames();             // …and the names a single path can reach twice
    static void countCaptureNames(const Node* n, std::map<std::string, int>& out);
    bool ok_ = true;
    std::string obsolete_;               // retired metachar seen (e.g. "\\A"), for X::Obsolete
    bool icase_ = false;
    bool curIcase_ = false; // parse-time adverb state: :i/:!i scoped to the enclosing group
    bool curImark_ = false; // parse-time adverb state: :m/:ignoremark scoped to the enclosing group
    bool sigspace_ = false;
    bool ratchet_ = false; // `token`/`rule`: quantifiers are possessive, matches commit (no backtracking)
    int assertDepth_ = 0; // >0 while parsing an assertion inner (so parseSeq stops at `>`)
    NodePtr root_;

    // :P5/:Perl5 — the pattern is Perl 5 syntax. A second FRONT-END over the same
    // Node AST; the matcher is untouched. Parse-time state mirrors the Raku
    // parser's scoped-adverb vars: (?i)/(?m)/(?s)/(?x) apply to the end of the
    // enclosing group.
    bool p5_ = false;
    bool p5Multi_ = false;   // (?m): ^/$ anchor at line boundaries
    bool p5DotAll_ = false;  // (?s): `.` also matches \n
    bool p5Ext_ = false;     // (?x): whitespace and #-comments are insignificant
    int p5Depth_ = 0;        // group nesting (an unmatched `)` is a syntax error)
    struct P5BadPattern {};  // thrown on P5 syntax errors → ok_ = false
    NodePtr p5Alt();
    NodePtr p5Seq();
    NodePtr p5Quant(NodePtr atom);
    NodePtr p5Atom();
    NodePtr p5Group();
    NodePtr p5Escape();
    NodePtr p5Class();
    void p5SkipX();          // (?x) mode: skip whitespace + # comments
    uint32_t p5Codepoint();  // decode ONE UTF-8 codepoint at pos_, advance

    // parser
    NodePtr parseAlt();
    NodePtr parseConj();
    NodePtr parseSeq();
    NodePtr parseQuant();
    NodePtr parseAtom();
    static NodePtr wsWrap(NodePtr inner); // sigspace: Seq(inner, <.ws>)
    void parseClassBodyMember(Node* node);
    void skipWs();
    char peek(size_t o = 0) const { return pos_ + o < pat_.size() ? pat_[pos_ + o] : '\0'; }
    bool eof() const { return pos_ >= pat_.size(); }

public:
    // matcher state — captures for the rule currently being matched
    struct MState {
        const std::string& s;
        std::vector<std::pair<long, long>> caps;
        GrammarHooks::NamedMap named;
        ChildMap children;                                 // named subrule sub-trees (grammar path)
        const SubResolver* resolver = nullptr;             // plain-regex subrule path (atomic)
        class GrammarMatcher* grammar = nullptr;           // grammar path (backtrackable)
        const std::set<std::string>* lexNames = nullptr;   // lexical `my regex NAME` overrides — shadow built-in subrules
        std::map<int, std::vector<std::pair<long, long>>> capReps; // list-valued positional capture occurrences
        long startPos = 0;                                 // where this frame's match began (for $/ in code assertions)
        long capFrom = -1;                                 // `<(` capture-start position (overall match .from), -1 = none
        long capTo = -1;                                   // `)>` capture-end position (overall match .to), -1 = none
        const GrammarHooks* hooks = nullptr;               // interpreter callbacks (null = lenient/no runtime eval)
        const std::string* curSym = nullptr;               // proto candidate's sym value, so `<sym>` matches it
        long firstCode = -1;                               // string pos at the first bare `{…}` block (ends the LTM declarative prefix)
        // Depth of the Alt RANKING probe. Ranking must not execute user code —
        // the probe measures how far each branch reaches, and a bare `{…}` is
        // zero-width and always passes, so running it changes no measurement and
        // fires its side effects a second time (`| 'print' <v> { say … }` said
        // everything twice). Assertions still run: they DECIDE the branch.
        int probing = 0;
        long litPrefix = -1;                               // end pos of the leading literal-atom run from startPos (-1 = not started)
        long steps = 0;                                    // backtracking step budget (guards catastrophic patterns)
        // Bare `{…}` blocks QUEUED rather than run where they appear: the matcher
        // is CPS + backtracking, so a block sitting on a branch that is later
        // abandoned would otherwise fire its side effects anyway — and fire them
        // again on every retry. Queued here, unwound when a branch fails, and run
        // in order only once the overall match is accepted. Off by default: the
        // GRAMMAR path wants its blocks eager (a later `<?{…}>` can read what one
        // set), so only the plain-regex entry points turn it on and drain it.
    };
    // Thrown when a match exceeds the step budget — a pathological pattern
    // (nested-quantifier backtracking) would otherwise hang or overflow the C++
    // stack. Caught at the match entry points and reported as a no-match/error.
    struct StepLimitExceeded {};
    // Thrown by the parser on a retired Perl 5 metachar (\A \z \G \p \Q \1 …);
    // the ctor records it so callers can raise X::Obsolete instead of no-match.
    struct ObsoleteEscape { std::string seq; };
    bool matchNode(const Node* n, MState& st, long pos, const FnRef& k) const;
    // {min,max} byte width the pattern can match; max = -1 means unbounded/unknown.
    std::pair<long, long> nodeWidth(const Node* n, MState& st) const;
    const Node* root() const { return root_.get(); }
    int ncaps() const { return ncaps_; }
    // Subrule capture keys under a repetition quantifier (null = none) — shared
    // into ParseNode/RxMatch so Match building can honour Rakudo's list arity.
    std::shared_ptr<const std::set<std::string>> listNamesPtr() const { return listNames_; }
    // the rule's list-valued positional capture indices, frozen on first request
    std::shared_ptr<const std::set<int>> listCapsPtr() const {
        if (!listCapsFrozen_ && !listCaps_.empty())
            listCapsFrozen_ = std::make_shared<const std::set<int>>(listCaps_);
        return listCapsFrozen_;
    }
    // ---- spliced sub-pattern ----
    // A regex VALUE interpolated into another regex (`s/^($token)…/`) cannot be
    // pasted in as text when the two are in different flavours: `rx:P5/[a-z]/`
    // read as Raku is a GROUP over `a`, `-`, `z`. So the splice carries its own
    // syntax with it, as an opaque marker the interpolation passes copy through
    // whole and the parser compiles with the right front-end:
    //     \x01 KIND LENGTH \x01 SOURCE          KIND = 'P' (Perl 5) | 'R' (Raku)
    // The length is what makes it opaque: the source may contain ANY character,
    // including the `]` and `$` that a delimiter-scanning marker would trip on.
    static std::string spliceOf(const std::string& src, bool p5) {
        return std::string("\x01") + (p5 ? 'P' : 'R') + std::to_string(src.size()) + "\x01" + src;
    }
    // Total byte length of the marker starting at `i`, or 0 if there is none.
    static size_t spliceSpan(const std::string& s, size_t i) {
        if (i >= s.size() || s[i] != '\x01' || i + 2 >= s.size()) return 0;
        if (s[i + 1] != 'P' && s[i + 1] != 'R') return 0;
        size_t j = i + 2, n = 0;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9') n = n * 10 + (size_t)(s[j++] - '0');
        if (j == i + 2 || j >= s.size() || s[j] != '\x01') return 0;
        return (j + 1 + n <= s.size()) ? j + 1 + n - i : 0;
    }
    // Fast path for a rule whose whole body is a single character matcher (e.g.
    // `token space { <[\ \t]> }`): returns true if this regex is exactly that.
    bool rootIsSingleChar() const;
    NodePtr parseSplice(); // compile a \x01-marked sub-pattern with its own front-end
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
                      const std::string& capKey, Regex::MState& st, long pos, const FnRef& k,
                      bool alsoBareName = false);

    // The parameter bindings of the rule currently being matched (for code-block access).
    const std::map<std::string, std::string>& currentParams() const;

    // Packrat memo of a ratchet (token/rule) subrule's deterministic match at a given
    // (rule, params, pos): tokens don't backtrack, so their first complete match is THE
    // match — caching it collapses the exponential re-descent of recursive LTM probing.
    struct MemoEntry {
        bool matched = false;
        long end = 0;
        long declEnd = 0; // string pos where the LTM declarative prefix ends (first bare code block, else end)
        long litPrefix = 0; // length of the leading literal-atom run (LTM specificity tie-break)
        long capFrom = -1, capTo = -1; // the rule body's `<( … )>` span (-1 = none): the
                                       // CAPTURE is trimmed to it, while matching continues at `end`
        std::vector<std::pair<long, long>> caps;
        GrammarHooks::NamedMap named;
        std::shared_ptr<const ChildMap> kids; // frozen once; replays share, never copy
        std::shared_ptr<const std::set<std::string>> listNames; // the rule's quantified capture keys
        std::shared_ptr<const std::set<int>> listCaps; // list-valued positional capture indices
        std::shared_ptr<const std::map<int, std::vector<std::pair<long, long>>>> capReps; // their per-iteration spans
    };
    long candDeclEnd_ = -1; // set by matchSubMeta after a candidate match: its declarative-prefix end (for proto LTM)
    long candLitPrefix_ = 0; // set alongside candDeclEnd_: leading-literal length (LTM specificity)
    void clearMemo() { reapMemo(); }
    // Destroying the packrat memo walks every frozen subtree — measured at
    // ~28% of a capturing grammar parse, all in destructor recursion and
    // page-return madvise, none of it needed before the parse result can be
    // used: the trees are refcounted and self-contained (the winning parse
    // holds its own refs, and shared_ptr counts are atomic). reapMemo hands
    // a large memo to a detached reaper thread so the frees happen off the
    // parse's critical path; small memos (or a saturated reaper) destroy
    // inline, which is exactly the old behavior.
    ~GrammarMatcher() { reapMemo(); }

    // G1 highwater (GRAMMAR-PLAN): the furthest input position where a named
    // rule FRESHLY failed during this parse, and which rule — rule-grained
    // parse diagnostics. Strict `>`: at one position the FIRST failure wins,
    // and recursive descent fails innermost-first, so that is the deepest,
    // most specific expectation (<key>, not the <entry> wrapping it). Reset
    // by parse(); read by the interpreter when the whole parse fails. Byte
    // offsets, as all matcher positions are.
    long hwPos = -1;
    std::string hwRule;
    void noteFail(long pos, const std::string& rule) {
        if (pos > hwPos) { hwPos = pos; hwRule = rule; }
    }

    using NameMeta = GrammarRuleMeta;
    const NameMeta& nameMeta(const std::string& name);      // cached per-name metadata (see GrammarRuleMeta)
    // The LTM expansion route for this grammar's rules (see LtmExpand::grammar):
    // 0 refuse, 1 = regexOut is the rule's compiled no-arg body, 2 = <ws>,
    // 3 = a single-char builtin class in flagOut.
    int ltmResolve(const std::string& name, const void*& regexOut, char& flagOut);

private:
    void reapMemo(); // defer a large memo's destruction to the background (see ~GrammarMatcher)
    std::unordered_map<uint64_t, MemoEntry> memo_;          // ratchet-token packrat cache (per parse), integer-keyed
    std::unordered_map<std::string, NameMeta> nameMeta_;    // per-name metadata cache (avoids repeated rules.find)
    std::map<std::string, std::unique_ptr<Regex>> cache_;   // name(+arg values) → compiled
    std::vector<std::map<std::string, std::string>> scope_; // parameterised-rule param bindings
    mutable std::map<std::string, std::string> mergedParams_; // currentParams() scratch: outer dynamic-var params merged in
    Regex* compiled(const std::string& name, const std::string& argstr, std::map<std::string, std::string>& boundOut);
    Regex* compiledFor(const Rule& rule, const std::string& name, const std::string& argstr, std::map<std::string, std::string>& boundOut);
    std::string evalArg(const std::string& e) const;
    std::vector<std::string> splitArgs(const std::string& s) const;
    std::string interpParams(const std::string& pat, const std::map<std::string, std::string>& sc) const;
};

} // namespace rakupp
