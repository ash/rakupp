// Constructing RakuAST:: nodes and rendering them back to source (RAKUAST-PLAN
// P2c). Two entry points, both reached only through the registry's builtin
// methods, both in the parse archive with throwing doubles in stub_eval.cpp.
//
// `.DEPARSE` is not a nicety here, it is the bridge: P3's `.EVAL` renders a
// tree to source and hands it to the existing parser, which is the whole reason
// this engine needs no RakuAST→AST compiler. So the renderer's contract is
// *correct Raku*, and only then Rakudo's exact spelling.
//
// Every rendering below is MEASURED, not invented:
// docs/dev/findings/rakuast/deparse-2026.08.tsv is what Rakudo 2026.08 answers
// for each of these constructions, produced by tools/rakuast-deparse-spec.raku,
// and t/regression/rakuast-deparse.raku asserts the same strings. When the
// oracle moves, re-run the tool and the diff is the work.
//
// Nodes are plain ObjectData over the registry's ClassInfos, with the
// syntax-bearing attributes in `attrs` under Rakudo's own names minus the `$!`
// (`value`, `left`, `infix`, `right`, …). Rakudo's compiler state — `$!origin`,
// `$!sorries`, `$!resolution` and the rest — is deliberately not reproduced.

#include "RakuAstClasses.h"

#include "AsciiCtype.h"
#include "Interpreter.h"
#include "Value.h"

#include <map>
#include <set>

namespace rakupp {

namespace {

// The class name without the `RakuAST::` prefix — every table below is keyed
// that way, because that is how the plan, the spec file and the dists spell it.
std::string shortName(const Value& node) {
    if (node.t != VT::Object || !node.obj() || !node.obj()->cls) return "";
    const std::string& n = node.obj()->cls->name;
    return isRakuAstName(n) ? n.substr(9) : n;
}

// Where a class's POSITIONAL `.new` arguments land. Everything else arrives as
// a named pair and is stored under its own key, so this table only has to carry
// the constructors Rakudo gives a positional signature.
// Measured: `RakuAST::IntLiteral.new(42)`, `RakuAST::Infix.new('eq')`,
// `RakuAST::Blockoid.new($statement-list)`, and the slurpy ones below.
const std::map<std::string, const char*>& positionalSlot() {
    static const std::map<std::string, const char*> t = {
        {"IntLiteral", "value"}, {"StrLiteral", "value"}, {"Literal", "value"},
        {"NumLiteral", "value"}, {"RatLiteral", "value"}, {"VersionLiteral", "value"},
        {"Var::Lexical", "name"}, {"Var::Dynamic", "name"},
        {"Var::Lexical::Constant", "name"},
        {"Infix", "operator"}, {"Prefix", "operator"}, {"Postfix", "operator"},
        {"ColonPair::True", "key"}, {"ColonPair::False", "key"},
        {"Blockoid", "statement-list"},
        {"Term::TopicCall", "call"}, {"Term::Name", "name"},
        {"Type::Simple", "name"}, {"Type::Capture", "name"},
        {"ParameterTarget::Term", "name"},
        {"Initializer::Assign", "expression"}, {"Initializer::Bind", "expression"},
        {"StatementModifier::If", "expression"},
        {"StatementModifier::Unless", "expression"},
        {"StatementModifier::While", "expression"},
        {"StatementModifier::Until", "expression"},
        {"StatementModifier::For", "expression"},
    };
    return t;
}

// …and the classes whose positionals are a LIST: every positional goes into one
// array slot. `RakuAST::ArgList.new($a, $b)`, `RakuAST::StatementList.new(…)`.
const std::map<std::string, const char*>& slurpySlot() {
    static const std::map<std::string, const char*> t = {
        {"ArgList", "args"}, {"Name", "parts"},
        {"StatementList", "statements"}, {"SemiList", "statements"},
    };
    return t;
}

bool isNode(const Value& v) {
    return v.t == VT::Object && v.obj() && v.obj()->cls && isRakuAstName(v.obj()->cls->name);
}

const Value* attr(const Value& node, const char* key) {
    if (node.t != VT::Object || !node.obj()) return nullptr;
    auto it = node.obj()->attrs.find(key);
    return it == node.obj()->attrs.end() ? nullptr : &it->second;
}

// A Raku double-quoted string literal for `s`. Rakudo renders StrLiteral in
// canonical double quotes whatever the source spelling was (all four quote
// forms are one QuotedString upstream), so there is nothing to preserve.
std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            case '$':  out += "\\$";  break;   // or it interpolates on the way back
            case '@':  out += "\\@";  break;
            default:   out += c;
        }
    }
    return out + "\"";
}

