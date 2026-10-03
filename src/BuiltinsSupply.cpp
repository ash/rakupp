// BuiltinsSupply.cpp — supplies (taps, delivery, timers, signals, async sockets) and the --exe built-in natives (rtB*)
//
// One of the parts BuiltinsParts.h lists; what they share is declared there.
#include "BuiltinsParts.h"
#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#elif defined(__linux__)
#include <sys/inotify.h>
#endif

namespace rakupp {

// ---------------- real supply wiring (on-demand supplies, async sockets) ----
// The tap-driven model that a live Cro server needs: `supply {…}` returns an
// on-demand Supply holding its block; tapping it runs the block with `emit`
// routed to the tap's callback and `whenever` wiring inner taps that stay live
// after the block returns (I/O workers push through them later). The legacy
// eager semantics survive as drainSupplyBlock for value-context consumers.

// A phaser block inside a supply/whenever body, as a callable closing over the
// body's definition scope (the phaser may run when the body never has).
static Value supplyPhaserCode(const Block* b, std::shared_ptr<Env> closure) {
    Value v; v.t = VT::Code; v.setCode(makePayload<Callable>());
    v.code()->body = &b->stmts; v.code()->isBlock = true; v.code()->closure = std::move(closure);
    return v;
}
// Collect LAST/QUIT/CLOSE phasers from a block's top-level statements.
void scanSupplyPhasers(const Value& blk, ValueList* lastP,
                              ValueList* quitP, ValueList* closeP,
                              std::shared_ptr<Env> phaserEnv) {
    if (blk.t != VT::Code || !blk.code() || !blk.code()->body) return;
    // phaserEnv (issue #18): a LAST phaser reads the block's parameters as of
    // the LAST invocation (`LAST { say "Done with $c" }`). The block's
    // DEFINITION closure never holds $c — callers that drain through a
    // param-mirroring shim pass the shim's env here instead.
    auto env = [&](const Value& b2) { return phaserEnv ? phaserEnv : b2.code()->closure; };
    for (auto& s : *blk.code()->body) {
        if (s->kind != NK::Block) continue;
        auto* b = static_cast<Block*>(s.get());
        if (lastP  && b->phaser == "LAST")  lastP->push_back(supplyPhaserCode(b, env(blk)));
        if (quitP  && b->phaser == "QUIT")  quitP->push_back(supplyPhaserCode(b, env(blk)));
        if (closeP && b->phaser == "CLOSE") closeP->push_back(supplyPhaserCode(b, env(blk)));
    }
}

void Interpreter::maybeFinishSupply(const std::shared_ptr<SupplyTapCtx>& ctx) {
    if (!ctx || ctx->doneFired || ctx->done) return;
    if (!ctx->blockDone || ctx->pending > 0) return;
    ctx->doneFired = true;
    if (ctx->doneCb.t == VT::Code) { ValueList na; try { callCallable(ctx->doneCb, na); } catch (...) {} }
    closeTapHandle(ctx->tap);
}

// One delivery into a supply activation (S-53). It runs now if nothing is
// running there, and otherwise joins the queue for whoever is running — this
// thread's own body, or another thread's, since a `start` block emitting into
// a Supplier delivers on its own thread. Deciding which, and queueing, happen
// under ctx->m; see drainSupplyQueue for the other half.
Value Interpreter::supplyDelivery(const std::shared_ptr<SupplyTapCtx>& ctx, long long sub,
                                  std::function<void(Interpreter&, ValueList&)> fn) {
    return ctxCallable(ctx, [ctx, sub, fn](Interpreter& I2, ValueList& args) -> Value {
        // (queued, it runs on whichever thread drains the queue: that thread
        // holds the activation, so the entry leaves `running` alone)
        Interpreter* ip = &I2;
        auto deferred = [&]() -> SupplyTapCtx::Deferred {
            auto saved = makePayload<ValueList>(args);
            return {sub, [ip, ctx, fn, saved] {
                if (ctx->done) return;
                ip->tctx_.tapStack.push_back(ctx);
                struct G { Interpreter* i; ~G() { i->tctx_.tapStack.pop_back(); } } g{ip};
                fn(*ip, *saved);
            }};
        };
        bool now;
        {
            std::lock_guard<std::mutex> lk(ctx->m);
            if (ctx->done || (sub && ctx->closedSubs.count(sub))) return Value::any();
            if (ctx->running > 0) { ctx->queue.push_back(deferred()); return Value::any(); }
            ctx->running++;                  // the activation is this thread's now
            // …but older deliveries still waiting go first: this one joins the
            // back of the line (a holder that died on an exception left them)
            now = ctx->queue.empty();
            if (!now) ctx->queue.push_back(deferred());
        }
        if (now) {
            try { fn(I2, args); }
            catch (...) { std::lock_guard<std::mutex> lk(ctx->m); ctx->running--; throw; }
        }
        I2.drainSupplyQueue(ctx);
        return Value::any();
    });
}

int Interpreter::runQuitPhasers(const ValueList& quitP, const Value& ex, Value& replacement) {
    bool consumed = false;
    for (auto& q : quitP) {
        if (q.t != VT::Code || !q.code() || !q.code()->body) continue;
        // The phaser's statements run HERE rather than through callCallable: a
        // matching `when` reports itself by unwinding, and a callable boundary
        // absorbs that signal as the block's return value. CATCH reads its own
        // clauses the same way. `$_` and `$!` are the exception, as in a CATCH.
        auto scope = std::make_shared<Env>();
        scope->parent = q.code()->closure ? q.code()->closure : tctx_.cur;
        auto savedEnv = tctx_.cur;
        uint64_t savedGF = tctx_.curGivenFrame;
        tctx_.cur = scope;
        tctx_.curGivenFrame = ExecContext::kNoFrame;
        struct R {
            Interpreter& I; std::shared_ptr<Env> e; uint64_t f;
            ~R() { I.tctx_.cur = e; I.tctx_.curGivenFrame = f; }
        } r{*this, savedEnv, savedGF};
        scope->define("$_", ex);
        scope->define("$!", ex);
        try { for (auto& st : *q.code()->body) exec(st.get()); }
        catch (BreakGivenEx&) { consumed = true; }   // a when/default matched: the quit is handled
        // …and a `done` inside the phaser has ALREADY ended the supply: the quit
        // is handled by definition, and must not also reach the tapper's quit
        // handler (Roast syntax.t's `QUIT { when … { emit …; done } }`).
        catch (DoneEx&) { return 0; }
        catch (ResumeEx&) {
            // a quit is long past the throw point, so it cannot be resumed —
            // the attempt itself becomes what the tapper is told about
            replacement = exceptionFor(RakuError{Value::typeObj("X::AdHoc"),
                                                 "Cannot resume a Supply quit", RakuError::NoCapture{}});
            return 2;
        }
        catch (RakuError& e2) { replacement = exceptionFor(e2); return 2; } // the phaser threw: that is the quit now
        catch (...) {}
    }
    return consumed ? 0 : 1;
}

// S-53. Called by the thread that holds the activation (its body has just
// returned, `running` still counting it): hand over what the whenevers'
// sources delivered while it ran, in arrival order, skipping anything whose
// subscription has since been closed — by `last`, or by its own source
// completing — then release the activation. The release happens in the same
// critical section that finds the queue empty. It used to follow it: the
// holder decremented `running` and then looked at the queue, so a delivery
// from another thread that saw `running` still set and queued in between was
// left there for ever (syntax.t test 53 waited on it), and the unlocked
// vector tore under concurrent emits. An explicit `done` empties the queue:
// nothing follows it.
void Interpreter::drainSupplyQueue(const std::shared_ptr<SupplyTapCtx>& ctx) {
    if (!ctx) return;
    for (;;) {
        std::function<void()> run;
        {
            std::lock_guard<std::mutex> lk(ctx->m);
            if (ctx->done) ctx->queue.clear();
            while (!run && !ctx->queue.empty()) {
                auto d = std::move(ctx->queue.front());
                ctx->queue.erase(ctx->queue.begin());
                if (!(d.sub && ctx->closedSubs.count(d.sub))) run = std::move(d.run);
            }
            if (!run) { ctx->running--; return; }
        }
        try { run(); }
        catch (...) { std::lock_guard<std::mutex> lk(ctx->m); ctx->running--; throw; }
    }
}

void Interpreter::closeTapHandle(const std::shared_ptr<TapHandle>& h) {
    if (!h) return;
    std::vector<std::function<void()>> closers;
    ValueList phasers;
    {
        std::lock_guard<std::mutex> lk(h->m);
        if (h->closed) return;
        h->closed = true;
        closers.swap(h->closers);
        phasers.swap(h->closePhasers);
    }
    for (auto& f : closers) { try { f(); } catch (...) {} }
    // S-59: several CLOSE phasers run in REVERSE order of declaration — the
    // innermost setup is torn down first, as with LEAVE.
    for (auto it = phasers.rbegin(); it != phasers.rend(); ++it)
        if (it->t == VT::Code) { ValueList na; try { callCallable(*it, na); } catch (...) {} }
}

Value Interpreter::drainSupplyBlock(const Value& s) {
    // Legacy eager semantics: run the block now, collecting emits; a die becomes
    // the supply's QUIT reason. Emits route to the collector via the tap stack.
    Value blk = (s.t == VT::Hash && s.hash()->count("block")) ? (*s.hash())["block"] : Value::nil();
    ValueList vals; bool quit = false; Value quitReason; std::string quitMsg;
    auto ctx = std::make_shared<SupplyTapCtx>();
    ctx->collect = &vals;
    // a `die` inside one of the block's WHENEVERs quits the supply too: the
    // delivery reports it through quitCb, which records it here
    auto lateQuit = std::make_shared<std::pair<bool, Value>>(false, Value());
    {
        Value qcb; qcb.t = VT::Code; qcb.setCode(makePayload<Callable>());
        qcb.code()->builtin = [lateQuit](Interpreter&, ValueList& a) -> Value {
            if (!lateQuit->first) { lateQuit->first = true; lateQuit->second = a.empty() ? Value::any() : a[0]; }
            return Value::any();
        };
        ctx->quitCb = qcb;
    }
    tctx_.tapStack.push_back(ctx);
    try {
        // S-53 holds here too: the body runs first, then what its whenevers'
        // sources delivered while it ran (the drain releases the activation).
        { std::lock_guard<std::mutex> lk(ctx->m); ctx->running++; }
        ctx->inBody = true;
        if (blk.t == VT::Code) {
            ValueList na;
            try { callCallable(blk, na); }
            catch (...) { { std::lock_guard<std::mutex> lk(ctx->m); ctx->running--; } ctx->inBody = false; throw; }
        }
        ctx->inBody = false;
        drainSupplyQueue(ctx);
    }
    catch (RakuError& e) { quit = true; quitReason = exceptionFor(e); quitMsg = e.message; }
    catch (DoneEx&) { ctx->clearQueue(); } // `done` in the body: normal end of the stream
    catch (...) { tctx_.tapStack.pop_back(); throw; }
    tctx_.tapStack.pop_back();
    // A whenever on a still-pending Promise holds the supply open (Cro's connector
    // `establish` awaits connect() inside the supply block). The eager drain must
    // wait for those one-shots to fire before treating the supply as finished —
    // but only while a worker exists that could still settle them, so a
    // never-kept promise doesn't hang `.list`.
    if (ctx->pending > 0 && parallelMode_) {
        // parallel mode: the one-shots fire on their own threads — just wait
        // for them (bounded by "somebody could still settle them"), or the
        // drain returns before a promise-whenever delivered (cro-core's
        // drain-waits test caught this the day parallel became the default)
        // (…and not past a `done`/quit: an endless source — Supply.interval —
        // stays "pending" for ever, and a cued job elsewhere keeps a worker live)
        // (sleepYield: a GIL this thread holds is released while it waits, or
        // an interval worker that needs it to deliver could never finish)
        while (ctx->pending > 0 && !ctx->done && !lateQuit->first && liveWorkers_.load() > 0)
            sleepYield(0.001);
    }
    else if (ctx->pending > 0 && gilHeld_ && !parallelMode_ && liveWorkers_.load() > 0) {
        static thread_local ExecContext parked;
        saveCtx(parked);
        gilYieldNotify();
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            gilLock();
            if (ctx->pending <= 0 || ctx->done || lateQuit->first || liveWorkers_.load() == 0) break;
            gilYieldNotify();
        }
        loadCtx(parked);
    }
    ctx->collect = nullptr; // vals is about to go out of scope with this frame
    {
        auto closers = std::move(ctx->closers); // on-close callbacks registered during the drain
        for (auto& cb : closers) if (cb.t == VT::Code) { try { ValueList na; callCallable(cb, na); } catch (...) {} }
    }
    if (!quit && lateQuit->first) {
        quit = true; quitReason = lateQuit->second;
        ValueList none;
        try { quitMsg = methodCall(quitReason, "message", none).toStr(); } catch (...) { quitMsg = quitReason.toStr(); }
    }
    Value out = Value::makeHash(); out.hashKind = "Supply";
    Value v = Value::array(); *v.arr() = std::move(vals); (*out.hash())["values"] = v;
    if (quit) { (*out.hash())["quit-reason"] = quitReason; (*out.hash())["quit-message"] = Value::str(quitMsg); }
    return out;
}

// An address for an async socket: IPv4 or IPv6 (`::1`), numeric or by name.
// "0.0.0.0" / "" binds every IPv4 interface, as it always has.
static bool asyncSockAddr(const std::string& hostIn, int port, sockaddr_storage& ss, socklen_t& len) {
    std::memset(&ss, 0, sizeof(ss));
    std::string host = hostIn == "localhost" ? "127.0.0.1" : hostIn;
    if (host.empty() || host == "0.0.0.0") {
        auto* a4 = (sockaddr_in*)&ss; a4->sin_family = AF_INET; a4->sin_port = htons((uint16_t)port);
        a4->sin_addr.s_addr = INADDR_ANY; len = sizeof(sockaddr_in); return true;
    }
    addrinfo hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    std::memcpy(&ss, res->ai_addr, res->ai_addrlen); len = (socklen_t)res->ai_addrlen;
    freeaddrinfo(res);
    if (ss.ss_family == AF_INET) ((sockaddr_in*)&ss)->sin_port = htons((uint16_t)port);
    else if (ss.ss_family == AF_INET6) ((sockaddr_in6*)&ss)->sin6_port = htons((uint16_t)port);
    return true;
}
bool asyncSockAddrFwd(const std::string& h, int p, sockaddr_storage& ss, socklen_t& len) {
    return asyncSockAddr(h, p, ss, len);
}
// The host and port of a socket address, IPv4 or IPv6.
static void asyncSockName(const sockaddr_storage& ss, std::string& host, long long& port) {
    char buf[INET6_ADDRSTRLEN] = {0};
    if (ss.ss_family == AF_INET6) {
        auto* a6 = (const sockaddr_in6*)&ss;
        inet_ntop(AF_INET6, &a6->sin6_addr, buf, sizeof(buf)); port = ntohs(a6->sin6_port);
    } else {
        auto* a4 = (const sockaddr_in*)&ss;
        inet_ntop(AF_INET, &a4->sin_addr, buf, sizeof(buf)); port = ntohs(a4->sin_port);
    }
    host = buf;
}
// Build an IO::Socket::Async connection value around a connected fd.
Value makeAsyncSocket(int fd) {
    Value s = Value::makeHash(); s.hashKind = "AsyncSocket";
    (*s.hash())["fd"] = Value::integer(fd);
    sockaddr_storage a{}; socklen_t alen = sizeof(a);
    std::string h; long long p = 0;
    if (::getsockname(fd, (sockaddr*)&a, &alen) == 0) {
        asyncSockName(a, h, p);
        (*s.hash())["socket-host"] = Value::str(h);
        (*s.hash())["socket-port"] = Value::integer(p);
    }
    alen = sizeof(a);
    if (::getpeername(fd, (sockaddr*)&a, &alen) == 0) {
        asyncSockName(a, h, p);
        (*s.hash())["peer-host"] = Value::str(h);
        (*s.hash())["peer-port"] = Value::integer(p);
    }
    return s;
}

// ---- signal(SIGINT, …) : OS signals delivered as a Supply --------------------
// Self-pipe trick: the async-signal-safe handler just writes the signum to a
// pipe; a single dispatcher worker reads the pipe and fans each signal out to
// the registered taps, running their whenever block under the GIL. This needs
// no per-signal thread and stays robust with worker threads around.
struct SignalTapRec {
    Value emit, done;                     // whenever block + done callback
    std::shared_ptr<ReactCtx> react;      // react ctx (so `done` closes it), or null
    std::shared_ptr<TapHandle> handle;    // closed => skip
};
#if !defined(_WIN32)
static int g_sigPipe[2] = {-1, -1};
static std::mutex g_sigTapMutex;
static std::multimap<int, std::shared_ptr<SignalTapRec>> g_sigTaps;
static std::set<int> g_sigInstalled;      // signals whose handler is installed
static void rakuppSignalHandler(int sig) {
    if (g_sigPipe[1] >= 0) { unsigned char c = (unsigned char)sig; ssize_t r = ::write(g_sigPipe[1], &c, 1); (void)r; }
}
#endif

// drainWorkers' wake-up for the signal dispatcher: it blocks in read() on the
// self-pipe where workerAbort_ is invisible, so without this it held the whole
// 2 s shutdown grace (a GUI window's close button felt seconds slow). A 0 byte
// — no real signal is 0 — tells it to unwind.
void Interpreter::wakeSignalWorker() {
#if !defined(_WIN32)
    if (g_sigPipe[1] >= 0) { unsigned char z = 0; ssize_t r = ::write(g_sigPipe[1], &z, 1); (void)r; }
#endif
}

