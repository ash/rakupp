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
    std::string s = isRakuAstName(n) ? n.substr(9) : n;
    // `$node but Role` names its anonymous subclass `…::TopicCall+{Role}`, which
    // matches no table key. A mixin does not change what a node DEPARSES as —
    // Rakudo dispatches by method and never sees the name — so cut it off.
    // Needle::Compile tags every needle it builds this way (`… but Type<and>`).
    auto plus = s.find("+{");
    if (plus != std::string::npos) s.resize(plus);
    return s;
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
        {"Var::Lexical", "name"}, {"Var::Dynamic", "name"}, {"Var::Attribute", "name"},
        {"Var::Lexical::Constant", "name"},
        {"Infix", "operator"}, {"Prefix", "operator"}, {"Postfix", "operator"},
        {"Assignment", "operator"}, {"MetaInfix::Assign", "infix"},
        {"ColonPair::True", "key"}, {"ColonPair::False", "key"},
        {"Blockoid", "statement-list"},
        {"Term::TopicCall", "call"}, {"Term::Name", "name"}, {"Term::Enum", "name"},
        {"Type::Simple", "name"}, {"Type::Capture", "name"},
        {"ParameterTarget::Term", "name"},
        {"Initializer::Assign", "expression"}, {"Initializer::Bind", "expression"},
        {"StatementModifier::If", "expression"},
        {"StatementModifier::Unless", "expression"},
        {"StatementModifier::While", "expression"},
        {"StatementModifier::Until", "expression"},
        {"StatementModifier::For", "expression"},
        {"StatementModifier::Given", "expression"},
        {"StatementModifier::With", "expression"},
        {"StatementModifier::Without", "expression"},
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

// What has to be escaped inside a double-quoted Raku string, in ONE place.
// It was in two: `quoted()` escaped the newline and the tab but not the brace,
// and the QuotedString arm's own inline loop escaped the brace but not the
// newline — so a literal segment carrying a `\n` came back out as an actual
// line break in the middle of a string. That is legal Raku on its own, which is
// why it survived the round trip for as long as it did; it stopped being legal
// the moment the same string also held a `{`, and the round-trip harness only
// reached the file at all once phaser blocks had a view.
//
// `$`/`@` or the text interpolates on the way back; `{` or it becomes a code
// block. Rakudo escapes all of these — measured against `StrLiteral.DEPARSE`.
void escapeDq(std::string& out, const std::string& s) {
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            case '$':  out += "\\$";  break;
            case '@':  out += "\\@";  break;
            case '{':  out += "\\{";  break;
            default:   out += c;
        }
    }
}

// A Raku double-quoted string literal for `s`. Rakudo renders StrLiteral in
// canonical double quotes whatever the source spelling was (all four quote
// forms are one QuotedString upstream), so there is nothing to preserve.
std::string quoted(const std::string& s) {
    std::string out = "\"";
    escapeDq(out, s);
    return out + "\"";
}

// A prefix operator that needs a space before its argument, which is how
// `Prefix.new("not")` renders `not ` and `not $_` comes out right.
//
// Two kinds need it. A WORD, because `not$_` is one identifier — and a
// REDUCTION metaop `[+]`, because the parser refuses `[+]@a` outright
// ("whitespace required before a reduction metaop's argument"). The second was
// found by the round-trip harness: `say [+] @a` rendered `say [+]@a`, which is
// not Raku, on four of the corpus's programs.
bool prefixNeedsSpace(const std::string& op) {
    if (op.empty()) return false;
    if (ascii::isalpha((unsigned char)op[0]) || op[0] == '_') return true;
    return op.front() == '[' && op.back() == ']';
}

