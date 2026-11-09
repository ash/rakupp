#pragma once
// Common prologue for the method-dispatch segments (MethodCallPart2/Part3/Tail).
//
// Those files are contiguous slices of what used to be one 9,138-line function in
// Builtins.cpp, so they need the same environment it had. Keeping that list here
// rather than in each file means a segment cannot drift from its siblings — and
// it keeps the Windows/POSIX split in Platform.h, where it belongs, instead of
// being re-guessed per file.
#include "Interpreter.h"
#if !defined(_WIN32)
#include <sys/resource.h>
#endif
#include <cstdint>
#include <climits>
#include <limits>
#include <memory>
#include <cstdlib>
#include "Unicode.h"
#include <complex>
#include <functional>
#include "Regex.h"
#include "MethodName.h"
#include "BuiltinsShared.h"
#include <algorithm>
#include <atomic>
#include <ctime>
#include <fstream>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>
#include "Platform.h"   // POSIX headers on Unix; Winsock + shims (incl. dirent) on Windows
#if !defined(_WIN32)
#include <dirent.h>
#endif
#include <csignal>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#endif
#include <condition_variable>
#include <optional>

namespace rakupp {
// The first of a class and its parents, depth first and every `is` of each
// (`class D is B is A` delegates through A too), that `has` says yes to.
template <class F> static ClassInfo* firstUpward(ClassInfo* c, const F& has) {
    if (!c) return nullptr;
    if (has(c)) return c;
    if (ClassInfo* hit = firstUpward(c->parent.get(), has)) return hit;
    for (auto& p : c->extraParents) if (ClassInfo* hit = firstUpward(p.get(), has)) return hit;
    return nullptr;
}
}
