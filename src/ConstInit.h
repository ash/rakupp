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
