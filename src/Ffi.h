#pragma once
// ---------------------------------------------------------------------------
// The libffi backend for NativeCall.
//
// Raku++ ships as ONE portable binary with no third-party dependencies, so it
// does not link libffi and does not vendor it. Instead the library is loaded at
// RUNTIME with dlopen (the same trick the TLS layer uses for OpenSSL, see
// docs/HTTPS.md) and the handful of ABI declarations we need are restated here.
// Where no libffi can be found — WebAssembly, a stripped container, Windows
// without libffi-8.dll — NativeCall falls back to the older fixed-prototype
// path in Interpreter::callNative, which is correct for a narrower set of
// signatures and throws for the rest rather than guessing.
//
// Two things make hand-declaring another project's ABI safe here:
//
//   * we never touch `ffi_cif`'s fields. Its size and layout are
//     target-dependent, so we hand libffi an over-sized zeroed buffer and only
//     ever pass the pointer back. `ffi_type`'s layout, by contrast, is stable
//     public ABI — callers have always had to build struct types by hand.
//   * FFI_DEFAULT_ABI is an enum whose value differs per target AND per libffi
//     release, so we do not hardcode it: load() probes the candidates and keeps
//     the first that passes a self-test of real calls (see selfTest in Ffi.cpp).
//     A failed probe disables the backend instead of miscalling.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <string>

namespace ffi {

// libffi's `ffi_type`. Stable public layout: a caller composing a struct type
// fills these fields itself, so libffi cannot change them.
struct Type {
    size_t         size;
    unsigned short alignment;
    unsigned short type;
    Type**         elements;   // null-terminated, for T_STRUCT
};

// `ffi_type`'s type codes, from libffi's ffi.h.
enum : unsigned short {
    T_VOID = 0, T_INT = 1, T_FLOAT = 2, T_DOUBLE = 3, T_LONGDOUBLE = 4,
    T_UINT8 = 5, T_SINT8 = 6, T_UINT16 = 7, T_SINT16 = 8,
    T_UINT32 = 9, T_SINT32 = 10, T_UINT64 = 11, T_SINT64 = 12,
    T_STRUCT = 13, T_POINTER = 14, T_COMPLEX = 15,
};

// An opaque, over-sized `ffi_cif`. 512 bytes is far past any target's real
// size (the largest is a few dozen bytes); alignment is generous for the same
// reason. Zeroed on construction so a partially-prepped cif can't be mistaken
// for a live one.
struct Cif {
    alignas(16) unsigned char buf[512] = {};
};

// The resolved library: entry points, the probed ABI, and why it is not
// available when it isn't.
struct Lib {
    bool        ok   = false;
    std::string path;            // the candidate that loaded ("" if none did)
    std::string why;             // human-readable reason when !ok
    int         abi  = 0;        // FFI_DEFAULT_ABI for this target, probed

    int   (*prep)(void*, int, unsigned, Type*, Type**)                    = nullptr;
    int   (*prep_var)(void*, int, unsigned, unsigned, Type*, Type**)      = nullptr;
    void  (*call)(void*, void (*)(void), void*, void**)                   = nullptr;
    void* (*closure_alloc)(size_t, void**)                                = nullptr;
    void  (*closure_free)(void*)                                          = nullptr;
    int   (*prep_closure_loc)(void*, void*,
                              void (*)(void*, void*, void**, void*),
                              void*, void*)                               = nullptr;

    Type *t_void = nullptr, *t_uint8 = nullptr, *t_sint8 = nullptr,
         *t_uint16 = nullptr, *t_sint16 = nullptr, *t_uint32 = nullptr,
         *t_sint32 = nullptr, *t_uint64 = nullptr, *t_sint64 = nullptr,
         *t_float = nullptr, *t_double = nullptr, *t_pointer = nullptr;
};

// Loads (and self-tests) on first call; never throws, never retries.
const Lib& lib();
inline bool available() { return lib().ok; }

// One line for diagnostics: what loaded, or why nothing did.
std::string describe();

// A native scalar by width/signedness, matching ncScalarWidth's answers — so
// the Raku type-name table stays in ONE place, in Interpreter.cpp. Returns
// null when the combination has no libffi type (which means "not a scalar").
Type* scalar(int width, bool sign, bool isFloat);

} // namespace ffi
