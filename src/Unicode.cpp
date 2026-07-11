#include "Unicode.h"
#include <cstdint>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <vector>
#include <utility>
#include <set>

namespace rakupp {
namespace ucd {
extern const uint32_t CCC[];    extern const size_t CCC_N;
extern const uint32_t CANON[];  extern const size_t CANON_N;
extern const uint32_t KOMPAT[]; extern const size_t KOMPAT_N;
extern const uint32_t COMP[];   extern const size_t COMP_N;
struct NameEnt { const char* name; uint32_t cp; };
extern const NameEnt NAMES[];   extern const size_t NAMES_N;
extern const int64_t NUMV[];    extern const size_t NUMV_N;
extern const uint32_t GBPROP[]; extern const size_t GBPROP_N;
extern const uint32_t GBRANGE[]; extern const size_t GBRANGE_N; // real UAX#29 classes + ExtPict, as ranges
extern const uint32_t INCB[]; extern const size_t INCB_N; // Indic_Conjunct_Break (rule GB9c)
extern const char* const CATNAMES[]; extern const uint32_t GCAT[]; extern const size_t GCAT_N;
struct BlockEnt { uint32_t lo, hi; const char* name; };
extern const BlockEnt BLOCKS[]; extern const size_t BLOCKS_N; // Unicode Blocks.txt
extern const char* const PROPNAMES[]; extern const size_t PROPNAMES_N;
struct PropRange { uint32_t lo, hi; uint16_t prop; };
extern const PropRange BINPROPS[]; extern const size_t BINPROPS_N; // DerivedCoreProperties + PropList
struct ScriptEnt { uint32_t lo, hi; const char* name; };
extern const ScriptEnt SCRIPTS[]; extern const size_t SCRIPTS_N; // Scripts.txt
struct BidiEnt { uint32_t lo, hi; const char* bc; };
extern const BidiEnt BIDI[]; extern const size_t BIDI_N; // DerivedBidiClass.txt
}

// Bidi_Class of cp ("L", "EN", "WS", …); default "L" in the assigned ranges' gaps.
static const char* uniBidiClass(uint32_t c) {
    size_t lo = 0, hi = ucd::BIDI_N;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < ucd::BIDI[mid].lo) hi = mid;
        else if (c > ucd::BIDI[mid].hi) lo = mid + 1;
        else return ucd::BIDI[mid].bc;
    }
    return "L";
}

// Binary Unicode property membership (Math, Lowercase, Soft_Dotted, Other_Math, …).
// `norm` is the already-normalized (lowercase, separator-stripped) property name.
// Returns 1 = has, 0 = hasn't, -1 = not a known binary property.
static int uniBinProp(uint32_t cp, const std::string& norm) {
    static std::unordered_map<std::string, std::vector<std::pair<uint32_t, uint32_t>>> M = [] {
        std::unordered_map<std::string, std::vector<std::pair<uint32_t, uint32_t>>> m;
        for (size_t i = 0; i < ucd::BINPROPS_N; i++)
            m[ucd::PROPNAMES[ucd::BINPROPS[i].prop]].push_back({ucd::BINPROPS[i].lo, ucd::BINPROPS[i].hi});
        return m;
    }();
    auto it = M.find(norm);
    if (it == M.end()) return -1;
    const auto& v = it->second;
    size_t lo = 0, hi = v.size();               // ranges are emitted sorted by lo
    while (lo < hi) { size_t mid = (lo + hi) / 2;
        if (cp < v[mid].first) hi = mid; else if (cp > v[mid].second) lo = mid + 1; else return 1; }
    return 0;
}

// `<:InBlockName>` block property: normalized (lowercase, alnum-only) name of the
// block containing cp, "" if none (an unassigned gap between blocks).
static const char* uniBlockName(uint32_t cp) {
    size_t lo = 0, hi = ucd::BLOCKS_N;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < ucd::BLOCKS[mid].lo) hi = mid;
        else if (cp > ucd::BLOCKS[mid].hi) lo = mid + 1;
        else return ucd::BLOCKS[mid].name;
    }
    return "";
}

