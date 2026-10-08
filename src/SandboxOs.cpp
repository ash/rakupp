// --sandbox's OS layer (SandboxOs.h).

#include "SandboxOs.h"

#if defined(__APPLE__) || defined(__linux__)
#include <cerrno>
#include <cstdint>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
// <sandbox.h>, declared rather than included: on a case-insensitive disk the
// include finds this project's own src/Sandbox.h first. The two functions are
// the SDK's public (deprecated, still served) Seatbelt entry points.
extern "C" {
int sandbox_init(const char* profile, uint64_t flags, char** errorbuf);
void sandbox_free_error(char* errorbuf);
}
#elif defined(__linux__)
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#endif

namespace rakupp {

#if defined(__APPLE__) || defined(__linux__)
namespace {

// A path as the kernel will see it: absolute, symlinks resolved (macOS's /tmp
// is /private/tmp, and Seatbelt matches the resolved name). "" when it does not
// exist, so a rule is never written for nothing.
std::string resolved(const std::string& p) {
    if (p.empty()) return "";
    char buf[PATH_MAX];
    if (!::realpath(p.c_str(), buf)) return "";
    return buf;
}

bool isDir(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Read lazily by the C library, so read now, while it still can be: after
// tzset the zone is cached, and DateTime.now keeps answering in local time.
void warmLibc() {
    ::tzset();
    std::time_t t = std::time(nullptr);
    struct tm tmv;
    ::localtime_r(&t, &tmv);
}

}  // namespace
#endif

#if defined(__APPLE__)
// ---------------------------------------------------------------- macOS ---

std::string sandboxOsDescribe() { return "Seatbelt"; }

namespace {

std::string sbplString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

}  // namespace

std::string sandboxOsEnter(const std::vector<std::string>& readable) {
    // Deny by default. What stays: reading the program and the module search
    // path, file METADATA anywhere (getcwd and realpath walk every ancestor —
    // so a stat can tell a program a path exists, but no byte of it), the
    // system's read-only answers about itself, and signals to this process.
    // Writing, process-fork, process-exec, network* and mach-lookup are all
    // simply never allowed.
    std::string reads;
    std::vector<std::string> all = readable;
    for (const char* p : {"/dev/null", "/dev/zero", "/dev/random", "/dev/urandom",
                          "/usr/share/zoneinfo", "/var/db/timezone", "/etc/localtime"})
        all.push_back(p);
    for (auto& p : all) {
        std::string r = resolved(p);
        if (r.empty()) continue;
        reads += isDir(r) ? " (subpath " + sbplString(r) + ")" : " (literal " + sbplString(r) + ")";
    }
    std::string profile =
        "(version 1)\n"
        "(deny default)\n"
        "(allow process-info* (target self))\n"
        "(allow signal (target self))\n"
        "(allow sysctl-read)\n"
        "(allow file-read-metadata)\n"
        "(allow file-write-data (literal \"/dev/null\"))\n"
        // the REPL's terminal: read and set its mode, read its size — and
        // nothing else, so not TIOCSTI, which types into the shell's input
        "(allow file-ioctl (ioctl-command TIOCGETA) (ioctl-command TIOCSETA) (ioctl-command TIOCSETAW)"
        " (ioctl-command TIOCSETAF) (ioctl-command TIOCGWINSZ))\n";
    if (!reads.empty()) profile += "(allow file-read*" + reads + ")\n";
    warmLibc();
    char* err = nullptr;
    int rc = ::sandbox_init(profile.c_str(), 0, &err);
    if (rc != 0) {
        std::string why = std::string("Seatbelt refused the profile: ") + (err ? err : "unknown error");
        if (err) ::sandbox_free_error(err);
        return why;
    }
    return "";
}

#elif defined(__linux__)
// ---------------------------------------------------------------- Linux ---

namespace {

// Landlock, defined here rather than taken from <linux/landlock.h>: the
// release builds' manylinux container has 4.18 kernel headers, and the ABI is
// stable — these numbers are the same on every architecture.
#ifndef __NR_landlock_create_ruleset
#define __NR_landlock_create_ruleset 444
#define __NR_landlock_add_rule 445
#define __NR_landlock_restrict_self 446
#endif
struct LlRuleset { uint64_t fs, net, scoped; };
struct __attribute__((packed)) LlPathBeneath { uint64_t allowed; int32_t parentFd; };
constexpr uint32_t kLlVersion = 1u << 0;          // LANDLOCK_CREATE_RULESET_VERSION
constexpr int kLlRulePathBeneath = 1;
constexpr uint64_t kLlReadFile = 1ull << 2, kLlReadDir = 1ull << 3;

// The Landlock ABI this kernel speaks, or -errno.
int landlockAbi() {
    long v = ::syscall(__NR_landlock_create_ruleset, nullptr, 0, kLlVersion);
    return v < 0 ? -errno : (int)v;
}

std::string kernelRelease() {
    struct utsname u;
    return ::uname(&u) == 0 ? std::string(u.release) : std::string("unknown");
}

std::string landlockMissing(int e) {
    if (e == -ENOSYS) {
        // ENOSYS from a kernel that HAS the call means something in between
        // answered for it — a container's or a supervisor's seccomp filter
        std::string rel = kernelRelease();
        int major = 0, minor = 0;
        std::sscanf(rel.c_str(), "%d.%d", &major, &minor);
        if (major > 5 || (major == 5 && minor >= 13))
            return "Landlock is hidden from this process: the kernel (" + rel + ") has it, but a "
                   "seccomp filter around rakupp, such as a container's, answers for it";
        return "this kernel (" + rel + ") has no Landlock; it needs Linux 5.13 or later";
    }
    if (e == -EOPNOTSUPP)
        return "Landlock is built into this kernel but not enabled (it has to be in the lsm= boot parameter)";
    if (e == -EPERM)
        return "Landlock is refused here — a container's own seccomp profile can do that";
    return std::string("Landlock is not available: ") + std::strerror(-e);
}

#if defined(__x86_64__)
#define SBX_AUDIT_ARCH AUDIT_ARCH_X86_64
#elif defined(__aarch64__)
#define SBX_AUDIT_ARCH AUDIT_ARCH_AARCH64
#elif defined(__riscv) && __riscv_xlen == 64
#ifndef AUDIT_ARCH_RISCV64
#define AUDIT_ARCH_RISCV64 (243 | __AUDIT_ARCH_64BIT | __AUDIT_ARCH_LE)
#endif
#define SBX_AUDIT_ARCH AUDIT_ARCH_RISCV64
#endif

// Newer syscalls share one number on every architecture; old headers lack them.
#ifndef __NR_clone3
#define __NR_clone3 435
#endif
#ifndef __NR_pidfd_getfd
#define __NR_pidfd_getfd 438
#endif
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#define __NR_io_uring_enter 426
#define __NR_io_uring_register 427
#endif
#ifndef __NR_open_tree
#define __NR_open_tree 428
#define __NR_move_mount 429
#define __NR_fsopen 430
#define __NR_fsconfig 431
#define __NR_fsmount 432
#define __NR_fspick 433
#endif
#ifndef __NR_mount_setattr
#define __NR_mount_setattr 442
#endif
#ifndef __NR_fchmodat2
#define __NR_fchmodat2 452
#endif
#ifndef __NR_setxattrat
#define __NR_setxattrat 463
#define __NR_removexattrat 466
#endif

std::string installSeccomp() {
#ifndef SBX_AUDIT_ARCH
    return "no seccomp filter is written for this CPU architecture";
#else
    std::vector<sock_filter> f;
    auto stmt = [&](uint16_t code, uint32_t k) { f.push_back(sock_filter{code, 0, 0, k}); };
    auto jeq = [&](uint32_t k, uint8_t jt, uint8_t jf) { f.push_back(sock_filter{BPF_JMP | BPF_JEQ | BPF_K, jt, jf, k}); };
    const uint32_t kAllow = SECCOMP_RET_ALLOW;
    const uint32_t kEperm = SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA);
    const uint32_t kEnosys = SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA);
    auto ldNr = [&] { stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)); };
    // the low 32 bits of an argument (every architecture here is little-endian)
    auto ldArg = [&](int i) { stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args) + 8 * i); };

    // Another architecture's syscall table numbers everything differently.
    stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch));
    jeq(SBX_AUDIT_ARCH, 1, 0);
    stmt(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
#if defined(__x86_64__)
    ldNr();                                           // …and so does x32's
    f.push_back(sock_filter{BPF_JMP | BPF_JGE | BPF_K, 0, 1, 0x40000000u});
    stmt(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
#endif
    // clone: a thread (CLONE_THREAD) yes, a process no
    ldNr(); jeq(__NR_clone, 0, 4);
    ldArg(0);
    f.push_back(sock_filter{BPF_JMP | BPF_JSET | BPF_K, 0, 1, 0x00010000u /*CLONE_THREAD*/});
    stmt(BPF_RET | BPF_K, kAllow);
    stmt(BPF_RET | BPF_K, kEperm);
    // clone3 hides its flags behind a pointer; ENOSYS makes the C library
    // fall back to clone for its threads
    ldNr(); jeq(__NR_clone3, 0, 1);
    stmt(BPF_RET | BPF_K, kEnosys);
    // signals to this process only
    const uint32_t self = (uint32_t)::getpid();
    for (long nr : {(long)__NR_kill, (long)__NR_tgkill, (long)__NR_rt_sigqueueinfo, (long)__NR_rt_tgsigqueueinfo}) {
        ldNr(); jeq((uint32_t)nr, 0, 4);
        ldArg(0); jeq(self, 0, 1);
        stmt(BPF_RET | BPF_K, kAllow);
        stmt(BPF_RET | BPF_K, kEperm);
    }
    // a terminal's input queue: TIOCSTI types into the shell that started us
    ldNr(); jeq(__NR_ioctl, 0, 5);
    ldArg(1); jeq(0x5412 /*TIOCSTI*/, 2, 0); jeq(0x541C /*TIOCLINUX*/, 1, 0);
    stmt(BPF_RET | BPF_K, kAllow);
    stmt(BPF_RET | BPF_K, kEperm);

    // Refused outright: starting programs, changing a file's metadata, sockets
    // of any family (Landlock's TCP rules exist only from ABI 4, and cover
    // neither UDP nor UNIX sockets), reaching into other processes, and the
    // kernel's administrative surface. io_uring is here because its operations
    // do not pass through this filter.
    static const long kDeny[] = {
        __NR_execve, __NR_execveat,
#ifdef __NR_fork
        __NR_fork,
#endif
#ifdef __NR_vfork
        __NR_vfork,
#endif
        __NR_socket, __NR_connect, __NR_bind, __NR_listen, __NR_accept, __NR_accept4,
        __NR_ptrace, __NR_process_vm_readv, __NR_process_vm_writev, __NR_pidfd_getfd, __NR_kcmp,
        __NR_tkill,
        __NR_mount, __NR_umount2, __NR_pivot_root, __NR_chroot, __NR_setns, __NR_unshare,
        __NR_open_tree, __NR_move_mount, __NR_fsopen, __NR_fsconfig, __NR_fsmount, __NR_fspick,
        __NR_mount_setattr,
        __NR_reboot, __NR_kexec_load,
#ifdef __NR_kexec_file_load
        __NR_kexec_file_load,
#endif
        __NR_init_module, __NR_finit_module, __NR_delete_module,
        __NR_bpf, __NR_perf_event_open, __NR_userfaultfd,
        __NR_keyctl, __NR_add_key, __NR_request_key,
        __NR_swapon, __NR_swapoff, __NR_acct, __NR_quotactl,
        __NR_open_by_handle_at, __NR_name_to_handle_at, __NR_fanotify_init,
        __NR_io_uring_setup, __NR_io_uring_enter, __NR_io_uring_register,
        // what Landlock leaves alone: a file's mode, owner, extended
        // attributes and times are not among its access rights
        __NR_fchmod, __NR_fchmodat, __NR_fchmodat2, __NR_fchown, __NR_fchownat,
        __NR_setxattr, __NR_lsetxattr, __NR_fsetxattr, __NR_setxattrat,
        __NR_removexattr, __NR_lremovexattr, __NR_fremovexattr, __NR_removexattrat,
        __NR_utimensat,
#ifdef __NR_chmod
        __NR_chmod,
#endif
#ifdef __NR_chown
        __NR_chown, __NR_lchown,
#endif
#ifdef __NR_utime
        __NR_utime,
#endif
#ifdef __NR_utimes
        __NR_utimes,
#endif
#ifdef __NR_futimesat
        __NR_futimesat,
#endif
#ifdef __NR_ioperm
        __NR_ioperm,
#endif
#ifdef __NR_iopl
        __NR_iopl,
#endif
    };
    ldNr();
    for (long nr : kDeny) {
        jeq((uint32_t)nr, 0, 1);
        stmt(BPF_RET | BPF_K, kEperm);
    }
    stmt(BPF_RET | BPF_K, kAllow);

    sock_fprog prog{(unsigned short)f.size(), f.data()};
    if (::syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &prog) == 0) return "";
    if (::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) == 0) return "";
    return std::string("a seccomp filter could not be installed (") + std::strerror(errno) +
           "): the kernel may lack CONFIG_SECCOMP_FILTER, or a container forbids it";