struct Deparser {
    Interpreter& I;
    // The side table: a live value with no source behind it renders as a
    // synthetic NAME, and `.EVAL` binds that name back to the value. The
    // counter runs whether or not anyone is collecting, so a bare user-called
    // `.DEPARSE` and the one `.EVAL` makes produce the same text.
    ValueList* side = nullptr;
    int litCount = 0;
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
    // `terminateLast` is a COMPUNIT thing, and it is measured rather than
    // guessed: a CompUnit terminates every statement (`say 1;\n`) while a bare
    // StatementList omits the last (`say 1\n`) — whether the tree was parsed or
    // constructed. Structural, so no per-statement state reproduces it.
    std::string statements(const Value& list, int indent, bool terminateLast = false) {
        const Value* ss = attr(list, "statements");
        std::string out;
        if (!ss || ss->t != VT::Array || !ss->arr()) return out;
        size_t n = ss->arr()->size();
        for (size_t i = 0; i < n; i++) {
            const Value& s = (*ss->arr())[i];
            std::string one = isNode(s) ? render(s, indent) : s.toStr();
            out += pad(indent) + one;
            // A BLOCK statement — `if`, `for`, a `sub` declaration — already
            // ends in its own newline, and Rakudo puts neither a `;` nor a
            // second newline after it. Getting this wrong put a bare `;` on a
            // line of its own after every closing brace, which does not parse;
            // the round-trip harness is what showed it.
            if (!one.empty() && one.back() == '\n') continue;
            // …and a statement that ends in `}` WITHOUT its own newline gets a
            // bare newline, not `;`. That is the rule upstream and it is not
            // per-class: `Statement::If` renders its own trailing newline and
            // `Statement::For` does not (both measured on a lone constructed
            // node), yet a list separates them identically — and so does it for
            // `my $c = -> { 1 }`, which is no kind of block statement at all.
            // Answering this per class instead gets one of those three wrong
            // whichever way it is written.
            if (!one.empty() && one.back() == '}') { out += "\n"; continue; }
            if (i + 1 < n || terminateLast) out += ";";
            out += "\n";
        }
        return out;
    }

    // `{ … }` around a statement list, indented one level. The closing brace
    // carries NO trailing newline — its enclosing statement adds one.
    // The one statement inside a single-statement Block, or null. An
    // interpolation segment is a Block upstream and renders as what it holds.
    const Value* soleStatement(const Value& blk) {
        const Value* body = attr(blk, "body");
        if (!body || !isNode(*body)) return nullptr;
        const Value* sl = attr(*body, "statement-list");
        if (!sl || !isNode(*sl)) return nullptr;
        const Value* ss = attr(*sl, "statements");
        if (!ss || ss->t != VT::Array || !ss->arr() || ss->arr()->size() != 1) return nullptr;
        const Value& st = (*ss->arr())[0];
        if (!isNode(st) || shortName(st) != "Statement::Expression") return nullptr;
        return attr(st, "expression");
    }