std::string uniGeneralCategory(uint32_t cp) {
    size_t lo = 0, hi = ucd::GCAT_N / 3;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint32_t s = ucd::GCAT[mid * 3], e = ucd::GCAT[mid * 3 + 1];
        if (cp < s) hi = mid; else if (cp > e) lo = mid + 1;
        else return ucd::CATNAMES[ucd::GCAT[mid * 3 + 2]];
    }
    return "Cn"; // unassigned
}

// Real Script property, from the pinned 16.0 Scripts.txt range table.
std::string uniScript(uint32_t c) {
    size_t lo = 0, hi = ucd::SCRIPTS_N;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < ucd::SCRIPTS[mid].lo) hi = mid;
        else if (c > ucd::SCRIPTS[mid].hi) lo = mid + 1;
        else return ucd::SCRIPTS[mid].name;
    }
    return "Unknown"; // unassigned / no script (Zzzz)
}

// normalize a property name/value for loose matching (lowercase, drop separators)
static std::string normProp(const std::string& s) {
    std::string n;
    for (char ch : s) if (std::isalnum((unsigned char)ch)) n += (char)std::tolower((unsigned char)ch);
    return n;
}

bool uniMatchesProp(uint32_t cp, const std::string& p) {
    // property-with-value form: <:bc<L>> (Bidi_Class), <:sc<Latin>>/<:Script<…>>,
    // <:gc<Lu>> (General_Category).  p arrives as e.g. "bc<L>".
    size_t lt = p.find('<');
    if (lt != std::string::npos && !p.empty() && p.back() == '>') {
        std::string prop = normProp(p.substr(0, lt));
        std::string val = p.substr(lt + 1, p.size() - lt - 2);
        if (prop == "bc" || prop == "bidiclass")
            return normProp(uniBidiClass(cp)) == normProp(val);
        if (prop == "sc" || prop == "script" || prop == "gc" || prop == "generalcategory")
            return uniMatchesProp(cp, val); // delegate to the bare-value handlers
        return uniMatchesProp(cp, val);     // unknown property key: match on the value alone
    }
    // script property: <:Latin> <:Syriac> <:Canadian_Aboriginal> … (bare script
    // value == <:Script<...>>). Loose-match p against the set of real script names.
    {
        static const std::set<std::string> scriptNames = [] {
            std::set<std::string> s;
            for (size_t i = 0; i < ucd::SCRIPTS_N; i++) {
                std::string n;
                for (const char* q = ucd::SCRIPTS[i].name; *q; q++) if (std::isalnum((unsigned char)*q)) n += (char)std::tolower((unsigned char)*q);
                s.insert(n);
            }
            return s;
        }();
        std::string norm;
        for (char ch : p) if (std::isalnum((unsigned char)ch)) norm += (char)std::tolower((unsigned char)ch);
        if (scriptNames.count(norm)) {
            std::string sc = uniScript(cp), scn;
            for (char ch : sc) if (std::isalnum((unsigned char)ch)) scn += (char)std::tolower((unsigned char)ch);
            return scn == norm;
        }
    }
    std::string cat = uniGeneralCategory(cp);
    if (p == "alpha" || p == "Alpha" || p == "Letter" || p == "L") return cat[0] == 'L'; // POSIX-ish; `Alphabetic` is the binary prop (L + Other_Alphabetic), handled below
    if (p == "Assigned") return cat != "Cn";
    if (p == "digit" || p == "Nd" || p == "decimal") return cat == "Nd";
    if (p == "alnum" || p == "Alnum") return cat[0] == 'L' || cat[0] == 'N';
    if (p == "space" || p == "Space" || p == "White_Space" || p == "blank" || p == "ws")
        return cat[0] == 'Z' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x0B || cp == 0x0C || cp == 0x85;
    if (p == "upper" || p == "Upper" || p == "Uppercase" || p == "Lu") return cat == "Lu";
    if (p == "lower" || p == "Lower" || p == "Lowercase" || p == "Ll") return cat == "Ll";
    if (p == "punct" || p == "Punct" || p == "Punctuation" || p == "P") return cat[0] == 'P';
    if (p == "word" || p == "Word") return cat[0] == 'L' || cat[0] == 'N' || cat == "Pc" || cp == '_';
    if (p == "cntrl" || p == "Control" || p == "Cc") return cat == "Cc";
    if (p == "N" || p == "Number" || p == "Numeric") return cat[0] == 'N';
    if (p == "M" || p == "Mark") return cat[0] == 'M';
    if (p == "S" || p == "Symbol") return cat[0] == 'S';
    if (p == "Z" || p == "Separator") return cat[0] == 'Z';
    if (p == "C" || p == "Other") return cat[0] == 'C';
    if (p == "LC" || p == "CasedLetter" || p == "Cased_Letter") return cat == "Ll" || cat == "Lu" || cat == "Lt";
    if (p.size() == 1) return cat[0] == p[0];   // single-letter category group
    // known 2-letter general category → exact check
    static const char* K[] = {"Lu","Ll","Lt","Lm","Lo","Mn","Mc","Me","Nd","Nl","No","Pc","Pd",
        "Ps","Pe","Pi","Pf","Po","Sm","Sc","Sk","So","Zs","Zl","Zp","Cc","Cf","Cs","Co","Cn"};
    for (auto* k : K) if (p == k) return cat == p;
    // long general-category names: <:UppercaseLetter> == <:Lu> == <:!Cn> …
    static const std::pair<const char*, const char*> LONG[] = {
        {"UppercaseLetter","Lu"},{"LowercaseLetter","Ll"},{"TitlecaseLetter","Lt"},
        {"ModifierLetter","Lm"},{"OtherLetter","Lo"},{"NonspacingMark","Mn"},
        {"SpacingMark","Mc"},{"EnclosingMark","Me"},{"DecimalNumber","Nd"},
        {"LetterNumber","Nl"},{"OtherNumber","No"},{"ConnectorPunctuation","Pc"},
        {"DashPunctuation","Pd"},{"OpenPunctuation","Ps"},{"ClosePunctuation","Pe"},
        {"InitialPunctuation","Pi"},{"FinalPunctuation","Pf"},{"OtherPunctuation","Po"},
        {"MathSymbol","Sm"},{"CurrencySymbol","Sc"},{"ModifierSymbol","Sk"},
        {"OtherSymbol","So"},{"SpaceSeparator","Zs"},{"LineSeparator","Zl"},
        {"ParagraphSeparator","Zp"},{"Control","Cc"},{"Format","Cf"},
        {"Surrogate","Cs"},{"PrivateUse","Co"},{"Unassigned","Cn"},
    };
    for (auto& lp : LONG) if (p == lp.first) return cat == lp.second;
    // binary Unicode property (Math, Soft_Dotted, White_Space, Other_Math, …)
    {
        std::string norm;
        for (char ch : p) if (std::isalnum((unsigned char)ch)) norm += (char)std::tolower((unsigned char)ch);
        int b = uniBinProp(cp, norm);
        if (b >= 0) return b == 1;
    }
    // block property `<:InArabic>` / `<:InLatin1Supplement>`: In-prefix + block name.
    if (p.size() > 2 && p[0] == 'I' && p[1] == 'n' && std::isupper((unsigned char)p[2])) {
        std::string q;
        for (char ch : p) if (std::isalnum((unsigned char)ch)) q += (char)std::tolower((unsigned char)ch);
        if (q.size() > 2 && q[0] == 'i' && q[1] == 'n') q = q.substr(2); // drop the In prefix
        // legacy block-name aliases renamed in later Unicode versions
        if (q == "cyrillicsupplementary") q = "cyrillicsupplement";
        return q == uniBlockName(cp);
    }
    return true; // unknown property (e.g. an unmodelled script): lenient match
}

