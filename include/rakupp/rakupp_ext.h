/* rakupp_ext.h — the C ABI for native extension modules.
 *
 * The XS analogue: a Raku module ships C or C++ source, zef compiles it against
 * this header at install time, and the resulting shared library provides subs
 * the interpreter calls directly. The module then versions independently of the
 * compiler, which is the whole point — a native JSON parser should not have to
 * ride in rakupp's own source tree to be fast.
 *
 * ================= THE ONE RULE =================
 * An extension NEVER sees `Value`. Every Raku value crosses this boundary as an
 * opaque `RkValue` handle.
 *
 * That is not fastidiousness. `sizeof(Value)` went 392 -> 376 -> 344 in a single
 * afternoon of docs/dev/plans/REPRESENTATION-PLAN.md, and phase 1 intends ~204
 * next. Any ABI that exposed the struct would have to freeze that work, or
 * silently miscompile every extension built against an older header. Handles
 * mean the layout stays free to move.
 *
 * The corollary: this is plain C, with no C++ types, no exceptions crossing the
 * boundary, and no allocation the extension has to free. A binding from Rust or
 * Zig would need nothing beyond this file.
 *
 * ================= LIFETIME =================
 * Handles belong to the CALL, not to the extension. Everything a sub creates is
 * arena-allocated in its RkCtx and released when it returns; the one value it
 * returns is copied out first. So an extension never frees anything, never
 * refcounts, and cannot leak. Handles must not be stored across calls — a saved
 * RkValue is dangling the moment its call returns.
 *
 * rk_root() is the deliberate exception, and it exists for HOSTS embedding
 * rakupp rather than for extensions: it lifts a value out of the arena so it can
 * outlive the call, and it is the only thing in this file you can leak. An
 * extension almost certainly wants C state instead.
 *
 * ================= ERRORS =================
 * Call rk_die() and return NULL. The interpreter raises a Raku exception at the
 * call site. Throwing a C++ exception across this boundary is undefined; the
 * host cannot catch what it did not compile — which is also why a Raku
 * exception thrown inside rk_call() does not unwind through your frames but
 * comes back as a NULL and a pending error. See rk_call and rk_error.
 *
 * ================= WRITING ONE =================
 *   #include <rakupp/rakupp_ext.h>
 *   static RkValue my_answer(RkCtx c) { return rk_int(c, 42); }
 *   static const RkSubDef subs[] = { {"answer", my_answer}, {0, 0} };
 *   static const RkModule mod = { RAKUPP_EXT_ABI, "My::Ext", subs };
 *   RAKUPP_EXT_EXPORT const RkModule* rakupp_ext_init(unsigned host_abi) {
 *       return host_abi >= RAKUPP_EXT_ABI ? &mod : 0;
 *   }
 *
 * and from Raku:
 *
 *   use Rakupp::Ext;
 *   rakupp-ext-load($path-to-so);   # installs `answer` into this scope
 */
#ifndef RAKUPP_EXT_H
#define RAKUPP_EXT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped when this file gains capability an extension cannot detect any other
 * way, or when the meaning or order of anything below changes.
 *
 *   1  the original surface: construct, inspect, arguments, rk_die
 *   2  rk_call/rk_call_value/rk_can (re-entering Raku), rk_error, rk_root
 *
 * THE HANDSHAKE. The host calls rakupp_ext_init with its own ABI, and retries
 * downward if that returns NULL. So the `host_abi == RAKUPP_EXT_ABI` test that
 * ABI-1 extensions were written with keeps working — a host at 2 that gets NULL
 * from init(2) asks again with init(1) — while an extension built against this
 * header should use `>=`, which states what it actually needs: a host at least
 * this new. The module's own abi_version tells the host which of its promises
 * are in play; a value NEWER than the host's is refused, because the host
 * cannot provide what it has not got.
 *
 * Undefined symbols are the failure mode this avoids. An extension calling
 * rk_call on a host that predates it would resolve nothing and abort at the
 * first call under lazy binding — a version check turns that into a sentence. */
#define RAKUPP_EXT_ABI 2u

#if defined(_WIN32)
#define RAKUPP_EXT_EXPORT __declspec(dllexport)
#else
#define RAKUPP_EXT_EXPORT __attribute__((visibility("default")))
#endif

