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

// The same server for a host that carries the messages itself (the browser
// demo on raku.online, where the server runs as WebAssembly in a worker): hand
// it one JSON-RPC message body, without the Content-Length header, and get
// back a JSON array of the bodies the server sends in answer — replies and
// notifications such as publishDiagnostics, in order. One session per
// process: open documents persist between calls. `reference` is used the
// first time only.
std::string lspExchange(const std::string& body, const std::string& reference = "");

} // namespace rakupp
