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
int uniCollate(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b); // UCA (DUCET) three-way compare
int32_t uniCharByName(const std::string& name);            // name -> codepoint, or -1
std::string uniNameOf(uint32_t cp);                        // codepoint -> name, or ""
bool uniNumValue(uint32_t cp, long long& num, long long& den); // numeric value as num/den
std::string uniGeneralCategory(uint32_t cp);                   // 2-letter general category ("Nd", "Lu", "Cn"…)
std::string uniScript(uint32_t cp);                            // approximate script ("Latin", "Greek"…)
bool uniMatchesProp(uint32_t cp, const std::string& prop);     // regex <:Prop> / char-property test
}
