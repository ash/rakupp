/* grammar.hpp — Raku grammars for C++. Header-only, over <rakupp/raku.hpp>.
 * Installed as <rakupp/grammar.hpp>.
 *
 * The C++ face of the grammar service (docs/dev/plans/GRAMMAR-PLAN.md): the
 * grammar stays a .raku file, parsing happens in an embedded Raku++
 * interpreter, and this header is only invocation, results and lifetime.
 * The Raku shim it drives ships INSIDE librakupp (rk_grammar_shim), so a
 * C++ program needs exactly two things: this header and the library.
 *
 *   #include <rakupp/grammar.hpp>          // pulls in <rakupp/raku.hpp> too
 *
 *   auto g = rakupp::Grammar::from_file("log.raku", "Log", "LogActions");
 *   if (auto m = g.parse(text)) {
 *       auto lines = (*m)["line"];
 *       for (size_t i = 0; i < lines.size(); i++)
 *           std::cout << lines[i]["ip"].str() << "\n";   // lazy: one call per leaf
 *   }
 *   rakupp::Tree all = m->tree();     // eager, opt-in (costs ~1.4x the parse)
 *
 * Lifetime is the part C++ gets for FREE, which is why it is the plan's
 * second host: a Match holds a rooted value (rk_root) and its destructor
 * unroots — the leak rooted handles reintroduce elsewhere cannot happen here.
 *
 * One interpreter per process, created on first use; one thread may talk to
 * it at a time (Raku code inside it threads as it pleases). Failures cross
 * as exceptions: RakuError for engine-side dies, ParseError (with line,
 * column, rule from the engine's highwater) from parse_or_throw.
 *
 * The general API — rakupp::eval, rakupp::call, Tree, RakuError, the shared
 * session — lives in <rakupp/raku.hpp>, which this header includes. Grammars
 * and bare Raku are one interpreter and one set of value rules.
 */
#pragma once

#include "raku.hpp"

#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace rakupp {

struct ParseError : RakuError {
    long pos = -1, line = -1, col = -1;
    std::string rule;
    explicit ParseError(const std::string& msg) : RakuError(msg) {}
};

namespace detail {

/* The rooted match value; the destructor is the whole lifetime story. */
struct Rooted {
    RkValue h = nullptr;
    explicit Rooted(RkValue rooted) : h(rooted) {}
    ~Rooted() { if (h) rk_unroot(Session::get().c, h); }
    Rooted(const Rooted&) = delete;
    Rooted& operator=(const Rooted&) = delete;
};

} // namespace detail

/* A lazy path under a rooted Match. Indexing accumulates; nothing crosses the
 * boundary until a terminal (str, int_, size, tree, …) — one rk_call each. */
class Node {
public:
    Node operator[](const std::string& key) const { return extended(key); }
    Node operator[](const char* key)        const { return extended(std::string(key)); }
    Node operator[](size_t i)               const { return extended(i); }
    /* an int literal would otherwise be ambiguous — 0 is also a null pointer */
    Node operator[](int i)                  const { return extended((size_t)i); }

    std::string str()  const { return leaf_str(walk("str")); }
    long long   int_() const { auto& S = detail::Session::get(); return rk_int_get(S.c, walk("int")); }
    double      num()  const { auto& S = detail::Session::get(); return rk_num_get(S.c, walk("num")); }
    bool truthy()  const { auto& S = detail::Session::get(); return rk_truthy(S.c, walk("bool")) != 0; }
    bool is_list() const { auto& S = detail::Session::get(); return rk_truthy(S.c, walk("islist")) != 0; }
    size_t size()  const { auto& S = detail::Session::get(); return (size_t)rk_int_get(S.c, walk("elems")); }
    Tree tree() const { return detail::Session::get().tree_of(walk("tree")); }
    Tree made() const { return detail::Session::get().tree_of(walk("made")); }

protected:
    std::shared_ptr<detail::Rooted> root_;
    std::vector<std::variant<size_t, std::string>> steps_;