// UAX #29 grapheme-break properties
enum GB { GB_Other, GB_CR, GB_LF, GB_Control, GB_Extend, GB_ZWJ, GB_RI, GB_Prepend,
          GB_SpacingMark, GB_L, GB_V, GB_T, GB_LV, GB_LVT, GB_ExtPict };

static int gbProp(uint32_t cp) {
    if (cp == 0x0D) return GB_CR;
    if (cp == 0x0A) return GB_LF;
    if (cp == 0x200D) return GB_ZWJ;
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF) return GB_RI;
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0xA960 && cp <= 0xA97C)) return GB_L;
    if ((cp >= 0x1160 && cp <= 0x11A7) || (cp >= 0xD7B0 && cp <= 0xD7C6)) return GB_V;
    if ((cp >= 0x11A8 && cp <= 0x11FF) || (cp >= 0xD7CB && cp <= 0xD7FB)) return GB_T;
    if (cp >= 0xAC00 && cp <= 0xD7A3) return ((cp - 0xAC00) % 28 == 0) ? GB_LV : GB_LVT;
    // Real UCD 16.0 data (unicode_gb_gen.cpp, from GraphemeBreakProperty.txt +
    // emoji-data.txt): (start, end, class) ranges — 1=Extend 2=SpacingMark
    // 3=Control 4=Prepend 5=Extended_Pictographic. This gets the cases a
    // general-category approximation misses: skin-tone modifiers (Sk but Extend),
    // ZWNJ (Cf but Extend), Prepend marks, and the exact ExtPict set for GB11.
    size_t lo = 0, hi = ucd::GBRANGE_N;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint32_t s = ucd::GBRANGE[mid * 3], e = ucd::GBRANGE[mid * 3 + 1];
        if (cp < s) hi = mid;
        else if (cp > e) lo = mid + 1;
        else switch (ucd::GBRANGE[mid * 3 + 2]) {
            case 1: return GB_Extend;
            case 2: return GB_SpacingMark;
            case 3: return GB_Control;
            case 4: return GB_Prepend;
            case 5: return GB_ExtPict;
            default: return GB_Other;
        }
    }
    return GB_Other;
}

