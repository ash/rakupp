// rakupp_web.cpp — Raku.js: WebAssembly entry point for Raku++.
//
// This file lives entirely OUTSIDE src/ and changes nothing in the interpreter.
// It simply calls the interpreter's existing public entry point,
// rakupp::rakuppRun() (declared in src/Runtime.h), and lets Emscripten capture
// whatever the Raku program writes to stdout/stderr through the host page's
// Module.print / Module.printErr callbacks.
//
// Why rakuppRun() and not rakuppRunBigStack(): the *BigStack variant spawns a
// pthread with a 1 GiB stack so deep recursion won't overflow the native stack.
// A single-threaded WASM build can't spawn that thread, so we call the plain
// entry point instead and reserve a large *WASM* stack at link time
// (-sSTACK_SIZE in build.sh). The interpreter's recursion guard measures the
// real stack size and throws X::Recursion before it overflows, so this is safe.

#include "Runtime.h"
#include "Highlight.h"

#include <emscripten/emscripten.h>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

extern "C" {

// Run a Raku program. `src` is UTF-8 Raku source; `stdin_text` (may be null)
// is what the program's standard input contains — `get` / `lines` / `prompt` /
// `$*IN.slurp` read it and then see EOF, so nothing ever blocks. Everything
// the program prints flows to the Module.print / Module.printErr callbacks the
// host page installs.
// Returns the Raku process exit code (0 = ok, 1 = parse/runtime error, 3 =
// internal error) — rakuppRun() catches ParseError / RakuError / std::exception
// internally and reports them to stderr, exactly like the native CLI.
EMSCRIPTEN_KEEPALIVE
int rakupp_run(const char* src, const char* stdin_text) {
    // The interpreter reads input exclusively through std::cin, so feeding it
    // is one rdbuf swap: no Emscripten TTY device, no stale-buffer or sticky
    // EOF state between runs (rdbuf() also clears cin's eof/fail bits).
    // Callers built against the old one-argument signature pass no second
    // WASM arg, which arrives here as null — i.e. an empty stdin.
    static std::istringstream web_stdin;
    web_stdin.clear();
    web_stdin.str(stdin_text ? stdin_text : "");
    std::streambuf* old_in = std::cin.rdbuf(web_stdin.rdbuf());

    int rc = rakupp::rakuppRun(src ? src : "",
                               /*args*/    {},
                               /*fileName*/ "web",
                               /*exePath*/  "rakupp.wasm",
                               /*libPaths*/ {});

    std::cin.rdbuf(old_in);
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
EMSCRIPTEN_KEEPALIVE
const char* rakupp_highlight(const char* src) {
    static std::string out;
    out = rakupp::highlight(src ? src : "", "html");
    return out.c_str();
}

// The Raku++ version string, for the playground footer / cache-busting.
EMSCRIPTEN_KEEPALIVE
const char* rakupp_version() {
#ifdef RAKUPP_VERSION
    return RAKUPP_VERSION;
#else
    return "unknown";
#endif
}

} // extern "C"