    Node() = default;
    Node(std::shared_ptr<detail::Rooted> r, std::vector<std::variant<size_t, std::string>> s)
        : root_(std::move(r)), steps_(std::move(s)) {}

    Node extended(std::variant<size_t, std::string> step) const {
        auto s = steps_;
        s.push_back(std::move(step));
        return Node(root_, std::move(s));
    }

    RkValue walk(const char* op) const {
        auto& S = detail::Session::get();
        RkValue path = rk_array(S.c);
        for (auto& st : steps_) {
            if (std::holds_alternative<size_t>(st))
                rk_push(S.c, path, rk_int(S.c, (long long)std::get<size_t>(st)));
            else {
                const std::string& k = std::get<std::string>(st);
                rk_push(S.c, path, rk_str(S.c, k.c_str(), k.size()));
            }
        }
        return S.call("rk-match-walk", {root_->h, path, S.str(op)});
    }

    static std::string leaf_str(RkValue v) {
        auto& S = detail::Session::get();
        size_t n = 0;
        const char* s = rk_str_get(S.c, v, &n);
        return std::string(s ? s : "", n);
    }
};

class Match : public Node {
public:
    explicit Match(RkValue rooted)
        : Node(std::make_shared<detail::Rooted>(rooted), {}) {}
};

class Grammar {
public:
    /* `name` is the grammar's name in the source; empty only when the grammar
     * declaration is the source's LAST statement. `actions` names an actions
     * class in the same source; each parse runs a fresh instance. Identical
     * source compiles once; named compiles are isolated per compile, so a
     * recompile never rebinds an earlier Grammar's body. */
    static Grammar from_source(const std::string& source, const std::string& name = "",
                               const std::string& actions = "") {
        auto& S = detail::Session::get();
        RkValue id = S.call("rk-grammar-compile", {S.str(source), S.str(name), S.str(actions)});
        Grammar g;
        g.id_ = rk_int_get(S.c, id);
        g.label_ = name.empty() ? "<anonymous>" : name;
        return g;
    }

    static Grammar from_file(const std::string& path, const std::string& name = "",
                             const std::string& actions = "") {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw RakuError("cannot read '" + path + "'");
        std::stringstream ss;
        ss << in.rdbuf();
        Grammar g = from_source(ss.str(), name, actions);
        g.label_ = path;
        return g;
    }

    /* std::nullopt when the input does not match; parse_or_throw for the
     * diagnosed exception instead. The whole input must match (.parse
     * anchors both ends). */
    std::optional<Match> parse(const std::string& text, const std::string& rule = "") const {
        auto& S = detail::Session::get();
        RkValue m = S.call("rk-grammar-parse", {rk_int(S.c, id_), S.str(text), S.str(rule)});
        if (rk_type(S.c, m) == RK_ANY) return std::nullopt;
        return Match(rk_root(S.c, m));
    }

    Match parse_or_throw(const std::string& text, const std::string& rule = "") const {
        if (auto m = parse(text, rule)) return *m;
        auto& S = detail::Session::get();
        RkValue d = S.call("rk-grammar-diagnosis", {S.str(text)});
        if (rk_type(S.c, d) != RK_HASH) throw ParseError(label_ + ": no match");
        Tree t = S.tree_of(d);
        auto& m = t.map();
        ParseError err(label_ + ": no match — failed at line " +
                       std::to_string(m.at("line").int_()) + " column " +
                       std::to_string(m.at("col").int_()) + " while trying <" +
                       m.at("rule").str() + ">");
        err.pos = m.at("pos").int_();
        err.line = m.at("line").int_();
        err.col = m.at("col").int_();
        err.rule = m.at("rule").str();
        throw err;
    }

private:
    long long id_ = -1;
    std::string label_;
};

} // namespace rakupp
