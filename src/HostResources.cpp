// HostResources.cpp — what the host says about its memory and this process's CPU:
// $*KERNEL.total-memory, .free-memory and .cpu-usage.
//
// Rakudo answers these from libuv (uv_get_total_memory, uv_get_free_memory) and
// getrusage(2), so the readings here follow libuv's choice per platform: on
// Linux the free figure is /proc/meminfo's MemAvailable, on macOS the VM's free
// page count, on Windows GlobalMemoryStatusEx. Without these the Kernel methods
// fell to the lenient accessor and answered the kernel's name ("linux").
//
// Also what the three Systemic objects answer for a method without an arm of
// its own (#279): `$*KERNEL.raku` (so `dd $*KERNEL`), `.WHAT`, `.HOW`, …, and
// X::Method::NotFound for a method Rakudo's Kernel, Distro and VM do not have.
#include "Platform.h"   // pulls <windows.h> on Windows
#include "BuiltinsShared.h"
#include "Interpreter.h"
#include <fstream>
#include <map>
#include <set>
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

// Rakudo 2026.09's own methods of each (beyond Any's and Mu's), for `.can`
// and for telling a method one of them lacks from one Any answers
int systemicHasMethod(const std::string& type, const std::string& method) {
    static const std::map<std::string, std::set<std::string>> kOwn = {
        {"Kernel", {
            "POPULATE", "Str", "arch", "archname", "auth", "bits", "cpu-cores", "cpu-cores-but-one", "cpu-usage",
            "desc", "endian", "free-memory", "gist", "hardware", "hostname", "name", "raku", "release", "signal",
            "signals", "signature", "total-memory", "version"
        }},
        {"Distro", {
            "POPULATE", "Str", "TWEAK", "auth", "cur-sep", "desc", "gist", "is-win", "name", "path-sep", "release",
            "signature", "version"
        }},
        {"VM", {
            "POPULATE", "Str", "TWEAK", "auth", "config", "desc", "gist", "name", "osname", "own-up",
            "platform-library-name", "precomp-ext", "precomp-target", "prefix", "remote-debugging",
            "request-garbage-collection", "signature", "version"
        }},
    };
    auto it = kOwn.find(type);
    return it == kOwn.end() ? 0 : it->second.count(method) ? 1 : -1;
}

// What a $*KERNEL / $*DISTRO / $*VM method no arm of its own answered gets
// here, from the end of their block in MethodCallPart2.cpp. It used to be a
// lenient accessor that answered the NAME for any method at all, so `.raku`,
// `.WHAT`, `.HOW`, `.defined`, `.elems` and a misspelt `.hostnmae` were all
// "darwin". Now: the faces Rakudo gives them (`.WHAT` the type and `.HOW` its;
// `.gist` Systemic's "$name ($version)", macOS's Distro and the VM answering
// theirs before this; `.raku` Rakudo's `Kernel.new(…)`, attributes in Rakudo's
// order, each its own method's answer), the rest of Rakudo's own methods, Mu's
// basics as for any defined object, nullopt for any other method Any has (the
// general chain answers it), and X::Method::NotFound for everything else.
std::optional<Value> Interpreter::systemicMeta(const Value& inv, const std::string& m) {
    const std::string kind = inv.hashKind;
    auto nm = inv.hash()->find("name");
    const std::string name = nm == inv.hash()->end() ? std::string() : nm->second.toStr();
    ValueList none;
    if (m == "WHAT") return Value::typeObj(kind);
    if (m == "HOW") return methodCall(Value::typeObj(kind), "HOW", none);   // as `Kernel.HOW`
    if (m == "name" || m == "Str" || m == "Stringy") return Value::str(name);
    // a Kernel has no .desc (Str) and no .auth ("unknown"), as in Rakudo; a
    // Linux Distro's are not read from os-release, and answer the name
    if (m == "desc") return kind == "Kernel" ? Value::typeObj("Str") : Value::str(name);
    if (m == "auth") return Value::str(kind == "Kernel" ? std::string("unknown") : name);
    if (m == "gist" && kind == "Kernel") return Value::str(name);   // Rakudo's, until .version is read
    if (m == "gist") return Value::str(name + " (" + methodCall(inv, "version", none).toStr() + ")");
    if (m == "raku") {
        static const char* const kernel[] = {"name", "version", "hardware", "arch", "bits", "release", "desc", "signature", nullptr};
        static const char* const distro[] = {"release", "path-sep", "name", "auth", "version", "signature", "desc", nullptr};
        static const char* const vm[] = {"config", "prefix", "precomp-ext", "precomp-target", "name", "auth", "version",
                                         "signature", "desc", nullptr};
        const char* const* attrs = kind == "Kernel" ? kernel : kind == "Distro" ? distro : vm;
        std::string out = kind + ".new(";
        for (const char* const* a = attrs; *a; ++a) {
            if (a != attrs) out += ", ";
            out += std::string(*a) + " => " + methodCall(methodCall(inv, *a, none), "raku", none).toStr();
        }
        return Value::str(out + ")");
    }
    if (kind == "Kernel" && m == "cpu-cores-but-one") {
        long long n = methodCall(inv, "cpu-cores", none).toInt();
        return Value::integer(n > 1 ? n - 1 : 1);
    }
    if (kind == "Distro" && m == "cur-sep") return Value::str(",");
#if defined(_WIN32)
    if (kind == "VM" && m == "osname") return Value::str("mswin32");
#else
    if (kind == "VM" && m == "osname") return Value::str(platKernelName());   // Rakudo's: the OS, not the VM
#endif
    if (kind == "VM" && m == "remote-debugging") return Value::integer(0);
    // Mu's answers for a defined object
    if (m == "defined" || m == "DEFINITE" || m == "so" || m == "Bool") return Value::boolean(true);
    if (m == "not") return Value::boolean(false);
    if (m == "elems") return Value::integer(1);
    if (m == "item") return inv;
    if (m == "WHICH") { Value w = Value::str(kind + "|" + name); w.hashKind = "ObjAt"; return w; }
    Value one = Value::array(); one.isList = true; one.arr()->push_back(inv);
    if (m == "list" || m == "cache" || m == "flat") return one;
    // …and Any's list methods, as on the list of one it is (`.keys` is (0,))
    if (m == "keys" || m == "values" || m == "pairs" || m == "kv" || m == "antipairs") return methodCall(one, m, none);
    // Cool's numeric and string methods are not Any's
    if (m == "Numeric" || m == "Int" || m == "Real")
        throw RakuError{Value::typeObj("X::Multi::NoMatch"),
                        "Cannot resolve caller " + m + "(" + kind + ":D: ); none of these signatures matches:\n    (Mu:U \\v:: *%_)"};
    if (m == "split" || m == "fmt")
        throw RakuError{Value::typeObj("X::Multi::NoMatch"), "Cannot resolve caller " + m + "(" + kind +
                        ":D); Routine does not have any candidates.  Is only the proto defined?"};
    if (!m.empty() && m[0] == '^') return std::nullopt;   // a meta-method is the HOW's
    if (tabledTypeHasMethod(kind, m) == 1) return std::nullopt;   // Any's, or one of Rakudo's this does not model
    throwTypedV("X::Method::NotFound",
                {{"method", Value::str(m)}, {"typename", Value::str(kind)}, {"private", Value::boolean(false)}},
                "No such method '" + m + "' for invocant of type '" + kind + "'");
}

} // namespace rakupp
