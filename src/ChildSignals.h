#pragma once
// fork() for a child that is about to exec, with the signal state a fresh
// program expects. POSIX only.
//
// The engine ignores SIGPIPE process-wide (Runtime.cpp, EmbedApi.cpp,
// JupyterKernel.cpp) so that a write to a closed socket is an EPIPE we can
// raise rather than a death. SIG_IGN survives execve, though, so every child of
// `run`, `shell`, `qx` and Proc::Async inherited it and got EPIPE where it
// expected to be killed: `run(<yes>, :out)` closed early left `yes` alive to
// print "yes: stdout: Broken pipe" and exit 1, where Rakudo's child dies of
// SIGPIPE silently. Rakudo (libuv) resets every disposition and the mask in the
// child; this resets the ones that are ours to reset.
//
// The order is libuv's. Every signal is blocked in the forking thread across
// fork(), so no handler of ours (the signal() Supply's self-pipe writer, the
// echo restorer) can run in the child between fork and exec — the child shares
// the parent's descriptors, and a SIGINT it caught there would reach the
// parent's Supply a second time. The child then puts SIGPIPE, and any signal
// that has a handler (exec would reset those anyway; this only closes the
// window), back to SIG_DFL, and only then clears the mask: a child is never
// started with signals blocked, whatever mask the forking thread had. Any
// other disposition we inherited as SIG_IGN, such as nohup's SIGHUP, is passed
// on as it came. The parent gets its own mask back as soon as fork() returns.
//
// Everything after fork() in the child is async-signal-safe (sigaction,
// sigprocmask), as it must be in a multithreaded process.
#if !defined(_WIN32)
#include <signal.h>
#include <pthread.h>
#include <unistd.h>

namespace rakupp {

inline void childSignalsForExec() {
    for (int sig = 1; sig < NSIG; sig++) {
        if (sig == SIGKILL || sig == SIGSTOP) continue;
        struct sigaction cur;
        if (::sigaction(sig, nullptr, &cur) != 0) continue;   // not a settable signal here
        bool handled = (cur.sa_flags & SA_SIGINFO) ? cur.sa_sigaction != nullptr
                                                   : (cur.sa_handler != SIG_DFL && cur.sa_handler != SIG_IGN);
        if (!handled && !(sig == SIGPIPE && cur.sa_handler == SIG_IGN)) continue;
        struct sigaction dfl{};
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        ::sigaction(sig, &dfl, nullptr);
    }
    sigset_t none;
    sigemptyset(&none);
    ::sigprocmask(SIG_SETMASK, &none, nullptr);
}

// fork(); in the child (0 returned), the signal state is already the one to
// exec with — see above. The parent, or a failed fork, has its mask restored.
inline pid_t forkForExec() {
    sigset_t all, old;
    sigfillset(&all);
    ::pthread_sigmask(SIG_BLOCK, &all, &old);
    pid_t pid = ::fork();
    if (pid == 0) childSignalsForExec();
    else ::pthread_sigmask(SIG_SETMASK, &old, nullptr);
    return pid;
}

} // namespace rakupp
#endif