#endif
}

}  // namespace

std::string sandboxOsDescribe() {
    int abi = landlockAbi();
    if (abi < 1) return "none — " + landlockMissing(abi);
#ifndef SBX_AUDIT_ARCH
    return "none — no seccomp filter is written for this CPU architecture";
#else
    if (::prctl(PR_GET_SECCOMP) < 0) return "none — this kernel has no seccomp";
    return "Landlock (ABI " + std::to_string(abi) + ") + seccomp";
#endif
}

std::string sandboxOsEnter(const std::vector<std::string>& readable) {
    int abi = landlockAbi();
    if (abi < 1) return landlockMissing(abi);
    // Every access right this ABI knows is HANDLED, so anything no rule grants
    // is refused; the rules below grant reading and nothing else.
    LlRuleset rs{};
    rs.fs = (1ull << 13) - 1;                      // ABI 1: execute … make_sym
    if (abi >= 2) rs.fs |= 1ull << 13;             // refer
    if (abi >= 3) rs.fs |= 1ull << 14;             // truncate
    if (abi >= 5) rs.fs |= 1ull << 15;             // ioctl_dev
    if (abi >= 4) rs.net = 3;                      // bind_tcp, connect_tcp
    if (abi >= 6) rs.scoped = 3;                   // abstract UNIX sockets, signals
    size_t rsSize = abi >= 6 ? 24 : abi >= 4 ? 16 : 8;
    int rfd = (int)::syscall(__NR_landlock_create_ruleset, &rs, rsSize, 0);
    if (rfd < 0) return std::string("Landlock refused the ruleset: ") + std::strerror(errno);

    std::vector<std::string> all = readable;
    for (const char* p : {"/dev/null", "/dev/zero", "/dev/random", "/dev/urandom",
                          "/etc/localtime", "/usr/share/zoneinfo", "/etc/ld.so.cache",
                          "/lib", "/lib64", "/usr/lib", "/usr/lib64", "/usr/local/lib"})
        all.push_back(p);
    for (auto& p : all) {
        std::string r = resolved(p);
        if (r.empty()) continue;
        int pfd = ::open(r.c_str(), O_PATH | O_CLOEXEC);
        if (pfd < 0) continue;
        LlPathBeneath pb{isDir(r) ? (kLlReadFile | kLlReadDir) : kLlReadFile, pfd};
        long rc = ::syscall(__NR_landlock_add_rule, rfd, kLlRulePathBeneath, &pb, 0);
        int e = errno;
        ::close(pfd);
        if (rc < 0) { ::close(rfd); return "Landlock refused a rule for " + r + ": " + std::strerror(e); }
    }
    warmLibc();
    if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        ::close(rfd);
        return std::string("no_new_privs could not be set: ") + std::strerror(errno);
    }
    std::string why = installSeccomp();
    if (!why.empty()) { ::close(rfd); return why; }
    long rc = ::syscall(__NR_landlock_restrict_self, rfd, 0);
    int e = errno;
    ::close(rfd);
    if (rc < 0) return std::string("Landlock could not restrict the process: ") + std::strerror(e);
    return "";
}

#else
// ---------------------------------------------------------------- other ---

std::string sandboxOsDescribe() {
    return "none — rakupp confines a process with Seatbelt on macOS and with Landlock and seccomp on Linux";
}

std::string sandboxOsEnter(const std::vector<std::string>&) {
    return "this platform has no OS sandbox rakupp can use (it uses Seatbelt on macOS, "
           "Landlock and seccomp on Linux)";
}

#endif

}  // namespace rakupp
