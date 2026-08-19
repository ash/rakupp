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
ignorecase, `s` for sigspace, `r` for ratchet, `m` for ignoremark, `5` for
Perl 5 pattern syntax (the last section of this chapter). A `token` compiles
with `r`; a `rule` with `sr`.

## The node tree

```cpp
// src/Regex.h
enum class K {
    Lit, Any, Class, Seq, Alt, Conj, Rep, Group,
    AnchorStart, AnchorEnd, WBLeft, WBRight, Nop,
    Subrule, Look, Code, VarMatch, CapStart, CapEnd, CondRef
};
```

Twenty kinds — the last one exists only for Perl 5 patterns, and this chapter
ends with it. The interesting information is in the fields rather than the
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

Retired, that is, in *Raku* syntax. Every one of those spellings is alive and
meaningful under the `:P5` adverb, where the same constructor parses the same
text with a different front-end — the last section of this chapter.

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

## `:P5` — the same engine wearing Perl 5 syntax

Raku specifies an escape hatch: `m:P5/…/` (long form `:Perl5`, also on `s///`
and `rx//`) promises that the pattern is Perl 5 syntax — `( )` captures,
`[ ]` character classes, `\1` backreferences, `(?i)` inline modifiers, `|` as
leftmost-first alternation. Roast holds it to an 11-file, 918-assertion corpus
(`S05-modifier/Perl_0.t` through `Perl_10.t`) generated from perl's own
`re_tests` suite.

The implementation is a **second front-end, not a second engine**. Seven
parser functions produce the same `Node` tree everything else in this chapter
produces, and the matcher of Chapter 20 runs it unchanged:

```cpp
// src/Regex.h
bool p5_ = false;
bool p5Multi_ = false;   // (?m): ^/$ anchor at line boundaries
bool p5DotAll_ = false;  // (?s): `.` also matches \n
bool p5Ext_ = false;     // (?x): whitespace and # comments insignificant
NodePtr p5Alt();  NodePtr p5Seq();   NodePtr p5Quant(NodePtr atom);
NodePtr p5Atom(); NodePtr p5Group(); NodePtr p5Escape();
NodePtr p5Class();
```

The structure is a port of the Perl-regex parser inside
`showcase/perl/perl.raku` — the Perl 5 interpreter written *in* Raku — widened
to the full `re_tests` surface: lookaround, named groups, backreferences,
conditionals, inline modifier groups. The four `(?imsx)` state booleans mirror
`curIcase_`: saved on entering a group, restored on leaving it, so a mid-group
`(?i)` scopes exactly as Perl scopes it.

### How the adverb reaches the constructor

Two roads lead here, and they meet in the constructor. On the `m//` road the
Raku lexer bakes leading adverbs into the literal's text — `m:P5:i/ab/`
travels as the string `":P5 :i ab"` — and unknown adverbs used to fall through
`skipWs()` untouched, becoming pattern *text* that could never match: `:P5`
was silently `Nil` on every input. Now the constructor pre-scans a leading
adverb run without committing, and commits only when `P5` or `Perl5` is among
it — because in Perl 5 syntax a bare `:` is a literal, so the ordinary
scoped-adverb machinery must never see the pattern. On the `s///` road,
`substSelect` parses the adverb run itself (it used to *throw* on `:P5`) and
passes the fact down as the flag character `5`.

Either way the constructor ends at the same line:

```cpp
// src/Regex.cpp — the constructor, P5 branch
if (p5_) {
    root_ = p5Alt();
    if (!eof()) throw P5BadPattern{}; // e.g. an unmatched `)`
}
```

with parse errors collapsing to `ok_ = false` exactly like Raku-syntax ones.

### Most of Perl desugars to nodes that already existed

The engine's node inventory turned out to be almost sufficient. Some of the
mappings are one-to-one — `(?:…)` is a `Group` with no index, `(?=…)` and
`(?<!…)` are `Look` with the right two booleans, `\1` is the in-flight
backreference `VarMatch` that Raku uses for `$0` inside a pattern. The rest
are small structural rewrites:

