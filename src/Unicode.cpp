#include "AsciiCtype.h"
#include "Unicode.h"
#include "ucd_seam.h" // the cuttable table groups, reached only via accessors
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
// Only the NEVER-CUT tables are named here (SLIM-PLAN: reached by ordinary
// string operations, so no stub will ever stand behind them). The cuttable
// groups — NAMES/NUMV, the collation triple, SCRIPTS/BLOCKS/BIDI — have no
// extern in this file at all: they are reached through ucd_seam.h's accessors,
// and a new direct reference is a compile error, not a hole in the seam.
extern const uint32_t CCC[];    extern const size_t CCC_N;
extern const uint32_t CANON[];  extern const size_t CANON_N;
extern const uint32_t KOMPAT[]; extern const size_t KOMPAT_N;
extern const uint32_t COMP[];   extern const size_t COMP_N;
extern const uint32_t GBRANGE[]; extern const size_t GBRANGE_N; // real UAX#29 classes + ExtPict, as ranges
extern const uint32_t INCB[]; extern const size_t INCB_N; // Indic_Conjunct_Break (rule GB9c)
extern const char* const CATNAMES[]; extern const uint32_t GCAT[]; extern const size_t GCAT_N;
extern const char* const PROPNAMES[]; extern const size_t PROPNAMES_N;
struct PropRange { uint32_t lo, hi; uint16_t prop; };
extern const PropRange BINPROPS[]; extern const size_t BINPROPS_N; // DerivedCoreProperties + PropList
// case tables (unicode_case_gen.cpp): SIMPLE are (cp,mapped) pairs; FULL/FOLD are (cp,m0,m1,m2) quads
extern const uint32_t SUPPER[]; extern const size_t SUPPER_N;
extern const uint32_t SLOWER[]; extern const size_t SLOWER_N;
extern const uint32_t STITLE[]; extern const size_t STITLE_N;
extern const uint32_t FUPPER[]; extern const size_t FUPPER_N;
extern const uint32_t FLOWER[]; extern const size_t FLOWER_N;
extern const uint32_t FTITLE[]; extern const size_t FTITLE_N;
extern const uint32_t FOLDF[];  extern const size_t FOLDF_N;
// enum-property range tables (unicode_props2_gen.cpp)
#define ENUMPROP(N) extern const char* const N##_VALUES[]; extern const size_t N##_VALUES_N; \
                    extern const uint32_t N##_RANGES[]; extern const size_t N##_RANGES_N;
ENUMPROP(AGE) ENUMPROP(LB) ENUMPROP(WB) ENUMPROP(SB) ENUMPROP(GCB) ENUMPROP(EAW)
ENUMPROP(HST) ENUMPROP(DT) ENUMPROP(NT) ENUMPROP(JT) ENUMPROP(JG)
ENUMPROP(NFCQC) ENUMPROP(NFDQC) ENUMPROP(NFKCQC) ENUMPROP(NFKDQC)
ENUMPROP(INPC) ENUMPROP(INSC)
#undef ENUMPROP
extern const uint32_t U1NAME_CPS[]; extern const char* const U1NAME_STRS[]; extern const size_t U1NAME_N;
extern const uint32_t JAMOSN_CPS[]; extern const char* const JAMOSN_STRS[]; extern const size_t JAMOSN_N;
extern const uint32_t BIDIBRACKET[]; extern const size_t BIDIBRACKET_N;
extern const uint32_t BIDIMIRROR[]; extern const size_t BIDIMIRROR_N;
}

