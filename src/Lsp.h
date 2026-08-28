#pragma once

namespace rakupp {

// Run the Language Server Protocol server on stdin/stdout (JSON-RPC).
// Blocks until the client sends `exit`; returns the process exit code.
//
// v1 scope — diagnostics only: it parses and lints each open document and
// publishes squiggles. It reuses the exact same pipeline as `--lint`
// (Lexer -> Parser -> lintProgram) plus parse-error reporting, so it can never
// disagree with the CLI, and it changes nothing in the engine.
int runLsp();

} // namespace rakupp
