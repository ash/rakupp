// The build's own identity, isolated in one translation unit.
//
// rakupp_build.h is regenerated on every build (cmake/BuildInfo.cmake), so it
// is the one header whose contents can change without any source changing.
// Keeping it out of every other file means a new commit recompiles this alone
// instead of the whole tree — and means nothing else can accidentally bake a
// build timestamp into itself.
#include "BuildInfo.h"

#if __has_include("rakupp_build.h")
#include "rakupp_build.h"
#endif

// A build without the generated header (a hand-rolled compile, an IDE that
// skipped the custom target) still links; it just cannot say which commit it
// came from, and says so rather than guessing.
#ifndef RAKUPP_BUILD
#define RAKUPP_BUILD "unknown"
#endif
#ifndef RAKUPP_BUILD_DATE
#define RAKUPP_BUILD_DATE "unknown"
#endif

namespace rakupp {
const char* buildId()   { return RAKUPP_BUILD; }
const char* buildDate() { return RAKUPP_BUILD_DATE; }
} // namespace rakupp
