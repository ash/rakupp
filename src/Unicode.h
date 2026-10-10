#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include <string>

namespace rakupp {
// Unicode normalization over codepoint sequences.
// mode: 0=NFD, 1=NFC, 2=NFKD, 3=NFKC
std::vector<uint32_t> uniNormalize(const std::vector<uint32_t>& cps, int mode);
int uniCombiningClass(uint32_t cp);
size_t uniGraphemeCount(const std::vector<uint32_t>& cps); // UAX #29 grapheme cluster count
std::vector<size_t> uniGraphemeStarts(const std::vector<uint32_t>& cps); // cluster start indices (front()==0)
// The same, for `cps = utf8cp(src)`: a byte of `src` that starts no well-formed
// sequence — a UTF8-C8 synthetic, which utf8cp passes through as its own value —
// is a cluster of its own, the way a Control is (it never combines, and nothing
// combines with it). Pass the string whenever the codepoints came from one.
size_t uniGraphemeCount(const std::vector<uint32_t>& cps, const std::string& src);
std::vector<size_t> uniGraphemeStarts(const std::vector<uint32_t>& cps, const std::string& src);
bool uniGraphemeLeadIsOdd(uint32_t cp);   // Extend / ZWJ / SpacingMark / Prepend: `.raku` writes such a cluster as codepoints
// Raku string indices are GRAPHEME indices, but a decoded string is a vector of
// CODEPOINTS, and the two coincide only while every cluster is one codepoint long.
// GraphemeMap is the translation, and exists so that `substr`/`index`/`flip` do not
// each re-derive it — they used to skip it entirely and index codepoints, which is
// silently wrong for any text carrying a combining mark.
//
// The common case costs one linear scan and no allocation: a string with no
// codepoint above U+02FF and no CR cannot cluster, so indices are identical and
// `trivial()` is true. Only a string that can actually cluster pays the full
// UAX #29 walk.
class GraphemeMap {
public:
    explicit GraphemeMap(const std::vector<uint32_t>& cps);
    GraphemeMap(const std::vector<uint32_t>& cps, const std::string& src); // cps = utf8cp(src)
    bool trivial() const { return starts_.empty(); }
    size_t count() const { return starts_.empty() ? ncps_ : starts_.size(); } // graphemes
    // codepoint index where grapheme `g` starts; count() maps to the end of the string
    size_t cpAt(size_t g) const {
        if (starts_.empty()) return g < ncps_ ? g : ncps_;
        return g < starts_.size() ? starts_[g] : ncps_;
    }
    size_t graphemeAt(size_t cp) const; // grapheme index containing codepoint `cp`
private:
    size_t ncps_;
    std::vector<size_t> starts_; // empty when one grapheme == one codepoint
};
size_t uniClusterEndUtf8(const std::string& s, size_t pos, size_t len);  // byte end of the grapheme cluster at `pos`
size_t uniGraphemeCountUtf8(const char* s, size_t len);   // clusters in s[0, len), the walk starting fresh at s[0]
bool uniGraphemeBreakCertain(uint32_t prev, uint32_t cur); // a cluster boundary between them, whatever precedes `prev`
// Joining two NFC strings. A codepoint is an NFC boundary when nothing before it
// can compose or reorder with it, so normalization never reaches across it.
bool uniNfcBoundaryBefore(uint32_t cp);
// Is this UTF-8 NFC, as uniNormalize sees it? 1 yes, 0 no, -1 cannot tell
// without normalizing (it holds a codepoint that composes with what precedes).
int uniNfcQuickCheckUtf8(const char* s, size_t n);
// How NFC(a ~ b) differs from the bytes of a then b, for a and b each NFC:
// unchanged, or a[0, i) ~ mid ~ b[j, nb).
struct UniNfcJoin {
    bool changed = false;
    size_t i = 0, j = 0;
    std::string mid;
};
UniNfcJoin uniNfcJoin(const char* a, size_t na, const char* b, size_t nb);
// Counting the result's graphemes from a's count, for the join J of a and b:
// count(result) = count(a) - tailA + uniGraphemeCountUtf8(result + p, …). `ok`
// is false when no restart point is near enough to find.
struct UniGraphemeJoin {
    bool ok = false;
    size_t p = 0, tailA = 0;
};
UniGraphemeJoin uniGraphemeJoin(const char* a, size_t na, const UniNfcJoin& J, const char* b, size_t nb);
int uniCollate(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b); // UCA (DUCET) three-way compare
int uniCollateLevels(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, const int lv[4]); // …per Collation level
int32_t uniCharByName(const std::string& name);            // name -> codepoint, or -1
std::string uniSeqByName(const std::string& name);          // NAMED SEQUENCE -> its UTF-8, or "" (any case)
std::string uniNameOf(uint32_t cp);                        // codepoint -> name, or ""
bool uniNumValue(uint32_t cp, long long& num, long long& den);
// probe form: false instead of throwing when --slim cut the table (lexer use)
bool uniNumValueQuiet(uint32_t cp, long long& num, long long& den); // numeric value as num/den
int uniDigitValue(uint32_t cp); // Nd decimal digit 0-9, -1 otherwise (never-cut table)
std::string uniGeneralCategory(uint32_t cp);                   // 2-letter general category ("Nd", "Lu", "Cn"…)
std::string uniScript(uint32_t cp);                            // approximate script ("Latin", "Greek"…)
bool uniMatchesProp(uint32_t cp, const std::string& prop);     // regex <:Prop> / char-property test
bool uniPropNeedsCutTables(const std::string& prop); // SLIM scan: would <:prop> reach the cuttable tables?
std::string uniBidiClassOf(uint32_t cp);                       // Bidi_Class short name ("L", "AL", …)
// Case mapping (UnicodeData simple + SpecialCasing full + CaseFolding).
uint32_t uniSimpleUpper(uint32_t cp);   // 1:1 mapping, or cp unchanged
uint32_t uniSimpleLower(uint32_t cp);
uint32_t uniSimpleTitle(uint32_t cp);
// full 1:N mapping — kind: 0=lower, 1=upper, 2=title, 3=fold. Always >= 1 codepoint.
std::vector<uint32_t> uniCaseMap(uint32_t cp, int kind);
// enumerated property value name, or "" if `prop` is not a handled enum property.
std::string uniEnumProp(const std::string& prop, uint32_t cp);
std::string uniUnicode1Name(uint32_t cp);      // Unicode 1.0 name ("" when none)
std::string uniCombiningClassName(uint32_t cp); // ccc by alias, e.g. "Not_Reordered"
std::string uniJamoShortName(uint32_t cp);      // Hangul jamo short name ("" when none)
uint32_t    uniBidiPairedBracket(uint32_t cp);  // paired bracket (the cp itself when none)
std::string uniBidiPairedBracketType(uint32_t cp); // "o", "c" or "n"
int32_t uniBidiMirror(uint32_t cp);  // Bidi_Mirroring_Glyph target codepoint, or -1
int uniBinaryProp(uint32_t cp, const std::string& prop); // 1/0 for a known binary prop, -1 if unknown
std::string uniBlockOf(uint32_t cp);                           // block name ("Basic Latin", …)

// A Unicode QUOTE opening at s[i] — ‘…’ / ‚…’, “…” / „…”, ｢…｣ (either closer
// of its family) — and the index just past its closer; 0 when s[i] opens none
// or the quote never closes. Raku code quotes with these as well, so a scanner
// stepping over code (a `{ }` block of a string or a pattern) steps over them:
// an ASCII quote or a brace inside one is text (`{ “doesn't” }`).
inline size_t uniQuoteSpanEnd(const std::string& s, size_t i) {
    if (i + 2 >= s.size()) return 0;
    const unsigned char a = (unsigned char)s[i], b = (unsigned char)s[i + 1], c = (unsigned char)s[i + 2];
    const char* cl1; const char* cl2 = nullptr;
    if (a == 0xE2 && b == 0x80 && (c == 0x98 || c == 0x9A)) { cl1 = "\xE2\x80\x99"; cl2 = "\xE2\x80\x98"; }
    else if (a == 0xE2 && b == 0x80 && (c == 0x9C || c == 0x9E)) { cl1 = "\xE2\x80\x9D"; cl2 = "\xE2\x80\x9C"; }
    else if (a == 0xEF && b == 0xBD && c == 0xA2) cl1 = "\xEF\xBD\xA3";
    else return 0;
    size_t end = s.find(cl1, i + 3);
    if (cl2) end = std::min(end, s.find(cl2, i + 3));
    return end == std::string::npos ? 0 : end + 3;
}
}
