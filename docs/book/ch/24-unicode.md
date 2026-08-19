\part{Unicode}

# Graphemes, Normalization, Collation

Raku specifies strings at the level of **graphemes** — what a reader would call
a character — not codepoints and certainly not bytes. `"e\c[COMBINING ACUTE
ACCENT]".chars` is 1. Sorting respects the Unicode Collation Algorithm.
`"\c[LATIN SMALL LETTER E WITH ACUTE]".uniname` answers a real name from the
character database.

None of that is optional, and Raku++ implements all of it from the pinned
Unicode 17.0 data with no external library.

## Storage: UTF-8 bytes, grapheme semantics

A `Str` holds UTF-8 bytes in its `CowStr`. Raku's *indices* are grapheme
indices. Those two facts have to be reconciled on every string operation, and
how that reconciliation is made cheap is the most consequential engineering
decision in this part of the system.

Strings are normalised to **NFC** on the way in — Raku's NFG storage model — so
a literal is normalised once, in place, on first evaluation:

```cpp
// src/Ast.h — StrLit
bool nfcDone = false;   // NFC-normalized in place on first eval
```

## The fast answer: when a byte index is a grapheme index

Most strings in most programs are ASCII. For those, byte index, codepoint index
and grapheme index are all the same number, and every conversion is a no-op —
if you can establish it cheaply.

```cpp
// src/BuiltinsShared.h
size_t asciiRun(const std::string& s, size_t limit);
bool allAscii(const std::string& s);
bool byteIsGraphemeIndex(const std::string& s);
bool cowAllAscii(const CowStr& s);
bool cowByteIsGraphemeIndex(const CowStr& s);
long long cowGraphemeCount(const CowStr& s);
```

The `cow*` forms are the memoised ones. They read the flags cached on the
promoted string body (Chapter 9), so the answer is computed once per string
rather than once per character examined.

The condition is narrow and exact: **all bytes below 0x80, and no carriage
return.** CR LF is the one ASCII sequence that forms a single grapheme cluster
under UAX #29, so an ASCII string containing a CR can still cluster.

Getting this wrong is not a correctness bug — it is a *complexity* bug. The
scanning operations call these once per character examined, so the difference
between memoised and not is the difference between a linear tokenizer and a
quadratic one.

## The slow answer: `GraphemeMap`

When a string can genuinely cluster, the translation between codepoint indices
and grapheme indices is built explicitly:

```cpp
// src/Unicode.h
class GraphemeMap {
public:
    explicit GraphemeMap(const std::vector<uint32_t>& cps);
    bool trivial() const { return starts_.empty(); }
    size_t count() const;                  // graphemes
    size_t cpAt(size_t g) const;           // codepoint index where g starts
    size_t graphemeAt(size_t cp) const;    // grapheme containing codepoint cp
private:
    size_t ncps_;
    std::vector<size_t> starts_;           // empty when 1 grapheme == 1 cp
};
```

The class exists because `substr`, `index` and `flip` each used to re-derive the
translation — or, worse, skip it entirely and index codepoints, which is silently
wrong for any text carrying a combining mark.

The common case costs one linear scan and no allocation: a string with no
codepoint above U+02FF and no CR cannot cluster, so `starts_` stays empty and
`trivial()` is true. Only a string that can actually cluster pays the full
UAX #29 walk.

## The subsystems

```cpp
// src/Unicode.h — the interface, abridged
std::vector<uint32_t> uniNormalize(const std::vector<uint32_t>&, int mode);
size_t uniGraphemeCount(const std::vector<uint32_t>& cps);
std::vector<size_t> uniGraphemeStarts(const std::vector<uint32_t>& cps);
size_t uniClusterEndUtf8(const std::string& s, size_t pos, size_t len);
int uniCollate(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b);
int32_t uniCharByName(const std::string& name);
std::string uniNameOf(uint32_t cp);
bool uniNumValue(uint32_t cp, long long& num, long long& den);
std::string uniGeneralCategory(uint32_t cp);
std::string uniScript(uint32_t cp);
bool uniMatchesProp(uint32_t cp, const std::string& prop);
uint32_t uniSimpleUpper(uint32_t cp);
std::vector<uint32_t> uniCaseMap(uint32_t cp, int kind); // 0lo 1up 2ti 3fold
std::string uniBlockOf(uint32_t cp);
int uniBinaryProp(uint32_t cp, const std::string& prop);
```

