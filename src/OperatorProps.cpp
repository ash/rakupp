// What `.prec` and its kin answer about an operator (rakudo#4780): the
// properties Rakudo gives each built-in one, a category's defaults for a
// declared one, and what its `is equiv`, `is tighter`, `is looser` and
// `is assoc` traits make of those. The parser keeps the traits on the
// declaration (precTraitsOfTokens, Lexer.cpp); the declaration turns them into
// properties here, once, as Rakudo does at compile time (opPropsFromTraits);
// the routine answers from them (codePrecMethod).
#include "BuiltinsParts.h"

namespace rakupp {

namespace {
using Props = std::map<std::string, std::string>;

// Rakudo's properties for each built-in operator (src/core.c/OperatorProperties
// .rakumod, as 2026.09's `.prec` reports them), level by level, tightest first
const std::vector<std::pair<const char*, std::vector<const char*>>> kLevels = {
    {"assoc=unary;dba=methodcall;fiddly=1;prec=y=",
     {"circumfix:<[ ]>", "circumfix:<{ }>", "postcircumfix:<[ ]>", "postcircumfix:<[; ]>", "postcircumfix:<{ }>",
      "postcircumfix:<{; }>", "postfix:<i>"}},
    {"assoc=unary;dba=autoincrement;prec=x=",
     {"postfix:<++>", "postfix:<-->", "postfix:<\xE2\x81\xBF>", "postfix:<\xE2\x9A\x9B++>",
      "postfix:<\xE2\x9A\x9B-->", "prefix:<++>", "prefix:<++\xE2\x9A\x9B>", "prefix:<-->",
      "prefix:<--\xE2\x9A\x9B>"}},
    {"assoc=right;dba=exponentiation;prec=w=",
     {"infix:<**>"}},
    {"assoc=unary;dba=symbolic-unary;prec=v=",
     {"prefix:<!>", "prefix:<+>", "prefix:<+^>", "prefix:<->", "prefix:<?>", "prefix:<?^>", "prefix:<^>",
      "prefix:<|>", "prefix:<~>", "prefix:<~^>", "prefix:<\xE2\x88\x92>", "prefix:<\xE2\x9A\x9B>"}},
    {"assoc=unary;dba=default-prefix;prec=v=",
     {"prefix:<let>", "prefix:<temp>"}},
    {"assoc=left;dba=multiplicative-iffy;iffy=1;prec=u=",
     {"infix:<%%>", "infix:<?&>"}},
    {"assoc=left;dba=multiplicative;prec=u=",
     {"infix:<%>", "infix:<*>", "infix:<+&>", "infix:</>", "infix:<div>", "infix:<gcd>", "infix:<lcm>",
      "infix:<mod>", "infix:<~&>", "infix:<\xC3\x97>", "infix:<\xC3\xB7>", "infix:\xC2\xAB+<\xC2\xBB",
      "infix:\xC2\xAB+>\xC2\xBB", "infix:\xC2\xAB~<\xC2\xBB", "infix:\xC2\xAB~>\xC2\xBB"}},
    {"assoc=left;dba=additive;prec=t=",
     {"infix:<+>", "infix:<+^>", "infix:<+|>", "infix:<->", "infix:<~^>", "infix:<~|>", "infix:<\xE2\x88\x92>"}},
    {"assoc=left;dba=additive-iffy;iffy=1;prec=t=",
     {"infix:<?^>", "infix:<?|>"}},
    {"assoc=left;dba=replication-x;prec=s=",
     {"infix:<x>"}},
    {"assoc=left;dba=replication-xx;prec=s=;thunky=t.",
     {"infix:<xx>"}},
    {"assoc=left;dba=concatenation;prec=r=",
     {"infix:<o>", "infix:<~>", "infix:<\xE2\x88\x98>"}},
    {"assoc=list;dba=junctive-and-iffy;iffy=1;prec=q=",
     {"infix:<&>"}},
    {"assoc=list;dba=junctive-and;prec=q=",
     {"infix:<(&)>", "infix:<(.)>", "infix:<\xE2\x88\xA9>", "infix:<\xE2\x8A\x8D>"}},
    {"assoc=list;dba=junctive-or;prec=p=",
     {"infix:<(+)>", "infix:<(-)>", "infix:<(^)>", "infix:<(|)>", "infix:<\xE2\x88\x96>", "infix:<\xE2\x88\xAA>",
      "infix:<\xE2\x8A\x8E>", "infix:<\xE2\x8A\x96>"}},
    {"assoc=list;dba=junctive-or-iffy;iffy=1;prec=p=",
     {"infix:<^>", "infix:<|>"}},
    {"assoc=non;dba=structural;diffy=1;prec=n=",
     {"infix:<..>", "infix:<..^>", "infix:<^..>", "infix:<^..^>", "infix:<but>", "infix:<cmp>", "infix:<coll>",
      "infix:<does>", "infix:<leg>", "infix:<unicmp>", "infix:\xC2\xAB<=>\xC2\xBB"}},
    {"assoc=chain;dba=chaining;diffy=1;iffy=1;prec=m=",
     {"infix:<!=>", "infix:<!~~>", "infix:<(==)>", "infix:<(cont)>", "infix:<(elem)>", "infix:<=:=>",
      "infix:<===>", "infix:<==>", "infix:<=~=>", "infix:<after>", "infix:<before>", "infix:<eq>", "infix:<eqv>",
      "infix:<ge>", "infix:<gt>", "infix:<le>", "infix:<lt>", "infix:<ne>", "infix:<~~>", "infix:<\xE2\x88\x88>",
      "infix:<\xE2\x88\x89>", "infix:<\xE2\x88\x8A>", "infix:<\xE2\x88\x8B>", "infix:<\xE2\x88\x8C>",
      "infix:<\xE2\x88\x8D>", "infix:<\xE2\x89\x85>", "infix:<\xE2\x89\xA0>", "infix:<\xE2\x89\xA1>",
      "infix:<\xE2\x89\xA2>", "infix:<\xE2\x89\xA4>", "infix:<\xE2\x89\xA5>", "infix:<\xE2\x89\xBC>",
      "infix:<\xE2\x89\xBD>", "infix:<\xE2\x8A\x82>", "infix:<\xE2\x8A\x83>", "infix:<\xE2\x8A\x84>",
      "infix:<\xE2\x8A\x85>", "infix:<\xE2\x8A\x86>", "infix:<\xE2\x8A\x87>", "infix:<\xE2\x8A\x88>",
      "infix:<\xE2\x8A\x89>", "infix:<\xE2\xA9\xB5>", "infix:<\xE2\xA9\xB6>", "infix:\xC2\xAB(<)\xC2\xBB",
      "infix:\xC2\xAB(<+)\xC2\xBB", "infix:\xC2\xAB(<=)\xC2\xBB", "infix:\xC2\xAB(>)\xC2\xBB",
      "infix:\xC2\xAB(>+)\xC2\xBB", "infix:\xC2\xAB(>=)\xC2\xBB", "infix:\xC2\xAB<=\xC2\xBB",
      "infix:\xC2\xAB<\xC2\xBB", "infix:\xC2\xAB>=\xC2\xBB", "infix:\xC2\xAB>\xC2\xBB"}},
    {"assoc=left;dba=tight-and;iffy=1;prec=l=;thunky=.t",
     {"infix:<&&>"}},
    {"assoc=left;dba=tight-defor;prec=k=;thunky=.t",
     {"infix:<//>"}},
    {"assoc=list;dba=tight-xor;iffy=1;prec=k=;thunky=..t",
     {"infix:<^^>"}},
    {"assoc=list;dba=tight-minmax;prec=k=",
     {"infix:<max>", "infix:<min>"}},
    {"assoc=left;dba=tight-or;iffy=1;prec=k=;thunky=.t",
     {"infix:<||>"}},
    {"assoc=right;dba=list assignment;fiddly=1;prec=i=;sub=e=",
     {"infix:<=>"}},
    {"assoc=right;dba=item-assignment;prec=i=",
     {"infix:<=~>", "infix:<\xE2\x87\x92>", "infix:<\xE2\x9A\x9B+=>", "infix:<\xE2\x9A\x9B-=>",
      "infix:<\xE2\x9A\x9B=>", "infix:<\xE2\x9A\x9B\xE2\x88\x92=>", "infix:\xC2\xAB=>\xC2\xBB"}},
    {"assoc=unary;dba=loose-unary;prec=h=",
     {"prefix:<not>", "prefix:<so>"}},
    {"assoc=list;dba=comma;nextterm=nulltermish;prec=g=",
     {"infix:<,>"}},
    {"assoc=list;dba=list-infix;prec=f=",
     {"infix:<...>", "infix:<...^>", "infix:<X>", "infix:<Z>", "infix:<^...>", "infix:<^...^>",
      "infix:<^\xE2\x80\xA6>", "infix:<^\xE2\x80\xA6^>", "infix:<minmax>", "infix:<\xE2\x80\xA6>",
      "infix:<\xE2\x80\xA6^>"}},
    {"assoc=left;dba=loose-and;iffy=1;prec=d=;thunky=.t",
     {"infix:<and>"}},
    {"assoc=list;dba=loose-andthen;prec=d=;thunky=.b",
     {"infix:<andthen>", "infix:<notandthen>"}},
    {"assoc=left;dba=loose-or;iffy=1;prec=c=;thunky=.t",
     {"infix:<or>"}},
    {"assoc=list;dba=loose-orelse;prec=c=;thunky=.b",
     {"infix:<orelse>"}},
    {"assoc=list;dba=loose-xor;iffy=1;prec=c=;thunky=.t",
     {"infix:<xor>"}},
};

Props parseProps(const std::string& s) {
    Props p;
    size_t at = 0;
    while (at < s.size()) {
        size_t semi = s.find(';', at);
        if (semi == std::string::npos) semi = s.size();
        const std::string kv = s.substr(at, semi - at);
        const size_t eq = kv.find('=');
        if (eq != std::string::npos) p[kv.substr(0, eq)] = kv.substr(eq + 1);
        at = semi + 1;
    }
    return p;
}

// `infix:<+>`, `infix:«<»`, `infix:<<«>>` → "infix" and "+"; false for a name
// that is no operator's
bool splitOpName(const std::string& n, std::string& cat, std::string& sym) {
    const size_t c = n.find(':');
    if (c == std::string::npos || c == 0 || (c + 1 < n.size() && n[c + 1] == ':')) return false;
    cat = n.substr(0, c);
    if (cat != "infix" && cat != "prefix" && cat != "postfix" && cat != "circumfix" && cat != "postcircumfix") return false;
    const std::string r = n.substr(c + 1);
    if (r.size() >= 4 && r.compare(0, 2, "<<") == 0 && r.compare(r.size() - 2, 2, ">>") == 0) sym = r.substr(2, r.size() - 4);
    else if (r.size() >= 4 && r.compare(0, 2, "\xC2\xAB") == 0 && r.compare(r.size() - 2, 2, "\xC2\xBB") == 0)
        sym = r.substr(2, r.size() - 4);
    else if (r.size() >= 2 && r[0] == '<' && r.back() == '>') sym = r.substr(1, r.size() - 2);
    else return false;
    return true;
}

const Props* builtinProps(const std::string& cat, const std::string& sym) {
    static const std::unordered_map<std::string, Props> table = [] {
        std::unordered_map<std::string, Props> t;
        for (auto& lv : kLevels) {
            const Props p = parseProps(lv.first);
            for (const char* n : lv.second) {
                std::string c, s;
                if (splitOpName(n, c, s)) t[c + " " + s] = p;
            }
        }
        return t;
    }();
    auto it = table.find(cat + " " + sym);
    return it == table.end() ? nullptr : &it->second;
}

// what a declared operator of the category has before any trait
Props categoryDefault(const std::string& cat) {
    if (cat == "infix")         return parseProps("assoc=left;dba=default-infix;prec=t=");
    if (cat == "prefix")        return parseProps("assoc=unary;dba=default-prefix;prec=v=");
    if (cat == "postfix")       return parseProps("assoc=unary;dba=default-postfix;prec=x=");
    if (cat == "circumfix")     return parseProps("dba=default-circumfix");
    if (cat == "postcircumfix") return parseProps("dba=default-postcircumfix;prec=y=");
    return {};
}

Props flatToProps(const std::vector<std::string>& flat) {
    Props p;
    for (size_t i = 0; i + 1 < flat.size(); i += 2) p[flat[i]] = flat[i + 1];
    return p;
}

// A routine's properties: what its traits set, its multis' if it is their
// dispatcher, a built-in's, else its category's defaults (none for a routine
// that is no operator)
Props propsOfCode(const Value& code) {
    if (code.t != VT::Code || !code.code()) return {};
    const auto& cold = code.code()->cold();
    if (!cold.opProps.empty()) return flatToProps(cold.opProps);
    for (auto& c : cold.candidates)
        if (c.t == VT::Code && c.code() && !c.code()->cold().opProps.empty()) return flatToProps(c.code()->cold().opProps);
    std::string cat, sym;
    if (!splitOpName(code.code()->ci().name, cat, sym)) return {};
    if (const Props* b = builtinProps(cat, sym)) return *b;
    return categoryDefault(cat);
}

// the operator a precedence trait names — `&infix:<+>`, `&infix:«<»`, `&[*]`,
// or a bare `*` (from `is tighter<*>`), which is of the declaring operator's
// own category — and its properties
Props propsOfRef(Interpreter& I, const std::string& selfCat, std::string ref) {
    std::string cat, sym;
    if (ref.size() > 3 && ref.compare(0, 2, "&[") == 0 && ref.back() == ']') { cat = "infix"; sym = ref.substr(2, ref.size() - 3); }
    else {
        if (!ref.empty() && ref[0] == '&') ref = ref.substr(1);
        if (!splitOpName(ref, cat, sym)) { cat = selfCat; sym = ref; }
    }
    // one this program declares has the properties ITS traits gave it
    for (const std::string& spelled : {"&" + cat + ":<" + sym + ">", "&" + cat + ":\xC2\xAB" + sym + "\xC2\xBB"})
        if (Value* f = I.tctx().cur ? I.tctx().cur->find(spelled) : nullptr)
            if (f->t == VT::Code && f->code() && f->code()->ci().builtin == nullptr) return propsOfCode(*f);
    if (const Props* b = builtinProps(cat, sym)) return *b;
    return categoryDefault(cat);
}
} // namespace

// The properties a declared operator's traits give it, flat (key, value)
// pairs: each precedence trait copies the operator it names, `tighter` and
// `looser` edit that one's precedence (Rakudo's `@=` and `:=` after each
// `=`), and the declared operator keeps an associativity of its own — its
// category's, or what an `is assoc` said
std::vector<std::string> opPropsFromTraits(Interpreter& I, const std::string& name, const std::vector<std::string>& traits) {
    std::string cat, sym;
    if (!splitOpName(name, cat, sym)) return {};
    Props p = categoryDefault(cat);
    for (size_t i = 0; i + 1 < traits.size(); i += 2) {
        const std::string& tr = traits[i];
        const std::string& arg = traits[i + 1];
        if (tr == "assoc") { p["assoc"] = arg; continue; }
        const std::string own = p.count("assoc") ? p["assoc"] : std::string();
        p = propsOfRef(I, cat, arg);
        if (tr == "tighter" || tr == "looser") {
            std::string prec;
            for (char ch : p["prec"]) prec += ch == '=' ? (tr == "tighter" ? "@=" : ":=") : std::string(1, ch);
            p["prec"] = prec;
        }
        if (!own.empty()) p["assoc"] = own;
    }
    std::vector<std::string> flat;
    for (auto& kv : p) { flat.push_back(kv.first); flat.push_back(kv.second); }
    return flat;
}

// `is prec(%p)`: the properties are the Hash's pairs, and only those
std::vector<std::string> opPropsOfHash(const Value& h) {
    std::vector<std::string> flat;
    if (h.t != VT::Hash || !h.hash()) return flat;
    std::map<std::string, std::string> sorted;
    for (auto& kv : *h.hash()) sorted[kv.first] = kv.second.toStr();
    for (auto& kv : sorted) { flat.push_back(kv.first); flat.push_back(kv.second); }
    return flat;
}

// `.prec` — the Hash of them, `.prec($key)` one of them (Rakudo dies "expected
// Hash:D but got Str" here, which rakudo#4780 records) — and `.precedence`,
// `.associative`, `.thunky` and `.iffy`
std::optional<Value> codePrecMethod(Interpreter&, const Value& inv, const std::string& m, const ValueList& args) {
    if (inv.t != VT::Code || !inv.code()) return std::nullopt;
    if (m != "prec" && m != "precedence" && m != "associative" && m != "thunky" && m != "iffy") return std::nullopt;
    const Props p = propsOfCode(inv);
    auto valueOf = [](const std::string& v) {
        const bool digits = !v.empty() && v.find_first_not_of("0123456789") == std::string::npos;
        return digits ? Value::integer(std::stoll(v)) : Value::str(v);
    };
    auto get = [&](const char* k) { auto it = p.find(k); return it == p.end() ? std::string() : it->second; };
    if (m == "precedence") return Value::str(get("prec"));
    if (m == "associative") return Value::str(get("assoc"));
    if (m == "thunky") return Value::str(get("thunky"));
    if (m == "iffy") return Value::boolean(get("iffy") == "1");
    if (!args.empty() && args[0].t != VT::Pair) {
        const std::string k = args[0].toStr();
        if (p.empty()) return Value::str("");
        auto it = p.find(k);
        return it == p.end() ? Value::nil() : valueOf(it->second);
    }
    Value h = Value::makeHash();
    for (auto& kv : p) (*h.hash())[kv.first] = valueOf(kv.second);
    return h;
}

} // namespace rakupp
