#pragma once
// Cross-platform compatibility layer. Source files include this instead of raw
// POSIX headers (<unistd.h>, <dlfcn.h>, <sys/socket.h>, …). On POSIX it just
// pulls those in; on Windows it maps the handful of APIs Raku++ uses onto Win32
// (Winsock, LoadLibrary, GetModuleFileName, …) so the same code compiles.

#include <string>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX            // keep std::min/std::max, not the windows.h macros
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <cstdlib>
#include <cstring>

using ssize_t = long long;

// --- dynamic loading: <dlfcn.h> -> LoadLibrary/GetProcAddress ---
#define RTLD_LAZY   0
#define RTLD_GLOBAL 0
#define RTLD_NOW    0
#define RTLD_DEFAULT ((void*)0)
inline void* dlopen(const char* path, int) { return path ? (void*)::LoadLibraryA(path) : (void*)::GetModuleHandleA(nullptr); }
inline void* dlsym(void* handle, const char* name) {
    HMODULE m = handle ? (HMODULE)handle : ::GetModuleHandleA(nullptr);
    return (void*)::GetProcAddress(m, name);
}
inline int   dlclose(void* handle) { return ::FreeLibrary((HMODULE)handle) ? 0 : -1; }
inline const char* dlerror() { return "dynamic load failed"; }

// --- filesystem/env shims ---
inline char* realpath(const char* path, char* resolved) { return ::_fullpath(resolved, path, 4096); }
inline int   setenv(const char* k, const char* v, int) { return ::_putenv_s(k, v); }
inline int   unsetenv(const char* k) { return ::_putenv_s(k, ""); }
#ifndef X_OK
#define X_OK 0
#define R_OK 4
#define W_OK 2
#define F_OK 0
#endif
inline int platform_access(const char* p, int m) { return ::_access(p, m == X_OK ? 0 : m); }
#define access platform_access

// --- poll(): Winsock's WSAPoll matches the POSIX signature (Vista+) ---
inline int poll(struct pollfd* fds, unsigned long n, int timeout) { return ::WSAPoll(fds, n, timeout); }

// --- mkdir: POSIX 2-arg -> Windows 1-arg ---
inline int mkdir(const char* path, int) { return ::_mkdir(path); }

// --- <dirent.h>: opendir/readdir/closedir over FindFirstFile ---
struct dirent { char d_name[260]; };
struct DIR {
    HANDLE h = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAA fd{};
    dirent de{};
    bool first = true;
};
inline DIR* opendir(const char* path) {
    std::string pat = std::string(path) + "\\*";
    DIR* d = new DIR;
    d->h = ::FindFirstFileA(pat.c_str(), &d->fd);
    if (d->h == INVALID_HANDLE_VALUE) { delete d; return nullptr; }
    return d;
}
inline dirent* readdir(DIR* d) {
    if (!d) return nullptr;
    if (d->first) d->first = false;
    else if (!::FindNextFileA(d->h, &d->fd)) return nullptr;
    ::strncpy(d->de.d_name, d->fd.cFileName, sizeof(d->de.d_name) - 1);
    d->de.d_name[sizeof(d->de.d_name) - 1] = '\0';
    return &d->de;
}
inline int closedir(DIR* d) {
    if (!d) return -1;
    ::FindClose(d->h);
    delete d;
    return 0;
}

// --- <sys/stat.h> gaps: MSVC has struct stat but not the S_IS* macros / mode_t ---
#include <sys/stat.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
using mode_t = int;

// --- erand48: POSIX 48-bit LCG (drand48 family), reimplemented for parity ---
inline double erand48(unsigned short xsubi[3]) {
    unsigned long long x = ((unsigned long long)xsubi[2] << 32) |
                           ((unsigned long long)xsubi[1] << 16) |
                            (unsigned long long)xsubi[0];
    x = (x * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
    xsubi[0] = (unsigned short)(x & 0xFFFF);
    xsubi[1] = (unsigned short)((x >> 16) & 0xFFFF);
    xsubi[2] = (unsigned short)((x >> 32) & 0xFFFF);
    return (double)x / (double)(1ULL << 48);
}

#else  // ---------------- POSIX ----------------

#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>

#endif

// Math constants: MinGW/MSVC don't define these without _USE_MATH_DEFINES.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

namespace rakupp {
// True on Windows, false elsewhere — for the few places that must branch at runtime.
inline bool onWindows() {
#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}
}
