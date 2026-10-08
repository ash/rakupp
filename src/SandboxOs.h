// --sandbox's OS layer (docs/dev/plans/SANDBOX-PLAN.md, S2): under the
// interpreter's own checks (Sandbox.h), the kernel confines the whole process,
// so a bug in the interpreter cannot turn into file, process or network access.
//
//   macOS   Seatbelt: sandbox_init with a deny-by-default profile
//   Linux   Landlock for files, seccomp-bpf for processes, sockets, ptrace …
//   other   none: `--sandbox` refuses to run, `--sandbox=language` runs
//           with the interpreter's checks alone
//
// CLI-only: main() is the one caller, so a binary built with --exe never links
// it, and an embedding host (RkConfig.sandbox) never confines its own process.
#pragma once
#include <string>
#include <vector>

namespace rakupp {

// What this binary can confine with on THIS machine, for `rakupp -V`:
// "Seatbelt", "Landlock (ABI 6) + seccomp", or why there is nothing.
std::string sandboxOsDescribe();

// Confine the process. `readable` are the files and directories the program
// may still read: the program file and the module search path. Nothing may be
// written, executed or connected to, and no process started. Returns "" once
// in force; otherwise the reason, and the program must not run. Call it before
// any thread exists: on Linux both mechanisms bind the calling thread and the
// threads it starts afterwards, not threads already running.
std::string sandboxOsEnter(const std::vector<std::string>& readable);

}  // namespace rakupp