// binary-search a per-property range table (lo,hi,valueIdx triples sorted by lo).
static std::string enumLookup(const uint32_t* r, size_t n3, const char* const* vals,
                              uint32_t cp, const char* dflt) {
    size_t lo = 0, hi = n3 / 3;
    while (lo < hi) { size_t mid = (lo + hi) / 2; uint32_t s = r[mid * 3], e = r[mid * 3 + 1];
        if (cp < s) hi = mid; else if (cp > e) lo = mid + 1; else return vals[r[mid * 3 + 2]]; }
    return dflt;
}
std::string uniEnumProp(const std::string& prop, uint32_t cp) {
    // normalize the property name (case/underscore-insensitive)
    std::string p; for (char c : prop) if (ascii::isalnum((unsigned char)c)) p += (char)ascii::tolower((unsigned char)c);
    #define TRY(N, dflt, ...) { static const char* ks[] = {__VA_ARGS__}; for (auto* k : ks) if (p == k) \
        return enumLookup(ucd::N##_RANGES, ucd::N##_RANGES_N, ucd::N##_VALUES, cp, dflt); }
    TRY(AGE, "Unassigned", "age")
    TRY(LB, "XX", "linebreak", "lb")
    TRY(WB, "Other", "wordbreak", "wb")
    TRY(SB, "Other", "sentencebreak", "sb")
    TRY(GCB, "Other", "graphemeclusterbreak", "gcb")
    TRY(EAW, "N", "eastasianwidth", "ea")
    TRY(HST, "NA", "hangulsyllabletype", "hst")
    TRY(DT, "None", "decompositiontype", "dt")
    TRY(NT, "None", "numerictype", "nt")
    TRY(JT, "U", "joiningtype", "jt")
    TRY(JG, "No_Joining_Group", "joininggroup", "jg")
    // the four normalization quick checks: every codepoint the UCD file does not
    // list is "Yes", and the values are reported by full name
    TRY(NFCQC,  "Yes", "nfcquickcheck",  "nfcqc")
    TRY(NFDQC,  "Yes", "nfdquickcheck",  "nfdqc")
    TRY(NFKCQC, "Yes", "nfkcquickcheck", "nfkcqc")
    TRY(NFKDQC, "Yes", "nfkdquickcheck", "nfkdqc")
    TRY(INPC, "NA",    "indicpositionalcategory", "inpc")
    TRY(INSC, "Other", "indicsyllabiccategory",   "insc")
    #undef TRY
    return "";
}

// Jamo_Short_Name — the short Hangul jamo name ("GG"); "" for everything else.
std::string uniJamoShortName(uint32_t cp) {
    size_t lo = 0, hi = ucd::JAMOSN_N;
    while (lo < hi) { size_t mid = (lo + hi) / 2; uint32_t k = ucd::JAMOSN_CPS[mid];
        if (cp < k) hi = mid; else if (cp > k) lo = mid + 1; else return ucd::JAMOSN_STRS[mid]; }
    return "";
}

// Bidi_Paired_Bracket / _Type. A codepoint that is not a paired bracket pairs
// with ITSELF and is type "n" — that is the UCD's own default, not an absence.
static const uint32_t* bracketRow(uint32_t cp) {
    size_t lo = 0, hi = ucd::BIDIBRACKET_N / 3;
    while (lo < hi) { size_t mid = (lo + hi) / 2; uint32_t k = ucd::BIDIBRACKET[mid * 3];
        if (cp < k) hi = mid; else if (cp > k) lo = mid + 1; else return &ucd::BIDIBRACKET[mid * 3]; }
    return nullptr;
}
uint32_t uniBidiPairedBracket(uint32_t cp) {
    const uint32_t* r = bracketRow(cp);
    return r ? r[1] : cp;
}
std::string uniBidiPairedBracketType(uint32_t cp) {
    const uint32_t* r = bracketRow(cp);
    return !r ? "n" : r[2] == 1 ? "o" : "c";
}

// Unicode_1_Name — the Unicode 1.0 name, kept for the codepoints (mostly
// controls) that have no Name of their own. "" when there is none.
std::string uniUnicode1Name(uint32_t cp) {
    size_t lo = 0, hi = ucd::U1NAME_N;
    while (lo < hi) { size_t mid = (lo + hi) / 2; uint32_t k = ucd::U1NAME_CPS[mid];
        if (cp < k) hi = mid; else if (cp > k) lo = mid + 1; else return ucd::U1NAME_STRS[mid]; }
    return "";
}

// Canonical_Combining_Class by NAME rather than number — the UCD's own value
// aliases; classes with no alias answer their number as a string.
std::string uniCombiningClassName(uint32_t cp) {
    int c = uniCombiningClass(cp);
    switch (c) {
        case 0:   return "Not_Reordered";
        case 1:   return "Overlay";
        case 6:   return "Han_Reading";
        case 7:   return "Nukta";
        case 8:   return "Kana_Voicing";
        case 9:   return "Virama";
        case 200: return "Attached_Below_Left";
        case 202: return "Attached_Below";
        case 214: return "Attached_Above";
        case 216: return "Attached_Above_Right";
        case 218: return "Below_Left";
        case 220: return "Below";
        case 222: return "Below_Right";
        case 224: return "Left";
        case 226: return "Right";
        case 228: return "Above_Left";
        case 230: return "Above";
        case 232: return "Above_Right";
        case 233: return "Double_Below";
        case 234: return "Double_Above";
        case 240: return "Iota_Subscript";
        default:  return std::to_string(c); // CCC10..CCC199 have no alias
    }
}
int32_t uniBidiMirror(uint32_t cp) {
    size_t lo = 0, hi = ucd::BIDIMIRROR_N / 2;
    while (lo < hi) { size_t mid = (lo + hi) / 2; uint32_t k = ucd::BIDIMIRROR[mid * 2];
        if (cp < k) hi = mid; else if (cp > k) lo = mid + 1; else return (int32_t)ucd::BIDIMIRROR[mid * 2 + 1]; }
    return -1;
}

// binary-search a SIMPLE (cp,mapped) table; returns mapped cp or the input unchanged.
static uint32_t caseSimple(const uint32_t* t, size_t n, uint32_t cp) {
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t mid = (lo + hi) / 2; uint32_t k = t[mid * 2];
        if (cp < k) hi = mid; else if (cp > k) lo = mid + 1; else return t[mid * 2 + 1]; }
    return cp;
}
// binary-search a FULL (cp,m0,m1,m2) table; appends its 1..3 codepoints, returns true if found.
static bool caseFull(const uint32_t* t, size_t n, uint32_t cp, std::vector<uint32_t>& out) {
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t mid = (lo + hi) / 2; uint32_t k = t[mid * 4];
        if (cp < k) hi = mid; else if (cp > k) lo = mid + 1;
        else { for (int j = 1; j <= 3; j++) if (t[mid * 4 + j]) out.push_back(t[mid * 4 + j]); return true; } }
    return false;
}
uint32_t uniSimpleUpper(uint32_t cp) { return caseSimple(ucd::SUPPER, ucd::SUPPER_N, cp); }
uint32_t uniSimpleLower(uint32_t cp) { return caseSimple(ucd::SLOWER, ucd::SLOWER_N, cp); }
uint32_t uniSimpleTitle(uint32_t cp) { uint32_t t = caseSimple(ucd::STITLE, ucd::STITLE_N, cp); return t == cp ? uniSimpleUpper(cp) : t; }
std::vector<uint32_t> uniCaseMap(uint32_t cp, int kind) {
    std::vector<uint32_t> out;
    switch (kind) {
        case 0: if (caseFull(ucd::FLOWER, ucd::FLOWER_N, cp, out)) return out; out.push_back(uniSimpleLower(cp)); return out;
        case 1: if (caseFull(ucd::FUPPER, ucd::FUPPER_N, cp, out)) return out; out.push_back(uniSimpleUpper(cp)); return out;
        case 2: if (caseFull(ucd::FTITLE, ucd::FTITLE_N, cp, out)) return out; out.push_back(uniSimpleTitle(cp)); return out;
        default: if (caseFull(ucd::FOLDF, ucd::FOLDF_N, cp, out)) return out; out.push_back(cp); return out;
    }
}

