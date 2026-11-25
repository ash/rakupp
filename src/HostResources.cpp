// HostResources.cpp — what the host says about its memory and this process's CPU:
// $*KERNEL.total-memory, .free-memory and .cpu-usage.
//
// Rakudo answers these from libuv (uv_get_total_memory, uv_get_free_memory) and
// getrusage(2), so the readings here follow libuv's choice per platform: on
// Linux the free figure is /proc/meminfo's MemAvailable, on macOS the VM's free
// page count, on Windows GlobalMemoryStatusEx. Without these the Kernel methods
// fell to the lenient accessor and answered the kernel's name ("linux").
#include "Platform.h"   // pulls <windows.h> on Windows
#include "BuiltinsShared.h"
#include <fstream>
#include <sstream>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif
#if !defined(_WIN32)
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace rakupp {

#if defined(__linux__)
// A `Key:   N kB` line of /proc/meminfo, in bytes; -1 when the key is absent
static long long meminfoBytes(const char* key) {
    std::ifstream in("/proc/meminfo");
    std::string line, k = std::string(key) + ":";
    while (std::getline(in, line)) {
        if (line.compare(0, k.size(), k) != 0) continue;
        std::istringstream ss(line.substr(k.size()));
        long long kb = -1;
        ss >> kb;
        return kb < 0 ? -1 : kb * 1024;
    }
    return -1;
}
#endif

long long hostTotalMemory() {
#if defined(_WIN32)
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    return GlobalMemoryStatusEx(&ms) ? (long long)ms.ullTotalPhys : 0;
#elif defined(__APPLE__)
    uint64_t mem = 0; size_t len = sizeof(mem);
    return sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0 ? (long long)mem : 0;
#else
#if defined(__linux__)
    long long b = meminfoBytes("MemTotal");
    if (b >= 0) return b;
#endif
    long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGESIZE);
    return pages > 0 && size > 0 ? (long long)pages * size : 0;
#endif
}

long long hostFreeMemory() {
#if defined(_WIN32)
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    return GlobalMemoryStatusEx(&ms) ? (long long)ms.ullAvailPhys : 0;
#elif defined(__APPLE__)
    vm_statistics64_data_t vs;
    mach_msg_type_number_t n = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vs, &n) != KERN_SUCCESS) return 0;
    return (long long)vs.free_count * (long long)sysconf(_SC_PAGESIZE);
#else
#if defined(__linux__)
    long long b = meminfoBytes("MemAvailable");
    if (b < 0) b = meminfoBytes("MemFree");
    if (b >= 0) return b;
#endif
#if defined(_SC_AVPHYS_PAGES)
    long pages = sysconf(_SC_AVPHYS_PAGES), size = sysconf(_SC_PAGESIZE);
    return pages > 0 && size > 0 ? (long long)pages * size : 0;
#else
    return 0;
#endif
#endif
}

// User plus system CPU time of this process, in microseconds (Rakudo's
// Kernel.cpu-usage sums the same four getrusage fields)
long long processCpuMicros() {
#if defined(_WIN32)
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
    auto t100ns = [](const FILETIME& f) { return ((long long)f.dwHighDateTime << 32) | f.dwLowDateTime; };
    return (t100ns(k) + t100ns(u)) / 10;
#else
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0;
    return (long long)ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec +
           (long long)ru.ru_stime.tv_sec * 1000000 + ru.ru_stime.tv_usec;
#endif
}

} // namespace rakupp
