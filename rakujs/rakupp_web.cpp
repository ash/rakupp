// rakupp_web.cpp — Raku.js: WebAssembly entry point for Raku++.
//
// Written against the PUBLIC embedding API (src/rakupp.h) rather than against
// the interpreter's insides. That is the point of the file: ABI-PLAN's A2 makes
// the playground the first real consumer of rakupp.h, so anything the API
// cannot express here is a hole in the API rather than a thing to work around
// locally. It found one — a playground needs whole-PROGRAM semantics (MAIN,
// `exit`, errors printed the way the CLI prints them), not expression
// evaluation, which is why rk_run exists alongside rk_eval.
//
// What this file no longer does, because the API does it:
//   * swapping std::cin's rdbuf by hand to feed stdin  -> rk_set_input
//   * reaching for rakupp::rakuppRun() and Interpreter  -> rk_run
//
// Why a fresh interpreter per run: the playground must not leak `my $x` from
// one run into the next. rk_new/rk_free around each program is what gives that,
// and it is cheap next to parsing.
//
// Why the default config (no own_stack): the *BigStack variant spawns a pthread
// with a 1 GiB stack so deep recursion won't overflow the native stack. A
// single-threaded WASM build can't spawn that thread, so we leave own_stack off
// and reserve a large *WASM* stack at link time (-sSTACK_SIZE in build.sh). The
// interpreter's recursion guard measures the real stack and throws X::Recursion
// before it overflows, so this is safe.

#include "Highlight.h"
#include "rakupp.h"

#include <emscripten/emscripten.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>

extern "C" {

// Run a Raku program. `src` is UTF-8 Raku source; `stdin_text` (may be null)
// is what the program's standard input contains — `get` / `lines` / `prompt` /
// `$*IN.slurp` read it and then see EOF, so nothing ever blocks. Everything the
// program prints flows to the Module.print / Module.printErr callbacks the host
// page installs, because Raku's output still goes to the process's own streams;
// the API can capture it (rk_set_output) but here Emscripten's are exactly what
// we want.
// Returns the Raku process exit code (0 = ok, 1 = parse/runtime error, 3 =
// internal error) — rk_run reports parse and runtime failures to stderr itself,
// exactly like the native CLI.
EMSCRIPTEN_KEEPALIVE
int rakupp_run(const char* src, const char* stdin_text) {
    RkInterp rk = rk_new(0);            // 0 = default config
    if (!rk) return 3;

    if (stdin_text) rk_set_input(rk, stdin_text, std::strlen(stdin_text));

    int rc = 3;
    rk_run(rk, src ? src : "", "web", &rc);

    rk_free(rk);                        // also restores stdin

    // Push the final (possibly newline-less) line out to the host before we
    // hand control back to JavaScript.
    std::cout.flush();
    std::cerr.flush();
    std::fflush(stdout);
    std::fflush(stderr);
    // Emscripten's TTY only emits a line to Module.print on a newline, so a
    // program ending in `print` (no trailing newline) leaves its last line
    // buffered — and it then leaks onto the FIRST line of the next run.
    // fsync triggers the TTY's flush op (fflush does not), clearing it.
    // Emscripten's own device layer, not something the embedding API owns.
    fsync(STDOUT_FILENO);
    fsync(STDERR_FILENO);
    return rc;
}

// Syntax-highlight Raku source with rakupp's own tokenizer (the same one behind
// `rakupp --highlight`) — so an embed can paint the editor exactly as the CLI
// would, instead of an approximate JS tokenizer. Returns HTML: a
// `<div class="highlight"><pre>…<span class="k">…</span>…</pre></div>` string
// using Pygments token classes. The result lives in a static string valid until
// the next call — the caller (synchronous, single-threaded) copies it out
// immediately, as with rakupp_version().
//
// Deliberately NOT part of rakupp.h: highlighting is a tool built on the lexer,
// not part of embedding a language, and the ABI stays as small as it can be.
EMSCRIPTEN_KEEPALIVE
const char* rakupp_highlight(const char* src) {
    static std::string out;
    out = rakupp::highlight(src ? src : "", "html");
    return out.c_str();
}

// The Raku++ version string, for the playground footer / cache-busting.
EMSCRIPTEN_KEEPALIVE
const char* rakupp_version() {
    return rk_version();
}

} // extern "C"
