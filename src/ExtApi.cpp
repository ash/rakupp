// ExtApi.cpp — the host side of rakupp_ext.h: the C ABI native extension
// modules are compiled against, and the loader that installs their subs.
//
// The design constraint is in the header: an extension never sees `Value`, so
// its layout stays free to change (it changed twice the day this was written).
// Everything here is the translation layer that buys that freedom.
//
// Lifetime, concretely. `RkCtx` owns a std::deque<Value> arena; every handle is
// the address of an element in it. A deque is used rather than a vector because
// an extension may create thousands of handles and a vector would reallocate,
// invalidating every handle it had already handed out. The arena dies with the
// call, after the returned value has been copied out — so an extension frees
// nothing and cannot leak, and a handle saved across calls is dangling by
// construction rather than by accident.
#include "Interpreter.h"
#include "Platform.h" // dlopen/dlsym and their Win32 shims
#include "Value.h"
#include "rakupp_ext.h"

#include <deque>
#include <string>

namespace rakupp {

namespace {

struct ExtCtx {
    std::deque<Value> arena;
    ValueList* args = nullptr;
    std::string error;
    bool failed = false;

    RkValue make(Value v) {
        arena.push_back(std::move(v));
        return reinterpret_cast<RkValue>(&arena.back());
    }
};

inline ExtCtx* C(RkCtx c) { return reinterpret_cast<ExtCtx*>(c); }
inline Value*  V(RkValue v) { return reinterpret_cast<Value*>(v); }

// A borrowed string has to outlive the call but not the arena, so it is parked
// in the arena as a Str Value and its bytes borrowed from there.
const char* borrow(ExtCtx* x, std::string s, size_t* len) {
    x->arena.push_back(Value::str(std::move(s)));
    const std::string& kept = x->arena.back().s.str();
    if (len) *len = kept.size();
    return kept.c_str();
}

} // namespace

extern "C" {

RkValue rk_any(RkCtx c)                 { return C(c)->make(Value::any()); }
RkValue rk_bool(RkCtx c, int t)         { return C(c)->make(Value::boolean(t != 0)); }
RkValue rk_int(RkCtx c, long long v)    { return C(c)->make(Value::integer(v)); }
RkValue rk_int_s(RkCtx c, const char* d) {
    return C(c)->make(Value::bigint(BigInt::fromString(d ? d : "0")));
}
RkValue rk_num(RkCtx c, double v)       { return C(c)->make(Value::number(v)); }
RkValue rk_rat_s(RkCtx c, const char* n, const char* d) {
    return C(c)->make(Value::rat(BigInt::fromString(n ? n : "0"),
                                 BigInt::fromString(d ? d : "1")));
}
RkValue rk_str(RkCtx c, const char* s, size_t len) {
    return C(c)->make(Value::str(std::string(s ? s : "", s ? len : 0)));
}

RkValue rk_array(RkCtx c) { return C(c)->make(Value::array()); }
void rk_push(RkCtx, RkValue a, RkValue v) {
    Value* av = V(a);
    if (av && av->arr && v) av->arr->push_back(*V(v));
}
void rk_list(RkCtx, RkValue a) { if (Value* av = V(a)) av->isList = true; }

RkValue rk_hash(RkCtx c) { return C(c)->make(Value::makeHash()); }
void rk_set(RkCtx, RkValue h, const char* k, size_t klen, RkValue v) {
    Value* hv = V(h);
    if (hv && hv->hash && v) (*hv->hash)[std::string(k ? k : "", k ? klen : 0)] = *V(v);
}
void rk_map(RkCtx, RkValue h) { if (Value* hv = V(h)) hv->hashKind = "Map"; }

RkType rk_type(RkCtx, RkValue v) {
    Value* x = V(v);
    if (!x) return RK_ANY;
    switch (x->t) {
        case VT::Nil: case VT::Any: case VT::Type: return RK_ANY;
        case VT::Bool:  return RK_BOOL;
        case VT::Int:   return RK_INT;
        case VT::Num:   return RK_NUM;
        case VT::Rat:   return RK_RAT;
        case VT::Str:   return RK_STR;
        case VT::Array: return RK_ARRAY;
        case VT::Hash:  return RK_HASH;
        default:        return RK_OTHER;
    }
}
int rk_truthy(RkCtx, RkValue v)          { Value* x = V(v); return x && x->truthy() ? 1 : 0; }
long long rk_int_get(RkCtx, RkValue v)   { Value* x = V(v); return x ? x->toInt() : 0; }
double rk_num_get(RkCtx, RkValue v)      { Value* x = V(v); return x ? x->toNum() : 0.0; }
const char* rk_str_get(RkCtx c, RkValue v, size_t* len) {
    Value* x = V(v);
    if (!x) { if (len) *len = 0; return ""; }
    return borrow(C(c), x->toStr(), len);
}

size_t rk_elems(RkCtx, RkValue v) {
    Value* x = V(v);
    if (!x) return 0;
    if (x->t == VT::Array) return x->arr ? x->arr->size() : 0;
    if (x->t == VT::Hash)  return x->hash ? x->hash->size() : 0;
    return 0;
}
RkValue rk_at_pos(RkCtx c, RkValue a, size_t i) {
    Value* x = V(a);
    if (!x || !x->arr || i >= x->arr->size()) return nullptr;
    return reinterpret_cast<RkValue>(&(*x->arr)[i]);
}
// Index-based hash access is O(i) on an ordered map, so iterating a hash this
// way is quadratic. Acceptable at ABI v1 because it keeps the C surface tiny and
// serializers dominate on the string building; a cursor goes in when a real
// extension needs one.
const char* rk_key_at(RkCtx c, RkValue h, size_t i, size_t* klen) {
    Value* x = V(h);
    if (!x || !x->hash || i >= x->hash->size()) return nullptr;
    auto it = x->hash->begin();
    std::advance(it, (long)i);
    return borrow(C(c), it->first, klen);
}
RkValue rk_val_at(RkCtx, RkValue h, size_t i) {
    Value* x = V(h);
    if (!x || !x->hash || i >= x->hash->size()) return nullptr;
    auto it = x->hash->begin();
    std::advance(it, (long)i);
    return reinterpret_cast<RkValue>(&it->second);
}

size_t rk_argc(RkCtx c) {
    ExtCtx* x = C(c);
    if (!x->args) return 0;
    size_t n = 0;
    for (auto& a : *x->args) if (!(a.t == VT::Pair && a.namedArg)) n++;
    return n;
}
RkValue rk_arg(RkCtx c, size_t i) {
    ExtCtx* x = C(c);
    if (!x->args) return nullptr;
    size_t n = 0;
    for (auto& a : *x->args) {
        if (a.t == VT::Pair && a.namedArg) continue;
        if (n++ == i) return reinterpret_cast<RkValue>(&a);
    }
    return nullptr;
}
RkValue rk_named(RkCtx c, const char* name) {
    ExtCtx* x = C(c);
    if (!x->args || !name) return nullptr;
    for (auto& a : *x->args)
        if (a.t == VT::Pair && a.namedArg && a.s == name)
            return a.pairVal ? reinterpret_cast<RkValue>(a.pairVal.get()) : nullptr;
    return nullptr;
}

void rk_die(RkCtx c, const char* msg) {
    ExtCtx* x = C(c);
    if (!x->failed) { x->failed = true; x->error = msg ? msg : "extension error"; }
}

} // extern "C"

// ---- loading -------------------------------------------------------------

// Handles stay open for the process's life. An extension's code is live as long
// as any closure over it can be called, and there is no point at which we can
// prove that is over — so this deliberately never dlcloses.
Value extLoadModule(const std::string& path, std::string& errOut,
                    std::vector<std::pair<std::string, Value>>& subsOut) {
    void* h = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!h) { errOut = "cannot load extension '" + path + "'"; return Value::any(); }
    auto init = (RkInitFn)dlsym(h, "rakupp_ext_init");
    if (!init) {
        errOut = "'" + path + "' is not a rakupp extension (no rakupp_ext_init)";
        return Value::any();
    }
    const RkModule* m = init(RAKUPP_EXT_ABI);
    if (!m) {
        errOut = "'" + path + "' was built for a different extension ABI (host is " +
                 std::to_string(RAKUPP_EXT_ABI) + ")";
        return Value::any();
    }
    if (m->abi_version != RAKUPP_EXT_ABI) {
        errOut = "'" + path + "' reports ABI " + std::to_string(m->abi_version) +
                 ", host is " + std::to_string(RAKUPP_EXT_ABI);
        return Value::any();
    }
    for (const RkSubDef* d = m->subs; d && d->name; d++) {
        RkSubFn fn = d->fn;
        std::string nm = d->name;
        // Each sub becomes an ordinary Code value; from Raku it is
        // indistinguishable from any other sub.
        Value code = Value::closure([fn, nm](ValueList& a) -> Value {
            ExtCtx ctx;
            ctx.args = &a;
            RkValue r = fn(reinterpret_cast<RkCtx>(&ctx));
            if (ctx.failed)
                throw RakuError{Value::typeObj("X::AdHoc"), ctx.error};
            // Copied out BEFORE the arena dies with this scope.
            return r ? *reinterpret_cast<Value*>(r) : Value::any();
        });
        subsOut.emplace_back(nm, code);
    }
    return Value::boolean(true);
}

} // namespace rakupp