**Normalization** implements all four forms — NFD, NFC, NFKD, NFKC — over
codepoint sequences: canonical decomposition, canonical ordering by combining
class, and canonical composition.

**Grapheme segmentation** is UAX #29 extended grapheme clusters, which is more
than "base plus combining marks": it covers Hangul syllable composition, regional
indicator pairs (flag emoji), emoji modifier sequences and zero-width joiner
sequences, and the CR LF rule.

**Collation** is the Unicode Collation Algorithm against the default table,
producing a three-way comparison. That is what `coll` and `.collate` use, and it
is why sorting Raku strings is not `strcmp`.

**Names** go both ways: name to codepoint for `\c[NAME]`, codepoint to name for
`.uniname`. This is the single largest file in the tree at 41,174 lines, and it
is a binary search plus a hash lookup at run time.

**Properties** cover general category, script, block, numeric value, bidi class,
mirroring, and the binary and enumerated properties. `uniMatchesProp` is what
backs `<:Nd>` and `<:L>` inside a regex character class — which is why a regex
class with a Unicode property in it is a `Class` node with a `uprop` string
rather than a byteset (Chapter 20).

**Case mapping** has two tiers: simple 1:1 mappings from `UnicodeData`, and full
1:N mappings from `SpecialCasing` and `CaseFolding`. The second is not optional:
uppercasing the German sharp s produces two characters, and Turkish dotted and
dotless i are language-sensitive.

## The tables are generated, and one generator is written in Raku

```
tools/ucd/                 pinned UCD + UCA 17.0 data files
tools/gen_unicode_*.py     table generators
tools/gen-unicode.raku     …and the ones written in Raku
src/unicode_*_gen.cpp      the output: 79,400 lines
```

The tables are checked in rather than generated at build time, which keeps the
build free of a data dependency and makes the Unicode version a reviewable part
of the source tree.

Two of the generators — the names and one of the property tables — are written
in **Raku and run by rakupp itself**. That is not a stunt. It is a load-bearing
test: generating a 41,000-line table exercises string handling, hashing, sorting
and file I/O at a scale no unit test reaches, and a bug in any of them shows up
as a table that does not compile or does not match.

It is also the project's standing rule for tooling: build the ecosystem tools in
Raku, run them with rakupp, and let the compiler eat its own output.

## Where Unicode shows up elsewhere

**In the lexer.** Identifiers may contain Unicode letters and combining marks,
so `consumeIdentChars` decodes codepoints rather than reading bytes. Superscript
digit runs fold into a `**` operator.

**In the regex engine.** A character class carries three parallel
representations for three ranges of complexity: a 256-bit byteset for bytes,
codepoint ranges for anything above 255, and a list of whole cluster strings for
multi-codepoint grapheme members. `:i` folds through `uniCaseMap`; `:m`
(ignoremark) compares NFD first-starter base codepoints and consumes the rest of
the cluster.

**In the longest-token automaton.** Its transition predicates reuse the regex
`Class` node's data rather than reimplementing any of this — the single most
important structural decision in Chapter 23, because two Unicode
implementations that disagreed would produce rankings that could not be
debugged.

**In collation-sensitive built-ins.** `cmp`, `leg`, `unicmp`, `coll`, `.sort`
and the ordering operators each pick their comparison from this layer.

## Honest limitations

- **Language-sensitive casing is not exposed.** The full case mappings are
  implemented, but there is no locale parameter, so the Turkish `i` rules are
  not reachable.
- **Collation is the default table**, with no tailoring and no locale.
- **Normalization is by codepoint sequence**, so a caller that has bytes pays a
  decode and an encode around it.
- **The Unicode version is pinned in the source tree**, so updating it is a
  regeneration and a review rather than a dependency bump — deliberate, but it
  does mean the tables age.
