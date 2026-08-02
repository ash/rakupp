#include "Ffi.h"
#include "Platform.h"   // dlopen/dlsym (and their Win32 shims)

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace ffi {

namespace {

// libffi's ffi_status: 0 = FFI_OK.
constexpr int FFI_OK = 0;

// ffi_arg — the width libffi widens small integer returns to. It is
// pointer-sized on every target we build for.
constexpr size_t ARG_SIZE = sizeof(void*) >= 8 ? 8 : 4;

unsigned long long readArg(const void* buf) {
    unsigned long long v = 0;
    std::memcpy(&v, buf, ARG_SIZE);
    return v;
}

// Candidate sonames, most specific first. RAKUPP_FFI=<path> is prepended.
const char* const kCandidates[] = {
    "libffi.so.8", "libffi.so.7", "libffi.so.6", "libffi.so",
    "libffi.dylib", "/usr/lib/libffi.dylib",
    "libffi-8.dll", "libffi-7.dll", "libffi.dll",
};

// FFI_DEFAULT_ABI's numeric value depends on both the target and the libffi
// release (the x86 enum gained members in 3.4, renumbering FFI_UNIX64 from 1
// to 8). We never hardcode it — this is only the order in which candidates are
// tried, so that the right one is normally the FIRST one self-tested and a
// wrong-but-accepted ABI is never actually called.
std::vector<int> abiCandidates() {
    std::vector<int> v;
#if defined(__aarch64__) || defined(_M_ARM64)
    v = {1, 2};                    // FFI_SYSV, FFI_WIN64
#elif defined(__x86_64__) || defined(_M_X64)
  #if defined(_WIN32)
    v = {9, 2, 10};                // FFI_WIN64 (3.4 / <=3.3), FFI_GNUW64
  #else
    v = {8, 1, 2};                 // FFI_UNIX64 in 3.4 / in <=3.3
  #endif
#elif defined(__i386__) || defined(_M_IX86)
    v = {1, 5};                    // FFI_SYSV, FFI_MS_CDECL
#elif defined(__arm__) || defined(_M_ARM)
    v = {1, 2};                    // FFI_SYSV, FFI_VFP
#endif
    for (int a = 1; a <= 12; a++)  // last resort: everything libffi might define
        if (std::find(v.begin(), v.end(), a) == v.end()) v.push_back(a);
    return v;
}

Lib g;

// Call `fn` through libffi with the given signature, into `ret`. Returns false
// if the cif could not be prepared (a wrong ABI is rejected here, before any
// call happens).
bool tryCall(int abi, void* fn, Type* rtype, std::vector<Type*> atypes,
             void** avalues, void* ret) {
    Cif cif;
    if (g.prep(cif.buf, abi, (unsigned)atypes.size(), rtype,
               atypes.empty() ? nullptr : atypes.data()) != FFI_OK)
        return false;
    g.call(cif.buf, (void (*)(void))fn, ret, avalues);
    return true;
}

// Real calls with known answers, through the ABI we are about to trust. The
// float/double pair is the important part: it is exactly what a mistaken ABI
// (or a mis-declared ffi_type) gets wrong, and it is what the old
// fixed-prototype path gets wrong today.
bool selfTest(int abi) {
    alignas(16) unsigned char r[32];

    {   // size_t strlen(const char*) — pointer argument, pointer-sized return
        const char* s = "abcdef";
        void* av[1] = { (void*)&s };
        Type* rt = ARG_SIZE == 8 ? g.t_uint64 : g.t_uint32;
        if (!tryCall(abi, (void*)(size_t (*)(const char*))&std::strlen,
                     rt, { g.t_pointer }, av, r)) return false;
        if (readArg(r) != 6) return false;
    }
    {   // int abs(int) — a narrow signed integer in and out
        int a = -3;
        void* av[1] = { &a };
        if (!tryCall(abi, (void*)(int (*)(int))&std::abs,
                     g.t_sint32, { g.t_sint32 }, av, r)) return false;
        if ((int)(long long)readArg(r) != 3) return false;
    }
    {   // double ldexp(double, int) — mixed float/integer register banks
        double a = 3.0; int b = 2; double out = 0;
        void* av[2] = { &a, &b };
        if (!tryCall(abi, (void*)(double (*)(double, int))&std::ldexp,
                     g.t_double, { g.t_double, g.t_sint32 }, av, &out)) return false;
        if (out != 12.0) return false;
    }
    {   // float ldexpf(float, int) — single precision really is single
        float a = 3.0f; int b = 2; float out = 0;
        void* av[2] = { &a, &b };
        if (!tryCall(abi, (void*)(float (*)(float, int))&std::ldexp,
                     g.t_float, { g.t_float, g.t_sint32 }, av, &out)) return false;
        if (out != 12.0f) return false;
    }
    return true;
}

bool resolve(void* h) {
    auto sym = [h](const char* n) { return dlsym(h, n); };
    g.prep     = (int (*)(void*, int, unsigned, Type*, Type**))sym("ffi_prep_cif");
    g.prep_var = (int (*)(void*, int, unsigned, unsigned, Type*, Type**))sym("ffi_prep_cif_var");
    g.call     = (void (*)(void*, void (*)(void), void*, void**))sym("ffi_call");
    g.closure_alloc    = (void* (*)(size_t, void**))sym("ffi_closure_alloc");
    g.closure_free     = (void (*)(void*))sym("ffi_closure_free");
    g.prep_closure_loc = (int (*)(void*, void*, void (*)(void*, void*, void**, void*),
                                  void*, void*))sym("ffi_prep_closure_loc");
    g.t_void    = (Type*)sym("ffi_type_void");
    g.t_uint8   = (Type*)sym("ffi_type_uint8");
    g.t_sint8   = (Type*)sym("ffi_type_sint8");
    g.t_uint16  = (Type*)sym("ffi_type_uint16");
    g.t_sint16  = (Type*)sym("ffi_type_sint16");
    g.t_uint32  = (Type*)sym("ffi_type_uint32");
    g.t_sint32  = (Type*)sym("ffi_type_sint32");
    g.t_uint64  = (Type*)sym("ffi_type_uint64");
    g.t_sint64  = (Type*)sym("ffi_type_sint64");
    g.t_float   = (Type*)sym("ffi_type_float");
    g.t_double  = (Type*)sym("ffi_type_double");
    g.t_pointer = (Type*)sym("ffi_type_pointer");
    return g.prep && g.call && g.t_void && g.t_uint8 && g.t_sint8 &&
           g.t_uint16 && g.t_sint16 && g.t_uint32 && g.t_sint32 &&
           g.t_uint64 && g.t_sint64 && g.t_float && g.t_double && g.t_pointer;
}

void load() {
    const char* env = std::getenv("RAKUPP_FFI");
    if (env && (!*env || !std::strcmp(env, "0") || !std::strcmp(env, "off") ||
                !std::strcmp(env, "no") || !std::strcmp(env, "false"))) {
        g.why = "disabled by RAKUPP_FFI";
        return;
    }
    std::vector<std::string> cands;
    if (env) cands.push_back(env);                       // an explicit path wins
    for (const char* c : kCandidates) cands.push_back(c);

    for (const std::string& c : cands) {
        void* h = dlopen(c.c_str(), RTLD_LAZY | RTLD_GLOBAL);
        if (!h) continue;
        if (!resolve(h)) { g.why = "loaded " + c + " but its entry points are missing"; continue; }
        for (int abi : abiCandidates()) {
            if (!selfTest(abi)) continue;
            g.abi = abi; g.path = c; g.ok = true;
            return;
        }
        g.why = "loaded " + c + " but no calling convention passed the self-test";
    }
    if (g.why.empty()) g.why = "no libffi found";
    g.ok = false;
}

} // namespace

const Lib& lib() {
    static const bool once = [] { load(); return true; }();
    (void)once;
    return g;
}

std::string describe() {
    const Lib& l = lib();
    if (!l.ok) return "libffi: unavailable (" + l.why + ")";
    return "libffi: " + l.path + " (abi " + std::to_string(l.abi) + ")";
}

Type* scalar(int width, bool sign, bool isFloat) {
    const Lib& l = lib();
    if (!l.ok) return nullptr;
    if (isFloat) return width == 4 ? l.t_float : l.t_double;
    switch (width) {
        case 1: return sign ? l.t_sint8  : l.t_uint8;
        case 2: return sign ? l.t_sint16 : l.t_uint16;
        case 4: return sign ? l.t_sint32 : l.t_uint32;
        case 8: return sign ? l.t_sint64 : l.t_uint64;
    }
    return nullptr;
}

} // namespace ffi
