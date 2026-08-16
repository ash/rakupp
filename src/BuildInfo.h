// Build identity, as reported by $*RAKU.compiler.build / .build-date.
//
// Defined in BuildInfo.cpp against a cmake-generated header. Declared here so
// callers do not include the generated one and pick up a rebuild dependency on
// it — that separation is the whole point (see cmake/BuildInfo.cmake).
#pragma once

namespace rakupp {
// `git describe --always --dirty` at build time: "v3.14.0-74-g9ff47ae", or with
// uncommitted changes "…-dirty", or "unknown" outside a git checkout.
const char* buildId();
// The build date, UTC, as "YYYY-MM-DD"; "unknown" if it was not stamped.
const char* buildDate();
} // namespace rakupp