// Bidi_Class of cp ("L", "EN", "WS", …); default "L" in the assigned ranges' gaps.
static const char* uniBidiClass(uint32_t c) {
    size_t n; const ucd::BidiEnt* T = ucd::bidiTable(&n); // seam: hoisted once
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < T[mid].lo) hi = mid;
        else if (c > T[mid].hi) lo = mid + 1;
        else return T[mid].bc;
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

// Strict binary-property test for uniprop(): 1/0 if `prop` is a known binary
// property, -1 if it is not one (so the caller does NOT fall back to a lenient match).
int uniBinaryProp(uint32_t cp, const std::string& prop) {
    std::string norm; for (char c : prop) if (ascii::isalnum((unsigned char)c)) norm += (char)ascii::tolower((unsigned char)c);
    // Emoji_All is not a UCD property but the UNION of the emoji-data ones —
    // "is this codepoint emoji in any sense".
    if (norm == "emojiall") {
        for (const char* k : {"emoji", "emojipresentation", "emojimodifier",
                              "emojimodifierbase", "emojicomponent", "extendedpictographic"})
            if (uniBinProp(cp, k) == 1) return 1;
        return 0;
    }
    return uniBinProp(cp, norm);
}

// `<:InBlockName>` block property: normalized (lowercase, alnum-only) name of the
// block containing cp, "" if none (an unassigned gap between blocks).
static const char* uniBlockName(uint32_t cp) {
    size_t n; const ucd::BlockEnt* T = ucd::blocksTable(&n); // seam: hoisted once
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < T[mid].lo) hi = mid;
        else if (cp > T[mid].hi) lo = mid + 1;
        else return T[mid].name;
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

// The `\s` set, in one place: category Z plus the ASCII controls Raku counts
// and NEL. Same rule the <:space> property assertion applies above.
bool uniIsSpaceCp(uint32_t cp) {
    if (cp < 128) return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x0B || cp == 0x0C;
    if (cp == 0x85) return true;                       // NEL
    std::string cat = uniGeneralCategory(cp);
    return !cat.empty() && cat[0] == 'Z';
}

// Real Script property, from the pinned 16.0 Scripts.txt range table.
std::string uniScript(uint32_t c) {
    size_t n; const ucd::ScriptEnt* T = ucd::scriptsTable(&n); // seam: hoisted once
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < T[mid].lo) hi = mid;
        else if (c > T[mid].hi) lo = mid + 1;
        else return T[mid].name;
    }
    return "Unknown"; // unassigned / no script (Zzzz)
}

