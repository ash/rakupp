// RAKUPP_CONSTINIT: this thread_local is constant-initialized.
//
// On a thread_local DECLARATION: its definition is constant-initialized (a
// scalar or pointer set to a constant, or left to zero). A file that does not
// define a thread_local cannot see that, so it calls a wrapper function on
// every access in case the definition needs dynamic initialization. Saying so
// turns each access into a bare TLS load. The attribute is checked at the
// definition: one that is not constant-initialized fails to compile, so it
// cannot lie. It goes FIRST in a declaration, before `extern`, `static` or
// `inline`: an attribute cannot stand between decl-specifiers.
//
// Clang only. C++20's `constinit` says the same, but a variable that has it on
// any declaration must have it on the initializing one too (no diagnostic
// required), and the definitions in the .cpp files do not carry it; at C++17
// (CMakeLists.txt) there is no `constinit` anyway. Elsewhere the macro is
// empty, which costs the check and the bare load, nothing else.
#pragma once

#if defined(__clang__)
#define RAKUPP_CONSTINIT [[clang::require_constant_initialization]]
#else
#define RAKUPP_CONSTINIT
#endif

// RAKUPP_POOLS: 0 under AddressSanitizer, else 1. SlabPool and RVec keep freed
// blocks on free lists of their own and hand them out again, which ASan sees
// as memory never freed, so a dangling pointer into a recycled block reads
// someone else's object without a report. With the pools off every block goes
// back to the allocator, and ASan's quarantine catches the read. (Here, beside
// the other build-wide switch, because both pools' headers include it.)
#if defined(__SANITIZE_ADDRESS__)
#define RAKUPP_POOLS 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define RAKUPP_POOLS 0
#endif
#endif
#ifndef RAKUPP_POOLS
#define RAKUPP_POOLS 1
#endif