// Signal number → its enum name ("SIGINT"), or "" if unknown.
static std::string signalNameOfNumber(int sig);
static const std::map<std::string, int>& signalNameMap();
const std::map<std::string, int>& signalNameMapFwd() { return signalNameMap(); }
// Build the Signal enum value passed to a whenever block ($_ / $sig).
static Value makeSignalEnumValue(int sig) {
    std::string name = signalNameOfNumber(sig);
    Value v = name.empty() ? Value::integer(sig) : Value::enumVal(name, sig);
    v.enumType = "Signal";
    return v;
}
Value makeSignalEnumValueFwd(int sig) { return makeSignalEnumValue(sig); }
// The Signal-enum names available on THIS platform. Each is `#ifdef`-guarded:
// Windows' <signal.h> defines only a handful (SIGINT/SIGILL/SIGFPE/SIGSEGV/
// SIGTERM/SIGABRT/SIGBREAK), so the rest are simply absent there.
static const std::map<std::string, int>& signalNameMap() {
    static const std::map<std::string, int> m = {
#ifdef SIGHUP
        {"SIGHUP", SIGHUP},
#endif
#ifdef SIGINT
        {"SIGINT", SIGINT},
#endif
#ifdef SIGQUIT
        {"SIGQUIT", SIGQUIT},
#endif
#ifdef SIGILL
        {"SIGILL", SIGILL},
#endif
#ifdef SIGTRAP
        {"SIGTRAP", SIGTRAP},
#endif
#ifdef SIGABRT
        {"SIGABRT", SIGABRT},
#endif
#ifdef SIGFPE
        {"SIGFPE", SIGFPE},
#endif
#ifdef SIGKILL
        {"SIGKILL", SIGKILL},
#endif
#ifdef SIGBUS
        {"SIGBUS", SIGBUS},
#endif
#ifdef SIGSEGV
        {"SIGSEGV", SIGSEGV},
#endif
#ifdef SIGSYS
        {"SIGSYS", SIGSYS},
#endif
#ifdef SIGPIPE
        {"SIGPIPE", SIGPIPE},
#endif
#ifdef SIGALRM
        {"SIGALRM", SIGALRM},
#endif
#ifdef SIGTERM
        {"SIGTERM", SIGTERM},
#endif
#ifdef SIGURG
        {"SIGURG", SIGURG},
#endif
#ifdef SIGSTOP
        {"SIGSTOP", SIGSTOP},
#endif
#ifdef SIGTSTP
        {"SIGTSTP", SIGTSTP},
#endif
#ifdef SIGCONT
        {"SIGCONT", SIGCONT},
#endif
#ifdef SIGCHLD
        {"SIGCHLD", SIGCHLD},
#endif
#ifdef SIGTTIN
        {"SIGTTIN", SIGTTIN},
#endif
#ifdef SIGTTOU
        {"SIGTTOU", SIGTTOU},
#endif
#ifdef SIGUSR1
        {"SIGUSR1", SIGUSR1},
#endif
#ifdef SIGUSR2
        {"SIGUSR2", SIGUSR2},
#endif
#ifdef SIGWINCH
        {"SIGWINCH", SIGWINCH},
#endif
#ifdef SIGBREAK
        {"SIGBREAK", SIGBREAK}, // Windows-only
#endif
    };
    return m;
}
int signalNumberOfName(const std::string& n) {
    auto it = signalNameMap().find(n);
    return it != signalNameMap().end() ? it->second : -1;
}
// Every Signal-enum name this build knows, for `Signal.WHO` and
// `Signal.^enum_value_list`. Without them the stash was empty, `Signal.WHO<SIGPIPE>`
// resolved to Any and numified to 0, and the `sigpipe` pragma quietly called
// signal(0, 0) instead of restoring SIGPIPE — a no-op nobody could see.
std::vector<std::pair<std::string, int>> signalNamesAndNumbers() {
    std::vector<std::pair<std::string, int>> out;
    for (auto& kv : signalNameMap()) out.emplace_back(kv.first, kv.second);
    return out;
}
static std::string signalNameOfNumber(int sig) {
    for (auto& kv : signalNameMap()) if (kv.second == sig) return kv.first;
    return "";
}

