#pragma once
// --profile: a routine-level instrumented wall-time profiler (v3 CLI step 6).
//
// Two hooks — callCallableRaw (subs/blocks with a routine boundary) and
// invokeMethod — feed a per-thread shadow stack; per routine we aggregate
// call count, inclusive and exclusive wall time. Builtins are attributed to
// their CALLER (we hook user-code routine entry, not the builtin dispatch
// chain), and recursion is handled the standard way: only the outermost
// active frame of a routine adds to its inclusive time.
//
// The off-cost when disabled is one predicted branch on a plain bool per
// call — measured at zero against the ±1.5% noise band before this was
// built (see CLI-PLAN.md, step 5/6). Everything here is portable C++17:
// no __builtin_expect, no __attribute__ — the prototype's GCC-isms broke
// the MSVC build once already.
#include <string>

namespace rakupp {
namespace prof {

extern bool on; // read on the hot path; set once before the program runs

// dest: "-" = table to stderr at exit; a path = table to that file;
// a path ending in .json = machine-readable dump (Rakudo's convention:
// the extension selects the format)
void setDest(const std::string& dest);

void enter(const void* key, const char* name, const char* file);
void leave();

// print/write the merged per-thread aggregates; no-op when disabled
void report();

} // namespace prof
} // namespace rakupp