// Indic_Conjunct_Break class: 0=None 1=Linker 2=Consonant 3=Extend (for GB9c)
static int incbProp(uint32_t cp) {
    size_t lo = 0, hi = ucd::INCB_N;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint32_t s = ucd::INCB[mid * 3], e = ucd::INCB[mid * 3 + 1];
        if (cp < s) hi = mid;
        else if (cp > e) lo = mid + 1;
        else return (int)ucd::INCB[mid * 3 + 2];
    }
    return 0;
}

// Indices (into cps) where a new grapheme cluster starts; front() is always 0.
std::vector<size_t> uniGraphemeStarts(const std::vector<uint32_t>& cps) {
    std::vector<size_t> starts;
    if (cps.empty()) return starts;
    starts.push_back(0);
    int prev = gbProp(cps[0]);
    bool pictSeq = (prev == GB_ExtPict);
    int riRun = (prev == GB_RI) ? 1 : 0;
    // GB9c conjunct state: 0=none, 1=InCB Consonant seen, 2=Consonant + Linker seen
    int incbState = (incbProp(cps[0]) == 2) ? 1 : 0;
    for (size_t i = 1; i < cps.size(); i++) {
        int cur = gbProp(cps[i]);
        int ip = incbProp(cps[i]);
        bool brk;
        if (prev == GB_CR && cur == GB_LF) brk = false;                                  // GB3
        else if (prev == GB_Control || prev == GB_CR || prev == GB_LF) brk = true;        // GB4
        else if (cur == GB_Control || cur == GB_CR || cur == GB_LF) brk = true;           // GB5
        else if (prev == GB_L && (cur == GB_L || cur == GB_V || cur == GB_LV || cur == GB_LVT)) brk = false; // GB6
        else if ((prev == GB_LV || prev == GB_V) && (cur == GB_V || cur == GB_T)) brk = false;               // GB7
        else if ((prev == GB_LVT || prev == GB_T) && cur == GB_T) brk = false;            // GB8
        else if (cur == GB_Extend || cur == GB_ZWJ) brk = false;                          // GB9
        else if (cur == GB_SpacingMark) brk = false;                                      // GB9a
        else if (prev == GB_Prepend) brk = false;                                         // GB9b
        else if (incbState == 2 && ip == 2) brk = false;                                  // GB9c (Indic conjuncts)
        else if (pictSeq && prev == GB_ZWJ && cur == GB_ExtPict) brk = false;             // GB11
        else if (prev == GB_RI && cur == GB_RI && (riRun % 2 == 1)) brk = false;          // GB12/13
        else brk = true;                                                                  // GB999
        if (brk) starts.push_back(i);
        riRun = (cur == GB_RI) ? (brk ? 1 : riRun + 1) : 0;
        if (cur == GB_ExtPict) pictSeq = true;
        else if (!brk && pictSeq && (cur == GB_Extend || cur == GB_ZWJ)) pictSeq = true;
        else pictSeq = false;
        // conjunct chain: a Consonant anchors, Linker upgrades, InCB-Extend carries
        if (brk) incbState = (ip == 2) ? 1 : 0;
        else if (ip == 2) incbState = 1;
        else if (incbState >= 1 && ip == 1) incbState = 2;
        else if (!(incbState >= 1 && ip == 3)) incbState = 0;
        prev = cur;
    }
    return starts;
}