    // The text of a `Doc::Paragraph`/`Doc::Markup` — its atoms in order, a Str
    // as itself and a nested markup through the renderer.
    std::string docAtoms(const Value& node) {
        std::string out;
        if (const Value* as = attr(node, "atoms"))
            if (as->t == VT::Array && as->arr())
                for (auto& e : *as->arr()) out += isNode(e) ? render(e, 0) : e.toStr();
        return out;
    }

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
            // `.from-value` holds a live value with no source behind it.
            // Numbers, strings and the ordinary containers round-trip through
            // their own `.raku`; a CLOSURE or an OBJECT does not — Rakudo
            // renders those as an address comment and the value is gone. Here
            // they render as a synthetic name that `.EVAL` binds back to the
            // value itself, so identity survives the trip (`===` afterwards).
            // That is the one place this renderer deliberately says something
            // Rakudo does not, and it is what makes the text bridge carry more
            // than literals.
            const Value* v = attr(node, "value");
            if (!v) return "Nil";
            if (v->t == VT::Code || v->t == VT::Object) {
                std::string nm = "$RAKUAST-LIT" + std::to_string(litCount++);
                if (side) side->push_back(*v);
                return nm;
            }
            ValueList none;
            return I.methodCall(*v, "raku", none).toStr();
        }
        if (c == "Var::Lexical" || c == "Var::Dynamic" || c == "Var::Lexical::Constant" ||
            c == "Var::Attribute" || c == "Var::Compiler::Lookup") {
            const Value* v = attr(node, "name");
            return v ? v->toStr() : "";
        }
        if (c == "Name") {
            // A name is its parts joined by `::` — but a SYMBOLIC part carries
            // its own `::(…)` and the empty part before it stands for the
            // nothing at the front, so those two cannot go through the plain
            // join. (Rakudo's own deparse drops the `::` here and renders
            // `::($x)` as `($x)`, which is a different program; ours does not.)
            const Value* p = attr(node, "parts");
            bool symbolic = false;
            if (p && p->t == VT::Array && p->arr())
                for (auto& e : *p->arr())
                    if (isNode(e) && shortName(e) == "Name::Part::Expression") symbolic = true;
            if (!symbolic) return joinList(p, "::", indent);
            std::string out;
            for (auto& e : *p->arr()) {
                if (!isNode(e)) { if (!out.empty()) out += "::"; out += e.toStr(); continue; }
                const std::string pc = shortName(e);
                if (pc == "Name::Part::Empty") continue;
                if (pc == "Name::Part::Expression")
                    { out += "::(" + opt(attr(e, "expr"), indent) + ")"; continue; }
                if (!out.empty()) out += "::";
                out += render(e, indent);
            }
            return out;
        }
        if (c == "Name::Part::Empty") return "";
        if (c == "Name::Part::Expression") return "::(" + opt(attr(node, "expr"), indent) + ")";
        if (c == "Name::Part::Simple") {
            const Value* v = attr(node, "name");
            return v ? v->toStr() : "";
        }
        if (c == "Term::Name" || c == "Term::Enum") return opt(attr(node, "name"), indent);
        if (c == "Term::Self")     return "self";
        if (c == "Term::Whatever") return "*";
        if (c == "Type::Enum") {
            return "enum " + opt(attr(node, "name"), indent) + " " +
                   opt(attr(node, "term"), indent) + "\n";
        }
        if (c == "Type::Subset") {
            std::string out = "subset " + opt(attr(node, "name"), indent);
            if (const Value* b = attr(node, "base-type"))
                if (isNode(*b)) out += " of " + render(*b, indent);
            if (const Value* w = attr(node, "where"))
                if (isNode(*w)) out += " where " + render(*w, indent);
            return out + "\n";
        }
        if (c == "QuotedString") {
            // A `words` processor means the angle form: `<a b c>`, not a string.
            // `enum C <a b c>` is the one place it turns up, and the quoted
            // spelling is a different program there — Rakudo refuses a list.
            if (const Value* pr = attr(node, "processors"))
                if (pr->t == VT::Array && pr->arr())
                    for (auto& e : *pr->arr())
                        if (e.toStr() == "words") {
                            std::string inner;
                            if (const Value* segs = attr(node, "segments"))
                                if (segs->t == VT::Array && segs->arr())
                                    for (auto& sg : *segs->arr())
                                        if (isNode(sg) && shortName(sg) == "StrLiteral") {
                                            const Value* v = attr(sg, "value");
                                            inner += v ? v->toStr() : "";
                                        }
                            return "<" + inner + ">";
                        }
        }
        if (c == "QuotedString") {
            // A parsed interpolating string: literal segments go in as they
            // were, and every other segment is a `{…}` block, which is the one
            // spelling that can carry any expression back through the parser.
            std::string out = "\"";
            if (const Value* segs = attr(node, "segments"))
                if (segs->t == VT::Array && segs->arr())
                    for (auto& s : *segs->arr()) {
                        if (isNode(s) && shortName(s) == "StrLiteral") {
                            const Value* v = attr(s, "value");
                            escapeDq(out, v ? v->toStr() : "");
                        } else if (isNode(s)) {
                            // The segment is a BLOCK upstream (`{…}` in a
                            // string is one), and it renders as its single
                            // statement rather than as a braced block — the
                            // braces are the interpolation's own.
                            const Value* inner = &s;
                            if (shortName(s) == "Block")
                                if (const Value* only = soleStatement(s)) inner = only;
                            out += "{" + render(*inner, indent) + "}";
                        }
                    }
            return out + "\"";
        }
        if (c == "ColonPair::True")  { const Value* k = attr(node, "key"); return ":" + (k ? k->toStr() : ""); }
        if (c == "ColonPair::False") { const Value* k = attr(node, "key"); return ":!" + (k ? k->toStr() : ""); }
        if (c == "ColonPair::Value") {
            const Value* k = attr(node, "key");
            return ":" + (k ? k->toStr() : "") + "(" + opt(attr(node, "value"), indent) + ")";
        }
        if (c == "FatArrow") {
            const Value* k = attr(node, "key");
            return (k ? k->toStr() : "") + " => " + opt(attr(node, "value"), indent);
        }

        // ---- operators ---------------------------------------------------
        if (c == "Infix" || c == "Prefix" || c == "Postfix" || c == "Assignment") {
            const Value* o = attr(node, "operator");
            std::string op = o ? o->toStr() : "";
            // A word prefix carries its own separating space; an infix gets
            // spaces from its application, so only the prefix does it here.
            if (c == "Prefix" && prefixNeedsSpace(op)) op += " ";
            return op;
        }
        // `+=` is the ASSIGN METAOP over `+`, not an operator spelled `+=`.
        if (c == "MetaInfix::Assign") return opt(attr(node, "infix"), indent) + "=";
        if (c == "ApplyInfix") {
            std::string op = opt(attr(node, "infix"), indent);
            // The operands live in the node's ArgList. `.new(:left, :right)` is
            // still the constructor every dist writes, so a node that carries
            // the two slots instead renders from them.
            const Value* args = attr(node, "args");
            if (args) {
                const Value* inner = args->t == VT::Object ? attr(*args, "args") : args;
                if (inner && inner->t == VT::Array && inner->arr() && inner->arr()->size() >= 2)
                    return render((*inner->arr())[0], indent) + " " + op + " " +
                           render((*inner->arr())[1], indent);
            }
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
        // `.^name` — the META-method call. Upstream the name is a plain Str
        // and the `dispatcher` (`^`, `?`, `!`) is its own attribute; ours keeps
        // a Name node, so this reads whichever is there.
        if (c == "Call::MetaMethod") {
            const Value* a = attr(node, "args");
            std::string args = opt(a, indent);
            bool hasArgs = a && isNode(*a) && !args.empty();
            const Value* d = attr(node, "dispatcher");
            std::string disp = d && !d->toStr().empty() ? d->toStr() : "^";
            return "." + disp + opt(attr(node, "name"), indent) +
                   (hasArgs ? "(" + args + ")" : "");
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
        if (c == "Circumfix::ArrayComposer")
            return "[" + opt(attr(node, "semilist"), indent) + "]";
        if (c == "Contextualizer::List") return "@(" + opt(attr(node, "target"), indent) + ")";
        if (c == "Contextualizer::Hash") return "%(" + opt(attr(node, "target"), indent) + ")";
        if (c == "Contextualizer::Item") return "$(" + opt(attr(node, "target"), indent) + ")";
        if (c == "Circumfix::HashComposer")
            return "{" + opt(attr(node, "semilist"), indent) + "}";
        if (c == "Postcircumfix::LiteralHashIndex")
            return "<" + opt(attr(node, "index"), indent) + ">";
        if (c == "Call::Term")
            return "(" + opt(attr(node, "args"), indent) + ")";
        if (c == "ApplyInfix::Chaining") {
            // `1 < $x < 10` — operands and infixes interleaved, which is the one
            // shape our tree and Rakudo's both keep whole rather than nesting.
            const Value* ops = attr(node, "operands");
            const Value* ixs = attr(node, "infixes");
            std::string out;
            if (ops && ops->t == VT::Array && ops->arr())
                for (size_t i = 0; i < ops->arr()->size(); i++) {
                    if (i) {
                        out += " ";
                        if (ixs && ixs->t == VT::Array && ixs->arr() && i - 1 < ixs->arr()->size())
                            out += render((*ixs->arr())[i - 1], indent);
                        out += " ";
                    }
                    out += render((*ops->arr())[i], indent);
                }
            return out;
        }

        // ---- statements and blocks ---------------------------------------
        if (c == "SemiList") {
            const Value* ss = attr(node, "statements");
            return joinList(ss, "; ", indent);
        }
        if (c == "StatementList") return statements(node, indent);
        if (c == "Statement::Empty") return "";
        if (c == "Statement::Use")    return "use "    + opt(attr(node, "module-name"), indent);
        if (c == "Statement::Import") return "import " + opt(attr(node, "module-name"), indent);
        if (c == "Statement::Need")   return "need "   + opt(attr(node, "module-name"), indent);
        if (c == "Statement::Expression") {
            std::string out = opt(attr(node, "expression"), indent);
            // A statement whose expression IS a block — a routine declaration,
            // a bare block — ends in its own newline and takes no `;`, which is
            // what Rakudo renders and what the statement list reads back off
            // the trailing character. Without it two `multi` candidates came
            // out separated by `};`, which does not parse.
            if (const Value* x = attr(node, "expression"))
                if (isNode(*x)) {
                    const std::string xc = shortName(*x);
                    if (xc == "Sub" || xc == "Method" || xc == "Submethod" || xc == "Block")
                        out += "\n";
                }
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
        if (c == "StatementModifier::Given")  return "given "  + opt(attr(node, "expression"), indent);
        if (c == "StatementModifier::With")   return "with "   + opt(attr(node, "expression"), indent);
        if (c == "StatementModifier::Without") return "without " + opt(attr(node, "expression"), indent);
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
            // `multi` is part of the declarator, not decoration: dropping it made
            // two `multi` candidates render as two `sub`s of the same name, which
            // is a redeclaration error rather than a program.
            if (const Value* mn = attr(node, "multiness"))
                if (!mn->toStr().empty()) kw = mn->toStr() + " " + kw;
            std::string nm = opt(attr(node, "name"), indent);
            std::string sig = opt(attr(node, "signature"), indent);
            return kw + nm + (sig.empty() ? "" : "(" + sig + ")") + " " +
                   blockoid(attr(node, "body"), indent);
        }
        if (c == "Statement::If" || c == "Statement::Elsif" || c == "Statement::Unless") {
            // `opt`, never `*attr(...)`: a node built by `.new` with a required
            // child left unset is a real shape — `.raku` round-trip tests
            // construct exactly that — and dereferencing the miss segfaulted.
            std::string out = (c == "Statement::If" ? "if " : c == "Statement::Unless" ? "unless " : "elsif ") +
                              opt(attr(node, "condition"), indent) + " " +
                              opt(attr(node, "then"), indent);
            // The elsif CHAIN. Dropping it rendered valid Raku that means
            // something else — `if a {1} else {4}` for a three-branch
            // statement — which is the one thing this renderer must never do.
            if (const Value* es = attr(node, "elsifs"))
                if (es->t == VT::Array && es->arr())
                    for (auto& e : *es->arr())
                        if (isNode(e)) out += "\n" + pad(indent) + render(e, indent);
            if (const Value* e = attr(node, "else"))
                if (isNode(*e)) out += "\n" + pad(indent) + "else " + render(*e, indent);
            return c == "Statement::Elsif" ? out : out + "\n";
        }
        // No trailing newline on `for` or `while`: measured on a lone
        // constructed node, Rakudo's `Statement::If` renders one and these do
        // not. The statement list is what separates them — see `statements()`.
        if (c == "Statement::For")
            return "for " + opt(attr(node, "source"), indent) + " " +
                   opt(attr(node, "body"), indent);
        // `with`/`without` are the `if` family's shape with their own keyword;
        // `repeat` puts its condition AFTER the block and takes no `;`.
        if (c == "Statement::With" || c == "Statement::Without") {
            std::string out = (c == "Statement::With" ? "with " : "without ") +
                              opt(attr(node, "condition"), indent) + " " +
                              opt(attr(node, c == "Statement::With" ? "then" : "body"), indent);
            if (const Value* e = attr(node, "else"))
                if (isNode(*e)) out += "\n" + pad(indent) + "else " + render(*e, indent);
            return out + "\n";
        }
        // …and NO trailing newline on `repeat`: it does not end in `}`, it ends
        // in the condition, so it is an ordinary statement and takes the `;`
        // the statement list gives it. Measured on both engines.
        if (c == "Statement::Loop::RepeatWhile" || c == "Statement::Loop::RepeatUntil")
            return "repeat " + opt(attr(node, "body"), indent) +
                   (c == "Statement::Loop::RepeatWhile" ? " while " : " until ") +
                   opt(attr(node, "condition"), indent);
        // `BEGIN { … }` — the class name IS the keyword, title-cased, so the
        // renderer upper-cases it back rather than carrying a second table.
        if (c.compare(0, 25, "StatementPrefix::Phaser::") == 0) {
            std::string kw = c.substr(25);
            for (char& ch : kw) ch = (char)ascii::toupper((unsigned char)ch);
            return kw + " " + opt(attr(node, "blorst"), indent) + "\n";
        }
        // ---- the `Doc::` subtree (P5) -----------------------------------
        // Pod renders back as pod. Rakudo re-emits the block's own source; we
        // rebuild it from the tree, which is the same thing for everything the
        // view carries and differs only in the trailing whitespace our pod DOM
        // does not keep. What matters here is that it RE-PARSES to the same
        // tree — the round-trip harness is the gate, and a `Doc::Block` with no
        // arm at all was a named `deparse` miss on every file with pod in it.
        if (c == "Doc::Markup") {
            const Value* l = attr(node, "letter");
            return (l ? l->toStr() : std::string()) + "<" + docAtoms(node) + ">";
        }
        if (c == "Doc::Paragraph") return docAtoms(node);
        if (c == "Doc::Block") {
            const Value* t = attr(node, "type");
            const std::string type = t ? t->toStr() : "pod";
            const Value* lv = attr(node, "level");
            const std::string level = lv ? lv->toStr() : "";
            std::string body;
            if (const Value* ps = attr(node, "paragraphs"))
                if (ps->t == VT::Array && ps->arr())
                    for (auto& e : *ps->arr()) {
                        std::string one = isNode(e) ? render(e, indent) : e.toStr();
                        if (one.empty()) continue;
                        // A BLANK LINE between pieces, always: a `=head1 H`
                        // one-liner already ends in its newline, and without a
                        // second one the paragraph after it was absorbed into
                        // the heading when the text was read back.
                        if (!body.empty()) { if (body.back() != '\n') body += "\n"; body += "\n"; }
                        body += one;
                    }
            // `=head1 TEXT` / `=item TEXT` are one-liners; a named block is the
            // `=begin`/`=end` pair. The blank line after `=begin` is Rakudo's.
            if (type == "head" || type == "item" || type == "para")
                return "=" + (type == "para" ? std::string("para") : type + level) + " " + body + "\n";
            return "=begin " + type + "\n\n" + body + "\n=end " + type + "\n";
        }
        // The one regex shape the view builds — see RakuAstView.cpp. Rakudo
        // renders the literal BARE (`rule TOP { hello }`), which matches the
        // same text, so the quotes do not come back.
        if (c == "RuleDeclaration" || c == "TokenDeclaration" || c == "RegexDeclaration") {
            const std::string kw = c == "TokenDeclaration" ? "token"
                                 : c == "RegexDeclaration" ? "regex" : "rule";
            return kw + " " + opt(attr(node, "name"), indent) + " { " +
                   opt(attr(node, "body"), indent) + " }\n";
        }
        if (c == "Regex::WithWhitespace") return opt(attr(node, "regex"), indent);
        if (c == "Regex::Quote") {
            const Value* q = attr(node, "quoted");
            std::string out;
            if (q && isNode(*q))
                if (const Value* segs = attr(*q, "segments"))
                    if (segs->t == VT::Array && segs->arr())
                        for (auto& sg : *segs->arr())
                            if (isNode(sg) && shortName(sg) == "StrLiteral") {
                                const Value* v = attr(sg, "value");
                                out += v ? v->toStr() : "";
                            }
            return out;
        }
        if (c == "Statement::Whenever")
            return "whenever " + opt(attr(node, "trigger"), indent) + " " +
                   opt(attr(node, "body"), indent) + "\n";
        if (c == "Statement::Given")
            return "given " + opt(attr(node, "source"), indent) + " " +
                   opt(attr(node, "body"), indent) + "\n";
        if (c == "Statement::When")
            return "when " + opt(attr(node, "condition"), indent) + " " +
                   opt(attr(node, "body"), indent) + "\n";
        if (c == "Statement::Default")
            return "default " + opt(attr(node, "body"), indent) + "\n";
        if (c == "Statement::Loop") {
            // `loop (setup; condition; increment) BLOCK`, and the bare `loop`
            // when it has none of the three.
            std::string setup = opt(attr(node, "setup"), indent);
            std::string cond  = opt(attr(node, "condition"), indent);
            std::string incr  = opt(attr(node, "increment"), indent);
            std::string head  = "loop ";
            if (!setup.empty() || !cond.empty() || !incr.empty())
                head += "(" + setup + "; " + cond + "; " + incr + ") ";
            return head + opt(attr(node, "body"), indent) + "\n";
        }
        // `class` / `role` / `grammar` / `module` — four classes upstream, and
        // the declarator is the class name lowercased.
        if (c == "Class" || c == "Role" || c == "Grammar" || c == "Module" || c == "Package") {
            std::string kw;
            for (char ch : c) kw += (char)ascii::tolower((unsigned char)ch);
            std::string sc;
            if (const Value* s = attr(node, "scope"))
                if (s->toStr() == "my") sc = "my ";
            return sc + kw + " " + opt(attr(node, "name"), indent) + " " +
                   opt(attr(node, "body"), indent) + "\n";
        }
        if (c == "Statement::Loop::While" || c == "Statement::Loop::Until")
            return (c == "Statement::Loop::While" ? "while " : "until ") +
                   opt(attr(node, "condition"), indent) + " " +
                   opt(attr(node, "body"), indent);

        // ---- declarations, signatures, types -----------------------------
        if (c == "VarDeclaration::Simple") {
            const Value* sc = attr(node, "scope");
            std::string scope = sc ? sc->toStr() : "my";
            const Value* sig = attr(node, "sigil");
            std::string out = scope + " " + opt(attr(node, "type"), indent);
            if (out.size() > scope.size() + 1) out += " ";
            // The TWIGIL is not decoration: `has $.x` declares a public
            // attribute with an accessor and `has $x` a private one with none.
            // Dropping it rendered valid Raku meaning a different program, and
            // the round trip could not see it for exactly that reason.
            const Value* tw = attr(node, "twigil");
            out += (sig ? sig->toStr() : "") + (tw ? tw->toStr() : "")
                 + opt(attr(node, "desigilname"), indent);
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
            // `names` is what makes a parameter NAMED, and it is not decoration:
            // without the colon `(*@a, :$solar)` renders `(*@a, $solar)`, which
            // the parser refuses as a required parameter after a variadic one.
            // An alias keeps both halves: `:key($var)`.
            const Value* names = attr(node, "names");
            std::string target = opt(attr(node, "target"), indent);
            if (names && names->t == VT::Array && names->arr() && !names->arr()->empty()) {
                std::string key = (*names->arr())[0].toStr();
                std::string bare = target.size() > 1 ? target.substr(1) : target;
                out += (key == bare) ? ":" + target : ":" + key + "(" + target + ")";
            } else {
                out += target;
            }
            if (const Value* o = attr(node, "optional")) if (o->truthy()) out += "?";
            if (const Value* r = attr(node, "required")) if (r->truthy()) out += "!";
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
            return (sl && isNode(*sl)) ? statements(*sl, indent, /*terminateLast=*/true)
                                       : std::string();
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
        const char* slot = rakuAstListSlot(cls);
        if (slot) {
            Value list = Value::array();
            for (auto& p : positionals) list.arr()->push_back(p);
            od->attrs[slot] = std::move(list);
        } else {
            auto ps = positionalSlot().find(cls);
            if (ps == positionalSlot().end())
                throw RakuError{Value::typeObj("X::Constructor::Positional"),
                    "Default constructor for '" + qualifiedName + "' only takes named arguments"};
            od->attrs[ps->second] = positionals[0];
        }
    }
    // `ApplyInfix.new(:left, :infix, :right)` is what every dist writes, and
    // what Rakudo's own constructor takes — but it does not KEEP the two slots:
    // it folds them into an ArgList, and `.left`/`.right` read back through it.
    // Folding here means the tree has one shape whoever built it.
    if (cls == "ApplyInfix" && od->attrs.find("args") == od->attrs.end()) {
        auto l = od->attrs.find("left"), r = od->attrs.find("right");
        if (l != od->attrs.end() && r != od->attrs.end()) {
            Value inner = Value::array();
            inner.arr()->push_back(l->second);
            inner.arr()->push_back(r->second);
            auto ad = std::make_shared<ObjectData>();
            if (const std::shared_ptr<ClassInfo>* ac = rakuAstClass("RakuAST::ArgList")) ad->cls = *ac;
            ad->attrs["args"] = std::move(inner);
            od->attrs.erase("left");
            od->attrs.erase("right");
            od->attrs["args"] = Value::object(ad);
        }
    }
    (void)I;
    return Value::object(od);
}

std::string rakuAstDeparse(Interpreter& I, const Value& node) {
    Deparser d{I};
    return d.render(node, 0);
}

Value rakuAstEval(Interpreter& I, const Value& node) {
    Deparser d{I};
    ValueList side;
    d.side = &side;
    std::string src = d.render(node, 0);
    // The caller's scope has to be visible, and it already is: EVAL of TEXT
    // compiles in the current lexical scope here exactly as it does in Rakudo —
    // Part I's four probes measured that on both engines before any of this was
    // designed, which is why the bridge needs no compiler of its own.
    if (side.empty()) return I.evalString(src, /*mainlinePH=*/true);
    // …and when the tree carried live values, the only thing added is their
    // synthetic names. They go in a CHILD scope: the parent link keeps every
    // name the caller had reachable, and the synthetic ones are gone the moment
    // this returns rather than leaking into the scope that asked.
    auto sc = std::make_shared<Env>();
    sc->parent = Interpreter::tctx_.cur;
    for (size_t i = 0; i < side.size(); i++)
        sc->define("$RAKUAST-LIT" + std::to_string(i), side[i]);
    struct ScopeSwap {
        std::shared_ptr<Env>& slot; std::shared_ptr<Env> saved;
        ~ScopeSwap() { slot = std::move(saved); }
    } swap{Interpreter::tctx_.cur, Interpreter::tctx_.cur};
    Interpreter::tctx_.cur = sc;
    return I.evalString(src, /*mainlinePH=*/true);
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
