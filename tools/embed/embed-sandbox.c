/* RkConfig.sandbox: a host that runs Raku it did not write.
 *
 * The host sets a secret in its own environment, then creates a sandboxed
 * interpreter (docs/guide/SANDBOX.md). The Raku code it evaluates must not see
 * the variable, must be refused a file and a process with
 * X::SecurityPolicy::Sandbox, and must still compute; the HOST must keep its
 * own access, because the sandbox confines the Raku code, not the process.
 * Plain C, as embed-host.c is. Built and run by tools/embed-smoke.raku.
 */
#define _POSIX_C_SOURCE 200112L   /* setenv */
#include <rakupp/rakupp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char* what) {
    if (ok) printf("  ok - %s\n", what);
    else  { printf("  NOT OK - %s\n", what); failures++; }
}

/* Evaluate and compare the result's string form. */
static void expect(RkInterp rk, const char* code, const char* want, const char* what) {
    RkValue v;
    const char* got = NULL;
    if (rk_eval(rk, code, &v) == RK_OK) got = rk_str_get(rk_ctx(rk), v, NULL);
    else printf("    (error: %s)\n", rk_last_error(rk));
    if (got && strcmp(got, want) != 0) printf("    (got: %s)\n", got);
    check(got && strcmp(got, want) == 0, what);
}

int main(void) {
    setenv("EMBED_SANDBOX_SECRET", "hunter2", 1);

    RkConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = sizeof cfg;
    cfg.sandbox = 1;
    RkInterp rk = rk_new(&cfg);
    check(rk != NULL, "rk_new with sandbox = 1");
    if (!rk) return 1;

    expect(rk, "6 * 7", "42", "sandboxed Raku still computes");
    expect(rk, "%*ENV<EMBED_SANDBOX_SECRET> // 'hidden'", "hidden",
           "the host's environment is not in %*ENV");
    expect(rk, "try { slurp '/etc/hosts' }; $!.^name ~ ' ' ~ $!.capability",
           "X::SecurityPolicy::Sandbox read", "a file read is refused");
    expect(rk, "try { run 'true' }; $!.^name ~ ' ' ~ $!.capability",
           "X::SecurityPolicy::Sandbox run", "starting a process is refused");

    /* the sandbox confines the Raku code, not the process */
    const char* secret = getenv("EMBED_SANDBOX_SECRET");
    check(secret && strcmp(secret, "hunter2") == 0, "the host still sees its own environment");

    rk_free(rk);
    if (failures) { printf("embed sandbox: %d failed\n", failures); return 1; }
    printf("embed sandbox: ok\n");
    return 0;
}