// `whenever Promise.in(N) { … }` in a react: fire the block ONCE after a real N-second
// delay, as a live react source — a worker sleeps (GIL released, so sibling whenevers'
// I/O runs meanwhile), then runs the block and drops the source. Without this the
// timer resolved immediately (a timeout fired at t=0, defeating the guard).
// `whenever Promise.in(N)` inside a supply {} block: a real timer. Holds the
// supply open (pending) and fires the block after N seconds — unless the supply
// closes first, in which case the worker cancels early (so it can't delay exit).
Value Interpreter::spawnSupplyTimer(double secs, Value blk, std::shared_ptr<SupplyTapCtx> ctx) {
    engageGil();
    ctx->pending++;
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    if (secs < 0) secs = 0;
    // one body at a time (S-53): the timer's body waits its turn behind a
    // whenever running on another thread, as every other delivery does
    Value fireW = supplyDelivery(ctx, 0, [blk, ctx](Interpreter& I2, ValueList&) {
        // shutdown mid-delay: release the pending hold, but never run the block
        if (!I2.workerAbort_.load(std::memory_order_relaxed) && !ctx->done && !ctx->doneFired) {
            ValueList one{Value::boolean(true)};
            try { I2.callCallable(blk, one); }
            catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
            catch (RakuError& e) {
                // S-57, as in the `whenever` Promise arm: a body that dies ends the
                // supply and reaches the TAPPER's quit handler. Without this the
                // throw escaped the worker's catch(...) below and the pending hold
                // was never released, so the supply hung instead of quitting.
                Value ex = I2.exceptionFor(e);
                if (ctx->quitCb.t == VT::Code) {
                    ValueList qa{ex};
                    try { I2.callCallable(ctx->quitCb, qa); } catch (...) {}
                }
                ctx->done = true;
                if (ctx->tap) I2.closeTapHandle(ctx->tap);
            }
        }
        ctx->pending--;
        I2.maybeFinishSupply(ctx);
    });
    // its place among this activation's timers: by deadline, then by creation
    auto end = std::chrono::steady_clock::now() +
               std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                   std::chrono::duration<double>(std::isfinite(secs) && secs < 1e9 ? secs : 1e9));
    std::pair<double, long long> slot;
    {
        std::lock_guard<std::mutex> lk(ctx->timerM);
        slot = {std::chrono::duration<double>(end.time_since_epoch()).count(), ++ctx->timerSeq};
        ctx->timerOrder.insert(slot);
    }
    throttleSpawn();
    addWorker(BigStackThread([self, end, slot, fireW, fin, spawnScope, ctx]() mutable {
        t_poll.isWorker = true;
        auto stopped = [&] { return ctx->done || ctx->doneFired || self->workerAbort_.load(std::memory_order_relaxed); };
        // GIL not held; slices against a fixed deadline (drift-free, huge/Inf-safe)
        // and wakes early on `done`/`.close` or shutdown.
        while (!stopped()) {
            auto now = std::chrono::steady_clock::now();
            if (now >= end) break;
            double left = std::chrono::duration<double>(end - now).count();
            std::this_thread::sleep_for(std::chrono::duration<double>(left < 0.05 ? left : 0.05));
        }
        // a timer due no later than this one, made before it, fires first
        while (!stopped()) {
            { std::lock_guard<std::mutex> lk(ctx->timerM);
              if (ctx->timerOrder.empty() || *ctx->timerOrder.begin() == slot) break; }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        self->gilLock();
        ExecContext wctx; self->loadCtx(wctx);
        self->tctx_.cur = spawnScope;
        self->tctx_.dynStack.push_back(spawnScope.get());
        ValueList none; try { self->callCallable(fireW, none); } catch (...) {}
        { std::lock_guard<std::mutex> lk(ctx->timerM); ctx->timerOrder.erase(slot); }   // the next one's turn
        self->gilYieldNotify();
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
}

// `Supply.interval(1).map({…})` — a kind-based live supply keeps the combinators
// it was given as a transform CHAIN on the Supply value itself: unlike a
// Supplier-backed supply there is no tap record to hang them on, because the
// values come from a worker, not from a fan-out over registered taps. So the
// chain is applied here, by wrapping the consumer: each value goes through
// map/grep/head/… before the block or emit callback sees it.
//
// A transform that dies is the SOURCE failing, not the consumer, so it QUITS
// this subscription: the block's QUIT phasers get the exception and the
// subscription ends. A die in the block BODY is a different thing entirely and
// must not be caught here (it kills the react — see spawnIntervalWhenever).
Value Interpreter::wrapSupplyChain(const Value& supply, Value consumer) {
    if (consumer.t != VT::Code) return consumer;
    if (!(supply.t == VT::Hash && supply.hash() && supply.hash()->count("chain"))) return consumer;
    // this subscription's OWN copy of the chain, each step with fresh state
    // (head/skip/unique count per subscription, exactly as in tapSupply)
    auto rec = std::make_shared<Value>(Value::makeHash());
    Value chain = Value::array();
    for (auto& step : *supply.hash()->at("chain").arr()) {
        Value s2 = Value::makeHash(); *s2.hash() = *step.hash();
        { Value st0 = Value::makeHash();
          (*st0.hash())["t0"] = Value::number(epochNowSecs());   // when this subscription began
          (*s2.hash())["state"] = st0; }
        chain.arr()->push_back(s2);
    }
    (*rec->hash())["chain"] = chain;
    ValueList quitP;
    scanSupplyPhasers(consumer, nullptr, &quitP, nullptr);
    Value w; w.t = VT::Code; w.setCode(makePayload<Callable>());
    w.code()->builtin = [rec, consumer, quitP](Interpreter& I, ValueList& a) -> Value {
        Value in = a.empty() ? Value::any() : a[0];
        bool complete = false;
        ValueList outs;
        try { outs = I.applyTapChain(*rec, in, complete); }
        catch (RakuError& e) {
            if (quitP.empty()) throw;   // nothing handles the quit: it leaves the react
            Value ex = I.exceptionFor(e);
            for (auto& q : quitP) { ValueList one{ex}; I.callCallable(q, one); }
            throw LastEx{};             // handled: this subscription is over
        }
        Value last = Value::any();
        for (auto& o : outs) { ValueList one{o}; last = I.callCallable(consumer, one); }
        if (complete) throw LastEx{};   // head/first reached its limit
        return last;
    };
    return w;
}

// `whenever Supply.interval(N)` inside a supply {…} block: a repeating ticker
// that holds the activation open (pending) and fires each tick under it, so the
// body's emits reach the downstream tap. Stops on `done`, on the activation's
// tap closing (.tap.close — the CLOSE-phaser stress in S17 syntax.t spawns and
// closes thousands of these), or at interpreter shutdown.
// `whenever $channel { … }` inside a `supply {}` block — the values sent to the
// channel, one run each, completing when the channel closes.
//
// The drain loop is spawnChannelWhenever's: polled and popped under the
// channel's own stripe, the same lock send/receive take, because an unlocked
// read of queue/closed tore under RAKUPP_PARALLEL. The WIRING is
// spawnSupplyInterval's: a live source holds the activation open with
// ctx->pending and fires through ctxCallable so the body's emits reach the
// downstream tap, then releases the hold so the supply can finish.
void Interpreter::runLastPhasers(const ValueList& lastP, std::shared_ptr<ReactCtx> rctx) {
    if (lastP.empty()) return;
    if (!rctx && !reactStack_.empty()) rctx = reactStack_.back();
    // `done` looks for its react on reactStack_, and these phasers run on the
    // source's WORKER, where nothing had pushed it — so `done` inside a LAST
    // quietly did nothing and the react waited for its other sources. Push it
    // for the duration, the way the QUIT path already does.
    const bool pushed = rctx && (reactStack_.empty() || reactStack_.back() != rctx);
    if (pushed) reactStack_.push_back(rctx);
    for (auto& p : lastP) {
        ValueList na;
        try { callCallable(p, na); }
        catch (DoneEx&) {
            if (rctx) {
                std::lock_guard<std::mutex> lk(rctx->m);
                rctx->closed = true;
                rctx->cv.notify_all();
            }
        }
        catch (...) {}
    }
    if (pushed && !reactStack_.empty()) reactStack_.pop_back();
}

Value Interpreter::spawnSupplyChannel(Value chan, Value blk, std::shared_ptr<SupplyTapCtx> ctx) {
    engageGil();
    // counted on the channel, so `.close` can let this reader drain first
    auto readerDelta = [](Value& c, long long d) {
        if (!c.hash()) return;
        std::lock_guard<std::recursive_mutex> lk(Interpreter::atomicStripe(c.hash()));
        auto& n = (*c.hash())["supplyReaders"];
        n = Value::integer((n.t == VT::Int ? n.toInt() : 0) + d);
    };
    readerDelta(chan, 1);
    ctx->pending++;
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    ValueList lastP, quitP;
    scanSupplyPhasers(blk, &lastP, &quitP, nullptr);
    // one body at a time (S-53): each value waits its turn behind a whenever
    // running on another thread, as every other delivery does
    Value fireW = supplyDelivery(ctx, 0, [blk, ctx, quitP](Interpreter& I2, ValueList& args) {
        if (ctx->done || ctx->doneFired) return;
        ValueList one = args;
        try { I2.callCallable(blk, one); }
        catch (NextEx&) {} catch (LastEx&) { ctx->done = true; } catch (DoneEx&) { ctx->done = true; }
        catch (RakuError& e) {
            // same rule as every other whenever arm: the body's own QUIT phasers
            // see it first, otherwise it goes downstream and closes the activation
            Value ex = I2.exceptionFor(e);
            bool handled = false;
            for (auto& q : quitP) { ValueList o2{ex}; try { I2.callCallable(q, o2); handled = true; } catch (...) {} }
            if (!handled && ctx->quitCb.t == VT::Code) { ValueList o2{ex}; try { I2.callCallable(ctx->quitCb, o2); } catch (...) {} }
            ctx->done = true;
            if (ctx->tap) I2.closeTapHandle(ctx->tap);
        }
    });
    throttleSpawn();
    addWorker(BigStackThread([self, chan, fireW, lastP, ctx, fin, spawnScope, readerDelta]() mutable {
        t_poll.isWorker = true;
        // Parallel mode has no GIL discipline; under the GIL this holds the lock
        // and yieldToWorkerFor cycles it (see spawnChannelWhenever — taking it
        // and never releasing made the first channel worker the accidental owner).
        if (!self->parallelMode_) self->gilLock();
        ExecContext wctx; self->loadCtx(wctx);
        tctx_.cur = spawnScope;
        tctx_.dynStack.push_back(spawnScope.get());
        auto stop = [&] {
            if (self->workerAbort_.load(std::memory_order_relaxed)) return true;
            if (ctx->done || ctx->doneFired) return true;
            if (ctx->tap) { std::lock_guard<std::mutex> lk(ctx->tap->m); if (ctx->tap->closed) return true; }
            return false;
        };
        for (;;) {
            if (stop()) break;
            Value v; bool got = false, drained = false;
            {   std::lock_guard<std::recursive_mutex> lk(Interpreter::atomicStripe(chan.hash()));
                auto qi = chan.hash() ? chan.hash()->find("queue") : ValueMap::iterator{};
                ValueList* q = chan.hash() && qi != chan.hash()->end() && qi->second.arr()
                             ? qi->second.arr() : nullptr;
                if (!q) drained = true;
                else if (!q->empty()) { v = q->front(); q->erase(q->begin()); got = true; }
                else {
                    auto ci = chan.hash()->find("closed");
                    drained = ci != chan.hash()->end() && ci->second.truthy();
                }
            }
            if (drained) break;
            if (!got) {
                if (self->parallelMode_) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                else self->yieldToWorkerFor(0.02);
                continue;
            }
            ValueList one{v};
            try { self->callCallable(fireW, one); } catch (...) {}
        }
        // the channel closed (or the activation went away): LAST phasers, then
        // release the hold so an otherwise-finished supply can complete
        for (auto& ph : lastP) { ValueList na; try { self->callCallable(ph, na); } catch (...) {} }
        ctx->pending--;
        try { self->maybeFinishSupply(ctx); } catch (...) {}
        readerDelta(chan, -1);
        if (!self->parallelMode_) self->gilYieldNotify(); // unlocks the GIL — parallel never took it
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
}

Value Interpreter::spawnSupplyInterval(double interval, double delay, Value blk,
                                       std::shared_ptr<SupplyTapCtx> ctx) {
    engageGil();
    ctx->pending++;
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    if (interval < 0.001) interval = 0.001; // Rakudo clamps a zero/negative interval
    if (delay < 0) delay = 0;
    auto tick = std::make_shared<long long>(0);
    ValueList quitP;
    scanSupplyPhasers(blk, nullptr, &quitP, nullptr);
    Value fireW = supplyDelivery(ctx, ++ctx->subSeq, [blk, ctx, tick, quitP](Interpreter& I2, ValueList&) {
        if (!ctx->done && !ctx->doneFired) {
            ValueList one{Value::integer((*tick)++)};
            try { I2.callCallable(blk, one); }
            // `done` in the tick body ends the supply: the ticker stops too
            catch (NextEx&) {} catch (LastEx&) { ctx->done = true; } catch (DoneEx&) { ctx->done = true; }
            catch (RakuError& e) {
                // a die in the tick body QUITS the enclosing supply (same rule as
                // the generic whenever path): its own QUIT phasers see it first,
                // otherwise it goes downstream and closes the activation. The
                // worker used to swallow it, so the failure vanished and the
                // ticker kept running.
                Value ex = I2.exceptionFor(e);
                bool handled = false;
                for (auto& q : quitP) { ValueList o2{ex}; try { I2.callCallable(q, o2); handled = true; } catch (...) {} }
                if (!handled && ctx->quitCb.t == VT::Code) { ValueList o2{ex}; try { I2.callCallable(ctx->quitCb, o2); } catch (...) {} }
                ctx->done = true;
                if (ctx->tap) I2.closeTapHandle(ctx->tap);
            }
        }
    });
    throttleSpawn();
    addWorker(BigStackThread([self, interval, delay, fireW, ctx, fin, spawnScope]() mutable {
        t_poll.isWorker = true;
        auto stop = [&] {
            if (self->workerAbort_.load(std::memory_order_relaxed)) return true;
            if (ctx->done || ctx->doneFired) return true;
            if (ctx->tap) { std::lock_guard<std::mutex> lk(ctx->tap->m); if (ctx->tap->closed) return true; }
            return false;
        };
        auto sleepChunked = [&](double secs) { // GIL not held; wakes early on teardown
            double left = secs;
            while (left > 0 && !stop()) {
                double c = left < 0.25 ? left : 0.25;
                std::this_thread::sleep_for(std::chrono::duration<double>(c));
                left -= c;
            }
        };
        sleepChunked(delay);
        while (!stop()) {
            self->gilLock();
            ExecContext wctx; self->loadCtx(wctx);
            self->tctx_.cur = spawnScope;
            self->tctx_.dynStack.push_back(spawnScope.get());
            if (!stop()) { ValueList none; try { self->callCallable(fireW, none); } catch (...) {} }
            self->gilYieldNotify();
            if (stop()) break;
            sleepChunked(interval);
        }
        self->gilLock(); // release the activation hold and let the supply finish
        ExecContext wctx2; self->loadCtx(wctx2);
        self->tctx_.cur = spawnScope;
        self->tctx_.dynStack.push_back(spawnScope.get());
        ctx->pending--;
        try { self->maybeFinishSupply(ctx); } catch (...) {}
        self->gilYieldNotify();
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
}

// Run a NATIVE continuation on a worker after a real delay — the `.then` of a
// timer promise (`Promise.in(3).then({…})` ran its block at t=0 before). The
// same sliced wait as spawnTimerWhenever: shutdown wakes it within ~50 ms, and
// an aborted worker never runs the continuation.
void Interpreter::spawnDelayedNative(double secs, std::function<void()> fn) {
    engageGil();
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    if (secs < 0) secs = 0;
    throttleSpawn();
    addWorker(BigStackThread([self, secs, fn, fin, spawnScope]() mutable {
        t_poll.isWorker = true;
        bool stopped = false;                                          // GIL not held
        auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(secs);
        for (;;) {
            if (self->workerAbort_.load(std::memory_order_relaxed)) { stopped = true; break; }
            auto now = std::chrono::steady_clock::now();
            if (now >= end) break;
            double left = std::chrono::duration<double>(end - now).count();
            std::this_thread::sleep_for(std::chrono::duration<double>(left < 0.05 ? left : 0.05));
        }
        self->gilLock();
        ExecContext wctx; self->loadCtx(wctx);
        tctx_.cur = spawnScope;
        tctx_.dynStack.push_back(spawnScope.get());
        if (!stopped) { try { fn(); } catch (...) {} }
        self->gilYieldNotify();
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
}

Value Interpreter::spawnTimerWhenever(double secs, Value blk, std::shared_ptr<ReactCtx> ctx) {
    engageGil();
    if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++; }
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    if (secs < 0) secs = 0;
    throttleSpawn();
    addWorker(BigStackThread([self, secs, blk, ctx, fin, spawnScope]() mutable {
        t_poll.isWorker = true;
        // The full delay is honored (issue #41 capped it at 35 s) — slept in
        // slices against a fixed deadline so shutdown or `done` wakes the worker
        // within ~50 ms, and in double-rep time so a huge/Inf timer can't
        // overflow sleep_for's int64 nanosecond range.
        bool stopped = false;                                          // GIL not held
        auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(secs);
        for (;;) {
            if (self->workerAbort_.load(std::memory_order_relaxed) || (ctx && ctx->closed)) { stopped = true; break; }
            auto now = std::chrono::steady_clock::now();
            if (now >= end) break;
            double left = std::chrono::duration<double>(end - now).count();
            std::this_thread::sleep_for(std::chrono::duration<double>(left < 0.05 ? left : 0.05));
        }
        self->gilLock();
        ExecContext wctx; self->loadCtx(wctx);
        tctx_.cur = spawnScope;
        tctx_.dynStack.push_back(spawnScope.get());
        if (ctx) self->reactStack_.push_back(ctx);
        if (!stopped && !(ctx && ctx->closed)) {
            // S-60: a timer Promise is kept with True, and the whenever body
            // receives that result — it used to be called with no argument, so
            // `$_` was Any.
            ValueList one{Value::boolean(true)};
            try { self->callCallable(blk, one); }
            catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
            catch (RakuError& e) {
                // a body that dies ends the react and rethrows at the `react`,
                // the same way a broken source Promise does. Ahead of catch(...),
                // which used to swallow it and leave the react waiting.
                Value ex = self->exceptionFor(e);
                if (ctx) {
                    std::lock_guard<std::mutex> lk(ctx->m);
                    if (!ctx->quitFlag) { ctx->quitFlag = true; ctx->quitErr = ex; }
                    ctx->closed = true; ctx->cv.notify_all();
                }
            }
            catch (...) {}
        }
        if (ctx) self->reactStack_.pop_back();
        if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
        self->gilYieldNotify();
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
}

// `Supply.interval($interval, $delay)` tapped: a worker emits ascending Ints —
// the first after $delay (0 = immediately, like Rakudo), then one per $interval,
// forever. Sleeps in small chunks so `done` (react ctx closed) or `.close` (tap
// handle) tears the worker down promptly instead of after a whole interval.
Value Interpreter::spawnIntervalWhenever(double interval, double delay, Value blk,
                                         std::shared_ptr<ReactCtx> ctx,
                                         std::shared_ptr<TapHandle> handle,
                                         Value doneCb) {
    engageGil();
    if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++; }
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    if (interval < 0.001) interval = 0.001; // Rakudo clamps a zero/negative interval
    if (delay < 0) delay = 0;
    throttleSpawn();
    addWorker(BigStackThread([self, interval, delay, blk, ctx, handle, fin, spawnScope, doneCb]() mutable {
        t_poll.isWorker = true;
        auto closedNow = [&] {
            if (self->workerAbort_.load(std::memory_order_relaxed)) return true; // mainline done: stop ticking
            if (ctx && ctx->closed) return true;
            if (handle) { std::lock_guard<std::mutex> lk(handle->m); if (handle->closed) return true; }
            return false;
        };
        auto sleepChunked = [&](double secs) { // GIL not held; wakes early on teardown
            double left = secs;
            while (left > 0 && !closedNow()) {
                double c = left < 0.25 ? left : 0.25;
                std::this_thread::sleep_for(std::chrono::duration<double>(c));
                left -= c;
            }
        };
        sleepChunked(delay);
        long long tick = 0;
        bool lastEx = false;
        while (!closedNow() && !lastEx) {
            self->gilLock();
            ExecContext wctx; self->loadCtx(wctx);
            tctx_.cur = spawnScope;
            tctx_.dynStack.push_back(spawnScope.get());
            if (ctx) self->reactStack_.push_back(ctx);
            if (!closedNow()) {
                ValueList one{Value::integer(tick++)};
                // the tick is passed as an ARGUMENT, and a block that takes none
                // refuses it (`Supply.interval(1).tap(-> { … })` dies "Too many
                // positionals", as in Rakudo); a bare block's implicit `$_` takes it
                try { self->callCallable(blk, one, nullptr, /*ownFrame=*/false, /*arityCheck=*/true); }
                catch (NextEx&) {}
                catch (LastEx&) { lastEx = true; } // `last` ends THIS subscription
                catch (DoneEx&) {} // `done` closed the ctx in its bookkeeping
                catch (RakuError& e) {
                    // a die in the whenever BODY kills the whole react and
                    // propagates (issue #18 — the reference output shows QUIT
                    // phasers do NOT catch a body die; they are for the
                    // source's own quit). Without this the swallowed die left
                    // the ticker running and the react waiting forever.
                    if (ctx) {
                        std::lock_guard<std::mutex> lk(ctx->m);
                        if (!ctx->quitFlag) { ctx->quitFlag = true; ctx->quitErr = e.payload.t == VT::Nil ? Value::str(e.message) : e.payload; }
                        ctx->closed = true; ctx->cv.notify_all();
                    } else {
                        // …and with no react to carry it, an unhandled death in a
                        // timer's tap block is the program's death: there is no
                        // caller left to hand it to (Roast interval.t asserts the
                        // process exits non-zero with the message on stderr).
                        std::cerr << self->renderError(e, self->btStyleForStderr());
                        std::cerr.flush();
                        std::_Exit(1);
                    }
                    lastEx = true;
                }
                catch (...) {}
            }
            if (ctx) self->reactStack_.pop_back();
            self->gilYieldNotify();
            if (closedNow() || lastEx) break;
            sleepChunked(interval);
        }
        // S-17: a ticker never completes on its own — but a chain on top of it
        // does (`interval(…).head(3)` is over after three), and that is what
        // `lastEx` records. Tell the tapper, so a consumer waiting for done
        // stops waiting. Closing the tap is not a completion and says nothing.
        if (lastEx && doneCb.t == VT::Code) {
            self->gilLock();
            ExecContext dctx; self->loadCtx(dctx);
            tctx_.cur = spawnScope;
            tctx_.dynStack.push_back(spawnScope.get());
            { ValueList na; try { self->callCallable(doneCb, na); } catch (...) {} }
            self->gilYieldNotify();
        }
        if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
    Value t = Value::makeHash(); t.hashKind = "Tap";
    if (handle) { t.extM() = handle; (*t.hash())["wired"] = Value::boolean(true); } // .close stops the ticker
    return t;
}

// `whenever $channel { … }` — a react source that runs the block once per value
// the channel receives, and completes when the channel closes. A worker does the
// waiting so the main thread's react loop stays free; without this the block ran
// ONCE, with the channel object itself as the topic.
Value Interpreter::spawnChannelWhenever(Value chan, Value blk, std::shared_ptr<ReactCtx> ctx) {
    engageGil();
    if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); ctx->liveSources++; }
    liveWorkers_++;
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    throttleSpawn();
    addWorker(BigStackThread([self, chan, blk, ctx, fin, spawnScope]() mutable {
        t_poll.isWorker = true;
        // Parallel mode has no GIL discipline: taking (and never releasing)
        // the lock here made the FIRST channel worker the accidental GIL
        // owner for its whole lifetime — every later channel whenever's
        // worker starved at this line (the two-channel react in
        // S17-supply/syntax.t, the last of the P5 isolation livelocks).
        // Under the GIL the lock is real and yieldToWorkerFor cycles it.
        if (!self->parallelMode_) self->gilLock();
        ExecContext wctx; self->loadCtx(wctx);
        tctx_.cur = spawnScope;
        tctx_.dynStack.push_back(spawnScope.get());
        if (ctx) self->reactStack_.push_back(ctx);
        for (;;) {
            if (ctx && ctx->closed) break;
            // Poll and POP under the channel's stripe — the same lock send and
            // receive use. The old loop read "closed"/"queue" and erased the
            // front element with NO lock, racing send's striped push_back and
            // close's map insert; under RAKUPP_PARALLEL a torn read made this
            // worker break early or miss a value, wedging every construct
            // downstream of the whenever (S17-supply/syntax.t's two-channel
            // react — the last isolation livelock of the P5 wall).
            Value v, failCause; bool got = false, fin = false, failed = false;
            {   std::lock_guard<std::recursive_mutex> lk(Interpreter::atomicStripe(chan.hash()));
                auto qi = chan.hash() ? chan.hash()->find("queue") : ValueMap::iterator{};
                ValueList* q = chan.hash() && qi != chan.hash()->end() && qi->second.arr() ? qi->second.arr() : nullptr;
                if (!q) fin = true;
                else if (!q->empty()) { v = q->front(); q->erase(q->begin()); got = true; }
                else {
                    auto ci = chan.hash()->find("closed");
                    fin = ci != chan.hash()->end() && ci->second.truthy();
                    // S-60: a FAILED channel is a quit, not a quiet end — the
                    // cause reaches the whenever's QUIT phasers, or the react.
                    if (fin) {
                        auto fi = chan.hash()->find("failCause");
                        if (fi != chan.hash()->end()) { failCause = fi->second; failed = true; }
                    }
                }
            }
            if (failed) {
                Value ex = causeException(*self, failCause, failCause.toStr());
                ValueList quitP;
                scanSupplyPhasers(blk, nullptr, &quitP, nullptr);
                if (!quitP.empty())
                    for (auto& q : quitP) { ValueList one{ex}; try { self->callCallable(q, one); } catch (...) {} }
                else if (ctx) {
                    std::lock_guard<std::mutex> lk(ctx->m);
                    if (!ctx->quitFlag) { ctx->quitFlag = true; ctx->quitErr = ex; }
                    ctx->closed = true; ctx->cv.notify_all();
                }
                break;
            }
            if (fin) break;
            if (!got) {
                // yieldToWorkerFor is a no-op in parallel mode (nothing to
                // yield) — without the sleep this loop was a hot spin
                if (self->parallelMode_) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                else self->yieldToWorkerFor(0.02);
                continue;
            }
            ValueList one{v};
            try { self->callCallable(blk, one); }
            catch (NextEx&) {} catch (LastEx&) { break; } catch (DoneEx&) { break; }
            catch (RakuError& e) {
                // a die in the whenever body kills the react and propagates
                // (issue-18 semantics, same as the interval arm) — the old
                // silent catch also swallowed real errors from the block,
                // which made this exact spot undebuggable
                if (ctx) {
                    std::lock_guard<std::mutex> lk(ctx->m);
                    if (!ctx->quitFlag) { ctx->quitFlag = true; ctx->quitErr = e.payload.t == VT::Nil ? Value::str(e.message) : e.payload; }
                    ctx->closed = true; ctx->cv.notify_all();
                }
                break;
            }
            catch (...) {}
            if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); if (ctx->closed) break; }
        }
        if (ctx) self->reactStack_.pop_back();
        if (ctx) { std::lock_guard<std::mutex> lk(ctx->m); if (ctx->liveSources > 0) ctx->liveSources--; ctx->cv.notify_all(); }
        if (!self->parallelMode_) self->gilYieldNotify(); // unlocks the GIL — parallel never took it
        self->liveWorkers_--;
        fin->store(true, std::memory_order_release);
    }), fin);
    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
}

Value Interpreter::tapSignal(const std::vector<int>& sigs, Value emitCb, Value doneCb,
                             std::shared_ptr<ReactCtx> reactCtx) {
    engageGil();
    auto handle = std::make_shared<TapHandle>();
#if defined(_WIN32)
    // Windows: the POSIX self-pipe + sigaction machinery isn't available, and
    // <signal.h> exposes only a handful of signals. Return a non-emitting tap
    // (Ctrl-C falls to the default handler) so `signal()` still type-checks and
    // programs that merely construct the Supply keep working.
    (void)sigs; (void)emitCb; (void)doneCb; (void)reactCtx;
    Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
    (*t.hash())["wired"] = Value::boolean(true);
    return t;
#else
    auto rec = std::make_shared<SignalTapRec>();
    rec->emit = emitCb; rec->done = doneCb; rec->react = reactCtx; rec->handle = handle;

    // Lazily create the self-pipe + dispatcher worker exactly once.
    static std::once_flag pipeOnce;
    Interpreter* self = this;
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    std::call_once(pipeOnce, [&] {
        if (::pipe(g_sigPipe) != 0) { g_sigPipe[0] = g_sigPipe[1] = -1; return; }
        liveWorkers_++;
        auto fin = std::make_shared<std::atomic<bool>>(false);
        throttleSpawn();
        addWorker(BigStackThread([self, spawnScope, fin]() mutable {
            t_poll.isWorker = true;
            for (;;) {
                unsigned char c;
                ssize_t n = ::read(g_sigPipe[0], &c, 1);        // GIL not held
                if (n <= 0) break;
                if (c == 0) break;                              // drainWorkers' quit byte
                int sig = c;
                std::vector<std::shared_ptr<SignalTapRec>> taps;
                {
                    std::lock_guard<std::mutex> lk(g_sigTapMutex);
                    auto range = g_sigTaps.equal_range(sig);
                    for (auto it = range.first; it != range.second; ++it) taps.push_back(it->second);
                }
                if (taps.empty()) continue;
                self->gilLock();
                ExecContext wctx; self->loadCtx(wctx);
                tctx_.cur = spawnScope;
                tctx_.dynStack.push_back(spawnScope.get());
                for (auto& t : taps) {
                    if (t->handle && t->handle->closed) continue;
                    // push the react ctx so a `done` inside the block closes it
                    if (t->react) self->reactStack_.push_back(t->react);
                    if (t->emit.t == VT::Code) {
                        Value sv = makeSignalEnumValue(sig);
                        ValueList one{sv};
                        try { self->callCallable(t->emit, one); }
                        catch (RakuError& e) { fprintf(stderr, "===WARNING=== signal handler died: %s\n", e.message.c_str()); }
                        catch (...) {}
                    }
                    if (t->react) self->reactStack_.pop_back();
                }
                self->gilYieldNotify();
            }
            // Shutdown (quit byte or pipe EOF): hand every registered tap's
            // react source back, or a react parked on `whenever signal(...)`
            // outlives us into drainWorkers' 2 s grace — the pause between a
            // GUI window's close button and the process actually exiting.
            {
                std::set<std::shared_ptr<SignalTapRec>> taps;   // dedup: one rec may serve several signals
                {
                    std::lock_guard<std::mutex> lk(g_sigTapMutex);
                    for (auto& kv : g_sigTaps) taps.insert(kv.second);
                    g_sigTaps.clear();
                }
                for (auto& t : taps) {
                    if (t->handle) { std::lock_guard<std::mutex> lk(t->handle->m); t->handle->closed = true; }
                    if (t->react) {
                        std::lock_guard<std::mutex> lk(t->react->m);
                        if (t->react->liveSources > 0) t->react->liveSources--;
                        t->react->cv.notify_all();
                    }
                }
            }
            self->liveWorkers_--;
            fin->store(true, std::memory_order_release);
        }), fin);
    });

    // Register the tap and install a handler for each signal (once each).
    {
        std::lock_guard<std::mutex> lk(g_sigTapMutex);
        for (int sig : sigs) {
            g_sigTaps.insert({sig, rec});
            if (g_sigInstalled.insert(sig).second) {
                struct sigaction sa{}; sa.sa_handler = rakuppSignalHandler;
                sigemptyset(&sa.sa_mask); sa.sa_flags = SA_RESTART;
                ::sigaction(sig, &sa, nullptr);
            }
        }
    }
    // closing the tap unregisters it (the handler stays; the dispatcher skips it)
    std::vector<int> sigsCopy = sigs;
    handle->closers.push_back([rec, sigsCopy] {
        std::lock_guard<std::mutex> lk(g_sigTapMutex);
        for (int sig : sigsCopy) {
            auto range = g_sigTaps.equal_range(sig);
            for (auto it = range.first; it != range.second; )
                it = (it->second == rec) ? g_sigTaps.erase(it) : std::next(it);
        }
    });
    // When wired into a react block, arrange for the tap to be torn down when the
    // block ends (via `done` or all sources completing) — otherwise the signal
    // dispatcher keeps this tap live and re-fires the handler on the next signal.
    if (reactCtx) {
        std::lock_guard<std::mutex> lk(reactCtx->m);
        reactCtx->extTaps.push_back(handle);
    }
    Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
    (*t.hash())["wired"] = Value::boolean(true);
    return t;
#endif // !_WIN32
}

// S-09. The kept events are {kind, val} records: an emit runs through the
// tap's own transform chain, a done or a quit ends the tap there and now. The
// store is emptied FIRST, so a handler that taps again during the replay does
// not see the same events twice.
void Interpreter::replayPreserved(const Value& sup, Value& tapRec) {
    if (!(sup.t == VT::Hash && sup.hash() && tapRec.hash())) return;
    auto SH = sup.hash();
    if (!(SH->count("preserving") && (*SH)["preserving"].truthy())) return;
    auto bit = SH->find("buffer");
    if (bit == SH->end() || !bit->second.arr() || bit->second.arr()->empty()) return;
    ValueList evs = *bit->second.arr();
    bit->second.arr()->clear();
    auto TH = tapRec.hash();
    Value emitCb = TH->count("emit") ? (*TH)["emit"] : Value::nil();
    Value doneCb = TH->count("done") ? (*TH)["done"] : Value::nil();
    Value quitCb = TH->count("quit") ? (*TH)["quit"] : Value::nil();
    for (auto& ev : evs) {
        if (!(ev.t == VT::Hash && ev.hash())) continue;
        const std::string k = (*ev.hash())["kind"].toStr();
        if (k == "emit") {
            bool complete = false;
            ValueList outs = applyTapChain(tapRec, (*ev.hash())["val"], complete);
            if (emitCb.t == VT::Code)
                for (auto& o : outs) {
                    ValueList one{o};
                    try { callCallable(emitCb, one); }
                    catch (NextEx&) {}
                    catch (LastEx&) { complete = true; break; }
                    catch (DoneEx&) { complete = true; break; }
                }
            if (complete) { (*TH)["closed"] = Value::boolean(true); (*TH)["ended"] = Value::boolean(true); return; }
        } else if (k == "done") {
            (*TH)["ended"] = Value::boolean(true);
            if (doneCb.t == VT::Code) { ValueList none; callCallable(doneCb, none); }
            return;
        } else if (k == "quit") {
            (*TH)["ended"] = Value::boolean(true);
            Value ex = (*ev.hash())["val"];
            if (quitCb.t == VT::Code) { ValueList one{ex}; callCallable(quitCb, one); }
            else {
                std::string qmsg = "quit";
                if (ex.t == VT::Object && ex.obj()) {
                    auto mit = ex.obj()->attrs.find("message");
                    if (mit != ex.obj()->attrs.end()) qmsg = mit->second.toStr();
                }
                throw RakuError{ex, qmsg};
            }
            return;
        }
    }
}
// The platform's own notifier, as Rakudo's libuv uses it: inotify on Linux (a
// file or a directory), FSEvents for a directory on macOS. The notifier's
// events wait in `pending` until the ticker takes them, so delivery rides the
// same supply machinery as every other source. Where none starts, the stat
// poller in watchFilter stands in.
struct NativeWatch {
    std::mutex m;
    std::vector<std::pair<std::string, bool>> pending;   // (entry name, renamed?)
    std::string real;                                    // the path as the notifier spells it
#if defined(__linux__)
    int fd = -1;
#elif defined(__APPLE__)
    void* stream = nullptr;
    dispatch_queue_t queue = nullptr;
    struct timespec startWall {};    // file ctimes are wall-clock
    struct timespec startMono {};
#endif
    ~NativeWatch();
    void take(std::vector<std::pair<std::string, bool>>& out);
};

#if defined(__APPLE__)
// CoreServices is opened at run time (as libssl and libffi are), so no link line
// learns a new framework. The declarations are FSEvents' C ABI.
namespace {
struct FSCtx { long version; void* info; void* retain; void* release; void* copyDescription; };
using FSCallback = void (*)(const void*, void*, size_t, void*, const uint32_t*, const uint64_t*);
struct FSApi {
    bool ok = false;
    void* (*CFStringCreateWithCString)(void*, const char*, uint32_t) = nullptr;
    void* (*CFArrayCreate)(void*, const void**, long, const void*) = nullptr;
    void (*CFRelease)(const void*) = nullptr;
    const void* typeArrayCallBacks = nullptr;
    void* (*Create)(void*, FSCallback, FSCtx*, void*, uint64_t, double, uint32_t) = nullptr;
    void (*SetDispatchQueue)(void*, dispatch_queue_t) = nullptr;
    unsigned char (*Start)(void*) = nullptr;
    void (*Stop)(void*) = nullptr;
    void (*Invalidate)(void*) = nullptr;
    void (*Release)(void*) = nullptr;
};
const FSApi& fsEventsApi() {
    static const FSApi api = [] {
        FSApi a;
        void* h = dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices", RTLD_LAZY);
        if (!h) return a;
        auto sym = [h](const char* n) { void* p = dlsym(h, n); return p ? p : dlsym(RTLD_DEFAULT, n); };
        a.CFStringCreateWithCString = reinterpret_cast<decltype(a.CFStringCreateWithCString)>(sym("CFStringCreateWithCString"));
        a.CFArrayCreate = reinterpret_cast<decltype(a.CFArrayCreate)>(sym("CFArrayCreate"));
        a.CFRelease = reinterpret_cast<decltype(a.CFRelease)>(sym("CFRelease"));
        a.typeArrayCallBacks = sym("kCFTypeArrayCallBacks");
        a.Create = reinterpret_cast<decltype(a.Create)>(sym("FSEventStreamCreate"));
        a.SetDispatchQueue = reinterpret_cast<decltype(a.SetDispatchQueue)>(sym("FSEventStreamSetDispatchQueue"));
        a.Start = reinterpret_cast<decltype(a.Start)>(sym("FSEventStreamStart"));
        a.Stop = reinterpret_cast<decltype(a.Stop)>(sym("FSEventStreamStop"));
        a.Invalidate = reinterpret_cast<decltype(a.Invalidate)>(sym("FSEventStreamInvalidate"));
        a.Release = reinterpret_cast<decltype(a.Release)>(sym("FSEventStreamRelease"));
        a.ok = a.CFStringCreateWithCString && a.CFArrayCreate && a.CFRelease && a.typeArrayCallBacks &&
               a.Create && a.SetDispatchQueue && a.Start && a.Stop && a.Invalidate && a.Release;
        return a;
    }();
    return api;
}
// libuv's reading of an FSEvents record (src/unix/fsevents.c): only the watched
// directory's own entries, never the directory itself; a create, remove or
// rename is a rename, and a modification of a file is a change.
void fsEventsCallback(const void*, void* info, size_t n, void* paths, const uint32_t* flags, const uint64_t*) {
    constexpr uint32_t kRenamed  = 0x100 | 0x200 | 0x800;                     // Created | Removed | Renamed
    constexpr uint32_t kModified = 0x400 | 0x1000 | 0x2000 | 0x4000 | 0x8000; // InodeMeta | Modified | FinderInfo | Owner | Xattr
    constexpr uint32_t kIsDir = 0x20000;
    auto* w = static_cast<NativeWatch*>(info);
    char** ps = static_cast<char**>(paths);
    for (size_t i = 0; i < n; i++) {
        std::string p = ps[i];
        const std::string& r = w->real;
        if (p.size() < r.size() || p.compare(0, r.size(), r) != 0) continue;
        if (p.size() > r.size() && p[r.size()] != '/') continue;
        std::string rest = p.substr(r.size());
        if (!rest.empty() && rest[0] == '/') rest.erase(0, 1);
        if (rest.empty() || rest.find('/') != std::string::npos) continue;
        // "Since now" still lets through writes made just before the stream
        // started that fseventsd had not logged yet — a directory copied and then
        // watched reported its own files as changed. Any change (write, chmod,
        // create, rename in) moves an entry's ctime, so in the first seconds an
        // entry whose ctime predates the watch is such a leftover.
        struct timespec nowMono;
        clock_gettime(CLOCK_MONOTONIC, &nowMono);
        struct stat st;
        if (nowMono.tv_sec - w->startMono.tv_sec < 3 && ::lstat(p.c_str(), &st) == 0 &&
            (st.st_ctimespec.tv_sec < w->startWall.tv_sec ||
             (st.st_ctimespec.tv_sec == w->startWall.tv_sec && st.st_ctimespec.tv_nsec < w->startWall.tv_nsec)))
            continue;
        bool renamed = true;
        if (!(flags[i] & kRenamed) && ((flags[i] & kModified) || !(flags[i] & kIsDir))) renamed = false;
        std::lock_guard<std::mutex> lk(w->m);
        w->pending.push_back({rest, renamed});
    }
}
}
#endif

NativeWatch::~NativeWatch() {
#if defined(__linux__)
    if (fd >= 0) ::close(fd);
#elif defined(__APPLE__)
    const FSApi& a = fsEventsApi();
    if (stream) { a.Stop(stream); a.Invalidate(stream); a.Release(stream); }
    if (queue) {
        dispatch_sync_f(queue, nullptr, [](void*) {});   // a callback still running finishes first
        dispatch_release(queue);
    }
#endif
}

void NativeWatch::take(std::vector<std::pair<std::string, bool>>& out) {
#if defined(__linux__)
    // libuv's inotify reading: an attribute or content change is a change,
    // anything else (create, delete, move, the watch itself going) a rename
    alignas(struct inotify_event) char buf[8192];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n <= 0) break;
        for (char* p = buf; p < buf + n;) {
            auto* e = reinterpret_cast<struct inotify_event*>(p);
            std::string name = e->len ? std::string(e->name) : std::string();
            out.push_back({name, (e->mask & ~(uint32_t)(IN_ATTRIB | IN_MODIFY)) != 0});
            p += sizeof(struct inotify_event) + e->len;
        }
    }
#endif
    std::lock_guard<std::mutex> lk(m);
    for (auto& e : pending) out.push_back(std::move(e));
    pending.clear();
}

static std::shared_ptr<NativeWatch> startNativeWatch(const std::string& path, bool isDir) {
#if defined(__linux__)
    (void)isDir;
    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) return nullptr;
    const uint32_t mask = IN_ATTRIB | IN_CREATE | IN_MODIFY | IN_DELETE | IN_DELETE_SELF |
                          IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO;
    if (inotify_add_watch(fd, path.c_str(), mask) < 0) { ::close(fd); return nullptr; }
    auto w = std::make_shared<NativeWatch>();
    w->fd = fd;
    return w;
#elif defined(__APPLE__)
    if (!isDir) return nullptr;   // a file is watched by the poller, which matches libuv's kqueue
    const FSApi& a = fsEventsApi();
    if (!a.ok) return nullptr;
    char rp[PATH_MAX];
    if (!::realpath(path.c_str(), rp)) return nullptr;
    auto w = std::make_shared<NativeWatch>();
    w->real = rp;
    void* s = a.CFStringCreateWithCString(nullptr, rp, 0x08000100 /* UTF-8 */);
    if (!s) return nullptr;
    const void* one[1] = {s};
    void* paths = a.CFArrayCreate(nullptr, one, 1, a.typeArrayCallBacks);
    clock_gettime(CLOCK_REALTIME, &w->startWall);
    clock_gettime(CLOCK_MONOTONIC, &w->startMono);
    FSCtx ctx{0, w.get(), nullptr, nullptr, nullptr};
    // as libuv asks: 50 ms latency, no deferral, one record per file
    if (paths) w->stream = a.Create(nullptr, fsEventsCallback, &ctx, paths,
                                    0xFFFFFFFFFFFFFFFFULL /* since now */, 0.05, 0x02 | 0x10);
    if (paths) a.CFRelease(paths);
    a.CFRelease(s);
    if (!w->stream) return nullptr;
    w->queue = dispatch_queue_create("rakupp.watch", DISPATCH_QUEUE_SERIAL);
    a.SetDispatchQueue(w->stream, w->queue);
    if (!a.Start(w->stream)) return nullptr;
    return w;
#else
    (void)path; (void)isDir;
    return nullptr;
#endif
}

// `$path.IO.watch` rides an interval ticker: this wraps the block a tick would
// run so that it runs once per IO::Notification::Change since the last look.
// A file reports itself; a directory reports its entries (not recursively).
double watchInterval(const Value& sup) {
    return sup.t == VT::Hash && sup.hash() && sup.hash()->count("dir") ? 0.05 : 0.01;
}
Value watchFilter(const Value& sup, const Value& blk) {
    using Sig = std::pair<long long, long long>;
    using Snap = std::map<std::string, Sig>;
    using Changes = std::vector<std::pair<std::string, bool>>;   // (entry name, renamed?)
    const bool isDir = sup.t == VT::Hash && sup.hash() && sup.hash()->count("dir");
    auto field = [&](const char* k) {
        return sup.t == VT::Hash && sup.hash() && sup.hash()->count(k) ? sup.hash()->at(k).toStr() : std::string();
    };
    const std::string path = field("path");
    std::string evPath = field("evpath");
    if (evPath.empty()) evPath = path;
    // Without a notifier: compare what a stat sees from one tick to the next.
    // Something appearing or disappearing is FileRenamed, a new size or mtime
    // FileChanged; a subdirectory counts only by presence, as its mtime moves
    // with contents that belong to a watch of their own.
    auto sig = [](const std::string& p, bool dirsByPresence) {
        struct stat st{};
        if (::stat(p.c_str(), &st) != 0) return Sig(-1, -1);
        if (dirsByPresence && S_ISDIR(st.st_mode)) return Sig(-2, -2);
#if defined(__APPLE__)
        long long mt = (long long)st.st_mtimespec.tv_sec * 1000000000LL + st.st_mtimespec.tv_nsec;
#elif defined(_WIN32)
        long long mt = (long long)st.st_mtime;
#else
        long long mt = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#endif
        return Sig((long long)st.st_size, mt);
    };
    auto snap = [path, isDir, sig]() {
        Snap s;
        if (!isDir) { s[""] = sig(path, false); return s; }
        std::error_code ec;
        for (std::filesystem::directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
            std::string name = it->path().filename().string();
            s[name] = sig(path + "/" + name, true);
        }
        return s;
    };
    std::shared_ptr<NativeWatch> native = startNativeWatch(path, isDir);
    auto last = std::make_shared<Snap>(native ? Snap() : snap());
    auto changesSince = [last, snap, native]() {
        Changes changes;
        if (native) { native->take(changes); return changes; }
        Snap now = snap();
        if (now == *last) return changes;
        // what went away first: a rename reports its old name, then its new one
        for (auto& [name, s] : *last)
            if (!now.count(name)) changes.push_back({name, true});
        for (auto& [name, s] : now) {
            auto it = last->find(name);
            if (it == last->end() || (it->second.first == -1) != (s.first == -1)) changes.push_back({name, true});
            else if (it->second != s) changes.push_back({name, false});
        }
        *last = std::move(now);
        return changes;
    };
    Value cb; cb.t = VT::Code; cb.setCode(makePayload<Callable>());
    cb.code()->builtin = [blk, evPath, changesSince](Interpreter& I2, ValueList&) -> Value {
        if (blk.t != VT::Code) return Value::any();
        Value r = Value::any();
        for (auto& [name, renamed] : changesSince()) {
            Value ev;
            coreEnumValue(renamed ? "FileRenamed" : "FileChanged", ev);
            Value pp = Value::pair("path", Value::str(name.empty() ? evPath : evPath + "/" + name));
            Value pe = Value::pair("event", ev);
            pp.namedArg = pe.namedArg = true;
            ValueList one{I2.methodCall(Value::typeObj("IO::Notification::Change"), "new", ValueList{pp, pe})};
            r = I2.callCallable(blk, one);
        }
        return r;
    };
    return cb;
}

Value Interpreter::tapSupply(const Value& s, Value emitCb, Value doneCb, Value quitCb) {
    if (!(s.t == VT::Hash && s.hashKind == "Supply" && s.hash())) {
        Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
    }
    auto& h = *s.hash();
    // a chain on a kind-based supply rides on the Supply itself (there is no tap
    // record to carry it), so apply it to the emit callback for every kind below
    if (h.count("chain") && !h.count("supplier") && !h.count("values"))
        emitCb = wrapSupplyChain(s, emitCb);
    // 1) on-demand block: run it now; whenevers inside wire inner taps that may
    //    outlive this call (fed by I/O workers).
    if (h.count("block")) {
        Value blk = h.at("block");
        auto handle = std::make_shared<TapHandle>();
        auto ctx = std::make_shared<SupplyTapCtx>();
        ctx->emitCb = emitCb; ctx->doneCb = doneCb; ctx->quitCb = quitCb; ctx->tap = handle;
        ValueList quitP;
        scanSupplyPhasers(blk, nullptr, &quitP, &handle->closePhasers);
        tctx_.tapStack.push_back(ctx);
        noCycleBreak_++;
        struct CBGuard { int& n; ~CBGuard() { n--; } } cbGuard{noCycleBreak_};
        try {
            { std::lock_guard<std::mutex> lk(ctx->m); ctx->running++; }
            ctx->inBody = true;
            if (blk.t == VT::Code) {
                ValueList na;
                try { callCallable(blk, na); }
                catch (...) { { std::lock_guard<std::mutex> lk(ctx->m); ctx->running--; } ctx->inBody = false; throw; }
            }
            ctx->inBody = false;
            tctx_.tapStack.pop_back();
            // S-53: the body has returned — now hand over what its whenevers'
            // sources delivered while it ran, and release the activation.
            tctx_.tapStack.push_back(ctx);
            try { drainSupplyQueue(ctx); } catch (...) { tctx_.tapStack.pop_back(); throw; }
            tctx_.tapStack.pop_back();
            // the block returned: with no live inner taps the supply is done
            ctx->blockDone = true;
            maybeFinishSupply(ctx);
        }
        catch (RakuError& e) {
            tctx_.tapStack.pop_back();
            Value ex = exceptionFor(e);
            bool handled = false;
            for (auto& q : quitP) { ValueList one{ex}; try { callCallable(q, one); handled = true; } catch (...) {} }
            if (!handled && quitCb.t == VT::Code) { ValueList one{ex}; try { callCallable(quitCb, one); handled = true; } catch (...) {} }
            closeTapHandle(handle);
            if (!handled) throw;
        }
        catch (DoneEx&) { // `done` in the supply body: normal end (its bookkeeping already ran)
            tctx_.tapStack.pop_back();
            ctx->blockDone = true;
            ctx->clearQueue();    // nothing follows an explicit done (S-54)
        }
        catch (...) { tctx_.tapStack.pop_back(); closeTapHandle(handle); throw; }
        Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
        (*t.hash())["wired"] = Value::boolean(true);
        return t;
    }
    // 2) live Supplier-backed supply: register a tap record; emit/done/quit fan out later
    if (h.count("supplier")) {
        Value tapRec = Value::makeHash();
        (*tapRec.hash())["emit"] = emitCb; (*tapRec.hash())["done"] = doneCb; (*tapRec.hash())["quit"] = quitCb;
        if (h.count("chain")) {
            Value chain = Value::array();
            for (auto& step : *h.at("chain").arr()) {
                Value s2 = Value::makeHash(); *s2.hash() = *step.hash();
                { Value st0 = Value::makeHash();
          (*st0.hash())["t0"] = Value::number(epochNowSecs());   // when this subscription began
          (*s2.hash())["state"] = st0; }
                chain.arr()->push_back(s2);
            }
            (*tapRec.hash())["chain"] = chain;
        }
        Value sup = h.at("supplier");
        // Supplier::Preserving: hand this fresh tap the events kept while nobody
        // was listening (Cro's request-into-$!in-before-connect pattern), and
        // empty the store — S-09's "resumes preserving for the next first tap".
        // Under the supplier's own stripe, so no live value overtakes the replay.
        if (sup.t == VT::Hash && sup.hash()->count("taps")) {
            std::lock_guard<std::recursive_mutex> regLk(supplierMutex(sup.hash()));
            (*sup.hash())["taps"].arr()->push_back(tapRec);
            replayPreserved(sup, tapRec);
        }
        // already-done supplier: fire done immediately so wiring completes
        if (sup.t == VT::Hash && sup.hash()->count("done_state") && (*sup.hash())["done_state"].truthy() &&
            !(tapRec.hash()->count("ended") && (*tapRec.hash())["ended"].truthy())) {
            (*tapRec.hash())["ended"] = Value::boolean(true);
            if (doneCb.t == VT::Code) { ValueList na; try { callCallable(doneCb, na); } catch (...) {} }
        }
        tapRec.hashKind = "Tap";
        return tapRec;
    }
    // 2b) Proc::Async stream — `whenever $proc.stdout.lines(:!chomp) {…}`
    // inside a supply block (TAP's parse-stream is exactly this shape): park
    // the record tap; runProcPromise feeds it when the process runs. This
    // used to fall through to the bottom SILENTLY, so the stream was never
    // captured and the block never fired.
    if (h.count("proc")) {
        registerProcStreamTap(s, emitCb, doneCb, quitCb);
        Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
    }
    // 3) async listen: bind now, accept on a worker; each connection is emitted
    //    (under the GIL) through emitCb.
    // signal Supply tapped inside a `supply {…}` block (no react ctx — the
    // block's own `done`/tapStack drives closure)
    if (h.count("kind") && h.at("kind").toStr() == "signal") {
        std::vector<int> sigs;
        if (h.count("signals") && h.at("signals").arr())
            for (auto& n : *h.at("signals").arr()) sigs.push_back((int)n.toInt());
        return tapSignal(sigs, emitCb, doneCb, nullptr);
    }
    // S-26/S-30: `flat` and `migrate` over a source that is not list-backed.
    //   flat    — every inner supply is subscribed AS IT ARRIVES and all of its
    //             values are emitted; an Iterable value is spread instead; done
    //             when the outer and every inner are done.
    //   migrate — each new inner REPLACES the previous one, whose subscription
    //             is closed; a value that is not a Supply is an error raised at
    //             the emitter (X::Supply::Migrate::Needs).
    if (h.count("kind") && (h.at("kind").toStr() == "flatten" || h.at("kind").toStr() == "migrate")) {
        const bool migrate = h.at("kind").toStr() == "migrate";
        Value src = h.at("src");
        auto handle = std::make_shared<TapHandle>();
        struct InnerState {
            int pending = 0;          // inner supplies not yet done
            bool outerDone = false;
            bool finished = false;
            Value current;            // migrate: the inner tap in force
        };
        auto st = std::make_shared<InnerState>();
        Interpreter* self = this;
        auto finish = [self, st, doneCb, handle]() {
            if (st->finished) return;
            st->finished = true;
            if (doneCb.t == VT::Code) { ValueList na; try { self->callCallable(doneCb, na); } catch (...) {} }
            self->closeTapHandle(handle);
        };
        auto maybeFinish = [st, finish]() { if (st->outerDone && st->pending == 0) finish(); };
        auto fail = [self, st, quitCb, handle](const Value& ex) {
            if (st->finished) return;
            st->finished = true;
            if (quitCb.t == VT::Code) { ValueList one{ex}; try { self->callCallable(quitCb, one); } catch (...) {} }
            self->closeTapHandle(handle);
        };
        // close a tap VALUE, whichever shape tapSupply handed back
        auto closeInner = [self](Value t) {
            if (!(t.t == VT::Hash && t.hash())) return;
            if (t.ext() && t.hash()->count("wired") && (*t.hash())["wired"].truthy())
                self->closeTapHandle(std::static_pointer_cast<TapHandle>(t.ext()));
            else { (*t.hash())["closed"] = Value::boolean(true); (*t.hash())["ended"] = Value::boolean(true); }
        };
        Value outerEmit; outerEmit.t = VT::Code; outerEmit.setCode(makePayload<Callable>());
        outerEmit.code()->builtin =
            [self, st, emitCb, fail, maybeFinish, closeInner, migrate](Interpreter& I, ValueList& a) -> Value {
            if (st->finished) return Value::any();
            Value v = a.empty() ? Value::any() : a[0];
            if (!(v.t == VT::Hash && v.hashKind == "Supply")) {
                if (migrate)
                    throw RakuError{Value::typeObj("X::Supply::Migrate::Needs"),
                                    "Can only migrate to a Supply"};
                // flat: an Iterable value is spread, anything else passes through
                if (emitCb.t == VT::Code) {
                    if (v.t == VT::Array && v.arr()) { for (auto& x : *v.arr()) { ValueList one{x}; try { I.callCallable(emitCb, one); } catch (...) {} } }
                    else if (v.t == VT::Range) { for (auto& x : v.flatten()) { ValueList one{x}; try { I.callCallable(emitCb, one); } catch (...) {} } }
                    else { ValueList one{v}; try { I.callCallable(emitCb, one); } catch (...) {} }
                }
                return Value::any();
            }
            if (migrate && st->current.t == VT::Hash) { closeInner(st->current); st->current = Value(); if (st->pending > 0) st->pending--; }
            st->pending++;
            Value ie; ie.t = VT::Code; ie.setCode(makePayload<Callable>());
            ie.code()->builtin = [st, emitCb](Interpreter& I2, ValueList& b) -> Value {
                if (st->finished || emitCb.t != VT::Code) return Value::any();
                ValueList one{b.empty() ? Value::any() : b[0]};
                try { I2.callCallable(emitCb, one); } catch (...) {}
                return Value::any();
            };
            Value id; id.t = VT::Code; id.setCode(makePayload<Callable>());
            id.code()->builtin = [st, maybeFinish](Interpreter&, ValueList&) -> Value {
                if (st->pending > 0) st->pending--;
                maybeFinish();
                return Value::any();
            };
            Value iq; iq.t = VT::Code; iq.setCode(makePayload<Callable>());
            iq.code()->builtin = [fail](Interpreter&, ValueList& b) -> Value {
                fail(b.empty() ? Value::any() : b[0]); return Value::any();
            };
            Value it = self->tapSupply(v, ie, id, iq);
            if (migrate) st->current = it;
            return Value::any();
        };
        Value outerDone; outerDone.t = VT::Code; outerDone.setCode(makePayload<Callable>());
        outerDone.code()->builtin = [st, maybeFinish](Interpreter&, ValueList&) -> Value {
            st->outerDone = true; maybeFinish(); return Value::any();
        };
        Value outerQuit; outerQuit.t = VT::Code; outerQuit.setCode(makePayload<Callable>());
        outerQuit.code()->builtin = [fail](Interpreter&, ValueList& a) -> Value {
            fail(a.empty() ? Value::any() : a[0]); return Value::any();
        };
        Value outerTap = tapSupply(src, outerEmit, outerDone, outerQuit);
        {
            std::lock_guard<std::mutex> lk(handle->m);
            if (!handle->closed) {
                Value ot = outerTap;
                Interpreter* ip = this;
                handle->closers.push_back([ip, ot] {
                    Value t = ot;
                    if (!(t.t == VT::Hash && t.hash())) return;
                    if (t.ext() && t.hash()->count("wired") && (*t.hash())["wired"].truthy())
                        ip->closeTapHandle(std::static_pointer_cast<TapHandle>(t.ext()));
                    else { (*t.hash())["closed"] = Value::boolean(true); (*t.hash())["ended"] = Value::boolean(true); }
                });
            }
        }
        Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
        (*t.hash())["wired"] = Value::boolean(true);
        return t;
    }
    // S-27/S-45/S-46: a combinator over sources that are not all list-backed.
    // Every tap subscribes to every source for itself and folds their events:
    //   merge       — every value as it comes; done when all sources are done
    //   zip         — a row once every source has an unconsumed value; done as
    //                 soon as a source is done and the others have caught up
    //   zip-latest  — once every source has spoken, each new value emits the
    //                 latest of all of them; done when all sources are done
    if (h.count("kind") && h.at("kind").toStr() == "combine") {
        const std::string op = h.at("op").toStr();
        ValueList srcs;
        if (h.count("sources") && h.at("sources").arr()) srcs = *h.at("sources").arr();
        Value withOp = h.count("with") ? h.at("with") : Value::nil();
        ValueList initial;
        if (h.count("initial") && h.at("initial").arr()) initial = *h.at("initial").arr();
        const size_t n = srcs.size();
        auto handle = std::make_shared<TapHandle>();
        struct CombineState {
            std::vector<ValueList> queues;   // zip: what each source is holding
            std::vector<Value> latest;       // zip-latest: its most recent value
            std::vector<char> have, ended;
            int liveCount = 0;
            bool finished = false;
        };
        auto st = std::make_shared<CombineState>();
        // Sources may emit from different threads at once (two Suppliers fed
        // by `start` blocks): the state is kept under `mx`, and deliveries go
        // one at a time — a delivery that arrives while another runs waits in
        // `pending` and is run by that one (the same rule as a Supplier's
        // own activation), so the block never runs beside itself.
        struct Serial { std::mutex m; bool running = false; std::deque<std::function<void()>> pending; };
        auto mx = std::make_shared<std::mutex>();
        auto serial = std::make_shared<Serial>();
        auto deliver = [serial](std::function<void()> f) {
            {   std::lock_guard<std::mutex> lk(serial->m);
                if (serial->running) { serial->pending.push_back(std::move(f)); return; }
                serial->running = true;
            }
            for (;;) {
                try { f(); } catch (...) {}
                std::lock_guard<std::mutex> lk(serial->m);
                if (serial->pending.empty()) { serial->running = false; return; }
                f = std::move(serial->pending.front());
                serial->pending.pop_front();
            }
        };
        st->queues.resize(n);
        st->latest.resize(n);
        st->have.assign(n, 0);
        st->ended.assign(n, 0);
        for (size_t i = 0; i < n && i < initial.size(); i++) { st->latest[i] = initial[i]; st->have[i] = 1; }
        st->liveCount = (int)n;
        Interpreter* self = this;
        // (each is called with `mx` NOT held: it runs the subscriber's code)
        auto finish = [self, st, doneCb, handle, deliver, mx]() {
            { std::lock_guard<std::mutex> lk(*mx); if (st->finished) return; st->finished = true; }
            deliver([self, doneCb, handle] {
                if (doneCb.t == VT::Code) { ValueList na; try { self->callCallable(doneCb, na); } catch (...) {} }
                self->closeTapHandle(handle);
            });
        };
        auto fail = [self, st, quitCb, handle, deliver, mx](const Value& ex) {
            { std::lock_guard<std::mutex> lk(*mx); if (st->finished) return; st->finished = true; }
            deliver([self, quitCb, handle, ex] {
                if (quitCb.t == VT::Code) { ValueList one{ex}; try { self->callCallable(quitCb, one); } catch (...) {} }
                self->closeTapHandle(handle);
            });
        };
        auto push = [self, st, emitCb, deliver](Value v) {
            if (emitCb.t != VT::Code) return;
            deliver([self, st, emitCb, v] {
                if (st->finished) return;
                ValueList one{v};
                try { self->callCallable(emitCb, one); } catch (...) {}
            });
        };
        auto row = [self, withOp](ValueList vs) -> Value {
            if (withOp.t == VT::Code) return self->callCallable(withOp, vs);
            Value tup = Value::array(); tup.isList = true; tup.itemized = true;
            *tup.arr() = std::move(vs);
            return tup;
        };
        for (size_t i = 0; i < n; i++) {
            Value e; e.t = VT::Code; e.setCode(makePayload<Callable>());
            e.code()->builtin = [i, n, op, st, push, row, finish, mx](Interpreter&, ValueList& a) -> Value {
                Value v = a.empty() ? Value::any() : a[0];
                if (op == "merge") {
                    { std::lock_guard<std::mutex> lk(*mx); if (st->finished) return Value::any(); }
                    push(v); return Value::any();
                }
                // the rows are worked out under the lock and delivered after it
                std::vector<ValueList> rows;
                bool end = false;
                {
                    std::lock_guard<std::mutex> lk(*mx);
                    if (st->finished) return Value::any();
                    if (op == "zip") {
                        st->queues[i].push_back(v);
                        for (;;) {
                            bool full = true;
                            for (size_t k = 0; k < n; k++) if (st->queues[k].empty()) { full = false; break; }
                            if (!full) break;
                            ValueList vs;
                            for (size_t k = 0; k < n; k++) { vs.push_back(st->queues[k].front()); st->queues[k].erase(st->queues[k].begin()); }
                            rows.push_back(std::move(vs));
                            // a source that has already finished and has nothing left
                            // to give ends the zip: the others cannot be paired again
                            for (size_t k = 0; k < n; k++)
                                if (st->ended[k] && st->queues[k].empty()) { end = true; break; }
                            if (end) break;
                        }
                    }
                    else if (op == "zip-latest") {
                        st->latest[i] = v; st->have[i] = 1;
                        bool all = true;
                        for (size_t k = 0; k < n; k++) if (!st->have[k]) { all = false; break; }
                        if (all) rows.push_back(ValueList(st->latest.begin(), st->latest.end()));
                    }
                }
                for (auto& r : rows) push(row(std::move(r)));
                if (end) finish();
                return Value::any();
            };
            Value d; d.t = VT::Code; d.setCode(makePayload<Callable>());
            d.code()->builtin = [i, op, st, finish, mx](Interpreter&, ValueList&) -> Value {
                bool fin = false;
                {
                    std::lock_guard<std::mutex> lk(*mx);
                    if (st->finished || st->ended[i]) return Value::any();
                    st->ended[i] = 1;
                    if (st->liveCount > 0) st->liveCount--;
                    fin = op == "zip" ? st->queues[i].empty() : st->liveCount == 0;
                }
                if (fin) finish();
                return Value::any();
            };
            Value q; q.t = VT::Code; q.setCode(makePayload<Callable>());
            q.code()->builtin = [fail](Interpreter&, ValueList& a) -> Value {
                fail(a.empty() ? Value::any() : a[0]); return Value::any();
            };
            Value innerTap = tapSupply(srcs[i], e, d, q);
            // closing this tap closes the subscriptions it made
            if (innerTap.t == VT::Hash && innerTap.hash()) {
                std::lock_guard<std::mutex> lk(handle->m);
                if (!handle->closed) {
                    if (innerTap.ext() && innerTap.hash()->count("wired") && (*innerTap.hash())["wired"].truthy()) {
                        auto ih = std::static_pointer_cast<TapHandle>(innerTap.ext());
                        Interpreter* ip = this;
                        handle->closers.push_back([ip, ih] { ip->closeTapHandle(ih); });
                    } else if (innerTap.hashKind == "Tap") {
                        auto rec = innerTap.hashS();
                        handle->closers.push_back([rec] {
                            (*rec)["closed"] = Value::boolean(true);
                            (*rec)["ended"] = Value::boolean(true);
                        });
                    }
                }
            }
            { std::lock_guard<std::mutex> lk(*mx); if (st->finished) break; }   // a synchronous source may have ended it already
        }
        if (n == 0) finish();
        Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
        (*t.hash())["wired"] = Value::boolean(true);
        return t;
    }
    // S-47, the concurrency form. At most `limit` calls of process(value) are in
    // flight; the supply emits the PROMISE of each as it is started, in value
    // order. `:control` changes the limit while the stream runs, and `:status`
    // receives a report when everything has finished.
    if (h.count("kind") && h.at("kind").toStr() == "throttle-run") {
        Value src = h.at("src");
        Value process = h.at("process");
        auto handle = std::make_shared<TapHandle>();
        struct RunState {
            // Every field below is reached from at least two threads: the
            // source's thread pushes a value and pumps, and each `process`
            // Promise settles on a worker of its own and pumps again from
            // there. In parallel mode -- the default since v3.0.0 -- a Promise
            // fires its `.then` continuations with NO GIL held (spawnPromise's
            // parallel branch says so in as many words), so nothing here is
            // serialised on our behalf.
            //
            // Unguarded, two Promises settling at once ran the pump together
            // and both took `pending.front()` and erased `pending.begin()`:
            // two threads inside one RVec::erase. That is a SIGSEGV when the
            // memmove walks off the end, and the SAME VALUE DISPATCHED TWICE
            // when it does not -- `.throttle(2, {…})` over three values
            // answering four Promises. Both were ~5% of runs of
            // t/regression/supply-combinators.raku.
            //
            // The rule the code below keeps: the lock is held for a field
            // access and NEVER across a callback, because every callback here
            // is user code that can re-enter this same supply.
            std::mutex m;
            ValueList pending;
            long long limit = 0, running = 0, emitted = 0;
            bool srcDone = false, finished = false;
            // ORDER: a body waits — for at most a moment — until the one
            // dispatched before it has finished. Every Promise here is a thread
            // of its own, and their start latencies differ, so `.throttle(3,
            // { $c.send: $_ })` sent its first three values in any order under
            // load (S17-channel/basic.t), where jobs queued to Rakudo's pool
            // start first-in first-out. A quick body therefore takes effect in
            // value order; a slow one lets the next start after the wait, so
            // the bodies still overlap.
            std::condition_variable cv;
            long long nextTicket = 0, turn = 0;
        };
        auto st = std::make_shared<RunState>();
        st->limit = h.count("elems") ? h.at("elems").toInt() : 1;
        if (st->limit < 0) st->limit = 0;
        Value statusSup = h.count("status") ? h.at("status") : Value::nil();
        Interpreter* self = this;
        auto report = [self, st, statusSup](const char* id) {
            if (statusSup.t != VT::Hash) return;
            Value r = Value::makeHash();
            {   // one snapshot, so the row cannot describe two different moments
                std::lock_guard<std::mutex> lk(st->m);
                (*r.hash())["allowed"] = Value::integer(st->limit - st->running);
                (*r.hash())["bled"] = Value::integer(0);
                (*r.hash())["buffered"] = Value::integer((long long)st->pending.size());
                (*r.hash())["emitted"] = Value::integer(st->emitted);
                (*r.hash())["id"] = Value::str(id);
                (*r.hash())["limit"] = Value::integer(st->limit);
                (*r.hash())["running"] = Value::integer(st->running);
                (*r.hash())["vent-at"] = Value::integer(0);
            }
            Value sup = statusSup;
            ValueList one{r};
            try { self->methodCall(sup, "emit", one); } catch (...) {}
        };
        auto finish = [self, st, doneCb, handle, report]() {
            // claim the teardown exactly once, then run it with the lock DOWN
            { std::lock_guard<std::mutex> lk(st->m); if (st->finished) return; st->finished = true; }
            report("done");
            if (doneCb.t == VT::Code) { ValueList na; try { self->callCallable(doneCb, na); } catch (...) {} }
            self->closeTapHandle(handle);
        };
        // pump() starts as much as the allowance permits, and is called again
        // whenever something changes: a new value, a raised limit, a finish.
        auto pump = std::make_shared<std::function<void()>>();
        *pump = [self, st, process, emitCb, pump, finish]() {
            for (;;) {
                Value v;
                long long ticket = 0;
                {   // One value claimed per turn, and the allowance SPENT for it
                    // before the lock drops: a second thread arriving here must
                    // find the slot already taken, or both dispatch the same
                    // value. Everything after this block -- spawning the
                    // Promise, emitting it -- is callback territory and runs
                    // unlocked.
                    std::lock_guard<std::mutex> lk(st->m);
                    if (st->finished || st->running >= st->limit || st->pending.empty()) return;
                    v = st->pending.front();
                    st->pending.erase(st->pending.begin());
                    st->running++;
                    st->emitted++;
                    ticket = st->nextTicket++;
                }
                Value body; body.t = VT::Code; body.setCode(makePayload<Callable>());
                Value pv = v, pf = process;
                body.code()->builtin = [pv, pf, st, ticket](Interpreter& I2, ValueList&) -> Value {
                    {   // bounded: a slow or stalled predecessor holds nobody long
                        std::unique_lock<std::mutex> lk(st->m);
                        st->cv.wait_for(lk, std::chrono::milliseconds(10),
                                        [&] { return st->turn >= ticket || st->finished; });
                    }
                    struct Next {
                        std::shared_ptr<RunState> st; long long t;
                        ~Next() {
                            { std::lock_guard<std::mutex> lk(st->m); if (st->turn <= t) st->turn = t + 1; }
                            st->cv.notify_all();
                        }
                    } next{st, ticket};
                    ValueList one{pv}; return I2.callCallable(pf, one);
                };
                Value pr = self->spawnPromise(body);
                if (pr.t == VT::Hash && pr.ext()) {
                    auto ps = std::static_pointer_cast<PromiseState>(pr.ext());
                    std::function<void()> whenDone = [st, pump, finish]() {
                        { std::lock_guard<std::mutex> lk(st->m); if (st->running > 0) st->running--; }
                        (*pump)();
                        bool last;
                        { std::lock_guard<std::mutex> lk(st->m);
                          last = st->srcDone && st->pending.empty() && st->running == 0; }
                        if (last) finish();
                    };
                    bool now = false;
                    { std::lock_guard<std::mutex> lk(ps->m); if (ps->done) now = true; else ps->thens.push_back(whenDone); }
                    if (now) whenDone();
                }
                if (emitCb.t == VT::Code) { ValueList one{pr}; try { self->callCallable(emitCb, one); } catch (...) {} }
            }
        };
        if (h.count("control")) {
            Value ctl = h.at("control");
            if (ctl.t == VT::Hash && ctl.hashKind == "Supplier") { ValueList na; ctl = methodCall(ctl, "Supply", na); }
            Value ctlEmit; ctlEmit.t = VT::Code; ctlEmit.setCode(makePayload<Callable>());
            ctlEmit.code()->builtin = [st, pump](Interpreter&, ValueList& a) -> Value {
                if (a.empty()) return Value::any();
                const std::string cmd = a[0].toStr();
                auto colon = cmd.find(':');
                if (colon == std::string::npos) return Value::any();
                std::string key = cmd.substr(0, colon), val = cmd.substr(colon + 1);
                while (!key.empty() && key.back() == ' ') key.pop_back();
                while (!val.empty() && val.front() == ' ') val.erase(val.begin());
                if (key == "limit") {
                    long long n = 0; bool ok = true;
                    try { n = std::stoll(val); } catch (...) { ok = false; }
                    if (ok) { std::lock_guard<std::mutex> lk(st->m); st->limit = n; }
                    (*pump)();
                }
                return Value::any();
            };
            tapSupply(ctl, ctlEmit, Value::nil(), Value::nil());
        }
        Value inEmit; inEmit.t = VT::Code; inEmit.setCode(makePayload<Callable>());
        inEmit.code()->builtin = [st, pump](Interpreter&, ValueList& a) -> Value {
            { std::lock_guard<std::mutex> lk(st->m); st->pending.push_back(a.empty() ? Value::any() : a[0]); }
            (*pump)();
            return Value::any();
        };
        Value inDone; inDone.t = VT::Code; inDone.setCode(makePayload<Callable>());
        inDone.code()->builtin = [st, finish](Interpreter&, ValueList&) -> Value {
            bool last;
            { std::lock_guard<std::mutex> lk(st->m);
              st->srcDone = true;
              last = st->pending.empty() && st->running == 0; }
            if (last) finish();
            return Value::any();
        };
        Value inQuit; inQuit.t = VT::Code; inQuit.setCode(makePayload<Callable>());
        Value qc = quitCb;
        inQuit.code()->builtin = [self, st, qc, handle](Interpreter&, ValueList& a) -> Value {
            { std::lock_guard<std::mutex> lk(st->m); if (st->finished) return Value::any(); st->finished = true; }
            if (qc.t == VT::Code) { ValueList one{a.empty() ? Value::any() : a[0]}; try { self->callCallable(qc, one); } catch (...) {} }
            self->closeTapHandle(handle);
            return Value::any();
        };
        tapSupply(src, inEmit, inDone, inQuit);
        Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
        (*t.hash())["wired"] = Value::boolean(true);
        return t;
    }
    // S-47: a throttled supply, tapped. The source feeds a buffer; a worker
    // releases at most `elems` of it per `seconds` tick, and the stream is done
    // only when the source is done AND the buffer has drained.
    if (h.count("kind") && h.at("kind").toStr() == "throttle") {
        Value src = h.at("src");
        // the allowance is LIVE: a `:control` supply may raise or lower it while
        // the stream runs, so it is a shared counter rather than a constant
        auto elemsP = std::make_shared<std::atomic<long long>>(
            h.count("elems") ? h.at("elems").toInt() : 1);
        if (elemsP->load() < 0) elemsP->store(0);
        double secs = h.count("seconds") ? h.at("seconds").toNum() : 0;
        double delay = h.count("delay") ? h.at("delay").toNum() : 0;
        if (secs < 0.001) secs = 0.001;
        if (h.count("control")) {
            Value ctlEmit; ctlEmit.t = VT::Code; ctlEmit.setCode(makePayload<Callable>());
            ctlEmit.code()->builtin = [elemsP](Interpreter&, ValueList& a) -> Value {
                if (a.empty()) return Value::any();
                const std::string cmd = a[0].toStr();
                auto colon = cmd.find(':');
                if (colon == std::string::npos) return Value::any();
                std::string key = cmd.substr(0, colon), val = cmd.substr(colon + 1);
                while (!key.empty() && key.back() == ' ') key.pop_back();
                while (!val.empty() && val.front() == ' ') val.erase(val.begin());
                if (key == "limit") { try { elemsP->store(std::stoll(val)); } catch (...) {} }
                return Value::any();
            };
            // `:control` is usually written `:$control` over a Supplier, which
            // is what a whenever would coerce for itself
            Value ctl = h.at("control");
            if (ctl.t == VT::Hash && ctl.hashKind == "Supplier") { ValueList na; ctl = methodCall(ctl, "Supply", na); }
            tapSupply(ctl, ctlEmit, Value::nil(), Value::nil());
        }
        auto handle = std::make_shared<TapHandle>();
        auto buf = makePayload<ValueList>();
        auto srcDone = std::make_shared<std::atomic<bool>>(false);
        auto quitEx = std::make_shared<Value>();
        auto quitSet = std::make_shared<std::atomic<bool>>(false);
        // A token bucket: `elems` values may pass in each tick, and one that
        // finds a token left goes through AT ONCE — waiting for the tick
        // boundary would delay the first values for no reason. The rest queue.
        auto spent = std::make_shared<std::atomic<long long>>(0);
        Value inEmit; inEmit.t = VT::Code; inEmit.setCode(makePayload<Callable>());
        Value emitOut = emitCb;
        inEmit.code()->builtin = [buf, spent, elemsP, emitOut](Interpreter& I, ValueList& a) -> Value {
            Value v = a.empty() ? Value::any() : a[0];
            if (spent->load() < elemsP->load() && emitOut.t == VT::Code) {
                spent->fetch_add(1);
                ValueList one{v};
                try { I.callCallable(emitOut, one); } catch (...) {}
                return Value::any();
            }
            buf->push_back(v);
            return Value::any();
        };
        Value inDone; inDone.t = VT::Code; inDone.setCode(makePayload<Callable>());
        inDone.code()->builtin = [srcDone](Interpreter&, ValueList&) -> Value {
            srcDone->store(true); return Value::any();
        };
        Value inQuit; inQuit.t = VT::Code; inQuit.setCode(makePayload<Callable>());
        inQuit.code()->builtin = [srcDone, quitEx, quitSet](Interpreter&, ValueList& a) -> Value {
            *quitEx = a.empty() ? Value::any() : a[0]; quitSet->store(true); srcDone->store(true);
            return Value::any();
        };
        tapSupply(src, inEmit, inDone, inQuit);
        engageGil();
        liveWorkers_++;
        auto fin = std::make_shared<std::atomic<bool>>(false);
        auto spawnScope = tctx_.cur ? tctx_.cur : global_;
        Interpreter* self = this;
        throttleSpawn();
        addWorker(BigStackThread([self, buf, srcDone, quitEx, quitSet, elemsP, secs, delay, spent,
                                  emitCb, doneCb, quitCb, handle, fin, spawnScope]() mutable {
            t_poll.isWorker = true;
            auto stopped = [&] {
                if (self->workerAbort_.load(std::memory_order_relaxed)) return true;
                std::lock_guard<std::mutex> lk(handle->m); return handle->closed;
            };
            auto nap = [&](double d) {
                double left = d;
                while (left > 0 && !stopped()) {
                    double c = left < 0.05 ? left : 0.05;
                    std::this_thread::sleep_for(std::chrono::duration<double>(c));
                    left -= c;
                }
            };
            nap(delay);
            bool firstPass = true;                  // the bucket starts FULL: the
            for (;;) {                              // first refill is one tick later
                if (stopped()) break;
                self->gilLock();
                ExecContext wctx; self->loadCtx(wctx);
                tctx_.cur = spawnScope;
                tctx_.dynStack.push_back(spawnScope.get());
                if (!firstPass) spent->store(0);    // a new tick refills the bucket
                while (!firstPass && spent->load() < elemsP->load() && !buf->empty()) {
                    Value v = buf->front(); buf->erase(buf->begin());
                    spent->fetch_add(1);
                    if (emitCb.t == VT::Code) { ValueList one{v}; try { self->callCallable(emitCb, one); } catch (...) {} }
                }
                bool over = srcDone->load() && buf->empty();
                if (over) {
                    if (quitSet->load()) {
                        if (quitCb.t == VT::Code) { ValueList one{*quitEx}; try { self->callCallable(quitCb, one); } catch (...) {} }
                    } else if (doneCb.t == VT::Code) { ValueList na; try { self->callCallable(doneCb, na); } catch (...) {} }
                }
                self->gilYieldNotify();
                if (over) break;
                firstPass = false;
                nap(secs);
            }
            self->liveWorkers_--;
            fin->store(true, std::memory_order_release);
        }), fin);
        Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
        (*t.hash())["wired"] = Value::boolean(true);
        return t;
    }
    // `$path.IO.watch`: a file's changes as a Supply (see watchFilter)
    if (h.count("kind") && h.at("kind").toStr() == "watch") {
        auto handle = std::make_shared<TapHandle>();
        std::shared_ptr<ReactCtx> rctx = reactStack_.empty() ? nullptr : reactStack_.back();
        return spawnIntervalWhenever(watchInterval(s), 0, watchFilter(s, emitCb), rctx, handle, doneCb);
    }
    // Supply.interval(N) tapped directly (.tap, or inside a supply {…} block):
    // each tap gets its OWN ticker; the returned Tap's handle stops it on .close.
    if (h.count("kind") && h.at("kind").toStr() == "interval") {
        double iv = h.count("interval") ? h.at("interval").toNum() : 1;
        double dl = h.count("delay") ? h.at("delay").toNum() : 0;
        // A scheduler of the caller's own: hand it one cue and let it decide when
        // the ticks happen. Each call of the cued code is one tick.
        if (h.count("scheduler") && h.at("scheduler").t == VT::Object) {
            auto tick = std::make_shared<long long>(0);
            Value cb; cb.t = VT::Code; cb.setCode(makePayload<Callable>());
            Value em = emitCb;
            cb.code()->builtin = [tick, em](Interpreter& I2, ValueList&) -> Value {
                if (em.t != VT::Code) return Value::any();
                ValueList one{Value::integer((*tick)++)};
                try { I2.callCallable(em, one); } catch (NextEx&) {} catch (LastEx&) {} catch (DoneEx&) {}
                return Value::any();
            };
            Value sched = h.at("scheduler");
            Value every = Value::number(iv); every.hashKind = "Duration";
            Value in = Value::number(dl); in.hashKind = "Duration";
            Value pEvery = Value::pair("every", every); pEvery.namedArg = true;
            Value pIn = Value::pair("in", in); pIn.namedArg = true;
            ValueList ca{cb, pEvery, pIn};
            methodCall(sched, "cue", ca);
            Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
        }
        auto handle = std::make_shared<TapHandle>();
        std::shared_ptr<ReactCtx> rctx = reactStack_.empty() ? nullptr : reactStack_.back();
        return spawnIntervalWhenever(iv, dl, emitCb, rctx, handle, doneCb);
    }
    if (h.count("kind") && h.at("kind").toStr() == "async-listen") {
        std::string host = h.count("host") ? h.at("host").toStr() : "localhost";
        int port = h.count("port") ? (int)h.at("port").toInt() : 0;
        sockaddr_storage addr{}; socklen_t addrLen = 0;
        bool badHost = !asyncSockAddr(host, port, addr, addrLen);
        int lfd = badHost ? -1 : ::socket(addr.ss_family, SOCK_STREAM, 0);
        if (!badHost && lfd < 0) throw RakuError{Value::typeObj("X::IO"), "Cannot create socket"};
        if (lfd >= 0) { int yes = 1; setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes)); }
        if (badHost || ::bind(lfd, (sockaddr*)&addr, addrLen) < 0 || ::listen(lfd, 128) < 0) {
            if (lfd >= 0) ::close(lfd);
            // a tap with a :quit hears the failure as the supply's quit
            if (quitCb.t == VT::Code) {
                Value ex = makeTypedEx("X::AdHoc", {}, "Cannot listen on " + host + ":" + std::to_string(port));
                ValueList one{ex};
                callCallable(quitCb, one);
                Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
            }
            throw RakuError{Value::typeObj("X::IO"), "Cannot listen on " + host + ":" + std::to_string(port)};
        }
        // the address actually bound — `.socket-port` answers the port a `0` asked the OS for
        std::string boundHost = host; long long boundPort = port;
        { sockaddr_storage ba{}; socklen_t bl = sizeof(ba);
          if (::getsockname(lfd, (sockaddr*)&ba, &bl) == 0) asyncSockName(ba, boundHost, boundPort); }
        engageGil();
        auto handle = std::make_shared<TapHandle>();
        // Closing the tap closes the listener — but a connection the kernel has
        // already COMPLETED belongs to the tap, as it does under Rakudo, whose
        // event loop accepts eagerly. Shutting the listener down dropped any the
        // worker had not yet taken off the backlog: under load the client's
        // `connect` had succeeded, its writes and close had gone through, and
        // the tap's callback never ran (S32-io/IO-Socket-Async.t's "both
        // receivers finished" hung in about one full Roast sweep in ten run
        // beside another). So the closer first takes what is waiting —
        // non-blocking, since the worker may take the same connection first —
        // and the worker delivers those after its accept loop ends.
        struct Pending { std::mutex m; std::vector<int> fds; };
        auto pending = std::make_shared<Pending>();
        handle->closers.push_back([lfd, pending] {
#ifdef _WIN32
            u_long nb = 1; ioctlsocket(lfd, FIONBIO, &nb);
#else
            ::fcntl(lfd, F_SETFL, ::fcntl(lfd, F_GETFL) | O_NONBLOCK);
#endif
            for (;;) {
                int cfd = ::accept(lfd, nullptr, nullptr);
                if (cfd < 0) break;
                std::lock_guard<std::mutex> lk(pending->m);
                pending->fds.push_back(cfd);
            }
            ::shutdown(lfd, SHUT_RDWR); ::close(lfd);
        });
        liveWorkers_++;
        auto fin = std::make_shared<std::atomic<bool>>(false);
        auto spawnScope = tctx_.cur ? tctx_.cur : global_;
        Interpreter* self = this;
        const std::string listenEnc = h.count("enc") ? h.at("enc").toStr() : std::string();
        throttleSpawn();
        addWorker(BigStackThread([self, lfd, emitCb, handle, fin, spawnScope, listenEnc, pending]() mutable {
            t_poll.isWorker = true;
            auto deliver = [&](int cfd) {
                self->gilLock();
                ExecContext wctx; self->loadCtx(wctx);           // fresh registers
                tctx_.cur = spawnScope;
                tctx_.dynStack.push_back(spawnScope.get());
                Value sock = makeAsyncSocket(cfd);
                if (!listenEnc.empty()) (*sock.hash())["enc"] = Value::str(listenEnc);   // `listen(…, :enc)`
                if (emitCb.t == VT::Code) {
                    ValueList one{sock};
                    try { self->callCallable(emitCb, one); }
                    catch (RakuError& e) { fprintf(stderr, "===WARNING=== async accept handler died: %s\n", e.message.c_str()); }
                    catch (...) {}
                }
                self->gilYieldNotify();
            };
            for (;;) {
                int cfd = ::accept(lfd, nullptr, nullptr);       // GIL not held
                if (cfd < 0) break;                              // closed / shutdown
                deliver(cfd);
            }
            // what the closer took off the backlog: delivered, unless the
            // program is going away, when they are only closed
            std::vector<int> late;
            { std::lock_guard<std::mutex> lk(pending->m); late.swap(pending->fds); }
            for (int cfd : late) {
                if (self->workerAbort_.load(std::memory_order_relaxed)) ::close(cfd);
                else deliver(cfd);
            }
            self->liveWorkers_--;
            fin->store(true, std::memory_order_release);
        }), fin);
        Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
        (*t.hash())["wired"] = Value::boolean(true);
        auto keptP = [](Value v) {
            auto ps = std::make_shared<PromiseState>();
            Value p = Value::makeHash(); p.hashKind = "Promise"; p.extM() = ps;
            ps->done = true; ps->result = v;
            (*p.hash())["status"] = Value::str("Kept"); (*p.hash())["result"] = v;
            return p;
        };
        (*t.hash())["socket-host"] = keptP(Value::str(boundHost));
        (*t.hash())["socket-port"] = keptP(Value::integer(boundPort));
        return t;
    }
    // 4) async read: a worker recv()s and emits Blob chunks; EOF fires done.
    if (h.count("kind") && h.at("kind").toStr() == "async-read") {
        Value sock = h.at("socket");
        int fd = (sock.t == VT::Hash && sock.hash()->count("fd")) ? (int)(*sock.hash())["fd"].toInt() : -1;
        engageGil();
        auto handle = std::make_shared<TapHandle>();
        // Closing a TAP must not touch the SOCKET. This used to
        // `shutdown(fd, SHUT_RD)` so the blocked recv() below would return, but
        // that shut the read half down for everyone: a program that taps
        // `$conn.Supply`, lets that tap go and taps it again got nothing the
        // second time — and the reader then closed the fd on its way out. The
        // loop polls with a timeout instead and notices handle->closed itself.
        // Log::Timeline's socket test is exactly this shape.
        liveWorkers_++;
        auto fin = std::make_shared<std::atomic<bool>>(false);
        auto spawnScope = tctx_.cur ? tctx_.cur : global_;
        bool bin = h.count("bin") && h.at("bin").truthy();
        Interpreter* self = this;
        // a single-byte encoding decodes chunk by chunk; UTF-8 keeps the carry below
        std::string readEnc = h.count("enc") ? h.at("enc").toStr() : std::string();
        if (readEnc == "utf8" || readEnc == "utf-8") readEnc.clear();
        // The enclosing `react`, carried to the worker: reactStack_ is
        // THREAD-LOCAL, so a `done` inside `whenever $conn.Supply {…}` ran on
        // this worker with an empty stack, found no react to close, and simply
        // returned True — the react then waited for ever. Every other async
        // source already hands its ReactCtx across (see tapSignal); this one
        // did not, so the commonest socket shape there is could not be ended
        // from inside its own handler.
        auto rctx0 = reactStack_.empty() ? std::shared_ptr<ReactCtx>() : reactStack_.back();
        throttleSpawn();
        addWorker(BigStackThread([self, fd, sock, emitCb, doneCb, quitCb, handle, fin, spawnScope, bin, rctx0, readEnc]() mutable {
            t_poll.isWorker = true;
            std::vector<char> buf(65536);
            bool malformed = false;   // bytes that are no UTF-8 at all: the supply QUITs
            // A CHARACTER supply must not split a character. The bytes arrive
            // on whatever boundary the network chose, and handing each chunk
            // over as a Str made a multi-byte character straddling two reads
            // into two broken ones — invisible to a plain `~` (our Str IS its
            // bytes, so they rejoin) and destructive to anything that reads
            // the chunk as text: `$m.uc` on "пр<half и>" mangles it for real.
            // Rakudo decodes incrementally here, so this keeps the undecodable
            // tail back for the next read. A `:bin` tap is unaffected — a Blob
            // has no characters to split.
            std::string carry;
            bool tapClosed = false;
            for (;;) {
                if (fd < 0) break;
                {   std::lock_guard<std::mutex> lk(handle->m);
                    if (handle->closed) { tapClosed = true; break; }
                }
                if (self->workerAbort_.load(std::memory_order_relaxed)) { tapClosed = true; break; }
                struct pollfd pfd { fd, POLLIN, 0 };
                int pr = ::poll(&pfd, 1, 20);                        // GIL not held
                if (pr == 0) continue;                               // nothing yet; re-check the tap
                if (pr < 0) { if (errno == EINTR) continue; break; }
                // Re-check between "readable" and the read. A closed tap must not
                // CONSUME: these bytes belong to whoever taps next, and a worker
                // that read them would emit into a dead tap and drop them. That
                // is how Log::Timeline lost the first of its queued events — the
                // client's first tap had gone, its worker had not yet noticed.
                {   std::lock_guard<std::mutex> lk(handle->m);
                    if (handle->closed) { tapClosed = true; break; }
                }
                ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);    // ready, so this will not block
                if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
                if (n <= 0) break;
                self->gilLock();
                ExecContext wctx; self->loadCtx(wctx);
                tctx_.cur = spawnScope;
                tctx_.dynStack.push_back(spawnScope.get());
                Value chunk;
                if (bin) { chunk = Value::str(std::string(buf.data(), (size_t)n)); chunk.hashKind = "Blob"; }
                else if (!readEnc.empty()) chunk = Value::str(self->decodeTextEnc(std::string(buf.data(), (size_t)n), readEnc));
                else {
                    carry.append(buf.data(), (size_t)n);
                    size_t take = utf8TextPrefixLen(carry);
                    // what is held back must be the START of a character; a byte
                    // that can begin none (0x80-0xBF, 0xF8-0xFF) is malformed input
                    // (whole characters may be held back too — a grapheme waiting
                    // for its marks — so only an ill-formed sequence counts, and
                    // only the very end may be an incomplete one)
                    {
                        bool bad = false;
                        for (size_t k = take; k < carry.size() && !bad; ) {
                            unsigned char b0 = (unsigned char)carry[k];
                            if (b0 < 0x80) { k++; continue; }
                            size_t need = b0 >= 0xF8 ? 0 : b0 >= 0xF0 ? 4 : b0 >= 0xE0 ? 3 : b0 >= 0xC0 ? 2 : 0;
                            if (!need) { bad = true; break; }
                            for (size_t j = k + 1; j < k + need && j < carry.size(); j++)
                                if (((unsigned char)carry[j] & 0xC0) != 0x80) { bad = true; break; }
                            k += need;
                        }
                        if (bad) { malformed = true; self->gilYieldNotify(); break; }
                    }
                    if (!take) { self->gilYieldNotify(); continue; }   // nothing whole yet
                    chunk = Value::str(carry.substr(0, take));
                    carry.erase(0, take);
                }
                if (emitCb.t == VT::Code) {
                    ValueList one{chunk};
                    if (rctx0) self->reactStack_.push_back(rctx0);
                    auto pop = [&] { if (rctx0 && !self->reactStack_.empty()) self->reactStack_.pop_back(); };
                    // `done`/`last` in the handler ends this tap, exactly as it
                    // does for a value-backed supply.
                    try { self->callCallable(emitCb, one); pop(); }
                    catch (NextEx&) { pop(); }
                    catch (LastEx&) { pop(); tapClosed = true; }
                    catch (DoneEx&) { pop(); tapClosed = true; }
                    catch (RakuError& e) { pop(); fprintf(stderr, "===WARNING=== async read handler died: %s\n", e.message.c_str()); }
                    catch (...) { pop(); }
                }
                self->gilYieldNotify();
                if (tapClosed) break;
            }
            self->gilLock();
            ExecContext wctx; self->loadCtx(wctx);
            tctx_.cur = spawnScope;
            tctx_.dynStack.push_back(spawnScope.get());
            // A tap that was merely CLOSED leaves the socket alone — it is still
            // open, may still be written to, and may be tapped again. Only a
            // real EOF or read error ends the connection, and then the worker
            // still owns the fd's lifetime.
            // Whatever was held back still belongs to the reader: at EOF there is
            // no next chunk to complete it, so it goes out as it stands — the
            // same thing `consume-all-chars` does when a decoder is drained.
            if (malformed) {
                if (quitCb.t == VT::Code) {
                    Value ex = self->makeTypedEx("X::AdHoc", {}, "Malformed UTF-8");
                    ValueList one{ex};
                    try { self->callCallable(quitCb, one); } catch (...) {}
                }
                tapClosed = true;   // no done after a quit; the socket stays the reader's to close
                carry.clear();
            }
            if (!tapClosed && !carry.empty() && emitCb.t == VT::Code) {
                ValueList one{ Value::str(carry) };
                try { self->callCallable(emitCb, one); } catch (...) {}
                carry.clear();
            }
            if (!tapClosed && doneCb.t == VT::Code) { ValueList na; try { self->callCallable(doneCb, na); } catch (...) {} }
            // The socket forgets the descriptor BEFORE it is closed: the number
            // is free for the next accept() at once, and a write still holding
            // it (a Cro handler subscribed to a broadcast after its client went
            // away) went into that NEW connection — chat messages for a dead
            // client reached whoever connected next.
            if (!tapClosed && fd >= 0 && sock.t == VT::Hash && sock.hash()) {
                (*sock.hash())["fd"] = Value::integer(-1);
                (*sock.hash())["closed"] = Value::boolean(true);
            }
            self->gilYieldNotify();
            if (!tapClosed && fd >= 0) ::close(fd);
            self->liveWorkers_--;
            fin->store(true, std::memory_order_release);
        }), fin);
        // A tap made INSIDE a react dies with the react — the same reason the
        // signal tap registers itself (see tapSignal). Without this the reader
        // worker outlived the block that made it and went on CONSUMING the
        // socket: bytes meant for whoever tapped next were read and delivered
        // into a react that had already finished, so a second tap on the same
        // connection saw nothing at all.
        if (!reactStack_.empty()) {
            auto rctx = reactStack_.back();
            std::lock_guard<std::mutex> lk(rctx->m);
            rctx->extTaps.push_back(handle);
        }
        Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
        (*t.hash())["wired"] = Value::boolean(true);
        return t;
    }
    // A UDP socket's Supply: one emit per datagram, decoded on its own (Rakudo
    // decodes each datagram whole, so nothing is held back for the next one).
    // An empty datagram is an empty Str, not the end. The tap ends when the
    // SOCKET is closed (then `done`), or when the tap itself is closed.
    if (h.count("kind") && h.at("kind").toStr() == "udp-read") {
        Value sock = h.at("socket");
        auto st = udpState(sock);
        auto handle = std::make_shared<TapHandle>();
        auto inertTap = [&] {
            Value t = Value::makeHash(); t.hashKind = "Tap"; t.extM() = handle;
            return t;
        };
        if (!st) return inertTap();
        bool closedNow = false;
        {   std::lock_guard<std::mutex> lk(st->m);
            closedNow = st->closed;
            if (!closedNow) {
                if (st->fd < 0) {   // a `udp` client listens on an ephemeral port
                    int fd = udpOpen(AF_INET, st->broadcast);
                    sockaddr_in any{}; any.sin_family = AF_INET; any.sin_addr.s_addr = INADDR_ANY; any.sin_port = 0;
                    if (fd >= 0 && ::bind(fd, (sockaddr*)&any, sizeof any) < 0) { ::close(fd); fd = -1; }
                    st->fd = fd;
                }
                if (st->fd >= 0) st->readers++;
                else closedNow = true;
            }
        }
        if (closedNow) {
            if (doneCb.t == VT::Code) { ValueList na; callCallable(doneCb, na); }
            return inertTap();
        }
        const int fd = st->fd;
        engageGil();
        liveWorkers_++;
        auto fin = std::make_shared<std::atomic<bool>>(false);
        auto spawnScope = tctx_.cur ? tctx_.cur : global_;
        const bool bin = h.count("bin") && h.at("bin").truthy();
        const bool datagram = h.count("datagram") && h.at("datagram").truthy();
        std::string readEnc = h.count("enc") ? h.at("enc").toStr() : std::string("utf-8");
        Interpreter* self = this;
        auto rctx0 = reactStack_.empty() ? std::shared_ptr<ReactCtx>() : reactStack_.back();
        throttleSpawn();
        addWorker(BigStackThread([self, st, fd, emitCb, doneCb, quitCb, handle, fin, spawnScope, bin, datagram, readEnc, rctx0]() mutable {
            t_poll.isWorker = true;
            { std::lock_guard<std::mutex> lk(st->m); st->readerTids.push_back(std::this_thread::get_id()); }
            std::vector<char> buf(65536);
            bool sockClosed = false, tapClosed = false;
            auto stop = [&] {
                {   std::lock_guard<std::mutex> lk(handle->m);
                    if (handle->closed) { tapClosed = true; return true; }
                }
                {   std::lock_guard<std::mutex> lk(st->m);
                    if (st->closed) { sockClosed = true; return true; }
                }
                if (self->workerAbort_.load(std::memory_order_relaxed)) { tapClosed = true; return true; }
                return false;
            };
            for (;;) {
                if (stop()) break;
                struct pollfd pfd { fd, POLLIN, 0 };
                int pr = ::poll(&pfd, 1, 20);                        // GIL not held
                if (pr == 0) continue;
                if (pr < 0) { if (errno == EINTR) continue; break; }
                if (stop()) break;                                   // a closed tap must not consume
                sockaddr_storage from{}; socklen_t fromLen = sizeof from;
                ssize_t n = ::recvfrom(fd, buf.data(), buf.size(), 0, (sockaddr*)&from, &fromLen);
                if (n < 0) {
                    // an ICMP port-unreachable from an earlier send surfaces
                    // here on some systems; the socket is still good
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNRESET || errno == ECONNREFUSED) continue;
                    break;
                }
                self->gilLock();
                ExecContext wctx; self->loadCtx(wctx);
                tctx_.cur = spawnScope;
                tctx_.dynStack.push_back(spawnScope.get());
                const std::string bytes(buf.data(), (size_t)n);
                bool quit = false;
                Value payload;
                if (bin) { payload = Value::str(bytes); payload.hashKind = "Buf"; payload.ofTypeM() = "uint8"; identify(payload); }
                else {
                    try {
                        Value blob = Value::str(bytes); blob.hashKind = "Buf"; blob.ofTypeM() = "uint8";
                        payload = self->methodCall(blob, "decode", ValueList{Value::str(readEnc)});
                    }
                    catch (RakuError& e) {
                        quit = true;
                        if (quitCb.t == VT::Code) {
                            ValueList one{e.payload.t == VT::Nil ? self->makeTypedEx("X::AdHoc", {}, e.message) : e.payload};
                            try { self->callCallable(quitCb, one); } catch (...) {}
                        }
                        else fprintf(stderr, "===WARNING=== UDP datagram decode failed: %s\n", e.message.c_str());
                    }
                }
                if (!quit && datagram) {
                    std::string host; long long port = 0;
                    asyncSockName(from, host, port);
                    Value o; o.t = VT::Object; o.setObj(makePayload<ObjectData>());
                    o.obj()->cls = self->classes_["IO::Socket::Async::Datagram"];
                    o.obj()->attrs["data"] = payload;
                    o.obj()->attrs["hostname"] = Value::str(host);
                    o.obj()->attrs["port"] = Value::integer(port);
                    payload = o;
                }
                if (!quit && emitCb.t == VT::Code) {
                    ValueList one{payload};
                    if (rctx0) self->reactStack_.push_back(rctx0);
                    auto pop = [&] { if (rctx0 && !self->reactStack_.empty()) self->reactStack_.pop_back(); };
                    try { self->callCallable(emitCb, one); pop(); }
                    catch (NextEx&) { pop(); }
                    catch (LastEx&) { pop(); tapClosed = true; }
                    catch (DoneEx&) { pop(); tapClosed = true; }
                    catch (RakuError& e) { pop(); fprintf(stderr, "===WARNING=== UDP read handler died: %s\n", e.message.c_str()); }
                    catch (...) { pop(); }
                }
                self->gilYieldNotify();
                if (quit) { tapClosed = true; break; }
                if (tapClosed) break;
            }
            // Let go of the descriptor BEFORE taking the GIL for `done`: a
            // close() on another thread waits for exactly this, GIL parked.
            {   std::lock_guard<std::mutex> lk(st->m);
                st->readers--;
                auto me = std::find(st->readerTids.begin(), st->readerTids.end(), std::this_thread::get_id());
                if (me != st->readerTids.end()) st->readerTids.erase(me);
                if (st->closed) sockClosed = true;
                if (st->closed && st->readers == 0 && st->fd >= 0) { ::close(st->fd); st->fd = -1; }
                st->cv.notify_all();
            }
            if (sockClosed && !tapClosed && doneCb.t == VT::Code) {
                self->gilLock();
                ExecContext wctx; self->loadCtx(wctx);
                tctx_.cur = spawnScope;
                tctx_.dynStack.push_back(spawnScope.get());
                ValueList na;
                try { self->callCallable(doneCb, na); } catch (...) {}
                self->gilYieldNotify();
            }
            self->liveWorkers_--;
            fin->store(true, std::memory_order_release);
        }), fin);
        if (!reactStack_.empty()) {
            auto rctx = reactStack_.back();
            std::lock_guard<std::mutex> lk(rctx->m);
            rctx->extTaps.push_back(handle);
        }
        Value t = inertTap();
        (*t.hash())["wired"] = Value::boolean(true);
        return t;
    }
    // 5) values-backed: eager push-through, then done (or quit)
    if (h.count("values")) {
        if (emitCb.t == VT::Code) for (auto& v : *h.at("values").arr()) {
            ValueList one{v};
            try { callCallable(emitCb, one); }
            catch (NextEx&) {}
            catch (LastEx&) { break; }
            catch (DoneEx&) { break; }
        }
        if (h.count("quit-reason")) {
            if (quitCb.t == VT::Code) { ValueList one{h.at("quit-reason")}; callCallable(quitCb, one); }
            else throw RakuError{h.at("quit-reason"),
                                 h.count("quit-message") ? h.at("quit-message").toStr() : "Supply quit"};
        }
        else if (doneCb.t == VT::Code) { ValueList na; callCallable(doneCb, na); }
    }
    Value t = Value::makeHash(); t.hashKind = "Tap"; return t;
}

// First numeric argument, coercing a Cool object via its .Bridge/.Numeric method.
// A custom Real — a class that `does Real` and defines `.Bridge` — has no
// numeric value of its own for `toNum()` to read: it answers 0. Ask the object
// for one instead, which is what `does Real` promises. Split out of numArg so
// that anything numifying an ARGUMENT can use it, not just the single-argument
// math builtins (S32-num/real-bridge.t).
double numValueOf(Interpreter& I, const Value& in) {
    Value v = in;
    if (v.t == VT::Object && v.obj()) {
        for (const char* acc : {"Bridge", "Numeric"}) {
            try { ValueList none; Value nv = I.methodCall(v, acc, none);
                  if (nv.isNumeric()) { v = nv; break; } } catch (...) {}
        }
    }
    return v.toNum();
}

// True named builtins (see Interpreter.h): real functions behind the hot
// builtins, shared by the interpreter's map entries and -O's direct calls.
Value rtBAbsSlow(Interpreter& I, const Value& v) {
    ValueList none;
    return I.methodCall(v, "abs", none);   // full semantics: augment, objects, junctions, Rat/big/Num
}
Value rtBChr(Interpreter& I, const Value& vIn) {
    // a STRING numifies as Raku reads numbers first: `"0x50".chr` is "P"
    // (integration/advent2009-day08.t)
    Value v = vIn;
    if (v.t == VT::Str && !v.isAllomorph() && v.hashKind.empty()) v = I.methodCall(v, "Int", ValueList{});
    long long cp = v.big() ? LLONG_MAX : v.toInt();
    if (cp < 0 || cp > 0x10FFFF)
        throw RakuError{Value::typeObj("X::AdHoc"),
            "chr codepoint " + (v.big() ? v.big()->toString() : std::to_string(cp)) + " is out of bounds"};
    // a Str is NFC, so a codepoint with a canonical singleton decomposition
    // comes back as its equivalent (`0x2000.chr.ord` is 0x2002); nothing
    // below U+0300 changes under NFC
    if (cp >= 0x300) {
        auto n = uniNormalize({(uint32_t)cp}, 1);
        if (n.size() != 1 || n[0] != (uint32_t)cp) {
            std::string out;
            for (uint32_t c : n) out += cpToUtf8(c);
            return Value::str(out);
        }
    }
    return Value::str(cpToUtf8((uint32_t)cp));
}
Value rtBOrd(Interpreter&, const Value& v) {
    auto c = utf8cp(v.toStr());
    return c.empty() ? Value::nil() : Value::integer(c[0]);
}
Value rtBSay(Interpreter& I, const Value& v)   { std::string out = I.gistOf(v); out += "\n"; return I.ioEmit(out, "$*OUT", false); }
Value rtBPrint(Interpreter& I, const Value& v) { return I.ioEmit(I.strInStrContext(v), "$*OUT", false); }
Value rtBPut(Interpreter& I, const Value& v)   { std::string out = I.strOf(v); out += "\n"; return I.ioEmit(out, "$*OUT", false); }
Value rtBNote(Interpreter& I, const Value& v)  { std::string out = I.gistOf(v); out += "\n"; return I.ioEmit(out, "$*ERR", true); }
Value rtBUc(Interpreter&, const Value& v)    { return Value::str(mapCase(v.toStr(), 1, 0)); }
Value rtBLc(Interpreter&, const Value& v)    { return Value::str(mapCase(v.toStr(), 0, 0)); }
Value rtBChars(Interpreter&, const Value& v) {
    // Straight off the cache for a plain Str: `.chars` in a scanning loop was
    // the other per-character O(n).
    if (v.t == VT::Str) return Value::integer(cowGraphemeCount(v.s));
    return Value::integer(graphemeCount(v.toStr()));
}
Value rtBSqrt(Interpreter& I, const Value& v) {
    if (v.t == VT::Complex) return complexSqrt(v.n, v.im());
    ValueList one{v};
    double x = numArg(I, one);   // same coercion the sub form uses (Object → .Bridge/.Numeric)
    if (x < 0 && I.langRev_ >= 2) return Value::complex(0, std::sqrt(-x));
    return Value::number(std::sqrt(x));
}
// Delegators — one methodCall, exactly the sub form (augment/objects/junctions intact).
static Value rtBMeth(Interpreter& I, const Value& v, const char* m) { ValueList none; return I.methodCall(v, m, none); }
Value rtBSignSlow(Interpreter& I, const Value& v) { return rtBMeth(I, v, "sign"); }
Value rtBTruncate(Interpreter& I, const Value& v) { return rtBMeth(I, v, "truncate"); }
Value rtBIsPrime(Interpreter& I, const Value& v)  { return rtBMeth(I, v, "is-prime"); }
Value rtBFlip(Interpreter& I, const Value& v)     { return rtBMeth(I, v, "flip"); }
Value rtBTrim(Interpreter& I, const Value& v)     { return rtBMeth(I, v, "trim"); }
Value rtBChomp(Interpreter& I, const Value& v)    { return rtBMeth(I, v, "chomp"); }
Value rtBChop(Interpreter& I, const Value& v)     { return rtBMeth(I, v, "chop"); }
Value rtBSin(Interpreter& I, const Value& v)   { return rtBMath1(I, v, "sin",   (double(*)(double))std::sin); }
Value rtBCos(Interpreter& I, const Value& v)   { return rtBMath1(I, v, "cos",   (double(*)(double))std::cos); }
Value rtBTan(Interpreter& I, const Value& v)   { return rtBMath1(I, v, "tan",   (double(*)(double))std::tan); }
Value rtBAsin(Interpreter& I, const Value& v)  { return rtBMath1(I, v, "asin",  (double(*)(double))std::asin); }
Value rtBAcos(Interpreter& I, const Value& v)  { return rtBMath1(I, v, "acos",  (double(*)(double))std::acos); }
Value rtBAtan(Interpreter& I, const Value& v)  { return rtBMath1(I, v, "atan",  (double(*)(double))std::atan); }
Value rtBSinh(Interpreter& I, const Value& v)  { return rtBMath1(I, v, "sinh",  (double(*)(double))std::sinh); }
Value rtBCosh(Interpreter& I, const Value& v)  { return rtBMath1(I, v, "cosh",  (double(*)(double))std::cosh); }
Value rtBTanh(Interpreter& I, const Value& v)  { return rtBMath1(I, v, "tanh",  (double(*)(double))std::tanh); }
Value rtBAsinh(Interpreter& I, const Value& v) { return rtBMath1(I, v, "asinh", rakuAsinh); }
Value rtBAcosh(Interpreter& I, const Value& v) { return rtBMath1(I, v, "acosh", rakuAcosh); }
Value rtBAtanh(Interpreter& I, const Value& v) { return rtBMath1(I, v, "atanh", (double(*)(double))std::atanh); }
// The logical working-directory name after entering `p` from `base`: purely
// textual, matching Rakudo's $*CWD — symlinks stay as the program spelled them
// (the real chdir has already validated the target), `.` and `..` collapse.
std::string logicalJoin(const std::string& base, const std::string& p) {
    std::string full = (!p.empty() && p[0] == '/') ? p : base + "/" + p;
    std::vector<std::string> keep;
    std::string cur;
    auto flush = [&] {
        if (cur.empty() || cur == ".") { cur.clear(); return; }
        if (cur == "..") { if (!keep.empty()) keep.pop_back(); }
        else keep.push_back(cur);
        cur.clear();
    };
    for (char c : full) { if (c == '/') flush(); else cur += c; }
    flush();
    std::string out;
    for (auto& s : keep) out += "/" + s;
    return out.empty() ? "/" : out;
}

// A relative IO::Path belongs to its own captured :CWD, and file operations
// must resolve against it: `IO::Path.new('x.txt', :CWD($dir)).e` used to stat
// x.txt wherever the PROCESS happened to stand (TAP::Harness runs whole suites
// through exactly that shape — SourceHandler dies "Failed to open file" on
// every .tap source given a :cwd). When the base IS the current directory —
// the overwhelmingly common case — keep the user's own spelling, so error
// texts and dir listings read the way they were written.
std::string Interpreter::ioFsPath(const Value& v) {
    if (v.hashKind != "IO" || v.t != VT::Str) return v.toStr();
    const std::string& p = v.s;
    if (p.empty() || p[0] == '/') return p;
#ifdef _WIN32
    // absolute here too: `D:\x`, `D:/x`, `\\host\x`, `\x`. A resolved path carries
    // a CWD of `/`, and joining the two made `/D:\x`, which no file test finds.
    if (p[0] == '\\' || (p.size() >= 2 && p[1] == ':')) return p;
#endif
    const std::string& base = v.ofType();
    if (base.empty()) return p;
    // …the current directory of the PROCESS, that is — the one the OS resolves
    // a relative path against. `temp $*CWD = $dir` moves $*CWD and not the
    // process, so a path captured under it ('foo'.IO.mkdir) must be joined,
    // or it lands wherever the program was started.
    if (!logicalCwd_.empty()) { if (base == logicalCwd_) return p; }
    else {
        char buf[4096];
        if (getcwd(buf, sizeof buf) && base == buf) return p;
    }
    return logicalJoin(base, p);
}

// ------------------------------------------------------- hidden line read ---
// `prompt(:hidden)`: read a line the terminal never echoes, for a password or
// any other secret typed at an interactive shell.
//
// ECHO is the ONLY flag cleared. ICANON stays on, so the kernel's line
// discipline still gives the typist backspace, ^U and ^W — the same deal
// `stty -echo` makes, and the reason this is not the REPL's raw mode.
//
// The saved settings go back on every exit path, including an exception out of
// the read, because a terminal left with echo off is a wrecked session: the
// shell keeps working but shows nothing typed into it.
//
// Not a tty — a pipe, a file, a here-doc, a test harness — is NOT an error and
// NOT silently refused: there is no echo to suppress, so the line is read
// plainly. That is what makes `:hidden` testable at all.
#if !defined(_WIN32)
// ^C at a password prompt must not wreck the shell. A destructor does not run
// when a signal's default action kills the process, so the terminal would be
// left with echo off and the user's next shell would show nothing they typed —
// measured, and exactly what naive `stty -echo` does. So the settings and a
// flag live at file scope, where a handler (which takes no context) can reach
// them. `tcsetattr` and `raise` are both async-signal-safe.
namespace {
termios g_echoSaved{};
volatile sig_atomic_t g_echoOff = 0;
const int g_echoSigs[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT };
const int g_echoNSigs = (int)(sizeof g_echoSigs / sizeof g_echoSigs[0]);
struct sigaction g_echoPrev[4];

extern "C" void echoRestoreHandler(int sig) {
    if (g_echoOff) {
        g_echoOff = 0;
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_echoSaved);
    }
    // Put the previous disposition back and re-raise: this handler exists only
    // to unwreck the terminal, never to change what the signal means. If rakupp
    // (or the embedder) had a handler of its own, it still runs.
    for (int i = 0; i < g_echoNSigs; i++)
        if (g_echoSigs[i] == sig) ::sigaction(sig, &g_echoPrev[i], nullptr);
    ::raise(sig);
}
} // namespace

struct EchoOff {
    bool on = false;
    EchoOff() {
        if (!::isatty(STDIN_FILENO)) return;
        if (::tcgetattr(STDIN_FILENO, &g_echoSaved) == -1) return;
        termios quiet = g_echoSaved;
        quiet.c_lflag &= ~(unsigned long)ECHO;
        // TCSAFLUSH, unlike the REPL's TCSADRAIN: anything typed AHEAD of the
        // prompt was typed while echo was still on, so it is already on the
        // screen. Draining it into the password would put that visible text in
        // the secret; discarding it is what getpass(3) does, and why.
        if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet) == -1) return;
        on = true;
        g_echoOff = 1;
        struct sigaction sa;
        std::memset(&sa, 0, sizeof sa);
        sa.sa_handler = echoRestoreHandler;
        sigemptyset(&sa.sa_mask);
        for (int i = 0; i < g_echoNSigs; i++)
            ::sigaction(g_echoSigs[i], &sa, &g_echoPrev[i]);
    }
    ~EchoOff() {
        if (!on) return;
        for (int i = 0; i < g_echoNSigs; i++)
            ::sigaction(g_echoSigs[i], &g_echoPrev[i], nullptr);
        g_echoOff = 0;
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_echoSaved);
    }
};
#endif
// True if a line was read; false at EOF. `echoed` says whether the terminal
// showed the keystrokes, which decides who has to supply the newline.
bool readHiddenLine(std::string& line, bool& echoed) {
    line.clear();
    echoed = true;
#if defined(_WIN32)
    // No termios. _getch reads a key without echoing it; the line discipline
    // goes with it, so backspace is ours to honour.
    if (_isatty(_fileno(stdin))) {
        echoed = false;
        for (;;) {
            int ch = _getch();
            if (ch == '\r' || ch == '\n') return true;
            if (ch == 3) { std::exit(130); }               // ^C
            if (ch == 26 || ch == EOF) return !line.empty(); // ^Z
            if (ch == '\b' || ch == 127) { if (!line.empty()) line.pop_back(); continue; }
            if (ch == 0 || ch == 0xE0) { _getch(); continue; } // a function key's second byte
            line += (char)ch;
        }
    }
#else
    EchoOff off;
    echoed = !off.on;
#endif
    if (!std::getline(std::cin, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

// The error `die` raises for these arguments — built here, not thrown, so a
// block that hands its errors on (execStmtHanding) can take a statement-level
// `die` without the C++ throw. The backtrace is captured as it is made.
RakuError Interpreter::dieError(ValueList& a) {
    Value payload = a.empty() ? Value::str("Died") : a[0];
    // die with no argument reuses the current $! ("Died" only if $! is undefined)
    // (…the ROUTINE's own $!: a sub's `die()` does not see its caller's error)
    if (a.empty()) {
        Value* be = nullptr;
        for (Env* en = tctx_.cur.get(); en; en = en->parent.get()) {
            if ((be = en->local("$!"))) break;
            if (en->routineFrame) break;
        }
        if (be && be->t != VT::Nil && be->t != VT::Type) payload = *be;
    }
    std::string msg = payload.toStr();
    // `die($p, 42)` — several values: the message is their concatenation
    // and the X::AdHoc's payload the whole list
    if (a.size() > 1) {
        msg.clear();
        for (auto& x : a) msg += x.t == VT::Object ? methodCall(x, "Str", ValueList{}).toStr() : x.toStr();
        Value lst = Value::array(); lst.isList = true; *lst.arr() = a;
        payload = lst;
    }
    // an object that is not an Exception is thrown as an X::AdHoc CARRYING it
    // (the X:: name marks only the built-in ones: a program's own `class
    // X::Libgsl { method throw { die self } }` with no `is Exception` is not one)
    bool isException = false;
    if (payload.t == VT::Object && payload.obj())
        for (ClassInfo* c = payload.obj()->cls.get(); c && !isException; c = c->parent.get())
            if (c->name == "Exception" || c->nativeParent == "Exception" ||
                (!c->decl && (c->name.rfind("X::", 0) == 0 || c->name.rfind("CX::", 0) == 0)))
                isException = true;
    // exception objects: prefer a readable .message / .Str accessor
    if (payload.t == VT::Object && payload.obj() && isException) {
        for (const char* acc : {"message", "Str"}) {
            try { ValueList none; Value m = methodCall(payload, acc, none);
                  if (m.t == VT::Str && !m.s.empty()) { msg = m.s; break; } } catch (...) {}
        }
    } else {
        if (payload.t == VT::Object && payload.obj() && a.size() == 1)   // its .Str is the message
            try { msg = methodCall(payload, "Str", ValueList{}).toStr(); } catch (...) {}
        // wrap a plain string/number into an X::AdHoc exception (so .message/.^name work in CATCH)
        auto it = classes_.find("X::AdHoc");
        if (it != classes_.end()) {
            Value ex; ex.t = VT::Object; ex.setObj(makePayload<ObjectData>());
            ex.obj()->cls = it->second;
            ex.obj()->attrs["message"] = Value::str(msg);
            ex.obj()->attrs["payload"] = a.empty() ? Value::str(msg) : a.size() > 1 ? payload : a[0]; // .payload is what was thrown
            payload = ex;
        }
    }
    g_btSettingFrames = 2;   // the setting's `die` and the `throw` it makes (see btCaptureNow)
    return RakuError{payload, msg};
}

} // namespace rakupp
