// HostResources.cpp — what the host says about its memory and this process's CPU:
// $*KERNEL.total-memory, .free-memory and .cpu-usage.
//
// Rakudo answers these from libuv (uv_get_total_memory, uv_get_free_memory) and
// getrusage(2), so the readings here follow libuv's choice per platform: on
// Linux the free figure is /proc/meminfo's MemAvailable, on macOS the VM's free
// page count, on Windows GlobalMemoryStatusEx. Without these the Kernel methods
// fell to the lenient accessor and answered the kernel's name ("linux").
//
// Also the three Systemic objects' own faces (#279): `$*KERNEL.raku` (so `dd
// $*KERNEL`), `.WHAT`, `.HOW` and `.gist`, which fell to the same accessor.
#include "Platform.h"   // pulls <windows.h> on Windows
#include "BuiltinsShared.h"
#include "Interpreter.h"
#include <fstream>
#include <sstream>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif
#if !defined(_WIN32)
#include <sys/resource.h>
#include <sys/utsname.h>
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

// $*KERNEL.hardware is uname -m; .arch is Rakudo's uname -p, which on macOS
// is "arm" or "i386" where the machine is arm64 or x86_64
std::string hostMachine(bool arch) {
#if !defined(_WIN32)
    struct utsname u;
    if (uname(&u) != 0) return "";
#if defined(__APPLE__)
    if (arch) return std::string(u.machine) == "arm64" ? "arm" : "i386";
#endif
    (void)arch;
    return u.machine;
#else
    (void)arch;
    return "";
#endif
}

// `.WHAT` is the type and `.HOW` its; `.gist` Systemic's "$name ($version)" (macOS's Distro
// and the VM answer theirs before this); `.raku` Rakudo's `Kernel.new(…)`,
// attributes in Rakudo's order, each its own method's answer. A Kernel has
// no `.desc` (Str), as in Rakudo.
Value Interpreter::systemicMeta(const Value& inv, const std::string& m) {
    if (m == "WHAT") return Value::typeObj(inv.hashKind);
    if (m == "desc") return Value::typeObj("Str");
    ValueList none;
    if (m == "HOW") return methodCall(Value::typeObj(inv.hashKind), "HOW", none);   // as `Kernel.HOW`
    if (m == "gist" && inv.hashKind == "Kernel") return methodCall(inv, "name", none);   // Rakudo's, until .version is read
    if (m == "gist")
        return Value::str(methodCall(inv, "name", none).toStr() + " (" + methodCall(inv, "version", none).toStr() + ")");
    static const char* const kernel[] = {"name", "version", "hardware", "arch", "bits", "release", "desc", "signature", nullptr};
    static const char* const distro[] = {"release", "path-sep", "name", "auth", "version", "signature", "desc", nullptr};
    static const char* const vm[] = {"config", "prefix", "precomp-ext", "precomp-target", "name", "auth", "version",
                                     "signature", "desc", nullptr};
    const char* const* attrs = inv.hashKind == "Kernel" ? kernel : inv.hashKind == "Distro" ? distro : vm;
    std::string out = inv.hashKind + ".new(";
    for (const char* const* a = attrs; *a; ++a) {
        if (a != attrs) out += ", ";
        out += std::string(*a) + " => " + methodCall(methodCall(inv, *a, none), "raku", none).toStr();
    }
    return Value::str(out + ")");
}

} // namespace rakupp
