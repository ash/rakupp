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
 * ================= ERRORS =================
 * Call rk_die() and return NULL. The interpreter raises a Raku exception at the
 * call site. Throwing a C++ exception across this boundary is undefined; the
 * host cannot catch what it did not compile.
 *
 * ================= WRITING ONE =================
 *   #include <rakupp/rakupp_ext.h>
 *   static RkValue my_answer(RkCtx c) { return rk_int(c, 42); }
 *   static const RkSubDef subs[] = { {"answer", my_answer}, {0, 0} };
 *   static const RkModule mod = { RAKUPP_EXT_ABI, "My::Ext", subs };
 *   RAKUPP_EXT_EXPORT const RkModule* rakupp_ext_init(unsigned host_abi) {
 *       return host_abi == RAKUPP_EXT_ABI ? &mod : 0;
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

/* Bumped whenever the meaning or order of anything below changes. The host
 * passes its own value to rakupp_ext_init; an extension that does not recognise
 * it must return NULL rather than guess, and the host then reports a clean
 * version mismatch instead of calling through a wrong-shaped table. */
#define RAKUPP_EXT_ABI 1u

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

/* ---- failing ---- */
/* Record the message and return NULL from the sub. The host raises it as a Raku
 * exception at the call site. Calling rk_die twice keeps the first message. */
RK_API void rk_die(RkCtx c, const char* message);

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
