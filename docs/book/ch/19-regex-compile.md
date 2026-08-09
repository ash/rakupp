\part{The Regex and Grammar Engine}

# Compiling a Regex

Raku's regex sub-language is not a bolt-on. It has its own quoting rules, its
own whitespace conventions, named rules, code assertions, and grammars built out
of it. Raku++ implements it as a **second, small recursive-descent compiler**
inside the runtime, producing a node tree that a backtracking matcher walks.

The whole thing is one class in one file:

```cpp
// src/Regex.h
class Regex {
public:
    explicit Regex(const std::string& pattern, const std::string& flags = "");
    bool ok() const;
    bool search(const std::string& subject, long startPos, RxMatch& out) const;
    bool matchAt(const std::string& subject, long pos, RxMatch& out,
                 const SubResolver& r,
                 const std::set<std::string>* lexNames = nullptr) const;
};
```

## The pattern text arrives unparsed

The Raku lexer does not tokenize regex syntax (Chapter 3). A `/…/` literal, an
`rx//`, an `s///` and a `token NAME { … }` body all travel through the token
stream as raw text. The regex compiler re-lexes that text with its own scanner.

That separation is the reason two grammars can coexist without either
contaminating the other, and it is why the constructor takes a `std::string`
rather than a token range.

Adverbs arrive as a **flags string** alongside the pattern: `i` for
ignorecase, `s` for sigspace, `r` for ratchet, `m` for ignoremark, and so on.
A `token` compiles with `r`; a `rule` with `sr`.

## The node tree

```cpp
// src/Regex.h
enum class K {
    Lit, Any, Class, Seq, Alt, Conj, Rep, Group,
    AnchorStart, AnchorEnd, WBLeft, WBRight, Nop,
    Subrule, Look, Code, VarMatch, CapStart, CapEnd
};
```

Nineteen kinds, and the interesting information is in the fields rather than the
tags:

```cpp
// src/Regex.h — Node, abridged
struct Node {
    K k;
    std::string lit;                                // Lit
    std::vector<std::unique_ptr<Node>> kids;
    std::vector<std::pair<unsigned char, unsigned char>> ranges;   // Class
    std::vector<std::pair<uint32_t, uint32_t>> cpRanges;
    std::vector<std::string> clusterMembers;        // multi-codepoint graphemes
    std::string classFlags, negClassFlags, uprop;
    bool negate = false, icase = false, imark = false, multiline = false;
    mutable uint32_t byteset[8];                    // built on first use
    mutable std::atomic<bool> bytesetReady{false};
    long min = 0, max = -1; bool greedy = true, possessive = false;  // Rep
    std::unique_ptr<Node> sep; bool sepTrail = false;    // X+ % Y
    std::string repCode;                            // ** { … }
    bool runOnly = false, ltmStop = false;          // Code
    bool firstMatch = false, classCombo = false;    // Alt
    int capIndex = -1; std::string capName; bool listCap = false;   // Group
    std::string ruleName, ruleArgs, ruleAlias;      // Subrule
    bool aliasDotted = false, ruleCapture = true;
    mutable const GrammarRuleMeta* metaCache = nullptr;
    bool behind = false;                            // Look
    mutable long lookMin = 0, lookMax = -1;
    mutable std::unique_ptr<LtmNfa> ltmNfa;         // Alt
};
```

Three fields deserve attention now; the rest appear where they are used.

## The byteset: a per-node character-class cache

A character class has to answer "does this byte match?" for every position it is
tested at, taking into account ranges, named classes, negation and
case-folding. Computing that from the class's own data each time is a loop.

So each `Class` node caches the answer for all 256 bytes as a bitmap, built on
first use:

```cpp
mutable uint32_t byteset[8];                   // 256 bits
mutable std::atomic<bool> bytesetReady{false};
```

The flag is an *acquire/release* atomic rather than a relaxed one, and the
comment says why: it gates a 32-byte array, so a relaxed store would not
guarantee that a reader which sees the flag also sees the bytes. This is the one
place in the codebase where the weaker ordering used everywhere else would be
wrong, and it is worth noticing as a general point — `DecidedOnce` is safe
because it publishes a *single word*.

Codepoints above 255 do not fit a byteset, so they live in `cpRanges` and are
tested separately. Multi-codepoint graphemes — a class member written
`\c[A, COMBINING ACUTE ACCENT]` — live in `clusterMembers` and are compared as
whole clusters.

## The parser

Five mutually recursive functions, one per precedence level:

```cpp
// src/Regex.h
NodePtr parseAlt();     // a | b   and   a || b
NodePtr parseConj();    // a & b
NodePtr parseSeq();     // concatenation
NodePtr parseQuant();   // * + ? ** N..M, with % separators
NodePtr parseAtom();    // everything else
```

