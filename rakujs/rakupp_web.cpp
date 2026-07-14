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

#include <emscripten/emscripten.h>
#include <cstdio>
#include <iostream>

extern "C" {

// Run a Raku program. `src` is UTF-8 Raku source. Everything the program prints
// flows to the Module.print / Module.printErr callbacks the host page installs.
// Returns the Raku process exit code (0 = ok, 1 = parse/runtime error, 3 =
// internal error) — rakuppRun() catches ParseError / RakuError / std::exception
// internally and reports them to stderr, exactly like the native CLI.
EMSCRIPTEN_KEEPALIVE
int rakupp_run(const char* src) {
    int rc = rakupp::rakuppRun(src ? src : "",
                               /*args*/    {},
                               /*fileName*/ "web",
                               /*exePath*/  "rakupp.wasm",
                               /*libPaths*/ {});
    // Push the final (possibly newline-less) line out to the host before we
    // hand control back to JavaScript.
    std::cout.flush();
    std::cerr.flush();
    std::fflush(stdout);
    std::fflush(stderr);
    return rc;
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