size_t uniGraphemeCount(const std::vector<uint32_t>& cps) {
    return uniGraphemeStarts(cps).size();
}

// Hangul syllable constants (UAX #15) — composed/decomposed by arithmetic.
static const uint32_t SBase = 0xAC00, LBase = 0x1100, VBase = 0x1161, TBase = 0x11A7;
static const uint32_t LCount = 19, VCount = 21, TCount = 28, NCount = 588, SCount = 11172;

static const std::unordered_map<uint32_t, int>& cccMap() {
    static std::unordered_map<uint32_t, int> m = [] {
        std::unordered_map<uint32_t, int> t;
        for (size_t i = 0; i + 1 < ucd::CCC_N; i += 2) t[ucd::CCC[i]] = (int)ucd::CCC[i + 1];
        return t;
    }();
    return m;
}
static const std::unordered_map<uint32_t, std::vector<uint32_t>>& decompMap(bool compat) {
    auto build = [](const uint32_t* d, size_t n) {
        std::unordered_map<uint32_t, std::vector<uint32_t>> t;
        for (size_t i = 0; i < n;) { uint32_t cp = d[i++]; uint32_t len = d[i++];
            std::vector<uint32_t> v; for (uint32_t k = 0; k < len; k++) v.push_back(d[i++]); t[cp] = std::move(v); }
        return t;
    };
    static std::unordered_map<uint32_t, std::vector<uint32_t>> canon = build(ucd::CANON, ucd::CANON_N);
    static std::unordered_map<uint32_t, std::vector<uint32_t>> kompat = build(ucd::KOMPAT, ucd::KOMPAT_N);
    return compat ? kompat : canon;
}
static const std::unordered_map<uint64_t, uint32_t>& compMap() {
    static std::unordered_map<uint64_t, uint32_t> m = [] {
        std::unordered_map<uint64_t, uint32_t> t;
        for (size_t i = 0; i + 2 < ucd::COMP_N; i += 3)
            t[((uint64_t)ucd::COMP[i] << 21) | ucd::COMP[i + 1]] = ucd::COMP[i + 2];
        return t;
    }();
    return m;
}

int uniCombiningClass(uint32_t cp) { auto& m = cccMap(); auto it = m.find(cp); return it == m.end() ? 0 : it->second; }

static void decomposeCp(uint32_t cp, bool compat, std::vector<uint32_t>& out) {
    if (cp >= SBase && cp < SBase + SCount) { // Hangul
        uint32_t s = cp - SBase;
        out.push_back(LBase + s / NCount);
        out.push_back(VBase + (s % NCount) / TCount);
        uint32_t t = s % TCount; if (t) out.push_back(TBase + t);
        return;
    }
    auto& tbl = decompMap(compat);
    auto it = tbl.find(cp);
    if (it != tbl.end()) { for (uint32_t d : it->second) out.push_back(d); return; }
    out.push_back(cp);
}

