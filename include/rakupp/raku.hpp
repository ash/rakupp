/* raku.hpp — run Raku from C++. Header-only, over rakupp.h.
 * Installed as <rakupp/raku.hpp>.
 *
 * The general half of the C++ binding: evaluate Raku source, call Raku subs
 * with C++ values, read the results back as C++ values. <rakupp/grammar.hpp>
 * is the other half — grammars and matches — and includes this one, so a
 * program that wants both needs only that include.
 *
 *   #include <rakupp/raku.hpp>
 *
 *   rakupp::eval("sub area($w, $h) { $w * $h }");
 *   rakupp::Tree a = rakupp::call("area", {3, 4});     // a.int_() == 12
 *   rakupp::Tree n = rakupp::eval("(2..^30).grep(*.is-prime).List");
 *   for (auto& p : n.list()) std::cout << p.int_() << "\n";
 *
 * Values cross as `Tree`, the same variant the grammar side returns: null,
 * bool, integer, number, string, list, map. It is both the argument type and
 * the result type, so what you send and what you get back read alike.
 *
 * One interpreter per process, created on first use; one thread may talk to
 * it at a time (Raku code inside it threads as it pleases). A Raku die
 * crosses as RakuError.
 */
#pragma once

#include "rakupp.h"

#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace rakupp {

struct RakuError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/* A value on either side of the boundary: null | bool | int | num | string |
 * list | map. As a RESULT it is what a Raku value converts to (a match node
 * with no captures converts to its text; named captures become map keys,
 * positional ones the keys "0", "1", …). As an ARGUMENT it is what the host
 * sends: rakupp::call("f", {1, "two", 3.0}) builds Int, Str, Num.
 *
 * A Raku integer wider than 64 bits arrives as a string of digits, because
 * that is the only lossless thing a `long long` can be handed. */
struct Tree {
    std::variant<std::nullptr_t, bool, long long, double, std::string,
                 std::vector<Tree>, std::map<std::string, Tree>> v = nullptr;

    Tree() = default;
    Tree(std::nullptr_t) {}
    Tree(bool b)                          : v(b) {}
    Tree(int i)                           : v((long long)i) {}
    Tree(long long i)                     : v(i) {}
    Tree(double d)                        : v(d) {}
    Tree(const char* s)                   : v(std::string(s)) {}
    Tree(std::string s)                   : v(std::move(s)) {}
    Tree(std::vector<Tree> l)             : v(std::move(l)) {}
    Tree(std::map<std::string, Tree> m)   : v(std::move(m)) {}

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(v); }
    bool is_list() const { return std::holds_alternative<std::vector<Tree>>(v); }
    bool is_map()  const { return std::holds_alternative<std::map<std::string, Tree>>(v); }
    const std::string& str() const { return std::get<std::string>(v); }
    long long int_() const { return std::get<long long>(v); }
    double    num()  const { return std::get<double>(v); }
    bool      boolean() const { return std::get<bool>(v); }
    const std::vector<Tree>& list() const { return std::get<std::vector<Tree>>(v); }
    const std::map<std::string, Tree>& map() const { return std::get<std::map<std::string, Tree>>(v); }
};

namespace detail {

struct Session {
    RkInterp rk = nullptr;
    RkCtx    c  = nullptr;

    static Session& get() {
        static Session s;
        return s;
    }

