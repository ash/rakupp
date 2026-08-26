// JupyterKernel.h — `rakupp --jupyter FILE`: the interpreter as a Jupyter
// kernel, so a notebook cell runs Raku the way `rakupp --mcp` makes an agent
// call run it. main.cpp only parses the flags and calls in here; everything
// else is JupyterKernel.cpp, including the ZeroMQ transport the protocol
// rides on — written out rather than linked, because a third-party
// dependency in this binary is the one thing the project does not do.
#pragma once
#include <string>
#include <vector>

namespace rakupp::jupyter {

struct Options {
    // The connection file Jupyter writes and passes as {connection_file}:
    // ports, the HMAC key, the transport. Everything the kernel binds.
    std::string connectionFile;
    // -M modules, loaded as `use <module>;` before the first cell.
    std::vector<std::string> preload;
};

// Serves until the frontend asks to shut down (or the process is killed).
// The return value is the process exit code.
int runKernel(const Options& opt);

struct InstallOptions {
    std::string selfExe;      // absolute path to THIS binary, written into argv
    std::string prefix;       // --prefix=DIR: DIR/share/jupyter/… instead of the user dir
    std::string name = "raku";        // the kernelspec directory name
    std::string displayName = "Raku++";
};

// Writes a kernelspec so `jupyter lab` / `jupyter console --kernel raku` can
// find this binary. Prints the path it wrote. Returns the process exit code.
int installKernelspec(const InstallOptions& opt);

} // namespace rakupp::jupyter