// normalize a property name/value for loose matching (lowercase, drop separators)
static std::string normProp(const std::string& s) {
    std::string n;
    for (char ch : s) if (ascii::isalnum((unsigned char)ch)) n += (char)ascii::tolower((unsigned char)ch);
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
        // sc/script/gc/generalcategory and unknown property keys alike: the
        // bare-value handlers match on the value alone
        return uniMatchesProp(cp, val);
    }
    // ORDER MATTERS from here down, and not for speed: everything up to the
    // binary properties answers from the never-cut tables, so a category or
    // binary-property assertion (<:Lu>, <:alpha>, <:White_Space>) must resolve
    // BEFORE anything touches the cuttable SCRIPTS/BLOCKS tables. Under
    // --slim=-unicode-props those accessors THROW; the script-name set used to
    // be built first, which made every <:Prop> assertion — category or not —
    // die in a slim binary. uniPropNeedsCutTables() below mirrors this exact
    // order; change one, change both.
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
        for (char ch : p) if (ascii::isalnum((unsigned char)ch)) norm += (char)ascii::tolower((unsigned char)ch);
        int b = uniBinProp(cp, norm);
        if (b >= 0) return b == 1;
    }
    // ---- everything below here reaches the CUTTABLE tables ----
    // script property: <:Latin> <:Syriac> <:Canadian_Aboriginal> … (bare script
    // value == <:Script<...>>). Loose-match p against the set of real script names.
    {
        static const std::set<std::string> scriptNames = [] {
            std::set<std::string> s;
            size_t tn; const ucd::ScriptEnt* T = ucd::scriptsTable(&tn); // seam: hoisted once
            for (size_t i = 0; i < tn; i++) {
                std::string n;
                for (const char* q = T[i].name; *q; q++) if (ascii::isalnum((unsigned char)*q)) n += (char)ascii::tolower((unsigned char)*q);
                s.insert(n);
            }
            return s;
        }();
        std::string norm;
        for (char ch : p) if (ascii::isalnum((unsigned char)ch)) norm += (char)ascii::tolower((unsigned char)ch);
        if (scriptNames.count(norm)) {
            std::string sc = uniScript(cp), scn;
            for (char ch : sc) if (ascii::isalnum((unsigned char)ch)) scn += (char)ascii::tolower((unsigned char)ch);
            return scn == norm;
        }
    }
    // block property `<:InArabic>` / `<:InLatin1Supplement>`: In-prefix + block name.
    if (p.size() > 2 && p[0] == 'I' && p[1] == 'n' && ascii::isupper((unsigned char)p[2])) {
        std::string q;
        for (char ch : p) if (ascii::isalnum((unsigned char)ch)) q += (char)ascii::tolower((unsigned char)ch);
        if (q.size() > 2 && q[0] == 'i' && q[1] == 'n') q = q.substr(2); // drop the In prefix
        // legacy block-name aliases renamed in later Unicode versions
        if (q == "cyrillicsupplementary") q = "cyrillicsupplement";
        if (q == "ascii") return cp <= 0x7F; // ASCII is a Blocks.txt alias for Basic Latin
        std::string b; // compare NORMALIZED (uniBlockName has spaces and caps)
        for (char ch : std::string(uniBlockName(cp)))
            if (ascii::isalnum((unsigned char)ch)) b += (char)ascii::tolower((unsigned char)ch);
        return q == b;
    }
    return true; // unknown property (e.g. an unmodelled script): lenient match
}

