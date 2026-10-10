// BuiltinsNqp.cpp — the nqp:: ops, interpreted (evalNqpOp) and compiled (rtNqpOp)
//
// One of the parts BuiltinsParts.h lists; what they share is declared there.
#include "BuiltinsParts.h"
#include "Sandbox.h"
#include <mutex>
#include <unordered_map>

namespace rakupp {

// The array-op SUB forms' argument contract (S32-array/{push,pop,shift,unshift}.t):
// `pop()` with nothing to pop from is a type error at Rakudo's compile time; the
// one-array forms (pop, shift) take no further positional; and none of them has
// a named parameter, so `push @a, a => 52` has nowhere to put the pair. Each
// used to answer a silent Any or push the Pair. `push([])` stays fine: the
// Array is the argument, and pushing nothing to it is allowed.
void arrayOpArgs(const std::string& op, const ValueList& a, bool oneArray) {
    if (a.empty())
        throw RakuError{Value::typeObj("X::TypeCheck::Argument"),
            "Calling " + op + "() with no arguments will never work: it needs the array to " + op};
    size_t positional = 0;
    for (auto& v : a) {
        if (v.t == VT::Pair && v.namedArg)
            throw RakuError{Value::typeObj("X::AdHoc"),
                "Unexpected named argument '" + v.s + "' passed to " + op};
        positional++;
    }
    if (oneArray && positional > 1)
        throw RakuError{Value::typeObj("X::Multi::NoMatch"),
            "Cannot resolve caller " + op + "(" + a[0].typeName() + ":D, " + a[1].typeName() + ") — " + op + " takes the array alone"};
}

// ---- nqp buffer read/write helpers -----------------------------------------
// MoarVM encodes (read|write)(u)int/num's last argument as size|endian: the low
// two bits pick the byte order (Endian enum — Native 0 / Little 1 / Big 2) and
// bits 2+ hold a size code, giving 1<<(flag>>2) bytes. See CBOR::Simple's $ne8/
// $be16/$be32/$be64 flags (nqp::bitor_i(BINARY_SIZE_*, Endian)).
static const bool g_hostLittle = [] { uint16_t x = 1; return *(uint8_t*)&x == 1; }();
static inline bool binFlagLittle(int e) { return e == 1 || (e == 0 && g_hostLittle); }
static inline void decodeBinFlag(long long flag, int& nbytes, int& endian) {
    endian = (int)(flag & 3);
    int code = (int)((flag >> 2) & 7);
    nbytes = 1 << code;                 // 0→1, 1→2, 2→4, 3→8
}
// kind: 'u' unsigned int, 'i' signed int, 'n' float/double
static void nqpBufWrite(std::string& bytes, long long off, const Value& val,
                        int nbytes, int endian, char kind) {
    if (off < 0) return;
    if ((long long)bytes.size() < off + nbytes) bytes.resize(off + nbytes, '\0');
    unsigned char raw[8] = {0};
    if (kind == 'n') {
        if (nbytes == 4) { float f = (float)val.toNum(); std::memcpy(raw, &f, 4); }
        else            { double d = val.toNum();        std::memcpy(raw, &d, 8); }
    } else {
        unsigned long long u = (unsigned long long)val.toInt();
        std::memcpy(raw, &u, nbytes <= 8 ? nbytes : 8);
    }
    for (int i = 0; i < nbytes; i++) {
        int src = (binFlagLittle(endian) == g_hostLittle) ? i : nbytes - 1 - i;
        bytes[off + i] = (char)raw[src];
    }
}
static Value nqpBufRead(const std::string& bytes, long long off,
                        int nbytes, int endian, char kind) {
    // Reading past the end THROWS; it does not answer zero. A decoder driving
    // itself off a buffer relies on that to stop — CBOR::Simple catches this
    // very message by name (`when /^ 'MVMArray: read_buf out of bounds' /`), and
    // without it `cbor-decode` got an endless supply of 0 bytes and never
    // terminated: 278,159 loop iterations in five seconds where Rakudo makes
    // one. The wording is MoarVM's because the dist matches on it — the one case
    // where copying upstream's prose is the requirement, not a habit.
    // `elems` counts this buffer's bytes, which is exact for the blob8 every
    // caller here uses and approximate for wider element types.
    if (off < 0 || off + nbytes > (long long)bytes.size())
        throw RakuError{Value::typeObj("X::AdHoc"),
            "MVMArray: read_buf out of bounds offset " + std::to_string(off) +
            " start 0 elems " + std::to_string(bytes.size()) +
            " count " + std::to_string(nbytes)};
    unsigned char raw[8] = {0};
    for (int i = 0; i < nbytes; i++) {
        long long p = off + i;
        if (p < 0 || p >= (long long)bytes.size()) continue;
        int dst = (binFlagLittle(endian) == g_hostLittle) ? i : nbytes - 1 - i;
        raw[dst] = (unsigned char)bytes[p];
    }
    if (kind == 'n') {
        if (nbytes == 4) { float f;  std::memcpy(&f, raw, 4); return Value::number((double)f); }
        double d; std::memcpy(&d, raw, 8); return Value::number(d);
    }
    unsigned long long u = 0; std::memcpy(&u, raw, nbytes <= 8 ? nbytes : 8);
    if (kind == 'i') { // sign-extend from nbytes
        if (nbytes < 8 && (u & (1ULL << (nbytes * 8 - 1)))) u |= ~((1ULL << (nbytes * 8)) - 1);
        return Value::integer((long long)u);
    }
    if (nbytes == 8 && (u >> 63)) { // uint64 beyond long long
        BigInt b((long long)(u & 0x7FFFFFFFFFFFFFFFULL));
        return Value::bigint(b + BigInt(2).pow(63));
    }
    return Value::integer((long long)u);
}

// ---- Unicode property codes -------------------------------------------------
// nqp::unipropcode answers MoarVM's NUMBER for a property, and the other
// Unicode ops take that number in place of the name: String::Utils keeps it in
// a `my int $gcprop`, so a name standing in for the code no longer fits.
// The numbers, and every name that reaches one, were read off Rakudo 2026.09 /
// MoarVM 2026.09, one probe per name. MoarVM files VALUE names under their
// property too ("Lu" is General_Category's 20, "W" East_Asian_Width's 7,
// "Narrow" Decomposition_Type's 19), and a name written in lower case matches
// case-insensitively ("alpha", "lu"); "ALPHA" is 0, as is any unknown name.
// Script and Block VALUE names ("Latin", "Basic_Latin") are the gap: MoarVM
// files them under 9 and 6, and here they answer 0.
namespace {
struct UniPropCodeEnt { const char* name; int code; };
const UniPropCodeEnt kUniPropCodes[] = {
    {"Joining_Group", 3}, {"jg", 3}, {"Case_Folding", 4}, {"cf", 4},
    {"Bidi_Mirroring_Glyph", 5}, {"bmg", 5}, {"ASCII", 6}, {"Block", 6}, {"blk", 6},
    {"Ambiguous", 7}, {"East_Asian_Width", 7}, {"F", 7}, {"Fullwidth", 7}, {"H", 7},
    {"Halfwidth", 7}, {"Na", 7}, {"Neutral", 7}, {"W", 7}, {"ea", 7}, {"Numeric_Value", 8},
    {"nv", 8}, {"Script", 9}, {"sc", 9}, {"Numeric_Value_Numerator", 10}, {"InCB", 12},
    {"Indic_Conjunct_Break", 12}, {"A", 13}, {"Canonical_Combining_Class", 13}, {"ccc", 13},
    {"InSC", 14}, {"Indic_Syllabic_Category", 14}, {"Line_Break", 15}, {"lb", 15}, {"Age", 16},
    {"age", 16}, {"Bidi_Class", 17}, {"bc", 17}, {"GCB", 18}, {"Grapheme_Cluster_Break", 18},
    {"Prepend", 18}, {"Decomposition_Type", 19}, {"Narrow", 19}, {"Wide", 19}, {"dt", 19},
    {"Cc", 20}, {"Cf", 20}, {"Close_Punctuation", 20}, {"Cn", 20}, {"Co", 20},
    {"Connector_Punctuation", 20}, {"Control", 20}, {"Cs", 20}, {"Currency_Symbol", 20},
    {"Dash_Punctuation", 20}, {"Decimal_Number", 20}, {"Enclosing_Mark", 20},
    {"Final_Punctuation", 20}, {"Format", 20}, {"General_Category", 20},
    {"Initial_Punctuation", 20}, {"Letter_Number", 20}, {"Line_Separator", 20}, {"Ll", 20},
    {"Lm", 20}, {"Lo", 20}, {"Lowercase_Letter", 20}, {"Lt", 20}, {"Lu", 20},
    {"Math_Symbol", 20}, {"Mc", 20}, {"Me", 20}, {"Mn", 20}, {"Modifier_Letter", 20},
    {"Modifier_Symbol", 20}, {"Nd", 20}, {"Nl", 20}, {"No", 20}, {"Nonspacing_Mark", 20},
    {"Open_Punctuation", 20}, {"Other_Letter", 20}, {"Other_Number", 20},
    {"Other_Punctuation", 20}, {"Other_Symbol", 20}, {"Paragraph_Separator", 20}, {"Pc", 20},
    {"Pd", 20}, {"Pe", 20}, {"Pf", 20}, {"Pi", 20}, {"Po", 20}, {"Private_Use", 20}, {"Ps", 20},
    {"Sc", 20}, {"Sk", 20}, {"Sm", 20}, {"So", 20}, {"Space_Separator", 20},
    {"Spacing_Mark", 20}, {"Surrogate", 20}, {"Titlecase_Letter", 20}, {"Unassigned", 20},
    {"Uppercase_Letter", 20}, {"Zl", 20}, {"Zp", 20}, {"Zs", 20}, {"cntrl", 20}, {"digit", 20},
    {"gc", 20}, {"Numeric_Value_Denominator", 21}, {"WB", 22}, {"Word_Break", 22}, {"InPC", 23},
    {"Indic_Positional_Category", 23}, {"na", 23}, {"SB", 24}, {"Sentence_Break", 24},
    {"Hangul_Syllable_Type", 25}, {"hst", 25}, {"AHex", 26}, {"ASCII_Hex_Digit", 26},
    {"Joining_Type", 27}, {"jt", 27}, {"NFC_QC", 28}, {"NFC_Quick_Check", 28}, {"NFG_QC", 29},
    {"NFKC_QC", 30}, {"NFKC_Quick_Check", 30}, {"Numeric_Type", 31}, {"nt", 31},
    {"Vertical_Orientation", 32}, {"vo", 32}, {"Alpha", 33}, {"Alphabetic", 33}, {"Any", 34},
    {"Assigned", 35}, {"Bidi_C", 36}, {"Bidi_Control", 36}, {"Bidi_M", 37},
    {"Bidi_Mirrored", 37}, {"C", 38}, {"Other", 38}, {"CI", 40}, {"Case_Ignorable", 40},
    {"Cased", 41}, {"CWCF", 42}, {"Changes_When_Casefolded", 42}, {"CWCM", 43},
    {"Changes_When_Casemapped", 43}, {"CWL", 44}, {"Changes_When_Lowercased", 44},
    {"CWKCF", 45}, {"Changes_When_NFKC_Casefolded", 45}, {"CWT", 46},
    {"Changes_When_Titlecased", 46}, {"CWU", 47}, {"Changes_When_Uppercased", 47}, {"Dash", 48},
    {"DI", 49}, {"Default_Ignorable_Code_Point", 49}, {"Dep", 50}, {"Deprecated", 50},
    {"Dia", 51}, {"Diacritic", 51}, {"Emoji", 52}, {"EComp", 53}, {"Emoji_Component", 53},
    {"EMod", 54}, {"Emoji_Modifier", 54}, {"EBase", 55}, {"Emoji_Modifier_Base", 55},
    {"EPres", 56}, {"Emoji_Presentation", 56}, {"ExtPict", 57}, {"Extended_Pictographic", 57},
    {"Ext", 58}, {"Extender", 58}, {"Comp_Ex", 59}, {"Full_Composition_Exclusion", 59},
    {"Gr_Base", 60}, {"Grapheme_Base", 60}, {"Gr_Ext", 61}, {"Grapheme_Extend", 61},
    {"Gr_Link", 62}, {"Grapheme_Link", 62}, {"Hex", 63}, {"Hex_Digit", 63}, {"Hyphen", 64},
    {"IDSB", 65}, {"IDS_Binary_Operator", 65}, {"IDST", 66}, {"IDS_Trinary_Operator", 66},
    {"IDSU", 67}, {"IDS_Unary_Operator", 67}, {"ID_Compat_Math_Continue", 68},
    {"ID_Compat_Math_Start", 69}, {"IDC", 70}, {"ID_Continue", 70}, {"IDS", 71},
    {"ID_Start", 71}, {"Ideo", 72}, {"Ideographic", 72}, {"Join_C", 73}, {"Join_Control", 73},
    {"L", 74}, {"Letter", 74}, {"Cased_Letter", 75}, {"LC", 75}, {"lc", 75}, {"LOE", 76},
    {"Logical_Order_Exception", 76}, {"Lower", 77}, {"Lowercase", 77}, {"Combining_Mark", 78},
    {"M", 78}, {"Mark", 78}, {"MVM_COLLATION_QC", 79}, {"Math", 80}, {"MCM", 81},
    {"Modifier_Combining_Mark", 81}, {"N", 82}, {"Number", 82}, {"NFD_QC", 83},
    {"NFD_Quick_Check", 83}, {"MVM_COLLATION_PRIMARY", 84}, {"MVM_COLLATION_SECONDARY", 85},
    {"NFKD_QC", 86}, {"NFKD_Quick_Check", 86}, {"NChar", 87}, {"Noncharacter_Code_Point", 87},
    {"OAlpha", 88}, {"Other_Alphabetic", 88}, {"MVM_COLLATION_TERTIARY", 89}, {"ODI", 90},
    {"Other_Default_Ignorable_Code_Point", 90}, {"OGr_Ext", 91}, {"Other_Grapheme_Extend", 91},
    {"OIDC", 92}, {"Other_ID_Continue", 92}, {"OIDS", 93}, {"Other_ID_Start", 93},
    {"OLower", 94}, {"Other_Lowercase", 94}, {"OMath", 95}, {"Other_Math", 95}, {"OUpper", 96},
    {"Other_Uppercase", 96}, {"P", 97}, {"Punctuation", 97}, {"Pat_Syn", 98},
    {"Pattern_Syntax", 98}, {"Pat_WS", 99}, {"Pattern_White_Space", 99}, {"PCM", 100},
    {"Prepended_Concatenation_Mark", 100}, {"QMark", 101}, {"Quotation_Mark", 101},
    {"Radical", 102}, {"RI", 103}, {"Regional_Indicator", 103}, {"S", 104}, {"Symbol", 104},
    {"STerm", 105}, {"Sentence_Terminal", 105}, {"SD", 106}, {"Soft_Dotted", 106},
    {"Term", 107}, {"Terminal_Punctuation", 107}, {"UIdeo", 108}, {"Unified_Ideograph", 108},
    {"Upper", 109}, {"Uppercase", 109}, {"VS", 110}, {"Variation_Selector", 110},
    {"WSpace", 111}, {"White_Space", 111}, {"space", 111}, {"XIDC", 112}, {"XID_Continue", 112},
    {"XIDS", 113}, {"XID_Start", 113}, {"Separator", 114}, {"Z", 114},
};
// The property each code reads back as, by the name `.uniprop` knows it by; ""
// for the four codes no name reaches. The General_Category groups (C, L, LC,
// M, N, P, S, Z) and Any / Assigned are tested against the category directly.
const char* const kUniPropCodeNames[] = {
    "", "", "", "Joining_Group", "Case_Folding", "Bidi_Mirroring_Glyph", "Block",
    "East_Asian_Width", "Numeric_Value", "Script", "Numeric_Value_Numerator", "",
    "Indic_Conjunct_Break", "Canonical_Combining_Class", "Indic_Syllabic_Category",
    "Line_Break", "Age", "Bidi_Class", "Grapheme_Cluster_Break", "Decomposition_Type",
    "General_Category", "Numeric_Value_Denominator", "Word_Break", "Indic_Positional_Category",
    "Sentence_Break", "Hangul_Syllable_Type", "ASCII_Hex_Digit", "Joining_Type",
    "NFC_Quick_Check", "NFG_QC", "NFKC_Quick_Check", "Numeric_Type", "Vertical_Orientation",
    "Alphabetic", "Any", "Assigned", "Bidi_Control", "Bidi_Mirrored", "C", "", "Case_Ignorable",
    "Cased", "Changes_When_Casefolded", "Changes_When_Casemapped", "Changes_When_Lowercased",
    "Changes_When_NFKC_Casefolded", "Changes_When_Titlecased", "Changes_When_Uppercased",
    "Dash", "Default_Ignorable_Code_Point", "Deprecated", "Diacritic", "Emoji",
    "Emoji_Component", "Emoji_Modifier", "Emoji_Modifier_Base", "Emoji_Presentation",
    "Extended_Pictographic", "Extender", "Full_Composition_Exclusion", "Grapheme_Base",
    "Grapheme_Extend", "Grapheme_Link", "Hex_Digit", "Hyphen", "IDS_Binary_Operator",
    "IDS_Trinary_Operator", "IDS_Unary_Operator", "ID_Compat_Math_Continue",
    "ID_Compat_Math_Start", "ID_Continue", "ID_Start", "Ideographic", "Join_Control", "L", "LC",
    "Logical_Order_Exception", "Lowercase", "M", "MVM_COLLATION_QC", "Math",
    "Modifier_Combining_Mark", "N", "NFD_Quick_Check", "MVM_COLLATION_PRIMARY",
    "MVM_COLLATION_SECONDARY", "NFKD_Quick_Check", "Noncharacter_Code_Point",
    "Other_Alphabetic", "MVM_COLLATION_TERTIARY", "Other_Default_Ignorable_Code_Point",
    "Other_Grapheme_Extend", "Other_ID_Continue", "Other_ID_Start", "Other_Lowercase",
    "Other_Math", "Other_Uppercase", "P", "Pattern_Syntax", "Pattern_White_Space",
    "Prepended_Concatenation_Mark", "Quotation_Mark", "Radical", "Regional_Indicator", "S",
    "Sentence_Terminal", "Soft_Dotted", "Terminal_Punctuation", "Unified_Ideograph",
    "Uppercase", "Variation_Selector", "White_Space", "XID_Continue", "XID_Start", "Z",
};
constexpr long long kUniPropCodeMax = (long long)(sizeof(kUniPropCodeNames) / sizeof(*kUniPropCodeNames)) - 1;

// MoarVM's value numbers for the two enumerated properties ecosystem code
// compares against: getuniprop_int of General_Category is 6 for Mn (String::
// Utils' nomark drops those), and nqp::unipvalcode answers the same numbers
// from either spelling of a value. Every other value is interned (below).
const std::map<std::string, long long>& uniGcValueCodes() {
    static const std::map<std::string, long long> m = {
        {"Cn", 0},  {"Lu", 1},  {"Ll", 2},  {"Lt", 3},  {"Lm", 4},  {"Lo", 5},
        {"Mn", 6},  {"Me", 7},  {"Mc", 8},  {"Nd", 9},  {"Nl", 10}, {"No", 11},
        {"Zs", 12}, {"Zl", 13}, {"Zp", 14}, {"Cc", 15}, {"Cf", 16}, {"Co", 17},
        {"Cs", 18}, {"Pd", 19}, {"Ps", 20}, {"Pe", 21}, {"Pc", 22}, {"Po", 23},
        {"Sm", 24}, {"Sc", 25}, {"Sk", 26}, {"So", 27}, {"Pi", 28}, {"Pf", 29},
        {"Unassigned", 0}, {"Uppercase_Letter", 1}, {"Lowercase_Letter", 2},
        {"Titlecase_Letter", 3}, {"Modifier_Letter", 4}, {"Other_Letter", 5},
        {"Nonspacing_Mark", 6}, {"Enclosing_Mark", 7}, {"Spacing_Mark", 8},
        {"Decimal_Number", 9}, {"digit", 9}, {"Letter_Number", 10}, {"Other_Number", 11},
        {"Space_Separator", 12}, {"Line_Separator", 13}, {"Paragraph_Separator", 14},
        {"Control", 15}, {"cntrl", 15}, {"Format", 16}, {"Private_Use", 17},
        {"Surrogate", 18}, {"Dash_Punctuation", 19}, {"Open_Punctuation", 20},
        {"Close_Punctuation", 21}, {"Connector_Punctuation", 22}, {"Other_Punctuation", 23},
        {"Math_Symbol", 24}, {"Currency_Symbol", 25}, {"Modifier_Symbol", 26},
        {"Other_Symbol", 27}, {"Initial_Punctuation", 28}, {"Final_Punctuation", 29}};
    return m;
}
const std::map<std::string, long long>& uniEawValueCodes() {
    static const std::map<std::string, long long> m = {
        {"N", 0}, {"A", 1}, {"H", 2}, {"W", 3}, {"F", 4}, {"Na", 5},
        {"Neutral", 0}, {"Ambiguous", 1}, {"Halfwidth", 2}, {"Wide", 3},
        {"Fullwidth", 4}, {"Narrow", 5}};
    return m;
}
// Values of the other string-valued properties get a number of their own,
// handed out on first sight and shared by unipvalcode and getuniprop_int, so
// matchuniprop's `getuniprop_int(cp, prop) == value` holds for them as well.
// Above MoarVM's small value numbers, so one is never mistaken for the other.
long long uniInternedValueCode(const std::string& s) {
    static std::mutex mu;
    static std::unordered_map<std::string, long long> ids;
    std::lock_guard<std::mutex> g(mu);
    auto it = ids.find(s);
    if (it != ids.end()) return it->second;
    long long id = 1000 + (long long)ids.size();
    ids.emplace(s, id);
    return id;
}
bool uniIsGcGroup(const std::string& p) {
    return p == "C" || p == "L" || p == "LC" || p == "M" || p == "N" || p == "P" ||
           p == "S" || p == "Z" || p == "Any" || p == "Assigned";
}
} // namespace

static long long uniPropCodeOf(const std::string& name) {
    for (auto& e : kUniPropCodes) if (name == e.name) return e.code;
    if (std::any_of(name.begin(), name.end(), [](char c) { return c >= 'A' && c <= 'Z'; })) return 0;
    for (auto& e : kUniPropCodes) {
        const char* k = e.name; size_t i = 0;
        while (k[i] && i < name.size() && ascii::tolower((unsigned char)k[i]) == name[i]) ++i;
        if (!k[i] && i == name.size()) return e.code;
    }
    return 0;
}

// The property an op's second argument names: MoarVM's number, or (as older
// rakupp code passed it) the name itself. "" when the number names nothing.
static std::string uniPropNameOf(const Value& p) {
    if (p.t == VT::Str) return p.toStr();
    long long c = p.toInt();
    return c > 0 && c <= kUniPropCodeMax ? kUniPropCodeNames[c] : "";
}

// nqp::unipvalcode(prop, value): MoarVM's number for a value of a property.
// A binary property has no value names (MoarVM answers 0 for "True" and "Y"
// alike — its regexes match those against a literal 1).
static long long uniPvalCodeOf(const std::string& prop, const std::string& value) {
    if (prop.empty()) return 0;
    if (prop == "General_Category") {
        auto it = uniGcValueCodes().find(value);
        return it == uniGcValueCodes().end() ? 0 : it->second;
    }
    if (prop == "East_Asian_Width") {
        auto it = uniEawValueCodes().find(value);
        return it == uniEawValueCodes().end() ? 0 : it->second;
    }
    if (uniIsGcGroup(prop) || uniBinaryProp(0, prop) >= 0) return 0;
    return uniInternedValueCode(value);
}

// ---- the `use nqp` compatibility subset ------------------------------------
// Reached only through NK::NqpOp nodes, which exist only in units that said
// `use nqp` — every other program pays nothing for any of this.
// Docs: docs/dev/MODULE-FINDINGS.md #4b. Int ops are plain int64; string ops
// are codepoint-indexed; comparisons return Int 1/0 (nqp truthiness).
Value Interpreter::evalNqpOp(NqpOp* n) {
    using O = NqpOpc;
    auto& a = n->args;
    // lazy forms first: they control their own argument evaluation
    switch (n->op) {
        case O::Stmts: {
            Value last = Value::nil();
            for (auto& e : a) {
                last = eval(e.get());
                // a cooperative return/last/next inside the sequence (no callable
                // boundary) sets a flag rather than throwing — stop evaluating
                // the rest and let it propagate (JSON::Fast's parse loops `return`
                // out of nqp::while(1, nqp::stmts(…)))
                if (tctx_.returning || tctx_.loopCtl) return last;
            }
            return last;
        }
        case O::While:
        case O::Until: {
            if (a.size() < 2) return Value::nil();
            long long guard = 0;
            while (boolify(eval(a[0].get())) == (n->op == O::While)) {
                if (tctx_.returning) return Value::nil();
                for (size_t i = 1; i < a.size(); i++) {
                    eval(a[i].get());
                    if (tctx_.returning) return Value::nil(); // cooperative return escapes
                    if (tctx_.loopCtl == 2) { tctx_.loopCtl = 0; return Value::nil(); } // last
                    if (tctx_.loopCtl == 1) { tctx_.loopCtl = 0; break; }               // next
                }
                if (++guard > 1000000000LL) break; // runaway backstop
            }
            return Value::nil();
        }
        // nqp::repeat_while(cond, body) / repeat_until — the body runs ONCE
        // before the test is ever taken, which is the whole difference from
        // While/Until above. `are` drives its type-agreement scan with it.
        case O::RepeatWhile:
        case O::RepeatUntil: {
            if (a.size() < 2) return Value::nil();
            long long guard = 0;
            do {
                if (tctx_.returning) return Value::nil();
                for (size_t i = 1; i < a.size(); i++) {
                    eval(a[i].get());
                    if (tctx_.returning) return Value::nil();
                    if (tctx_.loopCtl == 2) { tctx_.loopCtl = 0; return Value::nil(); } // last
                    if (tctx_.loopCtl == 1) { tctx_.loopCtl = 0; break; }               // next
                }
                if (++guard > 1000000000LL) break; // runaway backstop, as While has
            } while (boolify(eval(a[0].get())) == (n->op == O::RepeatWhile));
            return Value::nil();
        }
        case O::IfNull: {
            Value v = eval(a[0].get());
            if (v.t == VT::Nil || v.t == VT::Any) return a.size() > 1 ? eval(a[1].get()) : Value::nil();
            return v;
        }
        // nqp::handle(expr, 'CATCH', handler, …) — run expr; an exception runs
        // the matching handler, whose value is the op's. `paths` guards its
        // opendir with it: `nqp::handle(($!handle := nqp::opendir($p)), 'CATCH', 0)`.
        case O::Handle: {
            if (a.empty()) return Value::nil();
            try { return eval(a[0].get()); }
            catch (RakuError&) {
                for (size_t i = 1; i + 1 < a.size(); i += 2)
                    if (eval(a[i].get()).toStr() == "CATCH") return eval(a[i + 1].get());
                return Value::nil();
            }
        }
        // nqp::bindattr(@container, T, '$!reified'/'$!storage', $buffer) rebinds
        // the container's BACKING STORE to the buffer — they must then SHARE it
        // (pushes to the buffer show through the container). Needs the caller's
        // lvalue: the container's shared_ptr is repointed at the buffer's, so
        // both alias one vector/map. (Value-copy semantics can't express this.)
        case O::Bindattr:
        case O::P6BindAttrInvRes: {
            if (a.size() >= 4) {
                std::string an = eval(a[2].get()).toStr();
                if (an == "$!reified" || an == "$!storage" || an == "$!array") {
                    Value* lv = nullptr;
                    try { lv = lvalue(a[0].get()); } catch (RakuError&) {}
                    Value buf = eval(a[3].get());
                    // …but these are only the CONTAINER's backing store when the
                    // target is a container. A user class may declare an
                    // attribute of the same name and mean nothing of the kind —
                    // `class ReverseIterator does Iterator { has $!reified }`
                    // does, and reinterpreting the bind REPLACED the object with
                    // a bare Array, so `new` handed back an Array and the first
                    // `.pull-one` on it died. An object whose class really
                    // declares the attribute takes the ordinary bind below.
                    if (lv && lv->t == VT::Object && lv->obj() && lv->obj()->cls &&
                        lv->obj()->cls->findAttr(an.size() > 2 ? an.substr(2) : an))
                        break;
                    if (lv) {
                        if (buf.t == VT::Array || lv->t == VT::Array) {
                            if (lv->t != VT::Array) *lv = Value::array();
                            if (buf.t == VT::Array && buf.arr()) lv->setArr(buf.arrS()); // SHARE
                        } else if (buf.t == VT::Hash) {
                            lv->t = VT::Hash; lv->setHash(buf.hashS());               // SHARE
                        }
                        return n->op == O::P6BindAttrInvRes ? *lv : buf;
                    }
                }
            }
            break; // ordinary attr bind — fall through to the eager path
        }
        case O::OpenFh: { // nqp::open(path, mode) — a raw OS handle
            // (Crypt::Random reads /dev/urandom through exactly this trio)
            if (a.empty()) return Value::nil();
            Value pathv = eval(a[0].get());
            std::string mode = a.size() > 1 ? eval(a[1].get()).toStr() : "r";
            // --sandbox: the one file it may read is the entropy device, which
            // tells the program nothing about the host
            if (g_sandboxChecks) {
                const bool writes = mode.find('w') != std::string::npos || mode.find('a') != std::string::npos;
                const std::string p = pathv.toStr();
                if (writes || (p != "/dev/urandom" && p != "/dev/random"))
                    sandboxRefuse(*this, "nqp::open", writes ? SandboxCap::Write : SandboxCap::Read);
            }
#ifdef _WIN32
            int flags = mode.find('w') != std::string::npos ? (_O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY)
                      : mode.find('a') != std::string::npos ? (_O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY)
                      : (_O_RDONLY | _O_BINARY);
            int fd = ::_open(pathv.toStr().c_str(), flags, 0644);
#else
            int flags = mode.find('w') != std::string::npos ? (O_WRONLY | O_CREAT | O_TRUNC)
                      : mode.find('a') != std::string::npos ? (O_WRONLY | O_CREAT | O_APPEND)
                      : O_RDONLY;
            int fd = ::open(pathv.toStr().c_str(), flags, 0644);
#endif
            if (fd < 0)
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Failed to open file " + pathv.toStr() + ": " + std::strerror(errno)};
            Value h = Value::makeHash(); h.hashKind = "NqpFh";
            (*h.hash())["fd"] = Value::integer(fd);
            return h;
        }
        case O::ReadFh: { // nqp::readfh(fh, buf, n) — append up to n bytes into buf
            if (a.size() < 3) return Value::nil();
            Value fhv = eval(a[0].get());
            long long want = eval(a[2].get()).toInt();
            int fd = fhv.t == VT::Hash && fhv.hash() && fhv.hash()->count("fd")
                   ? (int)(*fhv.hash())["fd"].toInt() : -1;
            std::string bytes(want > 0 ? (size_t)want : 0, '\0');
            long long got = 0;
            if (fd >= 0 && want > 0) {
                long long off = 0; // short reads are legal; loop to n or EOF
                while (off < want) {
#ifdef _WIN32
                    long long r = ::_read(fd, &bytes[(size_t)off], (unsigned)(want - off));
#else
                    long long r = ::read(fd, &bytes[(size_t)off], (size_t)(want - off));
#endif
                    if (r <= 0) break;
                    off += r;
                }
                got = off;
            }
            bytes.resize((size_t)got);
            Value* lv = nullptr;
            try { lv = lvalue(a[1].get()); } catch (RakuError&) {}
            if (lv) { // the buffer is FILLED in place (`my $bytes := Buf.new`)
                if (lv->t != VT::Str) { lv->t = VT::Str; lv->s.clear(); }
                if (lv->hashKind.empty() || lv->t != VT::Str) lv->hashKind = "Buf";
                lv->s = lv->s.str() + bytes;
                return *lv;
            }
            Value b = Value::str(std::move(bytes)); b.hashKind = "Buf";
            return b;
        }
        // nqp::stat($path, FIELD) / nqp::lstat(…) — one stat(2), one field, by the
        // MoarVM field numbering Parser::nqpConstValue already spells out. Every
        // caller in the wild reaches for a field IO::Path does not expose:
        // Path::Finder matches on inode/device/uid/gid/nlinks/blocks/blocksize/
        // devtype and keys its symlink-loop guard on inode+device.
        case O::Stat: case O::Lstat: case O::StatTime: case O::LstatTime: {
            if (g_sandboxChecks) sandboxRefuse(*this, "nqp::stat", SandboxCap::Read);
            if (a.size() < 2) return Value::integer(-1);
            const std::string path = eval(a[0].get()).toStr();
            const long long field = eval(a[1].get()).toInt();
            const bool viaLink = n->op == O::Lstat || n->op == O::LstatTime;
            // the `_time` spellings answer a Num (MoarVM's are fractional
            // seconds); path-utils' accessors are typed on it
            const bool asNum = n->op == O::StatTime || n->op == O::LstatTime;
            auto timeVal = [&](long long secs) { return asNum ? Value::number((double)secs) : Value::integer(secs); };
#ifdef _WIN32
            struct ::_stat64 st;
            const bool ok = ::_stat64(path.c_str(), &st) == 0;
            const bool lok = ok;                       // no lstat on Windows,
            (void)viaLink;                             // so the link/follow choice is moot
            auto& lst = st;
#else
            struct ::stat st, lst;
            // ISLNK always needs the link's OWN inode, whichever op was called;
            // everything else follows the link unless this is nqp::lstat
            const bool lok = ::lstat(path.c_str(), &lst) == 0;
            const bool ok = viaLink ? lok : ::stat(path.c_str(), &st) == 0;
            if (viaLink) st = lst;
#endif
            // STAT_EXISTS answers the question rather than failing it. Every other
            // field on an unstattable path THROWS, as Rakudo does — a silent -1
            // would read as a real inode or uid to a caller comparing numbers.
            if (field == 0) return Value::integer(ok ? 1 : 0);
            if (field == 12 && lok) return Value::integer(S_ISLNK(lst.st_mode) ? 1 : 0);
            if (!ok || (field == 12 && !lok))
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Failed to stat file " + path + ": " + std::strerror(errno)};
            switch (field) {
                case  1: return Value::integer((long long)st.st_size);      // FILESIZE
                case  2: return Value::integer(S_ISDIR(st.st_mode) ? 1 : 0); // ISDIR
                case  3: return Value::integer(S_ISREG(st.st_mode) ? 1 : 0); // ISREG
                case  4: return Value::integer(S_ISCHR(st.st_mode) ||        // ISDEV
                                               S_ISBLK(st.st_mode) ? 1 : 0);
                case  6: return timeVal((long long)st.st_atime);            // ACCESSTIME
                case  7: return timeVal((long long)st.st_mtime);            // MODIFYTIME
                case  8: return timeVal((long long)st.st_ctime);            // CHANGETIME
                case 10: return Value::integer((long long)st.st_uid);       // UID
                case 11: return Value::integer((long long)st.st_gid);       // GID
                case -1: return Value::integer((long long)st.st_dev);       // PLATFORM_DEV
                case -2: return Value::integer((long long)st.st_ino);       // PLATFORM_INODE
                case -3: return Value::integer((long long)st.st_mode);      // PLATFORM_MODE
                case -4: return Value::integer((long long)st.st_nlink);     // PLATFORM_NLINKS
                case -5: return Value::integer((long long)st.st_rdev);      // PLATFORM_DEVTYPE
                default: break;
            }
#ifndef _WIN32
            // st_blksize / st_blocks are POSIX-only
            if (field == -6) return Value::integer((long long)st.st_blksize); // PLATFORM_BLOCKSIZE
            if (field == -7) return Value::integer((long long)st.st_blocks);  // PLATFORM_BLOCKS
#endif
            // CREATETIME: wherever the platform keeps the birth time, which is
            // not `st` on Linux — the `.created` method reads the same helper, so
            // the two spellings of this question cannot answer differently again.
            if (field == 5) {
                double b = 0;
                return timeVal(fileBirthSecs(path, st, b) ? (long long)b : 0LL);
            }
            // CREATETIME where the platform has none, and BACKUPTIME everywhere:
            // MoarVM answers 0 rather than failing
            if (field == 9) return timeVal(0);
            return Value::integer(-1); // an unknown field number
        }
        case O::CloseFh: {
            if (a.empty()) return Value::nil();
            Value fhv = eval(a[0].get());
            if (fhv.t == VT::Hash && fhv.hash() && fhv.hash()->count("fd"))
#ifdef _WIN32
                ::_close((int)(*fhv.hash())["fd"].toInt());
#else
                ::close((int)(*fhv.hash())["fd"].toInt());
#endif
            return Value::nil();
        }
        // Buffer writes mutate argument 0 in place, so they need its lvalue.
        case O::WriteUInt: case O::WriteInt: case O::WriteNum: {
            if (a.size() >= 4) {
                Value* lv = nullptr;
                try { lv = lvalue(a[0].get()); } catch (RakuError&) {}
                long long off  = eval(a[1].get()).toInt();
                Value     val  = eval(a[2].get());
                long long flag = eval(a[3].get()).toInt();
                int nb, en; decodeBinFlag(flag, nb, en);
                char kind = n->op == O::WriteNum ? 'n' : (n->op == O::WriteInt ? 'i' : 'u');
                if (lv) {
                    if (lv->t != VT::Str) { lv->t = VT::Str; lv->s.clear(); }
                    if (lv->hashKind.empty()) { lv->hashKind = "Buf"; identify(*lv); }
                    nqpBufWrite(lv->s.mut(), off, val, nb, en, kind);
                }
                return val;
            }
            break;
        }
        case O::SetElems: { // resize a buf (bytes) or array (elems) in place
            if (a.size() >= 2) {
                Value* lv = nullptr;
                try { lv = lvalue(a[0].get()); } catch (RakuError&) {}
                // …and a VALUE operand — `nqp::setelems(nqp::create(array[uint32]),
                // $n)` (String::Utils' nomark sizes its scratch buffer this way,
                // nested twice) — is resized through its shared storage and
                // handed back; it used to come back as Nil, and everything pushed
                // into the "buffer" afterwards was lost
                Value held;
                if (!lv) { held = eval(a[0].get()); lv = &held; }
                long long nn = eval(a[1].get()).toInt();
                if (nn < 0) nn = 0;
                if (lv) {
                    if (lv->t == VT::Str) lv->s.resize(nn * lv->blobElemSize(), '\0');
                    else if (lv->t == VT::Array && lv->arr()) lv->arr()->resize(nn, Value::number(0));
                }
                return lv ? *lv : Value::nil();
            }
            break;
        }
        case O::BindposN: { // native-num element store
            if (a.size() >= 3) {
                Value* lv = nullptr;
                try { lv = lvalue(a[0].get()); } catch (RakuError&) {}
                long long idx = eval(a[1].get()).toInt();
                Value val = eval(a[2].get());
                if (lv && lv->t == VT::Array && lv->arr() && idx >= 0) {
                    if ((long long)lv->arr()->size() <= idx) lv->arr()->resize(idx + 1, Value::number(0));
                    (*lv->arr())[idx] = Value::number(val.toNum());
                }
                return val;
            }
            break;
        }
        case O::Splice: { // nqp::splice(target, source, offset, count)
            // Buf/Blob bytes live in Value::s by value (not shared like arrays),
            // so a byte-buffer splice must mutate through the lvalue. Arrays share
            // their backing vector, so they fall through to the eager path.
            Value src0 = a.size() > 0 ? eval(a[0].get()) : Value::nil();
            if (src0.t == VT::Str) {
                Value* lv = nullptr;
                try { lv = lvalue(a[0].get()); } catch (RakuError&) {}
                Value    src = a.size() > 1 ? eval(a[1].get()) : Value::str("");
                long long off = a.size() > 2 ? eval(a[2].get()).toInt() : 0;
                long long cnt = a.size() > 3 ? eval(a[3].get()).toInt() : 0;
                if (lv) {
                    std::string& t = lv->s.mut();
                    if (off < 0) off = 0;
                    if (off > (long long)t.size()) t.resize(off, '\0');
                    if (cnt < 0 || off + cnt > (long long)t.size()) cnt = t.size() - off;
                    t.replace(off, cnt, src.s);
                }
                return lv ? *lv : src0;
            }
            break; // array splice: eager path (shared backing)
        }
        default: break;
    }
    // The argument list comes from the per-thread depth-indexed pool rather than
    // a fresh vector: see ExecContext::nqpArgs. The guard both restores the depth
    // and clears the buffer on every exit path, including a throw — clearing is
    // what keeps argument lifetimes identical to the old local-vector version,
    // and it is why the capacity (not the contents) is what gets reused.
    if (tctx_.nqpDepth >= tctx_.nqpArgs.size()) tctx_.nqpArgs.emplace_back();
    ValueList& v = tctx_.nqpArgs[tctx_.nqpDepth];
    struct ArgGuard {
        ExecContext& t; ValueList& buf;
        ArgGuard(ExecContext& tc, ValueList& b) : t(tc), buf(b) { ++t.nqpDepth; }
        ~ArgGuard() { --t.nqpDepth; buf.clear(); }
    } argGuard{tctx_, v};
    v.clear();
    v.reserve(a.size());
    // nqp ops operate on CONTAINERS: a variable holding a Proxy passes the proxy
    // itself (nqp::istype_nd($attr-var, AttrProxy) / nqp::iscont must see it),
    // where an ordinary Raku read would FETCH. Ops that want the value decont
    // explicitly.
    for (auto& e : a) {
        if (e->kind == NK::VarExpr) {
            auto* ve = static_cast<VarExpr*>(e.get());
            if (Value* p = tctx_.cur->find(ve->name)) { v.push_back(*p); continue; }
        }
        v.push_back(eval(e.get()));
    }
    // `nqp::create(self)` on a USER class makes an instance of that class —
    // uninitialised attributes, no BUILD — which is what a hand-rolled `new`
    // then fills (Hash::int binds `$!hash` through p6bindattrinvres). The
    // shared leaf op knows only the core REPRs and answered a bare buffer, so
    // `my %h is Hash::int` got an Array back from `.new` and fell through to
    // a plain Hash — every method the class defines silently unused.
    // The Unicode property reads go through the `uniprop` method, which knows
    // every property name and its value forms; the property arrives as MoarVM's
    // number (see uniPropCodeOf) and is read back by name. `_str` is the
    // value's string form (General_Category → "Lu", East_Asian_Width → "W";
    // "" for a binary property, as MoarVM has it), `_bool` 0/1, and `_int` a
    // value NUMBER — the one unipvalcode answers, so matchuniprop is exactly
    // `getuniprop_int(cp, prop) == value`, and hasuniprop that at a position.
    if ((n->op == NqpOpc::GetUniPropStr || n->op == NqpOpc::GetUniPropBool ||
         n->op == NqpOpc::GetUniPropInt || n->op == NqpOpc::MatchUniProp ||
         n->op == NqpOpc::HasUniProp) && v.size() >= 2) {
        long long cp;
        size_t pi = 1;
        if (n->op == NqpOpc::HasUniProp) {
            // codepoint-indexed, as nqp::ordat is; past either end matches nothing
            if (v.size() < 4) return Value::integer(0);
            std::vector<uint32_t> cps = utf8cp(v[0].toStr());
            long long at = v[1].toInt();
            if (at < 0 || at >= (long long)cps.size()) return Value::integer(0);
            cp = cps[(size_t)at];
            pi = 2;
        } else {
            cp = v[0].toInt();
        }
        const std::string prop = uniPropNameOf(v[pi]);
        Value r;
        if (prop.empty()) r = Value::boolean(false);
        else if (uniIsGcGroup(prop)) {
            const std::string gc = uniGeneralCategory((uint32_t)cp);
            bool in = prop == "Any" ? true
                    : prop == "Assigned" ? gc != "Cn"
                    : prop == "LC" ? (gc == "Lu" || gc == "Ll" || gc == "Lt")
                    : gc[0] == prop[0];
            r = Value::boolean(in);
        } else {
            ValueList pa; pa.push_back(Value::str(prop));
            r = methodCall(Value::integer(cp), "uniprop", pa);
        }
        if (n->op == NqpOpc::GetUniPropStr) return Value::str(r.t == VT::Bool ? std::string() : r.toStr());
        if (n->op == NqpOpc::GetUniPropBool) return Value::integer(r.truthy() ? 1 : 0);
        long long code;
        if (r.t == VT::Bool) code = r.truthy() ? 1 : 0;
        else if (r.t == VT::Str && (prop == "General_Category" || prop == "East_Asian_Width")) {
            const auto& tbl = prop == "General_Category" ? uniGcValueCodes() : uniEawValueCodes();
            auto g = tbl.find(r.toStr());
            code = g != tbl.end() ? g->second : 0;
        }
        else if (r.t == VT::Str) code = uniInternedValueCode(r.toStr());
        else code = r.toInt();
        if (n->op == NqpOpc::GetUniPropInt) return Value::integer(code);
        return Value::integer(code == v[pi + 1].toInt() ? 1 : 0);
    }
    if (n->op == NqpOpc::Create && v.size() == 1 && v[0].t == VT::Type) {
        std::string tn = v[0].s;
        auto it = classes_.find(tn);
        if (it == classes_.end()) it = classes_.find(resolveClassAlias(tn));
        if (it != classes_.end() && it->second) {
            std::string bare = tn;
            if (auto q = bare.rfind("::"); q != std::string::npos) bare = bare.substr(q + 2);
            static const std::set<std::string> kCoreRepr = {
                "Map", "Hash", "IterationMap", "List", "Uni", "NFC", "NFD", "NFKC", "NFKD",
                "IterationBuffer", "Array" };
            if (!kCoreRepr.count(bare)) {
                Value o; o.t = VT::Object; o.setObj(makePayload<ObjectData>());
                o.obj()->cls = it->second;
                return o;
            }
        }
    }
    // `nqp::can($obj, "name")` stays HERE rather than in rtNqpOp: the answer is
    // a method lookup, and it is usually asked of a META-OBJECT
    // (`nqp::can($type.HOW, "roles")` — RakuAST::Utils gates a parameterized
    // type that way), whose methods are built in rather than declared. Routing
    // it through `.can` reuses whatever that already knows; the free function
    // has no interpreter to ask.
    if (n->op == O::Can) {
        if (v.size() < 2) return Value::integer(0);
        ValueList ca{v[1]};
        Value r;
        try { r = methodCall(v[0], "can", ca); } catch (...) { return Value::integer(0); }
        return Value::integer(r.truthy() ? 1 : 0);
    }
    // `nqp::box_i($n, SomeType)` — the TYPE argument is load-bearing when it
    // names a user class deriving a built-in scalar. Array::Sorted::Util
    // reports "not found" as `nqp::box_i($i, NotFound)`, where
    // `class NotFound is Int { method defined(--> False) { } }` — the caller
    // then asks `.defined`, so a plain Int is the wrong answer and every
    // not-found test fails. Build the same boxed-object shape `.new` produces
    // for such a class: the user class on the outside, the native value inside,
    // so it numifies as the value and still answers its own methods.
    if ((n->op == O::BoxI || n->op == O::BoxN || n->op == O::P6BoxS) &&
        v.size() > 1 && v[1].t == VT::Type) {
        std::string tn = v[1].s;
        auto it = classes_.find(tn);
        if (it == classes_.end()) it = classes_.find(resolveClassAlias(tn));
        if (it != classes_.end() && it->second) {
            auto od = makePayload<ObjectData>();
            od->cls = it->second;
            od->hasBoxed = true;
            od->boxed() = n->op == O::BoxI ? Value::integer(v[0].toInt())
                      : n->op == O::BoxN ? Value::number(v[0].toNum())
                                         : Value::str(v[0].toStr());
            return Value::object(od);
        }
        // an unregistered name is a CORE type (Int/Num/Str): fall through to
        // the plain leaf below, which is already the right representation
    }
    return rtNqpOp(n->op, v); // eager leaf ops — shared with native codegen
}

// The eager (non-control) nqp ops, operating on ALREADY-evaluated arguments.
// Free function so native `--exe` codegen can call it directly: the lazy control
// forms (Stmts/While/Until/IfNull) are emitted as native C++ by the codegen and
// nqp::if/unless are Ternaries, so only these leaf ops need a runtime entry.
Value rtNqpOp(NqpOpc op, ValueList& v) {
    using O = NqpOpc;
    // An IterationBuffer is a tagged HASH here, not an Array, so the list ops
    // below — every one of which tests for VT::Array — silently did nothing to
    // one. `nqp::splice($buffer, nqp::list($value), $pos, 0)` is how
    // Array::Sorted::Util inserts into a buffer, and the buffer never grew.
    // Swap in the buffer's own `items` list, which SHARES its storage, so a
    // mutation still lands in the buffer the caller holds.
    {
        static const std::set<NqpOpc> kListOps = {
            O::Elems, O::Atpos, O::AtposI, O::AtposN, O::Bindpos, O::BindposI, O::BindposN,
            O::Push, O::PushI, O::PushS, O::Pop, O::PopS, O::Shift, O::ShiftI,
            O::Splice, O::SetElems, O::Slice, O::IsList, O::List };
        if (kListOps.count(op))
            for (auto& x : v)
                if (x.t == VT::Hash && x.hashKind == "IterationBuffer" && x.hash()) {
                    auto it = x.hash()->find("items");
                    if (it != x.hash()->end() && it->second.arr()) x = it->second;
                }
    }
    // An argument may arrive as the CONTAINER a `:=`-bound attribute holds (the
    // argument loop keeps containers for the ops that ask about them); a
    // number or string read looks through it. paths' `nqp::iseq_i(
    // $!readable-files, nqp::filereadable($path))` compared the Proxy — 0 —
    // and its walker produced no file at all.
    auto held = [&](size_t i) -> Value {
        return (v[i].t == VT::Hash && v[i].hashKind == "Proxy" && v[i].hash() && g_deproxy)
               ? g_deproxy(v[i]) : v[i];
    };
    auto I = [&](size_t i) -> long long { return i < v.size() ? held(i).toInt() : 0; };
    // The bignum `_I` ops want the WHOLE argument, not a native-int view of it:
    // reading a 10^25 operand through I() would silently truncate it.
    auto A0 = [&]() -> Value { return v.empty() ? Value::integer(0) : held(0); };
    auto A1 = [&]() -> Value { return v.size() > 1 ? held(1) : Value::integer(0); };
    auto bigCmp = [](const Value& x, const Value& y) -> int {
        if (x.big() || y.big()) {
            BigInt a = x.big() ? *x.big() : BigInt(x.toInt());
            BigInt b = y.big() ? *y.big() : BigInt(y.toInt());
            return BigInt::cmp(a, b);
        }
        long long a = x.toInt(), b = y.toInt();
        return a < b ? -1 : a > b ? 1 : 0;
    };
    // By reference: a Str argument is returned as-is, so the scanning ops below
    // don't copy the whole haystack once per character examined.
    static const CowStr kEmptyStr;
    // One slot per argument: an op may hold references to two coerced operands
    // at once (nqp::concat, nqp::eqat), so they can't share a scratch buffer.
    // CowStr rather than std::string so a Str argument is handed back with its
    // cached ASCII/grapheme state intact — that cache is what keeps the
    // scanning ops below O(1) per character instead of O(position).
    CowStr sTmp[8];
    auto S = [&](size_t i) -> const CowStr& {
        if (i >= v.size()) return kEmptyStr;
        if (v[i].t == VT::Str) return v[i].s;
        if (i >= 8) { sTmp[7] = held(i).toStr(); return sTmp[7]; }
        sTmp[i] = held(i).toStr();
        return sTmp[i];
    };
    auto cclassHas = [](long long mask, uint32_t cp) -> bool {
        // masks follow Parser::nqpConstValue; only the classes real modules use
        const std::string cat = uniGeneralCategory(cp);
        bool digit = cat == "Nd";
        bool alpha = !cat.empty() && cat[0] == 'L';
        bool space = cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' ||
                     cat == "Zs" || cat == "Zl" || cat == "Zp";
        if ((mask & 1) && cat == "Lu") return true;                // UPPERCASE (String::Utils' is-uppercase)
        if ((mask & 2) && cat == "Ll") return true;                // LOWERCASE (…and is-lowercase, behind :smartcase)
        if ((mask & 8) && digit) return true;                      // NUMERIC
        if ((mask & 4) && alpha) return true;                      // ALPHABETIC
        if ((mask & 32) && space) return true;                     // WHITESPACE
        if ((mask & 2048) && (digit || alpha)) return true;        // ALPHANUMERIC
        if ((mask & 8192) && (digit || alpha || cp == '_')) return true; // WORD
        if ((mask & 4096) && (cp == '\n' || cp == '\r')) return true;    // NEWLINE
        if ((mask & 16) && ((cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'f') ||
                            (cp >= 'A' && cp <= 'F'))) return true;      // HEXADECIMAL
        return false;
    };
    switch (op) {
        case O::IseqI: return Value::integer(I(0) == I(1));
        case O::IsneI: return Value::integer(I(0) != I(1));
        case O::IsltI: return Value::integer(I(0) <  I(1));
        case O::IsleI: return Value::integer(I(0) <= I(1));
        case O::IsgeI: return Value::integer(I(0) >= I(1));
        case O::IsgtI: return Value::integer(I(0) >  I(1));
        case O::AddI:  return Value::integer(I(0) + I(1));
        case O::SubI:  return Value::integer(I(0) - I(1));
        case O::MulI:  return Value::integer(I(0) * I(1));
        case O::BitandI: return Value::integer(I(0) & I(1));
        case O::BitorI:  return Value::integer(I(0) | I(1));
        case O::BitxorI: return Value::integer(I(0) ^ I(1));
        case O::BitshiftlI: return Value::integer(I(0) << I(1));
        case O::BitshiftrI: return Value::integer(I(0) >> I(1)); // arithmetic (signed)
        // nqp::div_i FLOORS: -7 div 2 is -4, not -3. (mod_i beside it truncates
        // — MoarVM is not uniform, and the probe is the authority.) Dividing by
        // zero THROWS, where mod_i answers 0.
        case O::DivI: {
            long long x = I(0), y = I(1);
            if (!y) throw RakuError{Value::typeObj("X::AdHoc"), "Division by zero"};
            long long q = x / y, r = x % y;
            if (r && ((r ^ y) < 0)) q--;          // C truncates; step down to the floor
            return Value::integer(q);
        }
        case O::IsFalse: return Value::integer(!v.empty() && v[0].truthy() ? 0 : 1);
        // nqp::print / nqp::say write a native str to the VM's OWN stdout, with
        // no stringification and — this is the part worth pinning — WITHOUT
        // consulting `$*OUT`. Rakudo does the same: a block that rebinds $*OUT
        // captures `say` and does not capture `nqp::say`. Routing these through
        // ioEmit instead would have been more useful and less true.
        case O::Print:
        case O::SayOp: {
            std::string out = v.empty() ? std::string() : v[0].toStr();
            if (op == O::SayOp) out += "\n";
            std::lock_guard<std::mutex> lk(rtOutMutex());
            std::cout << out;
            if (rtStdOutBuffer(false) == 0) std::cout.flush();
            return Value::nil();
        }
        // The string-scanning ops below each take an ASCII fast path first.
        // They are what a tokenizer written in Raku calls once per character,
        // always handing over the WHOLE text, so decoding that text per call
        // turns an O(n) scan into O(n^2) — 68 KB of JSON cost 1.9 s. On an
        // ASCII run a codepoint index is a byte index, so no decode is needed;
        // anything non-ASCII still falls through to utf8cp() unchanged.
        //
        // The ASCII test itself must be CACHED, not merely cheap: as a per-call
        // prefix scan it was still O(position), which left the quadratic exactly
        // where it was (13.9 s to parse 421 KB of JSON). cowAllAscii answers it
        // once per string — see its definition above.
        case O::Ordat: {
            const CowStr& cs = S(0);
            const std::string& s = cs.str();
            long long i = I(1);
            if (i < 0) return Value::integer(-1);
            if (cowAllAscii(cs))
                return Value::integer((size_t)i < s.size() ? (long long)(unsigned char)s[i] : -1);
            if (auto* ci = cowCpIndex(cs)) { // decode ONE codepoint, not the string
                long long ncp = (long long)ci->size() - 1;
                return Value::integer(i < ncp ? (long long)cpAtByte(s, (*ci)[(size_t)i]) : -1);
            }
            auto cps = utf8cp(s);
            return Value::integer(i < (long long)cps.size() ? (long long)cps[i] : -1);
        }
        case O::Eqat: {
            const CowStr& hc0 = S(0);
            const CowStr& ndc0 = S(1);
            const std::string& h = hc0.str();
            const std::string& nd = ndc0.str();
            long long at = I(2);
            if (at < 0) return Value::integer(0);
            size_t want = (size_t)at + nd.size();
            if (cowAllAscii(hc0) && cowAllAscii(ndc0))
                return Value::integer(want <= h.size() && h.compare((size_t)at, nd.size(), nd) == 0 ? 1 : 0);
            if (auto* ci = cowCpIndex(hc0)) {
                // UTF-8 is injective: codepoint-sequence equality IS byte
                // equality, so compare the needle's bytes at the byte offset of
                // codepoint `at` — no whole-string decode
                long long ncp = (long long)ci->size() - 1;
                if (at > ncp) return Value::integer(0);
                size_t hb = (*ci)[(size_t)at];
                return Value::integer(hb + nd.size() <= h.size() &&
                                      std::memcmp(h.data() + hb, nd.data(), nd.size()) == 0 ? 1 : 0);
            }
            auto hc = utf8cp(h), ndc = utf8cp(nd);
            if (at + (long long)ndc.size() > (long long)hc.size()) return Value::integer(0);
            for (size_t k = 0; k < ndc.size(); k++)
                if (hc[at + k] != ndc[k]) return Value::integer(0);
            return Value::integer(1);
        }
        case O::Substr: {
            const CowStr& cs = S(0);
            const std::string& s = cs.str();
            long long from = I(1);
            if (cowAllAscii(cs)) {
                long long len = v.size() > 2 ? I(2) : (long long)s.size() - from;
                if (from < 0) from = 0;
                if (from > (long long)s.size()) from = s.size();
                if (len < 0 || from + len > (long long)s.size()) len = (long long)s.size() - from;
                return Value::str(s.substr((size_t)from, (size_t)len));
            }
            if (auto* ci = cowCpIndex(cs)) { // byte slice via the cached offsets
                long long ncp = (long long)ci->size() - 1;
                long long len = v.size() > 2 ? I(2) : ncp - from;
                if (from < 0) from = 0;
                if (from > ncp) from = ncp;
                if (len < 0 || from + len > ncp) len = ncp - from;
                size_t a = (*ci)[(size_t)from];
                return Value::str(s.substr(a, (*ci)[(size_t)(from + len)] - a));
            }
            auto cps = utf8cp(s);
            long long len = v.size() > 2 ? I(2) : (long long)cps.size() - from;
            if (from < 0) from = 0;
            if (from > (long long)cps.size()) from = cps.size();
            if (len < 0 || from + len > (long long)cps.size()) len = cps.size() - from;
            std::string out;
            for (long long k = from; k < from + len; k++) out += cpToU8(cps[k]);
            return Value::str(out);
        }
        case O::Chars: {
            const CowStr& cs = S(0);
            if (cowAllAscii(cs)) return Value::integer((long long)cs.size());
            if (auto* ci = cowCpIndex(cs)) return Value::integer((long long)ci->size() - 1);
            return Value::integer((long long)utf8cp(cs.str()).size());
        }
        // NFC-composed, as Rakudo's NFG strings are: chaining nqp::concat with a
        // combining char must yield the composed grapheme (JSON::Fast's \u parser)
        case O::Concat: {
            auto known = [&](size_t i) { return i < v.size() ? nfcKnown(v[i]) : 1; };
            return Value::str(nfcConcat(S(0).str(), S(1).str(), known(0), known(1)));
        }
        // The digest of the string's UTF-8 bytes, in UPPERCASE hex — the
        // spelling MoarVM answers with, which App::RaCoCo asserts literally.
        case O::Sha1: return Value::str(sha1hex(S(0).str()));
        case O::Join: {
            const std::string sep = nfcNormalize(S(0).str());
            std::string out;
            if (v.size() > 1 && v[1].t == VT::Array && v[1].arr()) {   // NFG: composing across the joins
                bool first = true;
                for (auto& e : *v[1].arr()) {
                    if (!first) nfcAppendPart(out, sep, 1);
                    nfcAppendPart(out, e.toStr(), e);
                    first = false;
                }
            }
            return Value::str(std::move(out));
        }
        // `nqp::indexic($h, $n, $pos)` — index, ignoring case; `indexim` ignores
        // marks; `indexicim` ignores both. Positions are CHARACTER indices and both foldings
        // preserve them (one codepoint in, one out), so the answer indexes the
        // ORIGINAL string. has-word scans a whole haystack with these.
        case O::Indexic:
        case O::Indexim:
        case O::Indexicim: {
            std::string hs = S(0).str(), nds = S(1).str();
            bool fold = op != O::Indexim;      // `indexim` keeps case, drops marks
            if (op != O::Indexic) { hs = markFold(hs); nds = markFold(nds); }
            std::vector<uint32_t> hc, nc;
            for (uint32_t c : utf8cp(hs)) hc.push_back(fold ? toLowerCp(c) : c);
            for (uint32_t c : utf8cp(nds)) nc.push_back(fold ? toLowerCp(c) : c);
            long long from = v.size() > 2 ? I(2) : 0;
            if (from < 0) from = 0;
            if (nc.empty()) return Value::integer(from <= (long long)hc.size() ? from : -1);
            for (long long i = from; i + (long long)nc.size() <= (long long)hc.size(); i++) {
                bool hit = true;
                for (size_t j = 0; j < nc.size(); j++)
                    if (hc[i + j] != nc[j]) { hit = false; break; }
                if (hit) return Value::integer(i);
            }
            return Value::integer(-1);
        }
        case O::Index: {
            const CowStr& hcs = S(0);
            const CowStr& ncs = S(1);
            const std::string& hs = hcs.str();
            const std::string& nds = ncs.str();
            long long from = v.size() > 2 ? I(2) : 0;
            if (from < 0) from = 0;
            if (cowAllAscii(hcs) && cowAllAscii(ncs)) {   // byte search — std::string::find is vectorized
                if ((size_t)from > hs.size()) return Value::integer(-1);
                auto at = hs.find(nds, (size_t)from);
                return Value::integer(at == std::string::npos ? -1 : (long long)at);
            }
            if (auto* ci = cowCpIndex(hcs)) {
                // byte-level find (needle bytes ⟺ needle codepoints), accepted
                // only on a codepoint boundary; answer = codepoint index
                long long ncp = (long long)ci->size() - 1;
                if (nds.empty()) return Value::integer(from <= ncp ? from : -1);
                if (from > ncp) return Value::integer(-1);
                size_t b = (*ci)[(size_t)from];
                while (true) {
                    b = hs.find(nds, b);
                    if (b == std::string::npos) return Value::integer(-1);
                    if ((static_cast<unsigned char>(hs[b]) & 0xC0) != 0x80) {
                        auto it = std::lower_bound(ci->begin(), ci->end(), (uint32_t)b);
                        return Value::integer((long long)(it - ci->begin()));
                    }
                    b++;
                }
            }
            auto h = utf8cp(hs), nd = utf8cp(nds);
            if (nd.empty()) return Value::integer(from <= (long long)h.size() ? from : -1);
            for (long long at = from; at + (long long)nd.size() <= (long long)h.size(); at++) {
                bool ok = true;
                for (size_t k = 0; k < nd.size() && ok; k++) ok = h[at + k] == nd[k];
                if (ok) return Value::integer(at);
            }
            return Value::integer(-1);
        }
        case O::Chr: return Value::str(nfcNormalize(cpToU8((uint32_t)I(0))));   // NFC: U+2126 is U+03A9
        case O::StrFromCodes: {
            std::string out;
            if (!v.empty() && v[0].t == VT::Array && v[0].arr())
                for (auto& e : *v[0].arr()) out += cpToU8((uint32_t)e.toInt());
            // Rakudo strings are NFG: building one from codes COMPOSES, whatever
            // normalization the codes were in (JSON::Fast round-trips through
            // NFD codes and back)
            return Value::str(nfcNormalize(std::move(out)));
        }
        case O::StrToCodes: {
            // (str, NORMALIZE_* const, target-list) — fills target, returns it
            const CowStr& cs = S(0);
            auto cps = utf8cp(cs.str());
            long long nm = I(1); // our const values: 1 NFC, 2 NFD, 3 NFKC, 4 NFKD
            int mode = nm == 1 ? 1 : nm == 2 ? 0 : nm == 3 ? 3 : nm == 4 ? 2 : -1;
            // Every normalization form is the identity on ASCII: nothing there
            // composes, decomposes or reorders. JSON::Fast asks for NFD once per
            // escaped string, so the full uniNormalize walk was ~4 us a call for
            // text that could not change.
            if (mode >= 0 && !cowAllAscii(cs)) cps = uniNormalize(cps, mode);
            Value target = v.size() > 2 ? v[2] : Value::array();
            if (target.t != VT::Array || !target.arr()) target = Value::array();
            // the answer IS a Uni in the requested form — keep a created
            // target's own tag (nqp::create(NFD) above), name an untagged one
            if (target.s.empty() && mode >= 0)
                target.s = mode == 1 ? "NFC" : mode == 0 ? "NFD"
                         : mode == 3 ? "NFKC" : "NFKD";
            target.arr()->clear();
            target.arr()->reserve(cps.size());
            for (auto cp : cps) target.arr()->push_back(Value::integer((long long)cp));
            return target;
        }
        case O::FindNotCClass: {
            const CowStr& cs = S(1);
            const std::string& s = cs.str();
            long long mask = I(0), start = I(2), len = I(3);
            long long from = std::max<long long>(start, 0);
            long long want = start + len;
            if (cowAllAscii(cs)) {   // byte index == codepoint index throughout
                long long end = std::min<long long>(want, (long long)s.size());
                for (long long k = from; k < end; k++)
                    if (!cclassHas(mask, (uint32_t)(unsigned char)s[k])) return Value::integer(k);
                return Value::integer(end);
            }
            if (auto* ci = cowCpIndex(cs)) { // decode only the scanned window
                long long ncp = (long long)ci->size() - 1;
                long long end = std::min<long long>(want, ncp);
                for (long long k = from; k < end; k++)
                    if (!cclassHas(mask, cpAtByte(s, (*ci)[(size_t)k]))) return Value::integer(k);
                return Value::integer(end);
            }
            auto cps = utf8cp(s);
            long long end = std::min<long long>(want, (long long)cps.size());
            for (long long k = from; k < end; k++)
                if (!cclassHas(mask, cps[k])) return Value::integer(k);
            return Value::integer(end);
        }
        case O::IsCClass: {
            const CowStr& cs = S(1);
            const std::string& s = cs.str();
            long long i = I(2);
            if (i < 0) return Value::integer(0);
            if (cowAllAscii(cs))
                return Value::integer((size_t)i < s.size() &&
                                      cclassHas(I(0), (uint32_t)(unsigned char)s[i]) ? 1 : 0);
            if (auto* ci = cowCpIndex(cs)) {
                long long ncp = (long long)ci->size() - 1;
                return Value::integer(i < ncp && cclassHas(I(0), cpAtByte(s, (*ci)[(size_t)i])) ? 1 : 0);
            }
            auto cps = utf8cp(s);
            return Value::integer(i < (long long)cps.size() &&
                                  cclassHas(I(0), cps[i]) ? 1 : 0);
        }
        case O::List: case O::ListI: case O::ListS: {
            Value out = Value::array();
            for (auto& x : v) out.arr()->push_back(x);
            return out;
        }
        case O::Elems:
            if (v[0].t == VT::Str) return Value::integer(v[0].blobElems()); // Buf/Blob byte count
            return Value::integer(v[0].t == VT::Array && v[0].arr() ? (long long)v[0].arr()->size()
                                 : v[0].t == VT::Hash && v[0].hash() ? (long long)v[0].hash()->size() : 0);
        case O::Atpos: case O::AtposI: {
            long long i = I(1);
            // a NEGATIVE index counts from the end, as MoarVM's does:
            // String::Utils' is-sha1 walks past the string, `nqp::ordat` says
            // -1 there, and `nqp::atpos_i(@map, -1)` has to read the last slot
            if (v[0].t == VT::Array && v[0].arr() && i < 0) i += (long long)v[0].arr()->size();
            if (v[0].t == VT::Array && v[0].arr() && i >= 0 && i < (long long)v[0].arr()->size())
                return (*v[0].arr())[i];
            if (v[0].t == VT::Str && i >= 0 && i < v[0].blobElems())  // Buf/Blob byte
                return v[0].blobElemAt(i);
            return op == O::AtposI ? Value::integer(0) : Value::nil();
        }
        case O::Bindpos: case O::BindposI: {
            if (v[0].t == VT::Array && v[0].arr()) {
                long long i = I(1);
                while ((long long)v[0].arr()->size() <= i) v[0].arr()->push_back(Value::nil());
                (*v[0].arr())[i] = v[2];
            }
            return v.size() > 2 ? v[2] : Value::nil();
        }
        case O::Push: case O::PushI: case O::PushS:
            if (v[0].t == VT::Array && v[0].arr()) v[0].arr()->push_back(v[1]);
            return v[1];
        case O::PopS: {
            if (v[0].t == VT::Array && v[0].arr() && !v[0].arr()->empty()) {
                Value r = v[0].arr()->back(); v[0].arr()->pop_back(); return r;
            }
            return Value::nil();
        }
        case O::ShiftI: {
            if (v[0].t == VT::Array && v[0].arr() && !v[0].arr()->empty()) {
                Value r = v[0].arr()->front(); v[0].arr()->erase(v[0].arr()->begin()); return r;
            }
            return Value::integer(0);
        }
        case O::Splice: {
            // nqp::splice(target, source, offset, count) — replace in place
            if (v[0].t == VT::Array && v[0].arr()) {
                long long off = I(2), cnt = I(3);
                auto& t = *v[0].arr();
                if (off < 0) off = 0;
                if (off > (long long)t.size()) off = t.size();
                if (cnt < 0 || off + cnt > (long long)t.size()) cnt = t.size() - off;
                t.erase(t.begin() + off, t.begin() + off + cnt);
                if (v[1].t == VT::Array && v[1].arr())
                    t.insert(t.begin() + off, v[1].arr()->begin(), v[1].arr()->end());
            }
            return v[0];
        }
        case O::Hash: {
            Value h = Value::makeHash();
            for (size_t k = 0; k + 1 < v.size(); k += 2) (*h.hash())[v[k].toStr()] = v[k + 1];
            return h;
        }
        case O::Bindkey:
            if (v[0].t == VT::Hash && v[0].hash()) (*v[0].hash())[S(1)] = v[2];
            return v.size() > 2 ? v[2] : Value::nil();
        // nqp::isnull_s — the string null. A native `str` here cannot hold one:
        // `my str $entry = nqp::nextfiledir($h)` stores "" for the null the op
        // answers at the end of a directory, and paths' `nqp::until(…
        // nqp::isnull_s($entry) …)` spun forever. An empty string stands in
        // for the null (a directory entry is never empty; String::Utils'
        // Paragraphs keeps `$!next` as null_s the same way).
        case O::IsNullS:
            return Value::integer(v.empty() || v[0].t == VT::Nil || v[0].t == VT::Any ||
                                  (v[0].t == VT::Str && v[0].s.empty()) ? 1 : 0);
        case O::IsNull:
            // VM-level null, which is NOT Raku's undefined: Rakudo answers 0 for
            // both Nil and Any. Our nqp hash ops return Value::nil() for a missing
            // key, so that is what stands in for it here — and a type object,
            // being a real Raku value, is not null.
            return Value::integer(!v.empty() && v[0].t == VT::Nil ? 1 : 0);
        case O::Atkey: {
            if (v.size() < 2 || v[0].t != VT::Hash || !v[0].hash()) return Value::nil();
            auto it = v[0].hash()->find(S(1));
            return it == v[0].hash()->end() ? Value::nil() : it->second;
        }
        case O::ExistsKey:
            return Value::integer(v.size() >= 2 && v[0].t == VT::Hash && v[0].hash() &&
                                  v[0].hash()->count(S(1)) ? 1 : 0);
        case O::DeleteKey:
            if (v.size() >= 2 && v[0].t == VT::Hash && v[0].hash()) v[0].hash()->erase(S(1));
            return v.empty() ? Value::nil() : v[0];
        case O::Create: {
            std::string tn = v[0].t == VT::Type ? v[0].s : v[0].typeName();
            // a `my class IterationMap is repr("VMHash")` declared INSIDE a module
            // carries the package prefix (JSON::Fast::IterationMap) — the repr is
            // what matters, and the base name is our only proxy for it
            if (auto q = tn.rfind("::"); q != std::string::npos) tn = tn.substr(q + 2);
            if (tn == "Map") { Value m = Value::makeHash(); m.hashKind = "Map"; return m; } // keeps Map identity through p6bindattrinvres
            if (tn == "Hash" || tn == "IterationMap") return Value::makeHash();
            if (tn == "List") { Value r = Value::array(); r.isList = true; return r; }
            // `nqp::create(buf8.^pun)` — path-utils's sniffing reads a file's
            // first 4K into one (nqp::readfh fills a Buf in place)
            if (tn == "buf8" || tn == "Buf" || tn == "Blob" || tn == "blob8" || tn == "utf8") {
                Value b = Value::str(""); b.hashKind = "Buf"; return b;
            }
            // the Uni family keeps its NAME: `nqp::create(NFD)` must answer a
            // value that binds `Uni:D \codes` (JSON::Fast's unjsonify-string)
            if (tn == "Uni" || tn == "NFC" || tn == "NFD" || tn == "NFKC" || tn == "NFKD") {
                Value r = Value::array(); r.s = tn; return r;
            }
            return Value::array(); // IterationBuffer / … — a plain buffer
        }
        // Identity, as `===` reads it: reference types by their reference, a type
        // object by its name (IterationEnd is one), everything else by value.
        case O::Eqaddr: {
            if (v.size() < 2) return Value::integer(0);
            const Value& l = v[0]; const Value& r = v[1];
            bool same;
            if (l.t != r.t) same = false;
            else if (l.t == VT::Object) same = l.obj() == r.obj();
            else if (l.t == VT::Type)   same = l.s == r.s;
            else if (l.t == VT::Code)   same = l.code() == r.code();
            else if (l.t == VT::Array)  same = l.arr() == r.arr();
            else if (l.t == VT::Hash)   same = l.hash() == r.hash();
            else same = l.toStr() == r.toStr() && l.hashKind == r.hashKind;
            return Value::integer(same ? 1 : 0);
        }
        // nqp::objprimspec(T): 0 for an object type, 1/2/3 for the native
        // int/num/str kinds (10 is MoarVM's unsigned int, which AttrX::Mooish
        // treats as an int too).
        case O::ObjPrimSpec: {
            if (v.empty() || v[0].t != VT::Type) return Value::integer(0);
            const std::string& n = v[0].s;
            if (n == "str") return Value::integer(3);
            if (n == "num" || n == "num32" || n == "num64") return Value::integer(2);
            if (n == "int" || n == "int8" || n == "int16" || n == "int32" || n == "int64" ||
                n == "uint" || n == "uint8" || n == "uint16" || n == "uint32" || n == "uint64" ||
                n == "byte" || n == "long" || n == "longlong" || n == "ulong" || n == "ulonglong" ||
                n == "size_t" || n == "ssize_t" || n == "bool" || n == "atomicint")
                return Value::integer(1);
            return Value::integer(0);
        }
        // nqp::unipropcode('General_Category') is MoarVM's number for the
        // property (20); nqp::unipvalcode(20, 'Lu') a value's number (1). See
        // uniPropCodeOf. Unknown names answer 0.
        case O::UniPropCode:
            return Value::integer(v.empty() ? 0 : uniPropCodeOf(v[0].toStr()));
        case O::UniPvalCode:
            return Value::integer(v.size() < 2 ? 0 : uniPvalCodeOf(uniPropNameOf(v[0]), v[1].toStr()));
        case O::HllBool:
            return Value::boolean(!v.empty() && v[0].truthy());
        case O::Istype: {
            std::string tn = v[1].t == VT::Type ? v[1].s : v[1].typeName();
            // `nqp::istype(Mu, Any)` is 0: Any sits BELOW Mu, so the Mu type
            // object conforms only to Mu — the same rule `~~` applies. CBOR::Simple
            // tells CBOR null (Any:U) from CBOR undefined (Mu) by exactly this.
            if (tn == "Any" && v[0].t == VT::Type && v[0].s == "Mu") return Value::integer(0);
            return Value::integer(rtTypeMatch(v[0], tn) ? 1 : 0);
        }
        case O::Getattr: {
            const std::string& nm = S(2);
            // a stamped Proxy-subclass instance (AttrProxy) keeps its attrs as
            // prefixed keys on the proxy hash itself
            if (v[0].t == VT::Hash && v[0].hashKind == "Proxy" && v[0].hash() &&
                v[0].hash()->count("\x01cls")) {
                auto it = v[0].hash()->find("\x01" "a" + nm);
                return it != v[0].hash()->end() ? it->second : Value::nil();
            }
            // '$!reified' / '$!storage' name the container's own backing store
            if (v[0].t == VT::Array || v[0].t == VT::Hash) return v[0];
            // a Pair's two attributes: `nqp::getattr($p, Pair, '$!key')` is how
            // Hash::int's STORE reads each pair without a method call
            if (v[0].t == VT::Pair) {
                if (nm == "$!key" || nm == "key") return Value::str(v[0].s);
                if (nm == "$!value" || nm == "value") return v[0].pairVal() ? *v[0].pairVal() : Value::any();
            }
            // a Match's two positions, as Rakudo's cursor protocol reads them
            // (`nqp::getattr_i($cursor, Match, '$!pos')` in String::Utils'
            // replace); pos is -3 when the cursor matched nothing (MoarVM's value)
            if (v[0].t == VT::Match) {
                if (nm == "$!from" || nm == "from") return Value::integer(v[0].rFrom());
                if (nm == "$!pos"  || nm == "pos")  return Value::integer(v[0].rTo());
            }
            if (v[0].t == VT::Object && v[0].obj()) {
                std::string bare = nm.size() > 2 ? nm.substr(2) : nm;
                auto it = v[0].obj()->attrs.find(bare);
                if (it != v[0].obj()->attrs.end()) return it->second;
            }
            return Value::nil();
        }
        case O::Bindattr: case O::P6BindAttrInvRes: {
            const std::string& nm = S(2);
            if (v[0].t == VT::Hash && v[0].hashKind == "Proxy" && v[0].hash() &&
                v[0].hash()->count("\x01cls")) { // stamped Proxy-subclass instance
                (*v[0].hash())["\x01" "a" + nm] = v[3];
            } else if ((v[0].t == VT::Array && v[3].t == VT::Array && v[0].arr() && v[3].arr())) {
                *v[0].arr() = *v[3].arr();             // rebind the backing buffer
            } else if (v[0].t == VT::Hash && v[3].t == VT::Hash && v[0].hash() && v[3].hash()) {
                *v[0].hash() = *v[3].hash();           // (hashKind stays the invocant's: a Map keeps being a Map)
            } else if (v[0].t == VT::Object && v[0].obj()) {
                std::string bare = nm.size() > 2 ? nm.substr(2) : nm;
                // the readonly flag marks the CONTAINER the value came from (a
                // parameter, here `$iterator` in String::Utils' Paragraphs.new);
                // the attribute is a fresh slot, or its later `$!iterator :=
                // nqp::null` is "Cannot assign to a readonly variable"
                Value bound = v[3];
                bound.readonly = bound.immutableBind = false;
                v[0].obj()->attrs[bare] = std::move(bound);
            }
            return op == O::P6BindAttrInvRes ? v[0] : v[3];
        }
        case O::P6ScalarWithValue:
            return v.size() > 1 ? v[1] : Value::nil(); // container wrap is a no-op for us
        case O::What: { // the type object of the value (like .WHAT, no method dispatch)
            if (v.empty()) return Value::typeObj("Mu");
            if (v[0].t == VT::Type) return v[0];
            if (v[0].t == VT::Object && v[0].obj() && v[0].obj()->cls)
                return Value::typeObj(v[0].obj()->cls->name);
            return Value::typeObj(v[0].typeName());
        }
        case O::IsList:
            return Value::integer(!v.empty() && v[0].t == VT::Array ? 1 : 0);
        case O::IsCont:
            // a Proxy IS a container; ordinary values reach us decontainerized
            return Value::integer(!v.empty() && v[0].t == VT::Hash &&
                                  v[0].hashKind == "Proxy" ? 1 : 0);
        case O::IsTrue:
            return Value::integer(!v.empty() && v[0].truthy() ? 1 : 0);
        case O::IsConcrete:
            return Value::integer(!v.empty() && rtIsDefined(v[0]) ? 1 : 0);
        case O::P6Definite:   // the same test, handed back as a Raku Bool
            return Value::boolean(!v.empty() && rtIsDefined(v[0]));
        case O::CloneOp: { // shallow clone: fresh backing store, same elements
            if (v.empty()) return Value::nil();
            Value c = v[0];
            if (c.t == VT::Array && c.arr()) { auto na = makePayload<ValueList>(*c.arr()); c.setArr(na); }
            else if (c.t == VT::Hash && c.hash()) { auto nh = makePayload<ValueMap>(*c.hash()); c.setHash(nh); }
            else if (c.t == VT::Object && c.obj()) { auto no = makePayload<ObjectData>(*c.obj()); c.setObj(no); }
            return c;
        }
        case O::Shift: { // generic array shift (ShiftI is the int variant)
            if (!v.empty() && v[0].t == VT::Array && v[0].arr() && !v[0].arr()->empty()) {
                Value f = v[0].arr()->front(); v[0].arr()->erase(v[0].arr()->begin()); return f;
            }
            return Value::nil();
        }
        case O::LockOp: case O::UnlockOp:
            // nqp::lock/unlock guard concurrent lazy builds; under the GIL the
            // build is already atomic, so the guard is a no-op here
            return Value::nil();
        // The directory walk `paths` is written against. A handle reads its
        // directory ONCE on open and nextfiledir hands the names out one at a
        // time. As MoarVM does (2026.09), `.` and `..` are never handed out,
        // and the end of the directory is the empty string, which is what
        // nqp's null_s becomes in `my str $entry = nqp::nextfiledir($h)` —
        // paths reads it exactly that way, and a native str refuses Nil.
        // Failure to open THROWS: the caller wraps the op in nqp::handle.
        case O::OpenDir: {
            if (g_sandboxChecks) sandboxRefuseBare("nqp::opendir", SandboxCap::Read);
            const std::string path = S(0).str();
            DIR* d = ::opendir(path.c_str());
            if (!d) throw RakuError{Value::typeObj("X::AdHoc"),
                                    "Failed to open dir: " + path + ": " + std::strerror(errno)};
            Value entries = Value::array();
            while (dirent* de = ::readdir(d)) {
                const std::string nm = de->d_name;
                if (nm == "." || nm == "..") continue;
                entries.arr()->push_back(Value::str(nm));
            }
            ::closedir(d);
            Value h = Value::makeHash(); h.hashKind = "NqpDir";
            (*h.hash())["entries"] = entries;
            (*h.hash())["pos"] = Value::integer(0);
            return h;
        }
        case O::NextFileDir: {
            if (v.empty() || v[0].t != VT::Hash || !v[0].hash()) return Value::str("");
            auto& H = *v[0].hash();
            long long p = H["pos"].toInt();
            Value& ents = H["entries"];
            if (ents.t != VT::Array || !ents.arr() || p >= (long long)ents.arr()->size()) return Value::str("");
            H["pos"] = Value::integer(p + 1);
            return (*ents.arr())[(size_t)p];
        }
        case O::CloseDir: return Value::nil();
        // the file tests path-utils asks of the OS directly
        case O::FileReadable: case O::FileWritable: case O::FileExecutable: {
            if (g_sandboxChecks) sandboxRefuseBare("nqp::filereadable", SandboxCap::Read);
            int mode = op == O::FileReadable ? R_OK : op == O::FileWritable ? W_OK : X_OK;
            return Value::integer(::access(S(0).str().c_str(), mode) == 0 ? 1 : 0);
        }
        case O::FileIsLink: {
            if (g_sandboxChecks) sandboxRefuseBare("nqp::fileislink", SandboxCap::Read);
#ifdef _WIN32
            // no lstat on Windows — the same answer the Stat case gives there,
            // where S_ISLNK is a no-op macro and a symlink cannot be told apart
            return Value::integer(0);
#else
            struct ::stat st;
            return Value::integer(::lstat(S(0).str().c_str(), &st) == 0 && S_ISLNK(st.st_mode) ? 1 : 0);
#endif
        }
        // nqp::rindex(haystack, needle, ?from) — the LAST occurrence at or
        // before `from` (codepoint positions, as nqp::index counts); paths
        // splits a path at its last separator with it
        case O::Rindex: {
            const CowStr& hcs = S(0);
            const std::string& hs = hcs.str();
            const std::string& nd = S(1).str();
            if (cowAllAscii(hcs) && cowAllAscii(S(1))) {
                size_t from = v.size() > 2 ? (size_t)std::max<long long>(I(2), 0) : std::string::npos;
                auto at = hs.rfind(nd, from);
                return Value::integer(at == std::string::npos ? -1 : (long long)at);
            }
            auto h = utf8cp(hs), n = utf8cp(nd);
            long long last = v.size() > 2 ? I(2) : (long long)h.size();
            if (last > (long long)h.size() - (long long)n.size()) last = (long long)h.size() - (long long)n.size();
            for (long long k = last; k >= 0; k--) {
                bool hit = true;
                for (size_t j = 0; j < n.size() && hit; j++) hit = h[(size_t)k + j] == n[j];
                if (hit) return Value::integer(k);
            }
            return Value::integer(-1);
        }
        case O::X: { // nqp::x(str, count) — the string that many times (String::Utils pads with it)
            long long cnt = I(1);
            std::string out;
            if (cnt > 0) { const std::string& s = S(0).str(); out.reserve(s.size() * (size_t)cnt); for (long long k = 0; k < cnt; k++) out += s; }
            return Value::str(std::move(out));
        }
        case O::Flip: { // nqp::flip(str) — reversed by codepoint
            auto cps = utf8cp(S(0).str());
            std::string out;
            for (size_t k = cps.size(); k-- > 0; ) out += cpToU8(cps[k]);
            return Value::str(std::move(out));
        }
        case O::Split: { // nqp::split(separator, string) — note the order
            const std::string& sep = S(0).str();
            const std::string& s = S(1).str();
            Value out = Value::array();
            if (sep.empty()) { for (uint32_t cp : utf8cp(s)) out.arr()->push_back(Value::str(cpToU8(cp))); return out; }
            size_t at = 0;
            while (true) {
                size_t nx = s.find(sep, at);
                if (nx == std::string::npos) { out.arr()->push_back(Value::str(s.substr(at))); break; }
                out.arr()->push_back(Value::str(s.substr(at, nx - at)));
                at = nx + sep.size();
            }
            return out;
        }
        case O::IsneS: return Value::integer(S(0).str() != S(1).str() ? 1 : 0);
        case O::IseqS: return Value::integer(S(0).str() == S(1).str() ? 1 : 0);
        case O::IsltS: return Value::integer(S(0).str() <  S(1).str() ? 1 : 0);
        case O::IsleS: return Value::integer(S(0).str() <= S(1).str() ? 1 : 0);
        case O::IsgtS: return Value::integer(S(0).str() >  S(1).str() ? 1 : 0);
        case O::IsgeS: return Value::integer(S(0).str() >= S(1).str() ? 1 : 0);
        case O::NotI:  return Value::integer(I(0) ? 0 : 1);
        // nqp::mod_i TRUNCATES — the remainder takes the sign of the DIVIDEND,
        // as C's `%` does. It is NOT Raku's `%`, and it is not consistent with
        // nqp::div_i beside it, which floors; MoarVM is simply not uniform
        // here and the cross-engine probe is the authority (Rakudo 2026.08:
        // mod_i(-7,2) is -1, div_i(-7,2) is -4). Pinned in
        // t/regression/lizmat-nqp-ops.raku, which passes under Rakudo too.
        case O::ModI: {
            long long x = I(0), y = I(1);
            // by zero it THROWS, and with its own wording — div_i says
            // "Division by zero", mod_i says this. Answering 0 here was the
            // earlier behaviour and no engine does it.
            if (!y) throw RakuError{Value::typeObj("X::AdHoc"), "Modulation by zero"};
            return Value::integer(x % y);
        }
        // nqp::findcclass(class, str, start, count) — the FIRST position in the
        // window whose character IS of the class, or the window's end (the
        // mirror image of findnotcclass above)
        case O::FindCClass: {
            const CowStr& cs = S(1);
            const std::string& s = cs.str();
            long long mask = I(0), start = I(2), len = I(3);
            long long from = std::max<long long>(start, 0);
            long long want = start + len;
            if (cowAllAscii(cs)) {
                long long end = std::min<long long>(want, (long long)s.size());
                for (long long k = from; k < end; k++)
                    if (cclassHas(mask, (uint32_t)(unsigned char)s[k])) return Value::integer(k);
                return Value::integer(end);
            }
            auto cps = utf8cp(s);
            long long end = std::min<long long>(want, (long long)cps.size());
            for (long long k = from; k < end; k++)
                if (cclassHas(mask, cps[k])) return Value::integer(k);
            return Value::integer(end);
        }
        case O::Null: return Value::nil();
        case O::IsNanOrInf: {
            double d = v.empty() ? 0 : v[0].toNum();
            return Value::integer(std::isnan(d) || std::isinf(d) ? 1 : 0);
        }
        // num comparisons (NaN != NaN falls out of C++ float semantics)
        case O::IseqN: return Value::integer(v.size() > 1 && v[0].toNum() == v[1].toNum() ? 1 : 0);
        case O::IsneN: return Value::integer(v.size() > 1 && v[0].toNum() != v[1].toNum() ? 1 : 0);
        case O::AtposN: { // native-num element read
            if (!v.empty() && v[0].t == VT::Array && v[0].arr()) {
                long long idx = I(1);
                if (idx >= 0 && idx < (long long)v[0].arr()->size())
                    return Value::number((*v[0].arr())[idx].toNum());
            }
            return Value::number(0);
        }
        // buffer reads: (buf, offset, size|endian-flag)
        case O::ReadUInt: case O::ReadInt: case O::ReadNum: {
            if (v.empty() || v[0].t != VT::Str) return Value::integer(0);
            int nb, en; decodeBinFlag(I(2), nb, en);
            char kind = op == O::ReadNum ? 'n' : (op == O::ReadInt ? 'i' : 'u');
            return nqpBufRead(v[0].s, I(1), nb, en, kind);
        }
        case O::Slice: { // nqp::slice(buf, from, to) — inclusive byte range → Buf
            Value out = Value::str(""); out.hashKind = "Buf"; identify(out);
            if (!v.empty() && v[0].t == VT::Str) {
                long long from = I(1), to = I(2), len = (long long)v[0].s.size();
                if (from < 0) from = 0;
                if (to >= len) to = len - 1;
                if (to >= from) out.s = v[0].s.substr(from, to - from + 1);
            }
            return out;
        }
        case O::Decode: // nqp::decode(buf, 'utf8') — bytes → Str (rakupp strings are UTF-8)
            return Value::str(v.empty() ? std::string() : v[0].s);
        case O::AddBigI: { // nqp::add_I(a, b, Int) — bignum-safe add
            if (!v.empty() && (v[0].big() || (v.size() > 1 && v[1].big()))) {
                BigInt a = v[0].big() ? *v[0].big() : BigInt(v[0].toInt());
                BigInt b = (v.size() > 1) ? (v[1].big() ? *v[1].big() : BigInt(v[1].toInt())) : BigInt(0);
                BigInt r = a + b;
                return r.fitsLL() ? Value::integer(r.toLL()) : Value::bigint(r);
            }
            return Value::integer(I(0) + I(1));
        }
        // ---- the bignum `_I` family -------------------------------------
        // Each of these is the bignum-safe spelling of arithmetic rakupp
        // already performs correctly, so they DELEGATE to applyArith instead
        // of re-deriving overflow, sign and two's-complement behaviour that is
        // already written and tested once. The trailing type argument NQP
        // passes (`nqp::mul_I($a, $b, Int)`) says which box to put the answer
        // in; there is only one Int here, so it is ignored.
        //
        // The mapping is exact, and the cross-engine probe is why it is
        // trusted rather than assumed: div_I FLOORS, which is Raku's `div`
        // (-7 div 2 is -4), and mod_I FLOORS TOO, which is Raku's `%`
        // (-7 % 2 is 1) — unlike the native mod_i above, which truncates.
        case O::DivBigI: {
            Value d = v.size() > 1 ? v[1] : Value::integer(0);
            if (!d.big() && d.toInt() == 0)
                throw RakuError{Value::typeObj("X::AdHoc"), "Division by zero"};
            return applyArith("div", v[0], d);
        }
        // …and the BIGNUM spelling by zero answers the DIVIDEND rather than
        // throwing, where Raku's own `%` throws. libtommath's mp_mod does
        // that, so a program reaching this edge through nqp sees it.
        case O::ModBigI: {
            Value d = A1();
            if (!d.big() && d.toInt() == 0) return A0();
            return applyArith("%", A0(), d);
        }
        case O::MulBigI: return applyArith("*", A0(), A1());
        case O::SubBigI: return applyArith("-", A0(), A1());
        case O::PowBigI: return applyArith("**", A0(), A1());
        case O::GcdBigI: return applyArith("gcd", A0(), A1());
        case O::LcmBigI: return applyArith("lcm", A0(), A1());
        case O::BitandBigI: return applyArith("+&", A0(), A1());
        case O::BitorBigI:  return applyArith("+|", A0(), A1());
        case O::BitxorBigI: return applyArith("+^", A0(), A1());
        case O::BitshiftlBigI: return applyArith("+<", A0(), A1());
        case O::BitshiftrBigI: return applyArith("+>", A0(), A1());
        case O::NegBigI: return applyArith("-", Value::integer(0), A0());
        case O::AbsBigI: {
            Value x = A0();
            bool neg = x.big() ? (x.big()->sign < 0) : (x.toInt() < 0);
            return neg ? applyArith("-", Value::integer(0), x) : x;
        }
        // the comparison spellings answer a native 0/1, not a Bool
        case O::IseqBigI: return Value::integer(bigCmp(A0(), A1()) == 0 ? 1 : 0);
        case O::IsneBigI: return Value::integer(bigCmp(A0(), A1()) != 0 ? 1 : 0);
        case O::IsltBigI: return Value::integer(bigCmp(A0(), A1()) <  0 ? 1 : 0);
        case O::IsleBigI: return Value::integer(bigCmp(A0(), A1()) <= 0 ? 1 : 0);
        case O::IsgeBigI: return Value::integer(bigCmp(A0(), A1()) >= 0 ? 1 : 0);
        case O::IsgtBigI: return Value::integer(bigCmp(A0(), A1()) >  0 ? 1 : 0);
        case O::CmpBigI:  return Value::integer(bigCmp(A0(), A1()));
        // …and the native spellings, which answer the same -1/0/1. `cmp_n`
        // shares the numeric arm: the operands decide, as they do for `cmp`.
        case O::CmpS: { std::string a = v.size() > 0 ? v[0].toStr() : std::string(),
                                    b = v.size() > 1 ? v[1].toStr() : std::string();
                        return Value::integer(a < b ? -1 : a > b ? 1 : 0); }
        case O::CmpI: { double a = v.size() > 0 ? v[0].toNum() : 0,
                               b = v.size() > 1 ? v[1].toNum() : 0;
                        return Value::integer(a < b ? -1 : a > b ? 1 : 0); }
        // `isbig_I` asks whether the value needs more than a native int
        case O::IsBigI: return Value::integer(!v.empty() && v[0].big() ? 1 : 0);
        case O::ToStrBigI: return Value::str(v.empty() ? std::string("0") : v[0].toStr());
        case O::FromStrBigI: {
            BigInt b = BigInt::fromString(v.empty() ? std::string("0") : v[0].toStr());
            return b.fitsLL() ? Value::integer(b.toLL()) : Value::bigint(b);
        }
        // boxing leaves: rakupp's Int and Num ARE the boxed forms, so the box
        // op is the value itself in the right representation (box_s is P6BoxS).
        case O::BoxI: return Value::integer(I(0));
        case O::BoxN: return Value::number(v.empty() ? 0.0 : v[0].toNum());
        case O::SqrtN: return Value::number(std::sqrt(v.empty() ? 0.0 : v[0].toNum()));
        // nqp::pop — the generic tail take (PopS is the string-typed spelling,
        // and the _i/_n forms are the same op on the same storage here).
        case O::Pop: {
            if (!v.empty() && v[0].t == VT::Array && v[0].arr() && !v[0].arr()->empty()) {
                Value r = v[0].arr()->back(); v[0].arr()->pop_back(); return r;
            }
            return Value::nil();
        }
        // nqp::time answers NANOSECONDS since the epoch as an Int. (Older NQP
        // spelled the seconds form `time_i`; the modern op is the nanosecond
        // one, which is what `nano` reads.)
        case O::TimeOp: {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            return Value::integer(
                (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
        }
        case O::ReadLink: {
            if (g_sandboxChecks) sandboxRefuseBare("nqp::readlink", SandboxCap::Read);
            const std::string path = v.empty() ? std::string() : v[0].toStr();
            char buf[4096];
            // platform_readlink, not ::readlink: Windows has no POSIX readlink
            // and Platform.h answers the link's final target there instead.
            long long k = platform_readlink(path.c_str(), buf, sizeof(buf) - 1);
            if (k < 0) throw RakuError{Value::typeObj("X::AdHoc"),
                                       "Failed to readlink " + path + ": " + std::strerror(errno)};
            return Value::str(std::string(buf, (size_t)k));
        }
        case O::Decont: return v.empty() ? Value::nil() : v[0];        // container strip = identity
        case O::P6BoxS: return Value::str(v.empty() ? std::string() : v[0].toStr());
        // nqp::getcomp('Raku') — the running compiler, the same object
        // $*RAKU.compiler answers. NQP names a compiler by HLL, and the only
        // one this process has is ours; an unknown name is null, as in NQP,
        // which is what makes `nqp::getcomp("Raku") || nqp::getcomp('perl6')`
        // (the REPL-sandbox idiom) pick the first spelling that exists.
        case O::GetComp: {
            const std::string n = v.empty() ? std::string() : v[0].toStr();
            if (n != "Raku" && n != "raku" && n != "perl6" && n != "Perl6") return Value::nil();
            Value r = Value::makeHash();
            r.hashKind = "Compiler";
            (*r.hash())["name"] = Value::str("Raku++");
            (*r.hash())["ver"] = Value::str(kOracleEra);
            return r;
        }
        default: break;
    }
    throw RakuError{Value::typeObj("X::NYI"), "nqp op not implemented in this build"};
}

} // namespace rakupp
