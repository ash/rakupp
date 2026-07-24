#pragma once
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
size_t uniClusterEndUtf8(const std::string& s, size_t pos, size_t len);  // byte end of the grapheme cluster at `pos`
int uniCollate(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b); // UCA (DUCET) three-way compare
int32_t uniCharByName(const std::string& name);            // name -> codepoint, or -1
std::string uniNameOf(uint32_t cp);                        // codepoint -> name, or ""
bool uniNumValue(uint32_t cp, long long& num, long long& den); // numeric value as num/den
std::string uniGeneralCategory(uint32_t cp);                   // 2-letter general category ("Nd", "Lu", "Cn"…)
std::string uniScript(uint32_t cp);                            // approximate script ("Latin", "Greek"…)
bool uniMatchesProp(uint32_t cp, const std::string& prop);     // regex <:Prop> / char-property test
std::string uniBidiClassOf(uint32_t cp);                       // Bidi_Class short name ("L", "AL", …)
// Case mapping (UnicodeData simple + SpecialCasing full + CaseFolding).
uint32_t uniSimpleUpper(uint32_t cp);   // 1:1 mapping, or cp unchanged
uint32_t uniSimpleLower(uint32_t cp);
uint32_t uniSimpleTitle(uint32_t cp);
// full 1:N mapping — kind: 0=lower, 1=upper, 2=title, 3=fold. Always >= 1 codepoint.
std::vector<uint32_t> uniCaseMap(uint32_t cp, int kind);
// enumerated property value name, or "" if `prop` is not a handled enum property.
std::string uniEnumProp(const std::string& prop, uint32_t cp);
int32_t uniBidiMirror(uint32_t cp);  // Bidi_Mirroring_Glyph target codepoint, or -1
int uniBinaryProp(uint32_t cp, const std::string& prop); // 1/0 for a known binary prop, -1 if unknown
std::string uniBlockOf(uint32_t cp);                           // block name ("Basic Latin", …)
}
