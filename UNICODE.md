# Unicode in Raku++

Raku is one of the most Unicode-capable languages there is — grapheme-based
strings, normalization, collation, and character introspection are all part of
the language, and Roast tests them hard (S15 alone is ~91k assertions). This
document describes how Raku++ implements that: what works, where the data
comes from, and what is still missing.

**Measured standing (S15, Unicode / strings / NFG):** 69 of 82 files fully
pass, 91,201 / 91,242 reached assertions (99.96%). The UCA collation
conformance suite (S32-str, 8,271 tests) passes with zero real failures.

## The five subsystems

### 1. Grapheme clusters (UAX #29)

Raku strings are sequences of *graphemes*, not codepoints: `"e\x[301]"` (e +
combining acute) is one character, and so is `👨‍👩‍👧` (a ZWJ sequence).
`src/Unicode.cpp` implements the full UAX #29 extended-grapheme-cluster rules:

- `.chars`, `.comb`, `.substr`, `.flip`, `.uc`/`.lc`/`.tc` count and segment
  by cluster (`uniGraphemeStarts()` is the single shared segmentation routine).
- All rules GB1–GB999 including Hangul syllables (GB6–8), emoji ZWJ sequences
  (GB11), regional-indicator pairs (GB12/13), and **GB9c** — Indic conjunct
  breaks (Devanagari विराम chains; Unicode 17 extends this to Myanmar, Khmer
  and Balinese).
- Break classes come from the real `GraphemeBreakProperty.txt` +
  `emoji-data.txt` (Extended_Pictographic), not a general-category
  approximation — this is what gets skin-tone modifiers (category Sk but
  break-class Extend) and ZWNJ (Cf but Extend) right.
- Roast: `GraphemeBreakTest-{0..3}.t` (Unicode's own break-test data) and
  `emoji-test.t` (3,825 emoji sequences) fully pass.

### 2. Normalization (NFC / NFD / NFKC / NFKD)

`uniNormalize()` implements the full canonical/compatibility decomposition,
canonical reordering, and composition (with exclusions) pipeline. `Uni`,
`NFC`, `NFD`, `NFKC`, `NFKD` are real types; `Uni.new(...).Str` normalizes to
NFC, matching Raku's NFG rule that canonically-equivalent codepoint sequences
yield the same `Str`.

The tables (CCC, canonical/compat decompositions, composition pairs) are
generated **directly from `UnicodeData.txt` 17.0** by
`tools/gen_unicode_norm.py` — not from a host language's Unicode library,
whose data typically lags by several versions. Roast: all
`nf{c,d,kc,kd}-*.t` files and `mass-equality.t` (500 canonical-equivalence
cases) fully pass.

### 3. Collation (UCA / DUCET)

The infix operators `unicmp` and `coll` implement the Unicode Collation
Algorithm (UTS #10) with the DUCET 17.0 table (`allkeys.txt`: 38,785
single-codepoint entries, 964 contractions, 45,860 collation elements):

- NFD input, three-level sort keys (base letter → accents → case).
- Longest-first contiguous contraction matching (a 3-codepoint contraction's
  2-codepoint prefix need not itself be an entry — Tibetan `0FB2 0F71 0F80`).
- **Discontiguous matching** (S2.1.2/S2.1.3): canonical reordering can split a
  contraction (`0DDA 0334` normalizes to `0DD9 0334 0DCA`); an unblocked
  non-starter that extends the match is consumed out of the stream.
- Implicit weights for Han (via the real `Unified_Ideograph` property, with
  the core-block/extension split) and the fixed Tangut/Nushu/Khitan bases.
- Equal keys tie-break by codepoint order, per the conformance-test rule.

Roast: all four `CollationTest_NON_IGNORABLE-*.t` files (8,271 adjacent-pair
comparisons from Unicode's published conformance data) pass with zero real
failures. This is the *untailored* default table: no locale tailorings (CLDR),
so `"ö" unicmp "z"` gives the language-neutral answer, not the Swedish one.
(Rakudo does not do tailorings either.)

### 4. Character knowledge

- `uniname`/`.uniname` and `\c[NAME]` — names in both directions, including
  control-character aliases from `NameAliases.txt`; out-of-range/unassigned
  codepoints answer `<unassigned>`.
- `unival`/`univals` — numeric values as exact `Rat`s (`"½".unival` is the Rat `0.5`, `"↉".unival` is `0`).
- `uniprop` — general category, script.
- Regex property classes: `<:Lu>`, `<:Latin>`, `<:Script<Greek>>`,
  `<:bc<L>>` (bidi class), `<:InBasicLatin>` (blocks), and the binary
  properties from `PropList.txt`/`DerivedCoreProperties.txt`
  (`<:Math>`, `<:Soft_Dotted>`, …).

### 5. Case and encodings

- `.uc`/`.lc`/`.tc`/`.fc` with the common special cases (`ß.uc` is `SS`,
  final sigma) — see gaps below for the folding tail.
- `.encode`/`.decode` for utf8 (plus the gb2312/gb18030/shiftjis legacy
  encodings exercised by Roast), `Buf`/`Blob` round-trips.

## The data pipeline

All tables are generated into checked-in C++ sources; the raw UCD/UCA files
live in `tools/ucd/` so regeneration is reproducible and offline:

| Generator | Output | Source data | Version |
|---|---|---|---|
| `tools/gen_unicode_gb.py` | `src/unicode_gb_gen.cpp` | GraphemeBreakProperty, emoji-data, DerivedCoreProperties (InCB) | **17.0** |
| `tools/gen_unicode_norm.py` | `src/unicode_norm_gen.cpp` | UnicodeData, DerivedNormalizationProps | **17.0** |
| `tools/gen_unicode_coll.py` | `src/unicode_coll_gen.cpp` | allkeys.txt (DUCET) | **17.0** |
| `tools/gen_unicode.py` | `src/unicode_gen.cpp`, `src/unicode_names.cpp` | UnicodeData (names, categories, numeric values) | 16.0 |
| `tools/gen_unicode_props.py` etc. | `src/unicode_{props,scripts,blocks,bidi}_gen.cpp` | PropList, DerivedCoreProperties, Scripts, Blocks, DerivedBidiClass | 16.0 |

The grapheme, normalization and collation data track **Unicode 17.0** because
that is what Roast's generated test files (GraphemeBreakTest, emoji-test,
CollationTest) assert against. The remaining tables are pinned at 16.0; the
differences are additive (new characters), so mixing versions is safe in
practice — upgrading the rest to 17.0 is routine work when needed.

## Known gaps

Stated plainly, per the failing Roast tail (~41 assertions in S15 plus
scattered S32-str cases):

- **Case folding tail** — `"ß".fc` does not fold to `ss`; `:ignorecase` /
  `:ignoremark` on `contains`/`starts-with`/`index` handle ASCII but miss
  some non-ASCII foldings (`"FOÖ"`). `samemark` is not implemented.
- **`.collate` / `.sort`** do not route through the UCA yet — only the
  `unicmp`/`coll` infixes do.
- **NFG synthetic-codepoint corners** — `case-change.t`, `concatenation.t`
  and `crlf-encoding.t` still lose a few tests each (e.g. case-changing a
  string does not re-segment every synthetic cluster).
- **No locale tailorings** (CLDR) for collation, as noted above.
- `long-uni`-style stress cases pass, but `Uni.t` has a small tail
  (13/15) around `Uni` list semantics.