/* RK_API marks the host-side entry points, and matters only when rakupp itself
 * is being compiled: in a shared librakupp the rk_* surface is the ONLY thing
 * exported (everything else builds with hidden visibility), and on Windows it
 * is what places rk_* in the executable's export table so an extension can link
 * against the import library. For an extension including this header it expands
 * to nothing that changes a declaration's meaning. */
#ifndef RK_API
#  if defined(_WIN32)
#    if defined(RAKUPP_BUILDING)
#      define RK_API __declspec(dllexport)
#    else
#      define RK_API
#    endif
#  elif defined(__GNUC__)
#    define RK_API __attribute__((visibility("default")))
#  else
#    define RK_API
#  endif
#endif

/* Opaque. Never dereference, never sizeof, never store past the call. */
typedef struct RkValueOpaque* RkValue;
typedef struct RkCtxOpaque*   RkCtx;

/* rk_type() answers one of these. Deliberately coarse: it describes what an
 * extension can DO with a value, not Raku's type hierarchy. */
typedef enum {
    RK_ANY   = 0,  /* Any/Nil/a type object — JSON's null */
    RK_BOOL  = 1,
    RK_INT   = 2,
    RK_NUM   = 3,
    RK_RAT   = 4,
    RK_STR   = 5,
    RK_ARRAY = 6,
    RK_HASH  = 7,
    RK_OTHER = 8   /* something this ABI has no vocabulary for; stringify it */
} RkType;

/* ---- constructing ---- */
RK_API RkValue rk_any  (RkCtx c);
RK_API RkValue rk_bool (RkCtx c, int truthy);
RK_API RkValue rk_int  (RkCtx c, long long v);
/* Arbitrary precision: a decimal string, optionally signed. Raku's Int has no
 * width, and a JSON document may carry a 40-digit integer. */
RK_API RkValue rk_int_s(RkCtx c, const char* decimal);
RK_API RkValue rk_num  (RkCtx c, double v);
/* A Rat from decimal-string numerator and denominator, normalised by the host.
 * Strings rather than integers for the same reason as rk_int_s. */
RK_API RkValue rk_rat_s(RkCtx c, const char* numer, const char* denom);
/* UTF-8. `len` in bytes; the host copies, so the buffer need not outlive this. */
RK_API RkValue rk_str  (RkCtx c, const char* utf8, size_t len);

RK_API RkValue rk_array(RkCtx c);
RK_API void    rk_push (RkCtx c, RkValue array, RkValue v);
/* Mark an array as a List rather than an Array — Raku's immutable form. */
RK_API void    rk_list (RkCtx c, RkValue array);

RK_API RkValue rk_hash (RkCtx c);
RK_API void    rk_set  (RkCtx c, RkValue hash, const char* key, size_t keylen, RkValue v);
/* Mark a hash as a Map rather than a Hash — Raku's immutable form. */
RK_API void    rk_map  (RkCtx c, RkValue hash);

/* ---- inspecting (for extensions that consume Raku data) ---- */
RK_API RkType      rk_type   (RkCtx c, RkValue v);
RK_API int         rk_truthy (RkCtx c, RkValue v);
RK_API long long   rk_int_get(RkCtx c, RkValue v);
RK_API double      rk_num_get(RkCtx c, RkValue v);
/* Borrowed UTF-8, valid until the call returns. For a non-Str this is the
 * value's Str coercion, which is what a serializer wants. */
RK_API const char* rk_str_get(RkCtx c, RkValue v, size_t* len);

RK_API size_t  rk_elems (RkCtx c, RkValue v);   /* array or hash */
RK_API RkValue rk_at_pos(RkCtx c, RkValue array, size_t i);
/* Hash iteration by index, in the host's iteration order (which is key order).
 * `keylen` may be NULL. Returns NULL past the end. */
RK_API const char* rk_key_at(RkCtx c, RkValue hash, size_t i, size_t* keylen);
RK_API RkValue     rk_val_at(RkCtx c, RkValue hash, size_t i);

/* ---- arguments ---- */
RK_API size_t  rk_argc (RkCtx c);
RK_API RkValue rk_arg  (RkCtx c, size_t i);     /* positional; NULL if absent */
/* A named argument, or NULL when it was not passed — which is distinct from
 * being passed an undefined value. */
