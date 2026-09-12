#pragma once
#include <string>
#include <vector>

namespace rakupp {

struct Program;
class Interpreter;

void rakuppSetDocMode(bool on); // enable --doc (run DOC phasers + print rendered POD)
void rakuppSetLLException(bool on); // enable --ll-exception (full, uncollapsed backtraces)
void rakuppSetStackBytes(size_t bytes); // --stack-size: the program thread's stack (default 1 GiB)
void rakuppSetStageStats(bool on); // --stagestats: phase timings and module loads on stderr at exit

// Parse and interpret `src`. Returns the process exit code.
// This is the shared entry point used both by the `rakupp` CLI and by
// standalone executables produced by `rakupp --compile` (which embed their
// program source and link against the runtime library).
int rakuppRun(const std::string& src, std::vector<std::string> args,
              const std::string& fileName, const std::string& exePath,
              const std::vector<std::string>& libPaths = {});

// The same, run IN an interpreter the caller already has. rakuppRun() is this
// plus "make me one first"; the embedding API (rakupp.h's rk_run) is this with
// the host's own session interpreter, so a host embedding rakupp does not end
// up with a second, hidden one whose construction would steal the process
// globals from the first.
// `declCheck` turns on the before-the-run undeclared-variable check (DeclCheck.h).
// The CLI asks for it; an EMBEDDING host does not, because its interpreter may
// already hold globals the host installed, which no static pass over this
// source can see.
int rakuppRunOn(Interpreter& interp, const std::string& src, std::vector<std::string> args,
                const std::string& fileName, const std::string& exePath,
                const std::vector<std::string>& libPaths = {}, bool declCheck = false);

// Same as rakuppRun, but executes on a thread with a large stack so deep
// (but bounded) recursion works and the interpreter's recursion guard fires
// before a native stack overflow. Use this as the top-level entry point.
int rakuppRunBigStack(const std::string& src, std::vector<std::string> args,
                      const std::string& fileName, const std::string& exePath,
                      const std::vector<std::string>& libPaths = {});

// Run a generated `--exe` binary's main body on the same large-stack thread —
// interpreter-parity recursion budget on every platform.
int rakuppMainOnBigStack(int (*body)(void*), void* ctx);

// Guard for COMPILED binaries: refuse a first-position `-e`/`--eval` rather than
// silently re-running the embedded program. Returns 0 to continue, else an exit
// code. See the comment on the definition for the fork bomb this stops.
int rakuppRefuseInterpreterEval(int argc, char** argv);

// Interpret an already-built Program (real AOT: the AST is reconstructed at the
// compiled program's startup, so no lexing/parsing happens). `finish` is the
// `$=finish` POD data block (empty if none).
int rakuppRunProgram(Program& prog, std::vector<std::string> args,
                     const std::string& fileName, const std::string& exePath, const std::string& finish);
int rakuppRunProgramBigStack(Program& prog, std::vector<std::string> args,
                             const std::string& fileName, const std::string& exePath, const std::string& finish);

// The search path the parser/loader will actually use, in order: -I paths first,
// then the built-in `lib` / `.` / `rakulib`, then RAKULIB. One definition, so the
// precomp key, the module loader and the --exe/--aot bundler cannot disagree
// about which file a `use` resolves to.
std::vector<std::string> effectiveSearchPath(const std::vector<std::string>& dashI);

// Prepare the Windows console for rakupp's output. Two settings, both of which
// a console gets wrong for us by default:
//   * UTF-8, so rakupp's UTF-8 output (the version banner's em-dash, and any
//     Unicode a program prints) renders instead of mojibake;
//   * virtual terminal processing, so an escape sequence is COLOUR rather than
//     the literal `ESC[1;32m` a legacy console echoes. Windows Terminal's
//     pseudoconsole turns this on for us; conhost — what cmd.exe opens — does
//     not, which is where the REPL prompt was arriving as line noise.
// No-op on non-Windows. Call once at the top of an entry point's main(); the
// <windows.h> calls are kept in Runtime.cpp.
void setupConsole();

// Whether a glyph outside the BMP will DRAW on this fd (1 or 2), rather than
// merely arrive: a UTF-8 locale and an interactive terminal, overridden either
// way by RAKUPP_UNICODE=0|1. False for a pipe, a file and a CI log, so captured
// output is byte-identical everywhere. See the comment on the definition for
// what this cannot know (which glyphs the font actually holds).
bool consoleUnicode(int fd);

// Whether an escape sequence written to this fd (1 or 2) will be acted on
// rather than printed. Always true off Windows, where whether the fd is a
// terminal at all is the caller's business. On Windows it is false until
// setupConsole() has turned VT on for that handle — which it cannot do on a
// pre-2016 console, and does not do for a redirected one, a pipe and a file
// not being consoles at all.
bool consoleAnsi(int fd);

}
