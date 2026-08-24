// McpServer.h — `rakupp --mcp`: the interpreter served over the Model
// Context Protocol (stdio JSON-RPC), so MCP clients — Claude Code, Claude
// Desktop, and their kind — get Raku evaluation and grammar parsing as
// callable tools. The whole implementation is McpServer.cpp; main.cpp only
// parses the flags and calls in here.
#pragma once
#include <string>
#include <vector>

namespace rakupp::mcp {

struct Options {
    // A tools/call that runs longer than this answers isError and EXITS the
    // process — rk_eval cannot be interrupted, so a fresh process is the only
    // honest recovery, and MCP clients restart a server on the next call.
    // 0 disables the watchdog.
    int timeoutSecs = 120;
    // -M modules, loaded into the session as `use <module>;` before the
    // first tool call.
    std::vector<std::string> preload;
};

// Serves until stdin closes (how MCP clients end a server). The return value
// is the process exit code.
int runServer(const Options& opt);

} // namespace rakupp::mcp
