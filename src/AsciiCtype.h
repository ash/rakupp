// Locale-INDEPENDENT character classification, drop-in for <cctype>.
//
// The language must not change with the host process's locale. The rakupp
// CLI never calls setlocale(), so it always ran in the "C" locale and never
// saw the problem — but an EMBEDDER usually has a locale set: CPython
// coerces LC_CTYPE to UTF-8 at startup (PEP 538), GUI toolkits call
// setlocale(LC_ALL, ""), and on macOS the UTF-8 ctype tables then classify
// UTF-8 *lead bytes* as alphabetic — std::isalpha(0xC2) is true. That made
// the tokenizer eat the lead byte of » as an identifier character and turn
// `@x».succ` into "Two terms in a row" — from Python only, never from the
// CLI, which is exactly the kind of divergence an embedding must not have.
//
// These functions reproduce the "C" locale for every value, so replacing
// std::isX(c) with ascii::isX(c) changes nothing for the CLI and pins the
// embedded behavior to it. Non-ASCII classification (identifiers, \w, case)
// is handled by the UTF-8-aware code paths (isLetterCP, Unicode.cpp), never
// by these.
//
// Same contract as <cctype>: argument is EOF or an unsigned-char value; a
// plain negative char (a byte >= 0x80 passed unsanitized) classifies false /
// maps to itself rather than being undefined behavior.
#pragma once

namespace ascii {

constexpr int isdigit(int c)  { return c >= '0' && c <= '9'; }
constexpr int islower(int c)  { return c >= 'a' && c <= 'z'; }
constexpr int isupper(int c)  { return c >= 'A' && c <= 'Z'; }
constexpr int isalpha(int c)  { return islower(c) || isupper(c); }
constexpr int isalnum(int c)  { return isalpha(c) || isdigit(c); }
constexpr int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
constexpr int isspace(int c)  { return c == ' ' || (c >= '\t' && c <= '\r'); } // \t \n \v \f \r
constexpr int isprint(int c)  { return c >= 0x20 && c <= 0x7E; }
constexpr int isgraph(int c)  { return c >= 0x21 && c <= 0x7E; }
constexpr int iscntrl(int c)  { return (c >= 0 && c <= 0x1F) || c == 0x7F; }
constexpr int ispunct(int c)  { return isgraph(c) && !isalnum(c); }
constexpr int tolower(int c)  { return isupper(c) ? c + ('a' - 'A') : c; }
constexpr int toupper(int c)  { return islower(c) ? c - ('a' - 'A') : c; }

} // namespace ascii
