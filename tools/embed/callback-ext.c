/* The A1 gate extension: native code calling back INTO Raku.
 *
 * Every ABI-2 addition gets exercised here — rk_call by name, rk_call_value on
 * a Code argument, rk_can, and both halves of the error contract: an exception
 * the extension handles, and one it lets through with its type intact. Built
 * and run by tools/embed-smoke.raku against t/regression/ext-callback.raku.
 *
 * Also the worked example of what an extension is FOR after A1: not just
 * computing faster, but computing faster while still deferring to Raku for the
 * cases it does not want to reimplement.
 */
#include <rakupp/rakupp_ext.h>

/* Freestanding: no libc, so this file needs nothing but the ABI header. */
static size_t strlen_(const char* s) { size_t n = 0; while (s && s[n]) n++; return n; }

/* Call a Raku sub by name: `twice(n)` from the loading scope. */
static RkValue call_named(RkCtx c) {
    RkValue arg = rk_arg(c, 0);
    RkValue r = rk_call(c, "twice", &arg, 1);
    if (!r) return 0;                     /* let the pending error re-raise */
    return rk_int(c, rk_int_get(c, r) + 1);
}

/* Call a Code value handed in as an argument — `&comparator` and its kind. */
static RkValue call_value(RkCtx c) {
    RkValue fn  = rk_arg(c, 0);
    RkValue arg = rk_arg(c, 1);
    RkValue r = rk_call_value(c, fn, &arg, 1);
    if (!r) return 0;
    return r;
}

/* Sum an array by calling a Raku callback per element: the shape a native
 * sort/map/reduce takes, and the one that would have been impossible at ABI 1. */
static RkValue map_sum(RkCtx c) {
    RkValue fn = rk_arg(c, 0), arr = rk_arg(c, 1);
    long long total = 0;
    size_t n = rk_elems(c, arr), i;
    for (i = 0; i < n; i++) {
        RkValue e = rk_at_pos(c, arr, i);
        RkValue r = rk_call_value(c, fn, &e, 1);
        if (!r) return 0;
        total += rk_int_get(c, r);
    }
    return rk_int(c, total);
}

static RkValue can_see(RkCtx c) {
    size_t len = 0;
    const char* name = rk_str_get(c, rk_arg(c, 0), &len);
    return rk_bool(c, rk_can(c, name));
}

/* An exception the extension HANDLES: rk_call fails, rk_error has the message,
 * rk_clear_error puts the context back in the clear, and the sub returns a
 * value as though nothing had happened. */
static RkValue catches(RkCtx c) {
    RkValue r = rk_call(c, "explode", 0, 0);
    if (!r) {
        const char* msg = rk_error(c);
        RkValue out = rk_str(c, msg ? msg : "(no message)", msg ? strlen_(msg) : 12);
        rk_clear_error(c);
        return out;
    }
    return rk_str(c, "no failure", 10);
}

/* An exception the extension lets THROUGH: return NULL without clearing, and
 * the original Raku exception resumes at the call site with its own type. */
static RkValue propagates(RkCtx c) {
    RkValue r = rk_call(c, "explode", 0, 0);
    if (!r) return 0;
    return r;
}

/* Calling something that is not there at all. */
static RkValue calls_missing(RkCtx c) {
    RkValue r = rk_call(c, "no-such-routine-anywhere", 0, 0);
    if (!r) {
        const char* msg = rk_error(c);
        RkValue out = rk_str(c, msg ? msg : "", msg ? strlen_(msg) : 0);
        rk_clear_error(c);
        return out;
    }
    return rk_str(c, "unexpectedly found it", 21);
}

/* rk_root: a value that outlives its call. Rooted here, read on the next call,
 * released on the one after — which an arena handle could not survive. The
 * host-side lifetime, proven from the extension side because that is where a
 * test can reach it. */
static RkValue g_kept = 0;

static RkValue root_keep(RkCtx c) {
    if (g_kept) rk_unroot(c, g_kept);
    g_kept = rk_root(c, rk_arg(c, 0));
    return rk_bool(c, 1);
}
static RkValue root_read(RkCtx c) {
    if (!g_kept) return rk_any(c);
    /* The rooted handle works with every ordinary accessor, one call later. */
    size_t len = 0;
    const char* s = rk_str_get(c, g_kept, &len);
    return rk_str(c, s, len);
}
static RkValue root_release(RkCtx c) {
    rk_unroot(c, g_kept);
    g_kept = 0;
    rk_unroot(c, 0);                       /* NULL is a no-op */
    rk_unroot(c, rk_int(c, 7));            /* an arena handle is refused, not freed */
    return rk_bool(c, 1);
}

static const RkSubDef subs[] = {
    {"ext-call-named",   call_named},
    {"ext-call-value",   call_value},
    {"ext-map-sum",      map_sum},
    {"ext-can",          can_see},
    {"ext-catches",      catches},
    {"ext-propagates",   propagates},
    {"ext-calls-missing", calls_missing},
    {"ext-root-keep",    root_keep},
    {"ext-root-read",    root_read},
    {"ext-root-release", root_release},
    {0, 0}
};
static const RkModule mod = { RAKUPP_EXT_ABI, "Rakupp::CallbackTest", subs };

RAKUPP_EXT_EXPORT const RkModule* rakupp_ext_init(unsigned host_abi) {
    /* `>=`, not `==`: this needs rk_call, so it needs a host at least this new
     * — and says so, instead of refusing every future host as well. */
    return host_abi >= RAKUPP_EXT_ABI ? &mod : 0;
}