static void canonicalOrder(std::vector<uint32_t>& s) {
    // stable reorder of non-starters by combining class (insertion sort, never past a starter)
    for (size_t i = 1; i < s.size(); i++) {
        int cc = uniCombiningClass(s[i]);
        if (cc == 0) continue;
        size_t j = i;
        while (j > 0) { int pc = uniCombiningClass(s[j - 1]); if (pc == 0 || pc <= cc) break; std::swap(s[j], s[j - 1]); j--; }
    }
}

static uint32_t composePair(uint32_t a, uint32_t b) {
    if (a >= LBase && a < LBase + LCount && b >= VBase && b < VBase + VCount) // L+V
        return SBase + ((a - LBase) * VCount + (b - VBase)) * TCount;
    if (a >= SBase && a < SBase + SCount && (a - SBase) % TCount == 0 && b > TBase && b < TBase + TCount) // LV+T
        return a + (b - TBase);
    auto& m = compMap();
    auto it = m.find(((uint64_t)a << 21) | b);
    return it == m.end() ? 0 : it->second;
}

int32_t uniCharByName(const std::string& name) {
    auto algo = [&](const char* pfx) -> int32_t {
        size_t pl = strlen(pfx);
        if (name.size() > pl && name.compare(0, pl, pfx) == 0)
            return (int32_t)strtol(name.c_str() + pl, nullptr, 16);
        return -1;
    };
    int32_t a;
    if ((a = algo("CJK UNIFIED IDEOGRAPH-")) >= 0) return a;
    if ((a = algo("CJK COMPATIBILITY IDEOGRAPH-")) >= 0) return a;
    size_t lo = 0, hi = ucd::NAMES_N;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(name.c_str(), ucd::NAMES[mid].name);
        if (c == 0) return (int32_t)ucd::NAMES[mid].cp;
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return -1;
}

std::string uniNameOf(uint32_t cp) {
    static const std::unordered_map<uint32_t, const char*> rev = [] {
        std::unordered_map<uint32_t, const char*> m;
        for (size_t i = 0; i < ucd::NAMES_N; i++) m[ucd::NAMES[i].cp] = ucd::NAMES[i].name;
        return m;
    }();
    auto it = rev.find(cp);
    if (it != rev.end()) return it->second;
    if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x20000 && cp <= 0x2A6DF) || (cp >= 0x2A700 && cp <= 0x2EBEF)) {
        char b[40]; std::snprintf(b, sizeof b, "CJK UNIFIED IDEOGRAPH-%04X", cp); return b;
    }
    return "";
}

bool uniNumValue(uint32_t cp, long long& num, long long& den) {
    size_t lo = 0, hi = ucd::NUMV_N / 3;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint32_t c = (uint32_t)ucd::NUMV[mid * 3];
        if (c == cp) { num = ucd::NUMV[mid * 3 + 1]; den = ucd::NUMV[mid * 3 + 2]; return true; }
        if (cp < c) hi = mid; else lo = mid + 1;
    }
    return false;
}

std::vector<uint32_t> uniNormalize(const std::vector<uint32_t>& cps, int mode) {
    bool compat = (mode == 2 || mode == 3);
    bool compose = (mode == 1 || mode == 3);
    std::vector<uint32_t> d;
    d.reserve(cps.size() * 2);
    for (uint32_t cp : cps) decomposeCp(cp, compat, d);
    canonicalOrder(d);
    if (!compose) return d;
    if (d.empty()) return d;
    std::vector<uint32_t> out;
    out.push_back(d[0]);
    int startIdx = uniCombiningClass(d[0]) == 0 ? 0 : -1;
    int lastCC = uniCombiningClass(d[0]);
    for (size_t i = 1; i < d.size(); i++) {
        uint32_t c = d[i]; int cc = uniCombiningClass(c);
        uint32_t composed = 0;
        if (startIdx >= 0 && (lastCC < cc || lastCC == 0)) composed = composePair(out[startIdx], c);
        if (composed) { out[startIdx] = composed; }
        else { out.push_back(c); if (cc == 0) startIdx = (int)out.size() - 1; lastCC = cc; }
    }
    return out;
}
}