| Perl 5 | becomes |
|---|---|
| `\b` | `Alt(WBLeft, WBRight)` — either edge of a word |
| `\B` | that `Alt` inside a negated `Look` |
| `.` (no `/s`) | a negated `Class` holding `\n` |
| `a\|b` | `Alt` with `firstMatch` — Perl `\|` is Raku `\|\|`, never LTM |
| `[a\D]` | a `classCombo` `Alt`: the positive members, then `¬d` |
| `[^a\D]` | De Morgan: `Conj(¬a, d)` — all at one position, last consumes |
| `(?#…)` | consumed before quantifier detection, so `a(?#x){3}` repeats `a` |
| `(?{…})` | nothing — a constant no-op |
| `(??{"…"})` | a constant string sub-compiles; the root node is stolen |

The conditional `(?(N)yes|no)` on a *group number* is the one construct with
no structural equivalent — whether group N participated is match-time state —
and it is why `K::CondRef` exists: one node, the group number in `min`, the
two branches as kids, and a four-line matcher case that picks a kid by looking
at `st.caps`. The conditional on an *assertion*, `(?(?=…)yes|no)`, needs no
new node at all. The parser reads the condition **twice** — once straight,
once with its negation flipped, because `Node` trees have no clone — and emits
a first-match `Alt` whose branches each lead with their guard:

```
(?(COND)yes|no)   →   Alt||( Seq(COND, yes), Seq(¬COND, no) )
```

### Three places the engine had to learn something

Perl's anchors are almost rakupp's anchors. `$` matching at the end *or just
before a final newline* is what this engine's `$` always did, so `\Z` came
free. `\z` — the absolute end, trailing newline not welcome — did not exist,
and `(?m)^` is the engine's `^^` minus one position:

```cpp
// src/Regex.h — Node
bool absEnd = false;  // AnchorEnd: `\z` — not before a final newline
bool p5Line = false;  // AnchorStart, (?m) `^`: after \n, NOT at the end
```

The second flag encodes a genuine difference: `"a\nb\n" =~ /b\s^/m` must fail
in Perl, because Perl's multiline `^` does not match in the void after a final
newline, while the engine's `^^` does.

The third lesson is in `Rep`. The matcher refuses zero-width quantifier
iterations — the standard guard against `(x*)*` looping forever. Perl instead
admits **one** empty iteration and then stops, and `re_tests` 1327 checks the
observable consequence: in `2(]*)?$\1` the group matches empty, *participates*,
and the backreference then matches empty too. The relaxation sits in the
`Rep` case, gated so Raku patterns keep the strict guard:

```cpp
auto more = [&](long np) {
    if (np != p) return self(self, count + 1, np);
    return p5_ && count + 1 >= mn && finish(np, count + 1);
};
```

### Where the semantics are Rakudo's, not Perl's

Three decisions were made by the test corpus rather than by `perlre`, and the
source says so at each site. Named groups do not take positional indices —
in Perl, `(?<meow>…)` is also `$1`; in Rakudo's `:P5` (and per `Perl_10.t`)
it is `$<meow>` only, and `$/[0]` is the first *unnamed* group. A
backreference to a group that never matched **fails** — modern Perl agrees,
but it was the corpus (`(a)|\1` against `"x"` must not match) that settled it
after the implementation initially guessed the old matches-empty behaviour.
And interpolation follows Perl rather than Raku: a `$var` in a `:P5` pattern
splices its value as **raw regex source** (`interpP5Pattern`), not as the
quotemeta'd literal of `interpRegexPattern` — `my $r = '\d+'` really compiles
the `\d+`. Rakudo fudges its own interpolation tests as not-yet-implemented;
rakupp passes them.

The payoff of the front-end-only shape is that everything downstream is
shared: `rx:P5` values flow through `.match`, `.split`, `.comb` and `.subst`
untouched, `:g` and `:nth` and substitution plumbing never learn that the
pattern was foreign, and the corpus exercises the one backtracking matcher —
918 assertions of perl's own regression suite running against the same
`matchNode` that grammars use.
