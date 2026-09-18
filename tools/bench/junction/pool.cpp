// Break-even floor: what a *pooled* fan-out/join costs on this machine,
// with no interpreter in the picture at all. This is the best case any
// in-engine parallel junction could hope to match.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

struct Pool {
    std::vector<std::thread> th;
    std::mutex m; std::condition_variable cv, done_cv;
    int gen = 0, left = 0; bool stop = false;
    std::function<void(int)> job; int nw;
    Pool(int n) : nw(n) {
        for (int i = 0; i < n; i++) th.emplace_back([this, i] {
            int seen = 0;
            for (;;) {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [&]{ return stop || gen != seen; });
                if (stop) return;
                seen = gen; auto f = job; lk.unlock();
                f(i);
                lk.lock(); if (--left == 0) done_cv.notify_one();
            }
        });
    }
    void run(std::function<void(int)> f) {
        { std::lock_guard<std::mutex> lk(m); job = f; left = nw; gen++; }
        cv.notify_all();
        std::unique_lock<std::mutex> lk(m); done_cv.wait(lk, [&]{ return left == 0; });
    }
    ~Pool() { { std::lock_guard<std::mutex> lk(m); stop = true; } cv.notify_all(); for (auto& t : th) t.join(); }
};

int main() {
    for (int n : {2, 4, 8}) {
        Pool p(n);
        std::atomic<long> sink{0};
        p.run([&](int){ sink++; });                       // warm
        const int reps = 20000;
        auto t0 = std::chrono::steady_clock::now();
        for (int r = 0; r < reps; r++) p.run([&](int i){ sink += i; });
        auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / reps;
        printf("pooled fan-out/join, %d workers: %7.2f us per round (%6.2f us per worker)  sink=%ld\n",
               n, dt * 1e6, dt * 1e6 / n, sink.load());
    }
    // and the cost of a bare thread, for contrast
    const int reps = 2000;
    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; r++) { std::thread t([]{}); t.join(); }
    auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / reps;
    printf("bare std::thread spawn+join:        %7.2f us\n", dt * 1e6);
    return 0;
}