// SLIM-PLAN P4: would matching `<:p>` reach the cuttable SCRIPTS/BLOCKS/BIDI
// tables? The scan asks this for every property assertion in a regex, so the
// answer must mirror uniMatchesProp's dispatch order above EXACTLY — a name
// that resolves before the "cuttable" line there is safe (false here), and
// everything at or past it (scripts, In-blocks, bc<…>, and unknown names,
// which fall through the script-set lookup) needs the feature (true here).
// Runs inside the rakupp CLI, where every table is present.
bool uniPropNeedsCutTables(const std::string& p) {
    size_t lt = p.find('<');
    if (lt != std::string::npos && !p.empty() && p.back() == '>') {
        std::string prop = normProp(p.substr(0, lt));
        if (prop == "bc" || prop == "bidiclass") return true;          // bidiTable
        return uniPropNeedsCutTables(p.substr(lt + 1, p.size() - lt - 2));
    }
    static const std::set<std::string> posixish = {   // the literal names uniMatchesProp
        "alpha", "Alpha", "Letter", "L", "Assigned",  // tests against `cat` above the line
        "digit", "Nd", "decimal", "alnum", "Alnum",
        "space", "Space", "White_Space", "blank", "ws",
        "upper", "Upper", "Uppercase", "Lu", "lower", "Lower", "Lowercase", "Ll",
        "punct", "Punct", "Punctuation", "P", "word", "Word",
        "cntrl", "Control", "Cc", "N", "Number", "Numeric", "M", "Mark",
        "S", "Symbol", "Z", "Separator", "C", "Other",
        "LC", "CasedLetter", "Cased_Letter",
    };
    if (posixish.count(p)) return false;
    if (p.size() == 1) return false;                                    // category group letter
    static const char* K[] = {"Lu","Ll","Lt","Lm","Lo","Mn","Mc","Me","Nd","Nl","No","Pc","Pd",
        "Ps","Pe","Pi","Pf","Po","Sm","Sc","Sk","So","Zs","Zl","Zp","Cc","Cf","Cs","Co","Cn"};
    for (auto* k : K) if (p == k) return false;
    static const char* LONGN[] = {"UppercaseLetter","LowercaseLetter","TitlecaseLetter",
        "ModifierLetter","OtherLetter","NonspacingMark","SpacingMark","EnclosingMark",
        "DecimalNumber","LetterNumber","OtherNumber","ConnectorPunctuation","DashPunctuation",
        "OpenPunctuation","ClosePunctuation","InitialPunctuation","FinalPunctuation",
        "OtherPunctuation","MathSymbol","CurrencySymbol","ModifierSymbol","OtherSymbol",
        "SpaceSeparator","LineSeparator","ParagraphSeparator","Control","Format",
        "Surrogate","PrivateUse","Unassigned"};
    for (auto* k : LONGN) if (p == k) return false;
    if (uniBinProp('A', normProp(p)) >= 0) return false;   // known binary property name
    return true; // script, In-block, or unknown: all reach the cuttable tables
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

// UAX #29 grapheme-cluster boundaries, in one place.
//
// The rule chain (GB3-GB999) and the state it carries — the regional-indicator
// run, the emoji-ZWJ sequence flag, and the GB9c conjunct chain — used to be
// written out twice: once over a codepoint vector for uniGraphemeStarts, once
// over UTF-8 bytes for uniClusterEndUtf8. They agreed, which is the dangerous
// kind of duplication: the next rule fix would have had to land in both, and
// only one of them is exercised by the regex engine's grapheme stride.
struct GbState {
    int prev;          // gbProp of the previous codepoint
    bool pictSeq;      // inside an emoji ZWJ sequence (GB11)
    int riRun;         // consecutive regional indicators (GB12/13)
    int incbState;     // 0 none, 1 Consonant seen, 2 Consonant+Linker (GB9c)
    explicit GbState(uint32_t first)
        : prev(gbProp(first)), pictSeq(prev == GB_ExtPict),
          riRun(prev == GB_RI ? 1 : 0), incbState(incbProp(first) == 2 ? 1 : 0) {}
};

// Does a cluster boundary fall between the previous codepoint and `cur`?
static inline bool gbBreakBefore(const GbState& st, int cur, int ip) {
    if (st.prev == GB_CR && cur == GB_LF) return false;                                   // GB3
    if (st.prev == GB_Control || st.prev == GB_CR || st.prev == GB_LF) return true;       // GB4
    if (cur == GB_Control || cur == GB_CR || cur == GB_LF) return true;                   // GB5
    if (st.prev == GB_L && (cur == GB_L || cur == GB_V || cur == GB_LV || cur == GB_LVT))
        return false;                                                                     // GB6
    if ((st.prev == GB_LV || st.prev == GB_V) && (cur == GB_V || cur == GB_T)) return false; // GB7
    if ((st.prev == GB_LVT || st.prev == GB_T) && cur == GB_T) return false;              // GB8
    if (cur == GB_Extend || cur == GB_ZWJ) return false;                                  // GB9
    if (cur == GB_SpacingMark) return false;                                              // GB9a
    if (st.prev == GB_Prepend) return false;                                              // GB9b
    if (st.incbState == 2 && ip == 2) return false;                                       // GB9c
    if (st.pictSeq && st.prev == GB_ZWJ && cur == GB_ExtPict) return false;               // GB11
    if (st.prev == GB_RI && cur == GB_RI && (st.riRun % 2 == 1)) return false;            // GB12/13
    return true;                                                                          // GB999
}

// Carry the state across one codepoint, given the decision just made for it.
static inline void gbAdvance(GbState& st, int cur, int ip, bool brk) {
    st.riRun = (cur == GB_RI) ? (brk ? 1 : st.riRun + 1) : 0;
    if (cur == GB_ExtPict) st.pictSeq = true;
    else if (!brk && st.pictSeq && (cur == GB_Extend || cur == GB_ZWJ)) st.pictSeq = true;
    else st.pictSeq = false;
    // conjunct chain: a Consonant anchors, Linker upgrades, InCB-Extend carries
    if (brk) st.incbState = (ip == 2) ? 1 : 0;
    else if (ip == 2) st.incbState = 1;
    else if (st.incbState >= 1 && ip == 1) st.incbState = 2;
    else if (!(st.incbState >= 1 && ip == 3)) st.incbState = 0;
    st.prev = cur;
}

// Indices (into cps) where a new grapheme cluster starts; front() is always 0.
std::vector<size_t> uniGraphemeStarts(const std::vector<uint32_t>& cps) {
    std::vector<size_t> starts;
    if (cps.empty()) return starts;
    starts.push_back(0);
    GbState st(cps[0]);
    for (size_t i = 1; i < cps.size(); i++) {
        int cur = gbProp(cps[i]), ip = incbProp(cps[i]);
        bool brk = gbBreakBefore(st, cur, ip);
        if (brk) starts.push_back(i);
        gbAdvance(st, cur, ip, brk);
    }
    return starts;
}

// Byte offset of the end of the grapheme cluster beginning at UTF-8 byte `pos`
// (which must be a codepoint boundary). Walks forward applying the UAX #29
// pairwise rules — O(cluster length), the regex engine's grapheme-atom stride.
size_t uniClusterEndUtf8(const std::string& s, size_t pos, size_t len) {
    auto dec = [&](size_t p, uint32_t& cp) -> size_t { // -> byte length
        unsigned char c0 = (unsigned char)s[p];
        int clen = c0 < 0x80 ? 1 : (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xe ? 3 : (c0 >> 3) == 0x1e ? 4 : 1;
        if (p + clen > len) clen = 1;
        cp = clen == 1 ? c0 : (uint32_t)(c0 & (0xFF >> (clen + 1)));
        for (int i = 1; i < clen; i++) cp = (cp << 6) | ((unsigned char)s[p + i] & 0x3F);
        return (size_t)clen;
    };
    if (pos >= len) return pos;
    uint32_t cp; size_t p = pos + dec(pos, cp);
    GbState st(cp);
    while (p < len) {
        uint32_t c2; size_t clen = dec(p, c2);
        int cur = gbProp(c2), ip = incbProp(c2);
        if (gbBreakBefore(st, cur, ip)) break;
        p += clen;
        gbAdvance(st, cur, ip, false);   // reached only when the cluster continues
    }
    return p;
}

size_t uniGraphemeCount(const std::vector<uint32_t>& cps) {
    return uniGraphemeStarts(cps).size();
}

GraphemeMap::GraphemeMap(const std::vector<uint32_t>& cps) : ncps_(cps.size()) {
    // Cheap pre-check. Nothing below U+0300 extends a cluster — the combining
    // marks start there — with one exception: CR, because CR LF is a single
    // grapheme (GB3), which is why "a\r\nb".chars is 3. So a string free of both
    // cannot cluster, and its grapheme indices ARE its codepoint indices.
    for (uint32_t cp : cps) {
        if (cp >= 0x300 || cp == 0x0D) { starts_ = uniGraphemeStarts(cps); return; }
    }
}

size_t GraphemeMap::graphemeAt(size_t cp) const {
    if (starts_.empty()) return cp < ncps_ ? cp : ncps_;
    // the cluster containing `cp` is the last one starting at or before it
    auto it = std::upper_bound(starts_.begin(), starts_.end(), cp);
    return (size_t)(it - starts_.begin()) - (it == starts_.begin() ? 0 : 1);
}


// ---- UCA collation (DUCET, allkeys 17.0) — powers `unicmp` / `coll` ----
namespace {
struct CE { uint16_t l1, l2, l3; };
// Implicit-weight primaries (UTS #10 §10.1.3): siniform scripts get fixed bases;
// Han uses the real Unified_Ideograph property (block-split core vs extensions);
// everything else (unassigned/reserved) gets the FBC0 series.
void ucaImplicit(uint32_t cp, uint16_t& aaaa, uint16_t& bbbb) {
    if (cp >= 0x17000 && cp <= 0x18AFF) { aaaa = 0xFB00; bbbb = (uint16_t)((cp - 0x17000) | 0x8000); return; } // Tangut (+components)
    if (cp >= 0x18D00 && cp <= 0x18D8F) { aaaa = 0xFB00; bbbb = (uint16_t)((cp - 0x17000) | 0x8000); return; } // Tangut Supplement
    if (cp >= 0x1B170 && cp <= 0x1B2FF) { aaaa = 0xFB01; bbbb = (uint16_t)((cp - 0x1B170) | 0x8000); return; } // Nushu
    if (cp >= 0x18B00 && cp <= 0x18CFF) { aaaa = 0xFB02; bbbb = (uint16_t)((cp - 0x18B00) | 0x8000); return; } // Khitan Small Script
    bool han = uniBinProp(cp, "unifiedideograph") == 1;
    uint16_t base = !han ? 0xFBC0
                  : ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF)) ? 0xFB40 : 0xFB80;
    aaaa = (uint16_t)(base + (cp >> 15));
    bbbb = (uint16_t)((cp & 0x7FFF) | 0x8000);
}
// DUCET lookup for a 1-3 codepoint sequence -> (offset, count) into COLLCE
bool ducetLookup(const uint32_t* seq, size_t len, uint32_t& off, uint32_t& cnt) {
    if (len == 1) {
        size_t n; const uint32_t* S = ucd::collsingTable(&n); // seam: hoisted once
        size_t lo = 0, hi = n;
        while (lo < hi) { size_t mid = (lo + hi) / 2;
            if (S[mid * 3] < seq[0]) lo = mid + 1; else hi = mid; }
        if (lo < n && S[lo * 3] == seq[0]) {
            off = S[lo * 3 + 1]; cnt = S[lo * 3 + 2]; return true;
        }
        return false;
    }
    size_t n; const uint32_t* C = ucd::collcontrTable(&n); // seam: hoisted once
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t mid = (lo + hi) / 2;
        if (C[mid * 5] < seq[0]) lo = mid + 1; else hi = mid; }
    for (size_t k = lo; k < n && C[k * 5] == seq[0]; k++) {
        uint32_t c1 = C[k * 5 + 1], c2 = C[k * 5 + 2];
        size_t elen = c2 ? 3 : 2;
        if (elen != len) continue;
        if (c1 != seq[1]) continue;
        if (len == 3 && c2 != seq[2]) continue;
        off = C[k * 5 + 3]; cnt = C[k * 5 + 4];
        return true;
    }
    return false;
}