`parseAtom` is the large one. It handles literals and quoted literals, `.`, the
escape classes `\d \w \s` and their negations, `<[…]>` and `<-[…]>` character
classes with ranges, named classes and Unicode properties `<:Nd>`, groups
`[ ]` (non-capturing) and `( )` (capturing), named captures `$<x>=[…]`, subrule
calls `<name>` with their aliasing and capture variants, lookarounds, code
assertions `<?{…}>` and `<!{…}>`, side-effect blocks `{…}` and `:my`
declarations, `$var` interpolation, the capture-span markers `<(` and `)>`,
anchors, and word boundaries.

Two parse-time pieces of state make the adverbs scoped rather than global:

```cpp
// src/Regex.h
bool curIcase_ = false;   // :i / :!i, scoped to the enclosing group
bool curImark_ = false;   // :m / :ignoremark, likewise
int assertDepth_ = 0;     // >0 inside an assertion, so parseSeq stops at `>`
```

so an inline `:i` inside a group applies to that group's nodes and no further —
which is why `icase` and `imark` are per-*node* flags rather than per-regex ones.

## Sigspace

Under `:s`, or in a `rule`, whitespace in the pattern is significant and means
"match optional whitespace here". That is implemented structurally rather than
by a matcher flag:

```cpp
// src/Regex.h
static NodePtr wsWrap(NodePtr inner);   // Seq(inner, <.ws>)
```

Each atom is wrapped in a sequence with a non-capturing `<ws>` subrule after it.
The consequence is that sigspace needs no support anywhere in the matcher: the
`<ws>` calls are ordinary subrule nodes.

## Ratchet

`token` and `rule` are *ratcheting*: their quantifiers are possessive and their
matches commit — no backtracking into a token once it has matched.

```cpp
// src/Regex.h
bool ratchet_ = false;
```

The flag makes every `Rep` node possessive at compile time. It is also what
makes the packrat memo in Chapter 21 sound: a rule that does not backtrack has
exactly one match at a given position, so caching it is safe.

## Two whole-pattern optimisations

**A single-character rule is inlined at its call sites.** A grammar full of
`token space { <[\ \t]> }` would otherwise pay a full subrule call per
character:

```cpp
// src/Regex.h
bool rootIsSingleChar() const;
long trySingleChar(const std::string& s, long pos) const;   // pos+1, or -1
```

The call site tests the property once, caches the answer on the rule's metadata,
and thereafter tests the character directly.

**Lookbehind width is computed once.** A naive lookbehind scans every earlier
start position, which is O(pos) per attempt. Instead the inner pattern's
possible width is derived and the scan window bounded:

```cpp
// src/Regex.h
std::pair<long, long> nodeWidth(const Node* n, MState& st) const;
```

```cpp
// src/Regex.cpp — the Look case
if (!n->lookWidthReady) {
    auto w = nodeWidth(child, st);
    n->lookMin = w.first; n->lookMax = w.second; n->lookWidthReady = true;
}
long hi = pos - n->lookMin;
long lo = n->lookMax < 0 ? 0 : pos - n->lookMax;
```

O(width) instead of O(position).

## Retired Perl 5 metacharacters

Raku deliberately reuses some Perl 5 regex syntax for other things, and Roast
checks that the old spellings produce a specific error rather than silently
meaning something else:

```cpp
// src/Regex.h
struct ObsoleteEscape { std::string seq; };
const std::string& obsolete() const;   // non-empty: a retired P5 metachar
```

The constructor records the offending sequence — `\A`, `\z`, `\G`, `\p`, `\Q`,
`\1` — so the caller can raise `X::Obsolete` instead of reporting a failed
match. Distinguishing "this pattern is wrong" from "this pattern did not match"
is worth the extra field.

## Interpolation happens before compilation

`/$var/` and `/@words/` are resolved in the pattern *text*, before the regex is
compiled, by three interpreter functions:

```cpp
// src/Interpreter.h
std::string interpRegexPattern(const std::string& in);  // $var atoms
std::string spliceRegexVars(const std::string& pat);    // regex-valued vars
std::string rxInterpArrays(const std::string& pat);     // /@arr/ → alternation
```

An array interpolates as a **longest-first literal alternation**, which is the
semantics Raku specifies and which the sorting has to implement explicitly. A
regex-valued variable is spliced in as source text at construction, so
`rx/ $x /` where `$x` is a `Regex` composes.

A `$var` that must be read *at match time* — because the variable changes during
the match — is a different node, `K::VarMatch`, evaluated through the hooks in
the next chapter.

## What the compiler produces

The result of construction is a root node plus a little bookkeeping:

```cpp
// src/Regex.h
int ncaps_;                                        // positional capture count
std::set<int> listCaps_;                           // indices under a quantifier
std::shared_ptr<std::set<std::string>> listNames_; // named keys under one
std::shared_ptr<std::set<std::string>> hashNames_; // %<name>= keys
```

The two "list" sets exist because Raku's capture arity is decided by the
*pattern*, not by the match. A capture under a repetition quantifier is an array
**even when it matched once or not at all**. That fact is a property of the
compiled pattern, so it is computed once by `collectListNames` walking each
quantified atom, and then shared — as a `shared_ptr` to a frozen set — into
every match result the pattern produces. Sharing rather than copying matters
because a grammar parse produces one of these per rule invocation.
