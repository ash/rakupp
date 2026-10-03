#pragma once
#include <string>

namespace rakupp {

// Run the Language Server Protocol server on stdin/stdout (JSON-RPC).
// Blocks until the client sends `exit`; returns the process exit code.
//
// It publishes diagnostics from the same pipeline as `--lint` (Lexer -> Parser
// -> lintProgram + the undeclared-variable check), so it can never disagree
// with the CLI, and answers hover, completion and go-to-definition from the
// token stream and `reference` (docs/guide/REFERENCE.md, baked into the CLI;
// "" leaves built-ins undocumented). It never creates an interpreter, so
// opening a file cannot run it.
int runLsp(const std::string& reference = "");

} // namespace rakupp