RK_API RkValue rk_named(RkCtx c, const char* name);

/* ---- calling back into Raku (ABI 2) ---- */
/* Call a Raku routine by name with `argc` positional arguments, and return what
 * it returned. `argv` may be NULL when `argc` is 0.
 *
 * The name resolves the way it would at your sub's own call site: the LEXICAL
 * scope that invoked you, then outward to GLOBAL. So an extension loaded by a
 * module reaches that module's subs, which is what makes a native fast path
 * able to delegate the cases it does not handle.
 *
 * ON FAILURE — no such routine, or the routine threw — this returns NULL and
 * leaves a PENDING ERROR. rk_error() has the message. If your sub then returns
 * NULL, the original Raku exception is raised at the call site with its type
 * intact, so `CATCH { when X::Whatever }` still works. To handle the failure
 * yourself instead, call rk_clear_error() and carry on.
 *
 * A Raku exception never unwinds through your frames: C++ unwinding into C is
 * undefined behaviour, so it is caught at this boundary and handed back as
 * status. That is the same reason rk_die exists rather than a throw.
 *
 * The argument COUNT is checked, exactly as it is for a call written in Raku:
 * too few or too many positionals fails with X::Signature::ArityMismatch
 * rather than binding the missing ones to Any. The check applies to named
 * plain subs, the only thing this entry point can reach by name; blocks and
 * lambdas reached through rk_call_value, and multis, keep their own binding
 * rules.
 *
 * Named arguments are not expressible yet — positional only. */
RK_API RkValue rk_call(RkCtx c, const char* name, const RkValue* argv, size_t argc);
/* The same, for a Code value you were handed rather than a name — a callback
 * argument, `&comparator` and its kind. Fails if `code` is not callable. */
RK_API RkValue rk_call_value(RkCtx c, RkValue code, const RkValue* argv, size_t argc);
/* Is a routine of this name visible from here? Lets an extension use an
 * optional Raku helper without provoking an error to find out. */
RK_API int rk_can(RkCtx c, const char* name);

/* ---- failing ---- */
/* Record the message and return NULL from the sub. The host raises it as a Raku
 * exception at the call site. Calling rk_die twice keeps the first message. */
RK_API void rk_die(RkCtx c, const char* message);
/* The pending error's message, or NULL if there is none. Borrowed, and valid
 * until the next call on this RkCtx. (ABI 2) */
RK_API const char* rk_error(RkCtx c);
/* Discard the pending error — you are handling it. Returning NULL afterwards
 * then means an ordinary "returned Nil", not a re-raise. (ABI 2) */
RK_API void rk_clear_error(RkCtx c);

/* ---- rooted handles: values that outlive the call (ABI 2) ----
 * FOR HOSTS, not for extensions. rk_root copies a value out of the call arena
 * into storage that survives, and hands back a handle usable with every
 * accessor above until rk_unroot releases it. This is the one place the ABI
 * lets you leak, which is exactly why the arena — where you cannot — stays the
 * default an extension sees.
 *
 * An extension that thinks it wants this usually wants ordinary C state: a
 * parsed schema, a compiled pattern, a cache. Keep those in C, not in handles.
 * Rooting is for an embedder holding a result between calls into Raku.
 *
 * Thread-safe: the root store is shared, and since v3.0.0 several rakupp
 * threads can be inside extension calls at once. rk_unroot on a handle that is
 * not rooted (an arena handle, or one already unrooted) does nothing. */
RK_API RkValue rk_root  (RkCtx c, RkValue v);
RK_API void    rk_unroot(RkCtx c, RkValue rooted);

/* ---- registration ---- */
typedef RkValue (*RkSubFn)(RkCtx c);
typedef struct { const char* name; RkSubFn fn; } RkSubDef;
typedef struct {
    unsigned        abi_version;  /* must be RAKUPP_EXT_ABI */
    const char*     module_name;  /* for diagnostics */
    const RkSubDef* subs;         /* terminated by a {NULL, NULL} entry */
} RkModule;

/* The one symbol the host looks up. Return NULL if `host_abi` is not one you
 * were built for. */
typedef const RkModule* (*RkInitFn)(unsigned host_abi);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RAKUPP_EXT_H */
