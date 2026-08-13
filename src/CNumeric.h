// Locale-INDEPENDENT double formatting and parsing, drop-in for the
// snprintf/strtod the engine formats and reads numbers with.
//
// Same principle as AsciiCtype.h: the language must not change with the host
// process's locale. The CLI never calls setlocale() and always ran in the
// "C" locale — but an embedder usually has one set, and under LC_NUMERIC
// with a comma decimal separator (de_DE, fr_FR, ...) the libc pair breaks
// both directions: snprintf("%.2f", 1.5) → "1,50", and strtod("2.5e0")
// stops at the '.' and answers 2.0 — which turned `2.5e0` literals into 2,
// made `"4.25" + 0` throw X::Str::Numeric, and printed DateTime seconds as
// "12,5", from Python/German hosts only, never from the CLI.
//
// cnum::snprintf / cnum::strtod run the real libc calls under a cached "C"
// numeric locale, switched per-thread (uselocale on POSIX, _l calls on
// Windows), so they behave identically everywhere — including in a host
// that changes its locale after loading librakupp.
#pragma once

// newlocale/uselocale are POSIX 2008 names. glibc C++ always sees them
// (libstdc++ predefines _GNU_SOURCE), and Apple's headers expose everything
// by default (there the macro would RESTRICT — never define it), but a
// strict-ANSI musl build (Emscripten) hides them unless a feature macro
// asks. Only fires when no libc feature choice exists yet.
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_GNU_SOURCE) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#include <locale.h>
#else
#include <locale.h> // newlocale/uselocale (glibc); on macOS via xlocale
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#endif

namespace cnum {

#if defined(_WIN32)

inline _locale_t cLocale() {
    static _locale_t l = _create_locale(LC_ALL, "C");
    return l;
}

inline int vsnprintf(char* buf, size_t n, const char* fmt, va_list ap) {
    return _vsnprintf_l(buf, n, fmt, cLocale(), ap);
}

inline double strtod(const char* s, char** end) {
    return _strtod_l(s, end, cLocale());
}

#else

inline locale_t cLocale() {
    static locale_t l = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    return l;
}

inline int vsnprintf(char* buf, size_t n, const char* fmt, va_list ap) {
    locale_t old = uselocale(cLocale());
    int r = ::vsnprintf(buf, n, fmt, ap);
    uselocale(old);
    return r;
}

inline double strtod(const char* s, char** end) {
    locale_t old = uselocale(cLocale());
    double d = ::strtod(s, end);
    uselocale(old);
    return d;
}

#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
inline int snprintf(char* buf, size_t n, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = cnum::vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

// std::to_string(double)'s "%f" formatting, locale-pinned.
inline std::string to_string(double v) {
    char b[336]; // %f of DBL_MAX: 309 digits + point + 6 decimals + sign + NUL
    cnum::snprintf(b, sizeof b, "%f", v);
    return b;
}

// std::stod's contract over cnum::strtod: throws on a string with no leading
// number (overflow yields ±Inf rather than std::out_of_range — the Raku
// answer for "1e999" anyway).
inline double stod(const std::string& s) {
    char* end = nullptr;
    double d = cnum::strtod(s.c_str(), &end);
    if (end == s.c_str()) throw std::invalid_argument("cnum::stod: no conversion");
    return d;
}

} // namespace cnum
