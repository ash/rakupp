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
