/* The A2 gate: a HOST embedding Raku++, written in plain C.
 *
 * C rather than C++ on purpose. Every language binding this ABI is meant to
 * carry — ctypes, bun:ffi, Deno.dlopen, koffi, libloading, purego, DllImport —
 * reaches it through a C declaration and nothing else, so if this file needs a
 * C++ feature to be usable, the header is wrong. Built and run by
 * tools/embed-smoke.raku.
 *
 * It exercises what a real embedder actually does: evaluate, keep state between
 * evaluations, get typed values out, survive an error, capture output instead
 * of sharing the process's stdout, feed stdin, call a Raku routine directly,
 * and hold onto a value across evaluations.
 */
#include <rakupp/rakupp.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char* what) {
    if (ok) printf("  ok - %s\n", what);
    else  { printf("  NOT OK - %s\n", what); failures++; }
}

/* Output capture: everything Raku prints lands here instead of on stdout. */
static char captured[4096];
static size_t captured_len = 0;
static int    saw_stderr = 0;

/* A host function for the rk_register check: adds its two args plus the
 * userdata's int — arriving on the same RkCtx vocabulary an extension uses. */
static RkValue host_add(RkCtx c, void* userdata) {
    long long a = rk_int_get(c, rk_arg(c, 0));
    long long b = rk_int_get(c, rk_arg(c, 1));
    return rk_int(c, a + b + *(int*)userdata);
}

static void on_output(void* ud, const char* text, size_t len, int is_err) {
    (void)ud;
    if (is_err) saw_stderr = 1;
    if (captured_len + len < sizeof captured) {
        memcpy(captured + captured_len, text, len);
        captured_len += len;
        captured[captured_len] = 0;
    }
}