// Append the collation elements for cps starting at i; returns #cps consumed
// contiguously. May ERASE later non-starters from cps when a discontiguous
// contraction (UCA S2.1.2/S2.1.3) consumes them out of the stream.
size_t ucaElements(std::vector<uint32_t>& cps, size_t i, std::vector<CE>& out) {
    uint32_t seq[3] = {cps[i], 0, 0};
    size_t slen = 1;
    uint32_t off = 0, cnt = 0;
    bool have = ducetLookup(seq, 1, off, cnt);
    size_t consumed = 1;
    if (have) {
        // longest CONTIGUOUS match, longest-first: a 3-cp contraction's 2-cp
        // prefix need not itself be an entry (e.g. 0FB2 0F71 0F80 exists but
        // 0FB2 0F71 doesn't), so probe length 3 before length 2.
        if (i + 2 < cps.size()) {
            seq[1] = cps[i + 1]; seq[2] = cps[i + 2];
            uint32_t o2, c2;
            if (ducetLookup(seq, 3, o2, c2)) { off = o2; cnt = c2; slen = 3; consumed = 3; }
        }
        if (slen == 1 && i + 1 < cps.size()) {
            seq[1] = cps[i + 1]; seq[2] = 0;
            uint32_t o2, c2;
            if (ducetLookup(seq, 2, o2, c2)) { off = o2; cnt = c2; slen = 2; consumed = 2; }
        }
        if (slen == 1) { seq[1] = 0; seq[2] = 0; }
        // DISCONTIGUOUS extension: an unblocked non-starter C (no B in between with
        // ccc(B)==0 or ccc(B)>=ccc(C)) that extends the match is consumed out of
        // the stream (NFD reordering can split contractions like 0DD9+0DCA).
        if (slen < 3) {
            size_t j = i + consumed;
            std::vector<int> betweenCcc;
            while (j < cps.size() && slen < 3) {
                int cc = uniCombiningClass(cps[j]);
                if (cc == 0) break;
                bool blocked = false;
                for (int b : betweenCcc) if (b >= cc) { blocked = true; break; }
                if (!blocked) {
                    seq[slen] = cps[j];
                    uint32_t o2, c2;
                    if (ducetLookup(seq, slen + 1, o2, c2)) {
                        off = o2; cnt = c2; slen++;
                        cps.erase(cps.begin() + j); // consumed discontiguously
                        continue;
                    }
                    seq[slen] = 0;
                }
                betweenCcc.push_back(cc);
                j++;
            }
        }
        {   // seam: hoisted once, outside the per-element loop (P1's hot path)
            size_t n; const uint16_t* CE = ucd::collceTable(&n); (void)n;
            for (uint32_t j = 0; j < cnt; j++)
                out.push_back({CE[(off + j) * 3], CE[(off + j) * 3 + 1], CE[(off + j) * 3 + 2]});
        }
        return consumed;
    }
    // implicit weights (unassigned / siniform / Han not in the table)
    uint16_t aaaa, bbbb;
    ucaImplicit(cps[i], aaaa, bbbb);
    out.push_back({aaaa, 0x20, 0x2});
    out.push_back({bbbb, 0, 0});
    return 1;
}
} // namespace

