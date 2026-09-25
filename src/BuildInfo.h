// Build identity, as reported by $*RAKU.compiler.build / .build-date.
//
// Defined in BuildInfo.cpp against a cmake-generated header. Declared here so
// callers do not include the generated one and pick up a rebuild dependency on
// it — that separation is the whole point (see cmake/BuildInfo.cmake).
#pragma once

#include <string>

namespace rakupp {
// `git describe` at build time: "v3.14.0-74-g9ff47ae", or with uncommitted
// changes "…-modified", or "unknown" outside a git checkout.
const char* buildId();
// The release version with what buildId() adds past its tag folded on:
// "4.0.1" on the tagged commit, "4.0.1-184-g55788fb5" after it, and
// "…-modified" with uncommitted changes. False, with the bare release
// version, when the build id is not about that tag (no git checkout) —
// --version then prints the id beside it rather than hide the gap.
bool describedVersion(std::string& out);
// The build date, UTC, as "YYYY-MM-DD"; "unknown" if it was not stamped.
const char* buildDate();
// What this binary was built FOR and BY — "arm64-darwin", "clang 17.0.0".
// Both are compile-time constants of the compiler itself, so they belong to
// the build's identity even though the generated header knows nothing of them.
// --version prints them because a bug report needs them in the same breath.
const char* platform();
const char* compilerId();
} // namespace rakupp
