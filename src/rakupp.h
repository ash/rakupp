/* rakupp.h — the C ABI for EMBEDDING Raku++ in another program.
 *
 * The mirror of rakupp_ext.h. That header is native code called FROM Raku; this
 * one is Raku called FROM native code. Same boundary, opposite direction, and
 * deliberately ONE value vocabulary — which is why this file includes the other
 * rather than defining a second `RkValue`. Everything an extension can do with
 * a value, a host can do with the same functions.
 *
 * The include is one-way on purpose: an extension author never sees the
 * embedding surface, because rakupp_ext.h stands alone.
 *
 * ================= HELLO =================
 *   #include <rakupp/rakupp.h>
 *
 *   RkInterp rk = rk_new(0);                      // 0 = default config
 *   RkValue v;
 *   if (rk_eval(rk, "(1..10).grep(*.is-prime).sum", &v) == RK_OK)
 *       printf("%lld\n", rk_int_get(rk_ctx(rk), v));   // 17
 *   else
 *       fprintf(stderr, "raku: %s\n", rk_last_error(rk));
 *   rk_free(rk);
 *
 * ================= STATE =================
 * An interpreter is a SESSION, not a one-shot: `rk_eval(rk, "my $x = 41")` then
 * `rk_eval(rk, "$x + 1")` gives 42, exactly as the REPL behaves, because both
 * run in the same mainline scope.
 *
 * ================= LIFETIME =================
 * The same rule as the extension side, in the shape a host needs it: a value
 * from rk_eval is valid UNTIL THE NEXT rk_eval on that interpreter. Each
 * evaluation releases what the previous one produced.
 *
 * To keep one longer, root it — rk_root/rk_unroot in rakupp_ext.h, which exist
 * for exactly this. A rooted handle survives any number of evaluations and the
 * host frees it. That is the one place the ABI can leak, and it is opt-in.
 *
 * ================= ERRORS =================
 * Every entry point returns a status and never lets a C++ exception reach you:
 * unwinding C++ through your frames is undefined behaviour, not a bug report.
 * A failed call leaves the message in rk_last_error(rk).
 *
 * ================= THREADS =================
 * One interpreter, one thread, unless you serialise access yourself. Raku code
 * INSIDE the interpreter still uses as many threads as it likes — `start`,
 * `race`, `hyper` — and since v3.0.0 that is the default rather than opt-in. So
 * a host function reached from Raku can be entered on a thread you never
 * created, and by more than one at once. Make those re-entrant.
 *
 * ================= ONE PER PROCESS =================
 * rk_new returns NULL if an interpreter is already live in this process. The
 * interpreter wires up process-global state at construction (the NativeCall
 * callback target among it), so a second instance would quietly break the
 * first. Create and free sequentially and you will never see this; genuine
 * concurrent instances are a separate piece of work (EMBED-PLAN's E5), and
 * refusing loudly beats corrupting quietly in the meantime.
 */
#ifndef RAKUPP_H
#define RAKUPP_H

#include "rakupp_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped on a change to anything in THIS file. Independent of
 * RAKUPP_EXT_ABI: an addition here must never disturb an extension, which is
 * the promise the extension version negotiation was written for. */
#define RAKUPP_ABI 1u

typedef struct RkInterpOpaque* RkInterp;

/* Statuses. Anything non-zero leaves a message in rk_last_error(). */
#define RK_OK    0
#define RK_ERROR 1   /* the Raku code failed to parse, or threw */
#define RK_FATAL 2   /* the interpreter itself failed (out of memory, no interpreter) */

/* Three things the CLI does that a library must not do to its host uninvited.
 * All default OFF; set `size` to sizeof(RkConfig) so this struct can grow
 * without breaking a host compiled against an older header. */
typedef struct {
    unsigned size;
    /* Run Raku on a thread with a large stack, so deep recursion reaches the
     * interpreter's own guard rather than the native stack's end. The CLI does
     * this; a host may not want a thread it did not ask for, and the guard
     * measures the real stack either way, so this is safe to leave off. */
    int own_stack;
    /* Ignore SIGPIPE process-wide, so a Raku TCP server survives a client
     * hanging up. It is a PROCESS-wide disposition — this is your signal
     * handling, not ours, so you have to ask. */
    int handle_sigpipe;
    /* Flush std::cout/std::cerr after each evaluation. Off means the host owns
     * when its own streams flush. Irrelevant when rk_set_output is in use. */
    int own_stdout;
} RkConfig;

/* NULL config = every field above off, which is what most hosts want. */
RK_API RkInterp    rk_new (const RkConfig* cfg);
RK_API void        rk_free(RkInterp rk);
RK_API const char* rk_version(void);          /* the interpreter's version string */

/* The context for value operations on this interpreter's values — hand it to
 * rk_int_get, rk_at_pos, rk_call and everything else in rakupp_ext.h. This is
 * where the two directions meet: a host reaches Raku routines through exactly
 * the entry point an extension uses. */
RK_API RkCtx rk_ctx(RkInterp rk);

/* Evaluate Raku source in the interpreter's mainline scope and hand back the
 * value of the last statement. `out` may be NULL if you do not want it.
 * Returns RK_OK, or RK_ERROR with rk_last_error() set. */
RK_API int rk_eval(RkInterp rk, const char* src, RkValue* out);
/* The same, reading the program from a file — its path also becomes the
 * interpreter's idea of the running file, so `$?FILE` and relative `use lib`
 * resolve the way they would from the CLI. */
RK_API int rk_eval_file(RkInterp rk, const char* path, RkValue* out);

/* Run `src` as a whole PROGRAM rather than as an expression: MAIN dispatch,
 * phasers, `exit`, and a parse or runtime failure REPORTED to stderr in the
 * CLI's own words — where rk_eval would hand you the message and print
 * nothing. `exit_code` receives what the process would have returned (0 fine,
 * 1 a parse or runtime error, 3 internal). Either may be NULL.
 *
 * rk_eval is for embedding Raku as a scripting language, where the host wants
 * the value and decides what to do about a failure. This is for hosting Raku
 * AS Raku — a playground, a `raku`-alike, a test runner — where the program's
 * own output, including its error text, is the product.
 *
 * Returns RK_OK when the program ran (whatever its exit code), RK_ERROR only
 * if it could not be run at all. */
RK_API int rk_run(RkInterp rk, const char* src, const char* file_name, int* exit_code);

/* The last failure's message, or NULL if the last call succeeded. Borrowed,
 * and valid until the next call on this interpreter. */
RK_API const char* rk_last_error(RkInterp rk);

/* Capture everything Raku prints instead of sharing the host's stdout. `text`
 * is UTF-8 and NOT null-terminated — `len` is the length. `is_err` marks
 * stderr. Pass fn = NULL to restore the process's own streams.
 *
 * This replaces the std::cout rdbuf swap every embedder was otherwise going to
 * rediscover; Raku.js did exactly that by hand for two years. */
typedef void (*RkOutputFn)(void* userdata, const char* text, size_t len, int is_err);
RK_API void rk_set_output(RkInterp rk, RkOutputFn fn, void* userdata);

/* Feed the program's standard input, so `get`/`lines`/`$*IN.slurp` read `text`
 * and then see EOF rather than blocking on a terminal the host may not have.
 * Pass NULL to restore the process's own stdin. */
RK_API void rk_set_input(RkInterp rk, const char* text, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RAKUPP_H */