int main(void) {
    RkInterp rk = rk_new(0);            /* 0 = default config */
    if (!rk) { fprintf(stderr, "rk_new failed\n"); return 1; }
    printf("embed host: rakupp %s\n", rk_version());

    RkCtx c = rk_ctx(rk);
    RkValue v;

    /* --- evaluate, and get a typed value back --------------------------- */
    check(rk_eval(rk, "(1..10).grep(*.is-prime).sum", &v) == RK_OK &&
          rk_int_get(c, v) == 17, "rk_eval returns a value");

    check(rk_eval(rk, "'abc'.uc", &v) == RK_OK &&
          rk_type(c, v) == RK_STR &&
          strcmp(rk_str_get(c, v, 0), "ABC") == 0, "a Str comes back as a Str");

    check(rk_eval(rk, "[1, 2, 3]", &v) == RK_OK &&
          rk_type(c, v) == RK_ARRAY && rk_elems(c, v) == 3 &&
          rk_int_get(c, rk_at_pos(c, v, 1)) == 2, "an Array is walkable");

    check(rk_eval(rk, "{ a => 1, b => 2 }", &v) == RK_OK &&
          rk_type(c, v) == RK_HASH && rk_elems(c, v) == 2 &&
          strcmp(rk_key_at(c, v, 0, 0), "a") == 0, "a Hash is walkable");

    /* Values the engine carries on a plain representation without BEING it —
     * a Date is a hash of year/month/day inside, an enum value an Int, a Buf
     * a Str, an allomorph an Int whose Str is its source text — are RK_OTHER
     * to the ABI, and stringify. JSON::Native's C writer once serialised a
     * Date as {"year":2026,"month":8,"day":10} because rk_type said RK_HASH.
     * A Map is the one tagged hash that walks as a hash. */
    check(rk_eval(rk, "Date.new(2026, 8, 10)", &v) == RK_OK &&
          rk_type(c, v) == RK_OTHER &&
          strcmp(rk_str_get(c, v, 0), "2026-08-10") == 0,
          "a Date is RK_OTHER and stringifies, not a hash of its fields");
    check(rk_eval(rk, "enum Colour <Red Green>", 0) == RK_OK &&
          rk_eval(rk, "Green", &v) == RK_OK &&
          rk_type(c, v) == RK_OTHER &&
          strcmp(rk_str_get(c, v, 0), "Green") == 0,
          "an enum value is RK_OTHER and stringifies to its key, not its ordinal");
    check(rk_eval(rk, "Buf.new(1, 2)", &v) == RK_OK && rk_type(c, v) == RK_OTHER,
          "a Buf is RK_OTHER, not the Str inside it");
    check(rk_eval(rk, "<42>", &v) == RK_OK && rk_type(c, v) == RK_OTHER,
          "an allomorph is RK_OTHER, not the Int inside it");
    check(rk_eval(rk, "Map.new('a', 1)", &v) == RK_OK &&
          rk_type(c, v) == RK_HASH && rk_elems(c, v) == 1 &&
          strcmp(rk_key_at(c, v, 0, 0), "a") == 0, "a Map walks as a hash");

    /* --- an interpreter is a SESSION, not a one-shot -------------------- */
    check(rk_eval(rk, "my $x = 41", 0) == RK_OK, "a declaration evaluates");
    check(rk_eval(rk, "$x + 1", &v) == RK_OK && rk_int_get(c, v) == 42,
          "state persists across evaluations");
    check(rk_eval(rk, "sub twice($n) { $n * 2 }", 0) == RK_OK,
          "a sub can be defined");

    /* --- calling a Raku routine from the host, via the extension ABI ----
     * The same rk_call an extension uses: one vocabulary, both directions. */
    {
        RkValue arg = rk_int(c, 21);
        RkValue r = rk_call(c, "twice", &arg, 1);
        check(r && rk_int_get(c, r) == 42, "rk_call reaches a Raku routine from the host");
    }
    check(rk_can(c, "twice") && !rk_can(c, "not-a-routine"), "rk_can answers from the host");

    /* A C caller gets the SAME arity enforcement a Raku caller gets. Without
     * it a wrong argument count is silently plausible rather than wrong:
     * `twice()` bound $n to Any and returned 0, and every language binding in
     * bindings/ inherited that. Both directions are rejected, and the failure
     * arrives the normal way — NULL plus a context error to read and clear. */
    {
        RkValue two[2] = { rk_int(c, 1), rk_int(c, 2) };
        check(rk_call(c, "twice", 0, 0) == 0 && rk_error(c) != 0,
              "rk_call with too few arguments fails instead of binding Any");
        rk_clear_error(c);
        check(rk_call(c, "twice", two, 2) == 0 && rk_error(c) != 0,
              "rk_call with too many arguments fails");
        rk_clear_error(c);
        /* …and the correct call still works, from the same context. */
        RkValue arg = rk_int(c, 21);
        RkValue r = rk_call(c, "twice", &arg, 1);
        check(r && rk_int_get(c, r) == 42, "the session survives a rejected call");
    }

    /* --- errors are status, never an exception through C ---------------- */
    check(rk_eval(rk, "die 'boom'", &v) == RK_ERROR &&
          rk_last_error(rk) && strstr(rk_last_error(rk), "boom"),
          "a Raku exception becomes an error, not a crash");
    check(rk_eval(rk, "this is not ( valid raku", &v) == RK_ERROR &&
          rk_last_error(rk) != 0, "a parse failure becomes an error");
    /* …and the interpreter is still usable afterwards. */
    check(rk_eval(rk, "$x", &v) == RK_OK && rk_int_get(c, v) == 41,
          "the session survives an error");
    check(rk_last_error(rk) == 0, "rk_last_error clears on success");
    /* `exit` is the sharpest case: a library must never end its host. It
     * arrives as an error naming the code; rk_run is where exit is honored. */
    check(rk_eval(rk, "exit 7", &v) == RK_ERROR &&
          rk_last_error(rk) && strstr(rk_last_error(rk), "exit(7)"),
          "exit in evaluated code is an error, not the host's death");

    /* --- rooted values outlive the evaluation that made them ------------ */
    {
        RkValue kept;
        rk_eval(rk, "'survives'", &kept);
        RkValue rooted = rk_root(c, kept);
        rk_eval(rk, "1 + 1", 0);        /* releases the arena `kept` lived in */
        check(strcmp(rk_str_get(c, rooted, 0), "survives") == 0,
              "a rooted value survives the next evaluation");
        rk_unroot(c, rooted);
    }

    /* --- output capture: the rdbuf swap, made API ----------------------- */
    rk_set_output(rk, on_output, 0);
    rk_eval(rk, "say 'captured line'; note 'to stderr'", 0);
    rk_set_output(rk, 0, 0);            /* hand the streams back */
    check(strstr(captured, "captured line") != 0, "stdout is captured");
    check(saw_stderr, "stderr is captured and marked");
    check(strstr(captured, "to stderr") != 0, "…with its text");

    /* --- stdin, so `get` does not block on a terminal ------------------- */
    rk_set_input(rk, "fed line\n", 9);
    check(rk_eval(rk, "get()", &v) == RK_OK &&
          strcmp(rk_str_get(c, v, 0), "fed line") == 0, "stdin can be fed");
    rk_set_input(rk, 0, 0);

    /* --- a host function, callable from Raku (rk_register, ABI 2) ------- */
    {
        static int offset = 1; /* userdata: proves it arrives */
        RkValue r2;
        rk_register(rk, "host-add", host_add, &offset);
        check(rk_eval(rk, "host-add(20, 21)", &r2) == RK_OK &&
              rk_int_get(c, r2) == 42, "a registered host function is a Raku sub");
        check(rk_eval(rk, "[+] (1..5).map({ host-add($_, 0) - 1 })", &r2) == RK_OK &&
              rk_int_get(c, r2) == 15, "…and composes like one");
    }

    /* --- the grammar shim ships inside the library (ABI 2) -------------- */
    {
        const char* shim = rk_grammar_shim();
        check(shim && strstr(shim, "rk-grammar-compile") && strstr(shim, "rk-match-walk"),
              "rk_grammar_shim hands back the grammar service");
        check(rk_eval(rk, shim, 0) == RK_OK, "the baked shim evaluates");
        RkValue sv = rk_call(c, "rk-shim-abi", 0, 0);
        check(sv && rk_int_get(c, sv) >= 1, "…and answers its ABI stamp");
    }

    /* --- one interpreter per process, refused rather than corrupting ---- */
    check(rk_new(0) == 0, "a second interpreter is refused while one is live");

    rk_free(rk);
    /* --- own_stack=1: the CLI's big-stack thread, per evaluation ---------
     * The regression that earned this block: the session's execution
     * registers are thread-local, and an evaluation on a fresh big-stack
     * thread once found no scope to declare into — `my $x = 1` died while
     * `2 + 2` passed. The context now rides across the hop (the engine's own
     * saveCtx/loadCtx handoff), and a thrown Raku error is caught ON the
     * worker instead of terminating the process at its entry function.
     * Doubles as the after-free check: this is a NEW interpreter. */
    {
        RkConfig cfg;
        memset(&cfg, 0, sizeof cfg);
        cfg.size = sizeof cfg;
        cfg.own_stack = 1;
        RkInterp big = rk_new(&cfg);
        check(big != 0, "a new interpreter can be created after the first is freed");
        if (big) {
            RkCtx bc = rk_ctx(big);
            RkValue bv;
            check(rk_eval(big, "my $x = 1", 0) == RK_OK,
                  "own_stack=1: a declaration evaluates on the big-stack thread");
            check(rk_eval(big, "sub f($n) { $n <= 0 ?? 0 !! 1 + f($n - 1) }; $x + f(3)", &bv) == RK_OK &&
                  rk_int_get(bc, bv) == 4,
                  "own_stack=1: state and subs persist across the thread hops");
            check(rk_eval(big, "die 'boom'", &bv) == RK_ERROR &&
                  rk_last_error(big) && strstr(rk_last_error(big), "boom"),
                  "own_stack=1: a die is an error on the worker, not a process abort");
            {
                RkValue arg = rk_int(bc, 21);
                RkValue r = rk_call(bc, "f", &arg, 1);
                check(r && rk_int_get(bc, r) == 21,
                      "own_stack=1: rk_call from the host thread still sees the session");
            }
            /* rk_run has PROGRAM semantics: on the hop thread the mainline's
             * `exit` must unwind into the exit code, not end this process. */
            {
                int code = -1;
                check(rk_run(big, "my $n = 40; exit $n + 2", "t.raku", &code) == RK_OK &&
                      code == 42,
                      "own_stack=1: rk_run honors exit as an exit code");
            }
            rk_free(big);
        }
    }

    if (failures) { printf("embed host: %d FAILED\n", failures); return 1; }
    printf("embed host: ok\n");
    return 0;
}