// An operator that is a WORD takes a trailing space of its own, which is how
// `Prefix.new("not")` renders `not ` and `not $_` comes out right.
bool wordOperator(const std::string& op) {
    return !op.empty() && (ascii::isalpha((unsigned char)op[0]) || op[0] == '_');
}

struct Deparser {
    Interpreter& I;
    explicit Deparser(Interpreter& i) : I(i) {}

    [[noreturn]] void notImplemented(const std::string& cls) {
        // Never a wrong rendering: a class the renderer does not cover says so,
        // with the name, so the gap is a bug report rather than bad source.
        throw RakuError{Value::typeObj("X::NYI"),
                        "RakuAST::" + cls + " cannot be deparsed yet"};
    }

    std::string pad(int indent) { return std::string(indent * 4, ' '); }

    // A child that may be absent renders empty rather than throwing — an unset
    // optional attribute is how `.new` leaves the ones a caller did not pass.
    std::string opt(const Value* v, int indent) {
        return (v && isNode(*v)) ? render(*v, indent) : std::string();
    }

    // The elements of an array-valued attribute, rendered and joined.
    std::string joinList(const Value* v, const char* sep, int indent) {
        std::string out;
        if (!v || v->t != VT::Array || !v->arr()) return out;
        bool first = true;
        for (auto& e : *v->arr()) {
            if (!first) out += sep;
            first = false;
            out += isNode(e) ? render(e, indent) : e.toStr();
        }
        return out;
    }

    // A statement list: each statement on its own line, `;` after all but the
    // last, a newline after every one. Measured: one statement is "42\n", two
    // are "42;\n\"foo\"\n".
    std::string statements(const Value& list, int indent) {
        const Value* ss = attr(list, "statements");
        std::string out;
        if (!ss || ss->t != VT::Array || !ss->arr()) return out;
        size_t n = ss->arr()->size();
        for (size_t i = 0; i < n; i++) {
            const Value& s = (*ss->arr())[i];
            out += pad(indent) + (isNode(s) ? render(s, indent) : s.toStr());
            if (i + 1 < n) out += ";";
            out += "\n";
        }
        return out;
    }

    // `{ … }` around a statement list, indented one level. The closing brace
    // carries NO trailing newline — its enclosing statement adds one.
    std::string blockoid(const Value* body, int indent) {
        if (!body || !isNode(*body)) return "{\n" + pad(indent) + "}";
        const Value* sl = attr(*body, "statement-list");
        std::string inner = (sl && isNode(*sl)) ? statements(*sl, indent + 1) : std::string();
        return "{\n" + inner + pad(indent) + "}";
    }

