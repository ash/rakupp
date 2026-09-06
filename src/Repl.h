#pragma once
#include <string>
#include <vector>

namespace rakupp {

// The interactive read-eval-print loop.
//
// Entered ONLY by a bare `rakupp` attached to a terminal. Anything arriving on a
// pipe or a redirect (`echo … | rakupp`, `rakupp < file.raku`) is a complete
// program and keeps running as one — the tty test in main() is what separates
// them, and this function is never reached in that case.
//
// Lives in the `rakupp` executable rather than the runtime library, so nothing
// here is linked into the binaries `--exe` produces.
// `quiet` (-q) skips the banner — python's -q, for a session that starts at
// the prompt.
int rakuppRepl(const std::string& exePath, const std::vector<std::string>& libPaths,
               bool quiet = false);

// --repl-after (python -i): run `src` — as `fileName`, with `args` as @*ARGS —
// in the session's own interpreter, then hand the prompt over with everything
// the program declared still live. The program's END blocks run when the
// session ends, like its own. Opens the session whether or not stdin is a
// terminal: the flag is the request.
int rakuppReplAfter(const std::string& exePath, const std::vector<std::string>& libPaths,
                    bool quiet, const std::string& src, const std::string& fileName,
                    std::vector<std::string> args);

// Is stdin a terminal? This is the whole of the REPL-vs-program decision, so it
// lives next to the REPL rather than behind another platform #ifdef in main().
bool stdinIsTerminal();

// RAKUPP_REPL=1 — force a session even when stdin is not a terminal, so the loop
// can be driven from a script or a test. Unset, this is always false.
bool replForced();

} // namespace rakupp
