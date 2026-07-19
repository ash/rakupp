#pragma once
#include <string>
#include <vector>

namespace rakupp {

struct Program;

void rakuppSetDocMode(bool on); // enable --doc (run DOC phasers + print rendered POD)

// Parse and interpret `src`. Returns the process exit code.
// This is the shared entry point used both by the `rakupp` CLI and by
// standalone executables produced by `rakupp --compile` (which embed their
// program source and link against the runtime library).
int rakuppRun(const std::string& src, std::vector<std::string> args,
              const std::string& fileName, const std::string& exePath,
              const std::vector<std::string>& libPaths = {});

// Same as rakuppRun, but executes on a thread with a large stack so deep
// (but bounded) recursion works and the interpreter's recursion guard fires
// before a native stack overflow. Use this as the top-level entry point.
int rakuppRunBigStack(const std::string& src, std::vector<std::string> args,
                      const std::string& fileName, const std::string& exePath,
                      const std::vector<std::string>& libPaths = {});

// Run a generated `--exe` binary's main body on the same large-stack thread —
// interpreter-parity recursion budget on every platform.
int rakuppMainOnBigStack(int (*body)(void*), void* ctx);

// Interpret an already-built Program (real AOT: the AST is reconstructed at the
// compiled program's startup, so no lexing/parsing happens). `finish` is the
// `$=finish` POD data block (empty if none).
int rakuppRunProgram(Program& prog, std::vector<std::string> args,
                     const std::string& fileName, const std::string& exePath, const std::string& finish);
int rakuppRunProgramBigStack(Program& prog, std::vector<std::string> args,
                             const std::string& fileName, const std::string& exePath, const std::string& finish);

}
