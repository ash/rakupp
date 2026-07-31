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
#include <cerrno>

// MinGW-w64 ships real POSIX-ish headers for much of this — use them and shim
// only what msvcrt genuinely lacks. The full shims below are MSVC-only.
#if defined(__MINGW32__)
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#else
using ssize_t = long long;
#endif

// --- socket shutdown(2) how-flags: POSIX names onto Winsock ---
#ifndef SHUT_RD
#define SHUT_RD   SD_RECEIVE
#define SHUT_WR   SD_SEND
#define SHUT_RDWR SD_BOTH
#endif

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

// --- mkdir: POSIX 2-arg -> Windows 1-arg (an overload beside MinGW's 1-arg) ---
inline int mkdir(const char* path, int) { return ::_mkdir(path); }

#if !defined(__MINGW32__)
// --- <dirent.h>: opendir/readdir/closedir over FindFirstFile (MSVC has none) ---
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
#endif // !__MINGW32__

// --- <sys/stat.h> gaps: MSVC has struct stat but not the S_IS* macros / mode_t ---
#include <sys/stat.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#if !defined(__MINGW32__)
using mode_t = int; // MinGW's sys/types.h already has it
#endif

// --- symlink/link/readlink: no POSIX equivalents in msvcrt ---
// Prefixed names rather than POSIX ones: MinGW-w64 ships a real <unistd.h> and
// which of these it declares varies by version, so a plain `symlink` here could
// collide there. The call sites use platform_* on every platform.
inline int platform_symlink(const char* target, const char* linkpath) {
    DWORD flags = 0;
    DWORD attr = ::GetFileAttributesA(target);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    // Developer Mode lets an unprivileged user create symlinks; without it the
    // call needs SeCreateSymbolicLinkPrivilege. Older Windows rejects the flag,
    // so fall back to a plain attempt.
    flags |= 0x2 /* SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE */;
    if (::CreateSymbolicLinkA(linkpath, target, flags)) return 0;
    if (::CreateSymbolicLinkA(linkpath, target, flags & ~0x2u)) return 0;
    errno = (::GetLastError() == ERROR_PRIVILEGE_NOT_HELD) ? EACCES : EINVAL;
    return -1;
}
inline int platform_link(const char* target, const char* linkpath) {
    if (::CreateHardLinkA(linkpath, target, nullptr)) return 0;
    DWORD e = ::GetLastError();
    errno = (e == ERROR_FILE_NOT_FOUND)     ? ENOENT
          : (e == ERROR_ALREADY_EXISTS)     ? EEXIST
          : (e == ERROR_ACCESS_DENIED)      ? EACCES : EINVAL;
    return -1;
}
inline long long platform_readlink(const char* path, char* buf, size_t n) {
    // No raw reparse-point read without DeviceIoControl; resolve the link to its
    // final target instead. Our symlink() writes an absolute target anyway, so
    // the answer agrees with the POSIX side for links we created.
    HANDLE h = ::CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) { errno = ENOENT; return -1; }
    char tmp[4096];
    DWORD got = ::GetFinalPathNameByHandleA(h, tmp, (DWORD)sizeof tmp, FILE_NAME_NORMALIZED);
    ::CloseHandle(h);
    if (!got || got >= sizeof tmp) { errno = EINVAL; return -1; }
    const char* p = tmp;
    if (::strncmp(p, "\\\\?\\", 4) == 0) p += 4;   // strip the \\?\ prefix
    size_t len = ::strlen(p);
    if (len > n) len = n;
    ::memcpy(buf, p, len);
    return (long long)len;
}

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

// The POSIX side of the link shims (see the Windows branch for why they carry a
// prefix rather than their own names).
inline int platform_symlink(const char* target, const char* linkpath) { return ::symlink(target, linkpath); }
inline int platform_link(const char* target, const char* linkpath)    { return ::link(target, linkpath); }
inline long long platform_readlink(const char* path, char* buf, size_t n) {
    return (long long)::readlink(path, buf, n);
}

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
