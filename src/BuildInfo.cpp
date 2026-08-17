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

// The target and the toolchain, decided entirely by the compiler's own
// predefined macros. An unrecognised pair says so rather than guessing: a
// wrong answer in a bug report is worse than no answer.
#if   defined(__EMSCRIPTEN__)
#define RAKUPP_ARCH "wasm32"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define RAKUPP_ARCH "arm64"
#elif defined(__x86_64__) || defined(_M_X64)
#define RAKUPP_ARCH "x86_64"
#elif defined(__i386__) || defined(_M_IX86)
#define RAKUPP_ARCH "x86"
#elif defined(__riscv) && __riscv_xlen == 64
#define RAKUPP_ARCH "riscv64"
#elif defined(__powerpc64__)
#define RAKUPP_ARCH "ppc64"
#else
#define RAKUPP_ARCH "unknown-arch"
#endif

#if   defined(__EMSCRIPTEN__)
#define RAKUPP_OS "emscripten"
#elif defined(_WIN32)
#define RAKUPP_OS "win32"
#elif defined(__APPLE__)
#define RAKUPP_OS "darwin"
#elif defined(__linux__)
#define RAKUPP_OS "linux"
#elif defined(__FreeBSD__)
#define RAKUPP_OS "freebsd"
#elif defined(__OpenBSD__)
#define RAKUPP_OS "openbsd"
#elif defined(__NetBSD__)
#define RAKUPP_OS "netbsd"
#else
#define RAKUPP_OS "unknown-os"
#endif

#define RAKUPP_STR2(x) #x
#define RAKUPP_STR(x)  RAKUPP_STR2(x)

// Clang must be tested before GCC: it defines __GNUC__ too.
#if   defined(__clang__)
// Not __clang_version__: it carries a vendor build id ("17.0.0 (clang-1700…)")
// that says nothing a bug report can use.
#define RAKUPP_CC "clang " RAKUPP_STR(__clang_major__) "." RAKUPP_STR(__clang_minor__) \
                  "." RAKUPP_STR(__clang_patchlevel__)
#elif defined(__GNUC__)
#define RAKUPP_CC "gcc " RAKUPP_STR(__GNUC__) "." RAKUPP_STR(__GNUC_MINOR__) \
                  "." RAKUPP_STR(__GNUC_PATCHLEVEL__)
#elif defined(_MSC_VER)
#define RAKUPP_CC "msvc " RAKUPP_STR(_MSC_VER)
#else
#define RAKUPP_CC "unknown compiler"
#endif

namespace rakupp {
const char* buildId()    { return RAKUPP_BUILD; }
const char* buildDate()  { return RAKUPP_BUILD_DATE; }
const char* platform()   { return RAKUPP_ARCH "-" RAKUPP_OS; }
const char* compilerId() { return RAKUPP_CC; }
} // namespace rakupp