// three-way UCA comparison of two codepoint sequences: -1 / 0 / 1
int uniCollate(const std::vector<uint32_t>& acps, const std::vector<uint32_t>& bcps) {
    std::vector<uint32_t> a = uniNormalize(acps, 0), b = uniNormalize(bcps, 0); // NFD (UCA S1.1)
    std::vector<CE> ea, eb;
    for (size_t i = 0; i < a.size(); ) i += ucaElements(a, i, ea); // (may erase consumed non-starters)
    for (size_t i = 0; i < b.size(); ) i += ucaElements(b, i, eb);
    for (int level = 0; level < 3; level++) {
        size_t i = 0, j = 0;
        for (;;) {
            uint16_t wa = 0, wb = 0;
            while (i < ea.size()) { uint16_t w = level == 0 ? ea[i].l1 : level == 1 ? ea[i].l2 : ea[i].l3; i++; if (w) { wa = w; break; } }
            while (j < eb.size()) { uint16_t w = level == 0 ? eb[j].l1 : level == 1 ? eb[j].l2 : eb[j].l3; j++; if (w) { wb = w; break; } }
            if (wa != wb) return wa < wb ? -1 : 1;
            if (!wa) break; // both exhausted at this level
        }
    }
    // identical sort keys: the UCA conformance rule breaks ties by codepoint order
    for (size_t i = 0; i < acps.size() && i < bcps.size(); i++)
        if (acps[i] != bcps[i]) return acps[i] < bcps[i] ? -1 : 1;
    if (acps.size() != bcps.size()) return acps.size() < bcps.size() ? -1 : 1;
    return 0;
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


// Hangul syllable names are algorithmic (AC00..D7A3): HANGUL SYLLABLE <L><V><T>
// composed from the Jamo short names.
static const char* const HANGUL_L[19] = {"G","GG","N","D","DD","R","M","B","BB","S","SS","","J","JJ","C","K","T","P","H"};
static const char* const HANGUL_V[21] = {"A","AE","YA","YAE","EO","E","YEO","YE","O","WA","WAE","OE","YO","U","WEO","WE","WI","YU","EU","YI","I"};
static const char* const HANGUL_T[28] = {"","G","GG","GS","N","NJ","NH","D","L","LG","LM","LB","LS","LT","LP","LH","M","B","BS","S","SS","NG","J","C","K","T","P","H"};
static std::string hangulSyllableName(uint32_t cp) {
    uint32_t s = cp - 0xAC00;
    return std::string("HANGUL SYLLABLE ") + HANGUL_L[s / (21 * 28)] + HANGUL_V[(s / 28) % 21] + HANGUL_T[s % 28];
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
    if ((a = algo("TANGUT IDEOGRAPH-")) >= 0) return a;
    if ((a = algo("KHITAN SMALL SCRIPT CHARACTER-")) >= 0) return a;
    if ((a = algo("NUSHU CHARACTER-")) >= 0) return a;
    if (name.compare(0, 16, "HANGUL SYLLABLE ") == 0) {
        static const std::unordered_map<std::string, uint32_t> hangul = [] {
            std::unordered_map<std::string, uint32_t> m;
            for (uint32_t cp = 0xAC00; cp <= 0xD7A3; cp++) m[hangulSyllableName(cp)] = cp;
            return m;
        }();
        auto it = hangul.find(name);
        if (it != hangul.end()) return (int32_t)it->second;
        return -1;
    }
    size_t n; const ucd::NameEnt* T = ucd::namesTable(&n); // seam: hoisted once
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(name.c_str(), T[mid].name);
        if (c == 0) return (int32_t)T[mid].cp;
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return -1;
}

std::string uniNameOf(uint32_t cp) {
    static const std::unordered_map<uint32_t, const char*> rev = [] {
        std::unordered_map<uint32_t, const char*> m;
        size_t tn; const ucd::NameEnt* T = ucd::namesTable(&tn); // seam: hoisted once
        for (size_t i = 0; i < tn; i++) m[T[i].cp] = T[i].name;
        return m;
    }();
    auto it = rev.find(cp);
    if (it != rev.end()) return it->second;
    if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x20000 && cp <= 0x2A6DF) || (cp >= 0x2A700 && cp <= 0x2EE5D) ||
        (cp >= 0x30000 && cp <= 0x323AF)) {
        char b[40]; std::snprintf(b, sizeof b, "CJK UNIFIED IDEOGRAPH-%04X", cp); return b;
    }
    if (cp >= 0xAC00 && cp <= 0xD7A3) return hangulSyllableName(cp);
    if ((cp >= 0x17000 && cp <= 0x187FF) || (cp >= 0x18D00 && cp <= 0x18D8F)) {
        char b[32]; std::snprintf(b, sizeof b, "TANGUT IDEOGRAPH-%05X", cp); return b;
    }
    if (cp >= 0x18B00 && cp <= 0x18CFF) {
        char b[40]; std::snprintf(b, sizeof b, "KHITAN SMALL SCRIPT CHARACTER-%05X", cp); return b;
    }
    if (cp >= 0x1B170 && cp <= 0x1B2FF) {
        char b[32]; std::snprintf(b, sizeof b, "NUSHU CHARACTER-%05X", cp); return b;
    }
    return "";
}