    std::string render(const Value& node, int indent) {
        const std::string c = shortName(node);
        if (c.empty()) return node.toStr();

        // ---- literals and terms -----------------------------------------
        if (c == "IntLiteral" || c == "NumLiteral" || c == "RatLiteral") {
            const Value* v = attr(node, "value");
            return v ? v->toStr() : "0";
        }
        if (c == "StrLiteral") {
            const Value* v = attr(node, "value");
            return quoted(v ? v->toStr() : "");
        }
        if (c == "Literal") {
            // `.from-value` holds a live value with no source behind it; the
            // faithful rendering is its own `.raku`, which is what Rakudo does.
            const Value* v = attr(node, "value");
            if (!v) return "Nil";
            ValueList none;
            return I.methodCall(*v, "raku", none).toStr();
        }
        if (c == "Var::Lexical" || c == "Var::Dynamic" || c == "Var::Lexical::Constant" ||
            c == "Var::Attribute" || c == "Var::Compiler::Lookup") {
            const Value* v = attr(node, "name");
            return v ? v->toStr() : "";
        }
        if (c == "Name") {
            const Value* p = attr(node, "parts");
            return joinList(p, "::", indent);
        }
        if (c == "Name::Part::Simple") {
            const Value* v = attr(node, "name");
            return v ? v->toStr() : "";
        }
        if (c == "Term::Name") return opt(attr(node, "name"), indent);
        if (c == "ColonPair::True")  { const Value* k = attr(node, "key"); return ":" + (k ? k->toStr() : ""); }
        if (c == "ColonPair::False") { const Value* k = attr(node, "key"); return ":!" + (k ? k->toStr() : ""); }

        // ---- operators ---------------------------------------------------
        if (c == "Infix" || c == "Prefix" || c == "Postfix") {
            const Value* o = attr(node, "operator");
            std::string op = o ? o->toStr() : "";
            // A word prefix carries its own separating space; an infix gets
            // spaces from its application, so only the prefix does it here.
            if (c == "Prefix" && wordOperator(op)) op += " ";
            return op;
        }
        if (c == "ApplyInfix") {
            std::string op = opt(attr(node, "infix"), indent);
            return opt(attr(node, "left"), indent) + " " + op + " " +
                   opt(attr(node, "right"), indent);
        }
        if (c == "ApplyListInfix") {
            std::string op = opt(attr(node, "infix"), indent);
            std::string sep = op + " ";
            return joinList(attr(node, "operands"), sep.c_str(), indent);
        }
        if (c == "ApplyPrefix")
            return opt(attr(node, "prefix"), indent) + opt(attr(node, "operand"), indent);
        if (c == "ApplyPostfix") {
            const Value* opnd = attr(node, "operand");
            std::string post = opt(attr(node, "postfix"), indent);
            // An explicit `$_` invocant ELIDES, exactly as Rakudo renders it:
            // `$_.fc` is `.fc`, the same text Term::TopicCall gives. `.fc`
            // means `$_.fc`, so nothing is lost — but a renderer that keeps the
            // `$_` produces a differently shaped tree on the way back.
            if (opnd && isNode(*opnd) && shortName(*opnd) == "Var::Lexical") {
                const Value* n = attr(*opnd, "name");
                if (n && n->toStr() == "$_" && !post.empty() && post[0] == '.') return post;
            }
            return opt(opnd, indent) + post;
        }
        if (c == "Ternary")
            return opt(attr(node, "condition"), indent) + " ?? " +
                   opt(attr(node, "then"), indent) + " !! " + opt(attr(node, "else"), indent);

        // ---- calls -------------------------------------------------------
        if (c == "ArgList") return joinList(attr(node, "args"), ", ", indent);
        if (c == "Call::Name") {
            // A named call always shows its parens, empty or not: `foo()`.
            return opt(attr(node, "name"), indent) + "(" + opt(attr(node, "args"), indent) + ")";
        }
        if (c == "Call::Name::WithoutParentheses") {
            std::string a = opt(attr(node, "args"), indent);
            return opt(attr(node, "name"), indent) + (a.empty() ? "" : " " + a);
        }
        if (c == "Call::Method" || c == "Call::MaybeMethod" || c == "Call::PrivateMethod") {
            const Value* a = attr(node, "args");
            std::string dot = c == "Call::MaybeMethod" ? ".?" : c == "Call::PrivateMethod" ? "!" : ".";
            std::string args = opt(a, indent);
            // No arguments, no parens — measured `.fc` against `.match("foo")`.
            bool hasArgs = a && isNode(*a) && !args.empty();
            return dot + opt(attr(node, "name"), indent) + (hasArgs ? "(" + args + ")" : "");
        }
        if (c == "Term::TopicCall") return opt(attr(node, "call"), indent);
        if (c == "Postcircumfix::ArrayIndex")
            return "[" + opt(attr(node, "index"), indent) + "]";
        if (c == "Postcircumfix::HashIndex")
            return "{" + opt(attr(node, "index"), indent) + "}";
        if (c == "Circumfix::Parentheses")
            return "(" + opt(attr(node, "semilist"), indent) + ")";

        // ---- statements and blocks ---------------------------------------
        if (c == "SemiList") {
            const Value* ss = attr(node, "statements");
            return joinList(ss, "; ", indent);
        }
        if (c == "StatementList") return statements(node, indent);
        if (c == "Statement::Expression") {
            std::string out = opt(attr(node, "expression"), indent);
            if (const Value* m = attr(node, "condition-modifier"))
                if (isNode(*m)) out += " " + render(*m, indent);
            if (const Value* m = attr(node, "loop-modifier"))
                if (isNode(*m)) out += " " + render(*m, indent);
            return out;
        }
        if (c == "StatementModifier::If")     return "if "     + opt(attr(node, "expression"), indent);
        if (c == "StatementModifier::Unless") return "unless " + opt(attr(node, "expression"), indent);
        if (c == "StatementModifier::While")  return "while "  + opt(attr(node, "expression"), indent);
        if (c == "StatementModifier::Until")  return "until "  + opt(attr(node, "expression"), indent);
        if (c == "StatementModifier::For")    return "for "    + opt(attr(node, "expression"), indent);
        if (c == "Blockoid") {
            const Value* sl = attr(node, "statement-list");
            std::string inner = (sl && isNode(*sl)) ? statements(*sl, indent + 1) : std::string();
            return "{\n" + inner + pad(indent) + "}";
        }
        if (c == "Block") return blockoid(attr(node, "body"), indent);
        if (c == "PointyBlock") {
            std::string sig = opt(attr(node, "signature"), indent);
            return "-> " + (sig.empty() ? "" : sig + " ") + blockoid(attr(node, "body"), indent);
        }
        if (c == "Sub" || c == "Method" || c == "Submethod") {
            std::string kw = c == "Sub" ? "sub " : c == "Method" ? "method " : "submethod ";
            std::string nm = opt(attr(node, "name"), indent);
            std::string sig = opt(attr(node, "signature"), indent);
            return kw + nm + (sig.empty() ? "" : "(" + sig + ")") + " " +
                   blockoid(attr(node, "body"), indent);
        }
        if (c == "Statement::If") {
            std::string out = "if " + opt(attr(node, "condition"), indent) + " " +
                              render(*attr(node, "then"), indent);
            if (const Value* e = attr(node, "else"))
                if (isNode(*e)) out += "\n" + pad(indent) + "else " + render(*e, indent);
            return out + "\n";
        }
        if (c == "Statement::Unless")
            return "unless " + opt(attr(node, "condition"), indent) + " " +
                   opt(attr(node, "body"), indent) + "\n";
        if (c == "Statement::For")
            return "for " + opt(attr(node, "source"), indent) + " " +
                   opt(attr(node, "body"), indent);
        if (c == "Statement::While")
            return "while " + opt(attr(node, "condition"), indent) + " " +
                   opt(attr(node, "body"), indent);

        // ---- declarations, signatures, types -----------------------------
        if (c == "VarDeclaration::Simple") {
            const Value* sc = attr(node, "scope");
            std::string scope = sc ? sc->toStr() : "my";
            const Value* sig = attr(node, "sigil");
            std::string out = scope + " " + opt(attr(node, "type"), indent);
            if (out.size() > scope.size() + 1) out += " ";
            out += (sig ? sig->toStr() : "") + opt(attr(node, "desigilname"), indent);
            if (const Value* i = attr(node, "initializer"))
                if (isNode(*i)) out += render(*i, indent);
            return out;
        }
        if (c == "Initializer::Assign") return " = "  + opt(attr(node, "expression"), indent);
        if (c == "Initializer::Bind")   return " := " + opt(attr(node, "expression"), indent);
        if (c == "Signature")  return joinList(attr(node, "parameters"), ", ", indent);
        if (c == "Parameter") {
            std::string out;
            std::string ty = opt(attr(node, "type"), indent);
            if (!ty.empty()) out += ty + " ";
            // The slurpy marker is a TYPE OBJECT on the parameter, not a node:
            // `slurpy => RakuAST::Parameter::Slurpy::Flattened`.
            if (const Value* s = attr(node, "slurpy")) {
                std::string sn;
                if (s->t == VT::Type) sn = s->s;
                if (isRakuAstName(sn)) sn = sn.substr(9);
                if (sn == "Parameter::Slurpy::Flattened")           out += "*";
                else if (sn == "Parameter::Slurpy::Unflattened")    out += "**";
                else if (sn == "Parameter::Slurpy::SingleArgument") out += "+";
                else if (sn == "Parameter::Slurpy::Capture")        out += "|";
            }
            out += opt(attr(node, "target"), indent);
            if (const Value* d = attr(node, "default"))
                if (isNode(*d)) out += " = " + render(*d, indent);
            return out;
        }
        if (c == "ParameterTarget::Var")  { const Value* n = attr(node, "name"); return n ? n->toStr() : ""; }
        if (c == "ParameterTarget::Term") return opt(attr(node, "name"), indent);
        if (c == "Type::Simple")  return opt(attr(node, "name"), indent);
        if (c == "Type::Setting") return opt(attr(node, "name"), indent);
        if (c == "Type::Capture") return "::" + opt(attr(node, "name"), indent);
        if (c == "Type::Definedness") {
            const Value* d = attr(node, "definite");
            bool definite = d && d->truthy();
            return opt(attr(node, "base-type"), indent) + (definite ? ":D" : ":U");
        }
        if (c == "Type::Coercion")
            return opt(attr(node, "base-type"), indent) + "(" +
                   opt(attr(node, "constraint"), indent) + ")";
        if (c == "Type::Parameterized")
            return opt(attr(node, "base-type"), indent) + "[" +
                   opt(attr(node, "args"), indent) + "]";
        if (c == "Trait::Is")  return "is "  + opt(attr(node, "name"), indent);
        if (c == "Trait::Of")  return "of "  + opt(attr(node, "type"), indent);
        if (c == "Trait::Does") return "does " + opt(attr(node, "type"), indent);

        // ---- whole units --------------------------------------------------
        if (c == "CompUnit") {
            const Value* sl = attr(node, "statement-list");
            return (sl && isNode(*sl)) ? statements(*sl, indent) : std::string();
        }
        // A source-slice node keeps its own text — a regex literal's DEPARSE is
        // the literal, which is why no regex tree is needed to render one.
        if (c == "QuotedRegex") {
            const Value* s = attr(node, "source-slice");
            if (s) return s->toStr();
        }
        notImplemented(c);
    }
};

} // namespace

