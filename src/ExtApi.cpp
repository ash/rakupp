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
#include "ExtCtx.h"   // the arena and handle casts, shared with EmbedApi.cpp
#include "Interpreter.h"
#include "Platform.h" // dlopen/dlsym and their Win32 shims
#include "Value.h"
#include "rakupp_ext.h"

#include <mutex>
#include <string>
#include <unordered_set>

namespace rakupp {

namespace {

// Rooted values (ABI 2). Heap slots rather than arena elements, because the
// point is to outlive the arena; the set is what makes rk_unroot able to
// refuse a handle it never issued, and a mutex because since v3.0.0 several
// rakupp threads can be inside extension calls at once.
std::mutex                 g_rootMu;
std::unordered_set<Value*> g_roots;

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
    if (av && av->arr() && v) av->arr()->push_back(*V(v));
}
void rk_list(RkCtx, RkValue a) { if (Value* av = V(a)) av->isList = true; }

RkValue rk_hash(RkCtx c) { return C(c)->make(Value::makeHash()); }
void rk_set(RkCtx c, RkValue h, const char* k, size_t klen, RkValue v) {
    Value* hv = V(h);
    if (!hv || !hv->hash() || !v) return;
    (*hv->hash())[std::string(k ? k : "", k ? klen : 0)] = *V(v);
    // A new key ahead of the remembered position shifts every index after it,
    // so the memo has to go. Building a hash and then walking it is ordinary;
    // interleaving the two is not, and correctness beats the fast path here.
    if (C(c)->memoHash == hv->hash()) C(c)->memoHash = nullptr;
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
    if (x->t == VT::Array) return x->arr() ? x->arr()->size() : 0;
    if (x->t == VT::Hash)  return x->hash() ? x->hash()->size() : 0;
    return 0;
}
RkValue rk_at_pos(RkCtx c, RkValue a, size_t i) {
    Value* x = V(a);
    if (!x || !x->arr() || i >= x->arr()->size()) return nullptr;
    return reinterpret_cast<RkValue>(&(*x->arr())[i]);
}
// Index-based access keeps the C surface tiny — no cursor type, no iterator
// lifetime for an extension to get wrong. The cost it used to carry, an
// std::advance from begin() on every call, is paid by ExtCtx::at's remembered
// position instead: sequential walks, which is what every serializer does, now
// cost one ++ per element.
const char* rk_key_at(RkCtx c, RkValue h, size_t i, size_t* klen) {
    Value* x = V(h);
    if (!x || !x->hash()) return nullptr;
    auto it = C(c)->at(x->hash(), i);
    if (it == x->hash()->end()) return nullptr;
    return borrow(C(c), it->first, klen);
}
RkValue rk_val_at(RkCtx c, RkValue h, size_t i) {
    Value* x = V(h);
    if (!x || !x->hash()) return nullptr;
    auto it = C(c)->at(x->hash(), i);
    if (it == x->hash()->end()) return nullptr;
    return reinterpret_cast<RkValue>(const_cast<Value*>(&it->second));
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
            return a.pairVal() ? reinterpret_cast<RkValue>(a.pairVal()) : nullptr;
    return nullptr;
}

void rk_die(RkCtx c, const char* msg) {
    ExtCtx* x = C(c);
    if (!x->failed) { x->failed = true; x->error = msg ? msg : "extension error"; }
}

const char* rk_error(RkCtx c) {
    ExtCtx* x = C(c);
    if (x->hasPending) return x->pending.message.c_str();
    return x->failed ? x->error.c_str() : nullptr;
}

void rk_clear_error(RkCtx c) {
    ExtCtx* x = C(c);
    x->hasPending = false;
    x->pending = RakuError{Value::any(), ""};
    x->failed = false;
    x->error.clear();
}

// ---- calling back into Raku ----

namespace {

// The one place a C++ exception could reach an extension's frames, so it is the
// one place that must catch everything. A Raku exception is kept whole for
// re-raising; anything else is flattened to a message, because a std::bad_alloc
// re-raised as itself would unwind straight past the interpreter's handlers.
RkValue extCall(ExtCtx* x, const Value& code, const RkValue* argv, size_t argc) {
    ValueList args;
    args.reserve(argc);
    for (size_t i = 0; i < argc; i++) {
        Value* a = argv ? V(argv[i]) : nullptr;
        args.push_back(a ? *a : Value::any());
    }
    try {
        // arityCheck: a C caller gets the SAME arity enforcement a Raku caller
        // gets. Without it a wrong argument count is silently plausible rather
        // than wrong — sub area($w, $h) called with one argument binds $h to
        // Any and returns 0, and every language binding inherits that. The
        // check is the one in callCallableRaw, so it applies to named plain
        // user subs only: blocks and lambdas handed to rk_call_value, methods,
        // multis and builtins keep their lax binding exactly as before.
        return x->make(x->interp->callCallable(code, std::move(args), nullptr,
                                               /*ownFrame=*/false, /*arityCheck=*/true));
    }
    catch (RakuError& e) {
        x->hasPending = true;
        x->pending = e;
        return nullptr;
    }
    catch (const std::exception& e) {
        x->hasPending = true;
        x->pending = RakuError{Value::typeObj("X::AdHoc"), e.what()};
        return nullptr;
    }
    catch (...) {
        // Control-flow signals (ExitEx, NextEx, a worker abort) reach here too.
        // An extension frame is not a place any of them can legally resume, so
        // they become an error rather than crossing C.
        x->hasPending = true;
        x->pending = RakuError{Value::typeObj("X::AdHoc"),
                               "control flow escaped a call made from a native extension"};
        return nullptr;
    }
}

// A routine visible from the extension's own call site: the lexical scope that
// invoked it, then outward to GLOBAL — the same walk the Raku source would do.
Value* extLookup(ExtCtx* x, const char* name) {
    if (!x->interp || !name) return nullptr;
    return x->interp->extFindRoutine(std::string("&") + name);
}

} // namespace

RkValue rk_call(RkCtx c, const char* name, const RkValue* argv, size_t argc) {
    ExtCtx* x = C(c);
    Value* fn = extLookup(x, name);
    if (!fn) {
        x->hasPending = true;
        x->pending = RakuError{Value::typeObj("X::Undeclared::Symbols"),
                               std::string("Undeclared routine '") + (name ? name : "") +
                               "' called from a native extension"};
        return nullptr;
    }
    return extCall(x, *fn, argv, argc);
}

RkValue rk_call_value(RkCtx c, RkValue code, const RkValue* argv, size_t argc) {
    ExtCtx* x = C(c);
    Value* f = V(code);
    if (!f || f->t != VT::Code) {
        x->hasPending = true;
        x->pending = RakuError{Value::typeObj("X::AdHoc"),
                               "rk_call_value was given something that is not callable"};
        return nullptr;
    }
    return extCall(x, *f, argv, argc);
}

int rk_can(RkCtx c, const char* name) { return extLookup(C(c), name) ? 1 : 0; }

// ---- rooted handles ----

RkValue rk_root(RkCtx, RkValue v) {
    Value* src = V(v);
    Value* slot = new Value(src ? *src : Value::any());
    std::lock_guard<std::mutex> lk(g_rootMu);
    g_roots.insert(slot);
    return reinterpret_cast<RkValue>(slot);
}

void rk_unroot(RkCtx, RkValue rooted) {
    Value* slot = V(rooted);
    if (!slot) return;
    {
        std::lock_guard<std::mutex> lk(g_rootMu);
        // An arena handle, or one already unrooted, is not ours to delete —
        // and in C the difference is invisible without this check.
        if (!g_roots.erase(slot)) return;
    }
    delete slot;
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
    // The downgrade handshake (rakupp_ext.h). ABI-1 extensions were written as
    // `host_abi == RAKUPP_EXT_ABI`, so a host at 2 asking with 2 gets NULL from
    // every one of them; asking again with 1 is what keeps them loading. An
    // extension built against this header answers `>=` and takes the first ask.
    const RkModule* m = nullptr;
    for (unsigned probe = RAKUPP_EXT_ABI; probe >= 1u && !m; probe--) m = init(probe);
    if (!m) {
        errOut = "'" + path + "' was built for a different extension ABI (host is " +
                 std::to_string(RAKUPP_EXT_ABI) + ")";
        return Value::any();
    }
    // Older is fine — it can only use what already existed. Newer is not: it
    // was built against entry points this host does not have.
    if (m->abi_version > RAKUPP_EXT_ABI) {
        errOut = "'" + path + "' reports ABI " + std::to_string(m->abi_version) +
                 ", host is " + std::to_string(RAKUPP_EXT_ABI) + " (rebuild it, or upgrade rakupp)";
        return Value::any();
    }
    for (const RkSubDef* d = m->subs; d && d->name; d++) {
        RkSubFn fn = d->fn;
        std::string nm = d->name;
        // Each sub becomes an ordinary Code value; from Raku it is
        // indistinguishable from any other sub. Built as a Callable directly
        // rather than through Value::closure, which drops the Interpreter& the
        // wrapper is handed — and that reference is precisely what rk_call
        // needs to get back INTO Raku.
        Value code;
        code.t = VT::Code;
        code.setCode(std::make_shared<Callable>());
        code.code()->name = nm;
        code.code()->builtin = [fn, nm](Interpreter& I, ValueList& a) -> Value {
            ExtCtx ctx;
            ctx.args = &a;
            ctx.interp = &I;
            RkValue r = fn(reinterpret_cast<RkCtx>(&ctx));
            if (ctx.failed)
                throw RakuError{Value::typeObj("X::AdHoc"), ctx.error};
            // A failed rk_call the extension neither handled nor cleared: it
            // returned NULL, so the exception it swallowed resumes here, with
            // its own type, as though C had never been in the way.
            if (!r && ctx.hasPending)
                throw ctx.pending;
            // Copied out BEFORE the arena dies with this scope.
            return r ? *reinterpret_cast<Value*>(r) : Value::any();
        };
        subsOut.emplace_back(nm, code);
    }
    return Value::boolean(true);
}

} // namespace rakupp