    Session() {
        rk = rk_new(nullptr);
        if (!rk) throw RakuError("rk_new refused: an interpreter is already live in this process");
        c = rk_ctx(rk);
        if (rk_eval(rk, rk_grammar_shim(), nullptr) != RK_OK)
            throw RakuError(std::string("grammar shim failed to load: ") +
                            (rk_last_error(rk) ? rk_last_error(rk) : "?"));
    }
    ~Session() {
        if (rk) rk_free(rk);
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    RkValue str(const std::string& s) { return rk_str(c, s.c_str(), s.size()); }

    /* rk_call with the error contract: NULL return = a Raku die, rethrown. */
    RkValue call(const char* name, std::vector<RkValue> args) {
        RkValue r = rk_call(c, name, args.empty() ? nullptr : args.data(), args.size());
        if (!r) {
            const char* e = rk_error(c);
            std::string msg = e ? e : (std::string(name) + " failed");
            rk_clear_error(c);
            throw RakuError(msg);
        }
        return r;
    }

    /* C++ -> engine. The result is UNROOTED: valid until the next eval or
     * call, which is long enough to pass it as an argument and no longer. */
    RkValue value_of(const Tree& t) {
        if (t.is_null()) return rk_any(c);
        if (auto* b = std::get_if<bool>(&t.v))        return rk_bool(c, *b ? 1 : 0);
        if (auto* i = std::get_if<long long>(&t.v))   return rk_int(c, *i);
        if (auto* d = std::get_if<double>(&t.v))      return rk_num(c, *d);
        if (auto* s = std::get_if<std::string>(&t.v)) return rk_str(c, s->c_str(), s->size());
        if (auto* l = std::get_if<std::vector<Tree>>(&t.v)) {
            RkValue a = rk_array(c);
            for (auto& item : *l) rk_push(c, a, value_of(item));
            return a;
        }
        auto& m = std::get<std::map<std::string, Tree>>(t.v);
        RkValue h = rk_hash(c);
        for (auto& kv : m) rk_set(c, h, kv.first.c_str(), kv.first.size(), value_of(kv.second));
        return h;
    }

    /* engine -> C++ */
    Tree tree_of(RkValue v) {
        switch (rk_type(c, v)) {
            case RK_ANY:  return {};
            case RK_BOOL: return {rk_truthy(c, v) != 0};
            case RK_INT:  return {rk_int_get(c, v)};
            case RK_NUM:
            case RK_RAT:  return {rk_num_get(c, v)};
            case RK_ARRAY: {
                std::vector<Tree> out;
                size_t n = rk_elems(c, v);
                out.reserve(n);
                for (size_t i = 0; i < n; i++) out.push_back(tree_of(rk_at_pos(c, v, i)));
                return {std::move(out)};
            }
            case RK_HASH: {
                std::map<std::string, Tree> out;
                size_t n = rk_elems(c, v);
                for (size_t i = 0; i < n; i++) {
                    size_t kl = 0;
                    const char* k = rk_key_at(c, v, i, &kl);
                    out.emplace(std::string(k, kl), tree_of(rk_val_at(c, v, i)));
                }
                return {std::move(out)};
            }
            default: { /* RK_STR and RK_OTHER stringify */
                size_t n = 0;
                const char* s = rk_str_get(c, v, &n);
                return {std::string(s ? s : "", n)};
            }
        }
    }
};

} // namespace detail

/* Evaluate Raku source in the interpreter's mainline scope and return the
 * last statement's value. State PERSISTS across calls, exactly as in the
 * REPL: eval a `sub` here and call() finds it below. */
inline Tree eval(const std::string& source) {
    auto& S = detail::Session::get();
    RkValue out = nullptr;
    if (rk_eval(S.rk, source.c_str(), &out) != RK_OK)
        throw RakuError(rk_last_error(S.rk) ? rk_last_error(S.rk) : "rk_eval failed");
    return S.tree_of(out);
}

/* Call a Raku routine by name with C++ arguments:
 *     rakupp::call("area", {3, 4})
 * The routine must be visible in the mainline scope — declared by an earlier
 * eval, or by a file you evaluated. A die inside it throws RakuError. */
inline Tree call(const std::string& name, const std::vector<Tree>& args = {}) {
    auto& S = detail::Session::get();
    std::vector<RkValue> argv;
    argv.reserve(args.size());
    for (auto& a : args) argv.push_back(S.value_of(a));
    return S.tree_of(S.call(name.c_str(), std::move(argv)));
}

/* Does the mainline scope have a routine of this name? */
inline bool can(const std::string& name) {
    return rk_can(detail::Session::get().c, name.c_str()) != 0;
}

/* The engine's version string, e.g. "3.14.0". */
inline std::string version() { return rk_version() ? rk_version() : ""; }

} // namespace rakupp