// Decimal-digit value (0-9) of an Nd codepoint, -1 otherwise. Every decimal
// run in Unicode is one contiguous 0..9 decade; this table is the decade
// STARTS, nothing else, and it is NEVER-CUT on purpose: ordinary string
// numification ("\u0664\u0662".Int is 42) transliterates through it, and the SLIM
// never-cut criterion is exactly "reached by ordinary string operations".
// unival()/univals() stay on the cuttable NUMV table below — Rats like \u2154
// are the names feature; plain digits are not. (Promoted from the Lexer's
// private copy when the P4 differential caught a slim binary throwing on
// "\u0664\u0662".Int — same table, one owner now.)
int uniDigitValue(uint32_t cp) {
    static const uint32_t zeros[] = {
        0x0030, 0x0660, 0x06F0, 0x07C0, 0x0966, 0x09E6, 0x0A66, 0x0AE6, 0x0B66,
        0x0BE6, 0x0C66, 0x0CE6, 0x0D66, 0x0DE6, 0x0E50, 0x0ED0, 0x0F20, 0x1040,
        0x1090, 0x17E0, 0x1810, 0x1946, 0x19D0, 0x1A80, 0x1A90, 0x1B50, 0x1BB0,
        0x1C40, 0x1C50, 0xA620, 0xA8D0, 0xA900, 0xA9D0, 0xA9F0, 0xAA50, 0xABF0,
        0xFF10, 0x104A0, 0x10D30, 0x11066, 0x110F0, 0x11136, 0x111D0, 0x112F0,
        0x11450, 0x114D0, 0x11650, 0x116C0, 0x11730, 0x118E0, 0x11950, 0x11C50,
        0x11D50, 0x11DA0, 0x16A60, 0x16B50, 0x1D7CE, 0x1D7D8, 0x1D7E2, 0x1D7EC,
        0x1D7F6, 0x1E140, 0x1E2F0, 0x1E950, 0x1FBF0,
        // UCD 16 additions the Lexer's private copy predated (found by the P4
        // cross-check of this table against NUMV over the whole range).
        // NOTE Ol Onal: its zero is U+1E5F1 — a decade NOT aligned to …0,
        // which is why this is a table of zeros and not a (cp % 10) formula.
        0x10D40, 0x116D0, 0x116DA, 0x11BF0, 0x11DE0, 0x11F50,
        0x16130, 0x16AC0, 0x16D70, 0x1CCF0, 0x1E4F0, 0x1E5F1,
    };
    for (uint32_t z : zeros) if (cp >= z && cp <= z + 9) return (int)(cp - z);
    return -1;
}

bool uniNumValue(uint32_t cp, long long& num, long long& den) {
    size_t n; const int64_t* V = ucd::numvTable(&n); // seam: hoisted once; n = rows of 3
    size_t lo = 0, hi = n / 3;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint32_t c = (uint32_t)V[mid * 3];
        if (c == cp) { num = V[mid * 3 + 1]; den = V[mid * 3 + 2]; return true; }
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

std::string uniBidiClassOf(uint32_t cp) { return uniBidiClass(cp); }
std::string uniBlockOf(uint32_t cp) { return uniBlockName(cp); }
}