Value rakuAstNew(Interpreter& I, const std::string& qualifiedName, ValueList& args) {
    const std::shared_ptr<ClassInfo>* ci = rakuAstClass(qualifiedName);
    if (!ci) throw RakuError{Value::typeObj("X::Undeclared::Symbols"),
                             "Undeclared name '" + qualifiedName + "'"};
    auto od = std::make_shared<ObjectData>();
    od->cls = *ci;
    const std::string cls = isRakuAstName(qualifiedName) ? qualifiedName.substr(9) : qualifiedName;

    // Named arguments keep their own key — that is how every Rakudo constructor
    // beyond the handful of positional ones is spelled, and storing an unknown
    // one costs nothing and loses nothing.
    ValueList positionals;
    for (auto& a : args) {
        if (a.t == VT::Pair && a.namedArg && a.pairVal()) od->attrs[a.s] = *a.pairVal();
        else positionals.push_back(a);
    }
    if (!positionals.empty()) {
        auto sl = slurpySlot().find(cls);
        if (sl != slurpySlot().end()) {
            Value list = Value::array();
            for (auto& p : positionals) list.arr()->push_back(p);
            od->attrs[sl->second] = std::move(list);
        } else {
            auto ps = positionalSlot().find(cls);
            if (ps == positionalSlot().end())
                throw RakuError{Value::typeObj("X::Constructor::Positional"),
                    "Default constructor for '" + qualifiedName + "' only takes named arguments"};
            od->attrs[ps->second] = positionals[0];
        }
    }
    (void)I;
    return Value::object(od);
}

std::string rakuAstDeparse(Interpreter& I, const Value& node) {
    Deparser d{I};
    return d.render(node, 0);
}

// `RakuAST::Name.from-identifier("foo")` / `.from-identifier-parts("Foo","Bar")`
// — the spelling every dist uses to make a name, rather than `.new` over Part
// objects. The parts are stored as plain strings; the renderer joins them.
Value rakuAstNameFrom(Interpreter&, const ValueList& parts) {
    const std::shared_ptr<ClassInfo>* ci = rakuAstClass("RakuAST::Name");
    auto od = std::make_shared<ObjectData>();
    if (ci) od->cls = *ci;
    Value list = Value::array();
    for (auto& p : parts) list.arr()->push_back(Value::str(p.toStr()));
    od->attrs["parts"] = std::move(list);
    return Value::object(od);
}

} // namespace rakupp
