// JupyterKernel.cpp — `rakupp --jupyter FILE`: the interpreter as a Jupyter kernel.
//
// A notebook talks to its kernel over five ZeroMQ sockets whose ports arrive
// in a JSON connection file: shell (cells come in, replies go out), control
// (shutdown and interrupt, on their own socket so a busy shell cannot block
// them), stdin (unused here — see below), iopub (everything the frontend
// displays: output, results, errors, the busy/idle lamp) and heartbeat (an
// echo the frontend pings to know the kernel lives).
//
// Everything engine-side goes through the public C ABI (rakupp.h), exactly as
// McpServer.cpp does: one interpreter, created once, living for the kernel's
// life — so a sub defined in cell 3 is callable in cell 9, because rk_eval IS
// a session. Cell output crosses through rk_set_output and is republished on
// iopub as it arrives, so a long loop prints while it runs.
//
// THE TRANSPORT IS OURS. Jupyter's wire protocol rides on ZeroMQ, and rakupp
// links no third-party library, so this file speaks ZMTP 3.0 (the ZeroMQ
// Message Transport Protocol, rfc.zeromq.org/spec/23) over plain TCP: the
// 64-byte greeting, the NULL-mechanism READY handshake, MORE-chained frames,
// and only the three socket behaviours a kernel needs —
//
//   ROUTER (shell, control, stdin) — a reply goes back down the connection it
//           arrived on. libzmq's ROUTER prepends a routing frame for its own
//           application's benefit; ours does not need one, because the peer's
//           connection IS the route, and a DEALER on the other end reads the
//           frames we write unchanged.
//   PUB    (iopub) — fan out to every subscriber whose prefix matches the
//           topic frame. Subscriptions arrive both ways (a 3.0 message whose
//           first byte is 0x01, a 3.1 SUBSCRIBE command); both are honoured.
//   REP    (heartbeat) — echo the frames back, which is all the frontend's
//           REQ socket wants to see.
//
// Two things a kernel usually has and this one does not, both honest rather
// than faked:
//   * stdin is pinned to EOF (rk_set_input), so `get` in a cell returns Nil
//     instead of hanging a notebook that may have no way to answer.
//   * a running cell cannot be interrupted — rk_eval has no interrupt in the
//     ABI — so interrupt_request answers, says so on iopub, and EXITS, which
//     the frontend shows as a restart. Same choice as the MCP watchdog: a
//     wedged client is worse than a lost session.
#include "JupyterKernel.h"
#include "JsonLite.h"
#include "Platform.h"
#include "rakupp.h"
#ifndef _WIN32
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace rakupp::jupyter {
namespace {

using rakupp::json::Json;
using rakupp::json::dumps;
using rakupp::json::parse;

// ---------------------------------------------------------------------------
// SHA-256 and HMAC. Every Jupyter message carries an HMAC-SHA256 signature of
// its four JSON blobs, keyed by the connection file's `key`: a kernel that
// cannot verify one has to reject the message, so this cannot be optional.
// Textbook FIPS 180-4 — small, self-contained, and gated against RFC 4231
// vectors by tools/jupyter-smoke.raku.
// ---------------------------------------------------------------------------

class Sha256 {
public:
    Sha256() { reset(); }
    void reset() {
        static const uint32_t iv[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                       0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        std::memcpy(h_, iv, sizeof h_);
        len_ = 0;
        n_ = 0;
    }
    void update(const void* data, size_t n) {
        const uint8_t* p = (const uint8_t*)data;
        len_ += n;
        while (n) {
            size_t take = 64 - n_;
            if (take > n) take = n;
            std::memcpy(buf_ + n_, p, take);
            n_ += take;
            p += take;
            n -= take;
            if (n_ == 64) { block(buf_); n_ = 0; }
        }
    }
    void update(const std::string& s) { update(s.data(), s.size()); }
    void finish(uint8_t out[32]) {
        uint64_t bits = len_ * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (n_ != 56) update(&zero, 1);
        uint8_t tail[8];
        for (int i = 0; i < 8; i++) tail[i] = (uint8_t)(bits >> (56 - 8 * i));
        update(tail, 8);
        for (int i = 0; i < 8; i++)
            for (int j = 0; j < 4; j++) out[i * 4 + j] = (uint8_t)(h_[i] >> (24 - 8 * j));
    }

private:
    static uint32_t ror(uint32_t x, int k) { return (x >> k) | (x << (32 - k)); }
    void block(const uint8_t p[64]) {
        static const uint32_t K[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
                   ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
    }

    uint32_t h_[8];
    uint64_t len_ = 0;
    uint8_t buf_[64];
    size_t n_ = 0;
};

// The kernel's own diagnostics go to the C stderr, deliberately. rk_set_output
// swaps std::cerr's streambuf process-wide so a CELL's output can be captured,
// which means a std::cerr line from the kernel would arrive in the notebook as
// the cell's own stderr — a log message wearing a user's output as a disguise.
// stderr the FILE* is untouched by that swap, and Jupyter puts it in its log.
void logf(const std::string& msg) {
    std::string line = "rakupp --jupyter: " + msg + "\n";
    std::fputs(line.c_str(), stderr);
    std::fflush(stderr);
}

std::string toHex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 0xF]; }
    return s;
}

// HMAC-SHA256 of the concatenated parts, hex-encoded — the signature field's
// exact spelling in the protocol (lower-case hex, never base64).
std::string hmacSha256Hex(const std::string& key, const std::vector<std::string>& parts) {
    uint8_t k[64];
    std::memset(k, 0, sizeof k);
    if (key.size() > 64) {
        Sha256 s;
        s.update(key);
        uint8_t d[32];
        s.finish(d);
        std::memcpy(k, d, 32);
    }
    else { std::memcpy(k, key.data(), key.size()); }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = (uint8_t)(k[i] ^ 0x36); opad[i] = (uint8_t)(k[i] ^ 0x5c); }
    Sha256 inner;
    inner.update(ipad, 64);
    for (auto& p : parts) inner.update(p);
    uint8_t id[32];
    inner.finish(id);
    Sha256 outer;
    outer.update(opad, 64);
    outer.update(id, 32);
    uint8_t od[32];
    outer.finish(od);
    return toHex(od, 32);
}

// Comparing signatures with == leaks their prefix through timing; a forged
// message must not be gradually discoverable.
bool constantTimeEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned diff = 0;
    for (size_t i = 0; i < a.size(); i++) diff |= (unsigned)(a[i] ^ b[i]);
    return diff == 0;
}

// ---------------------------------------------------------------------------
// Small protocol values: message ids and dates.
// ---------------------------------------------------------------------------

std::string uuid4() {
    static std::mutex mx;
    static std::mt19937_64 rng((uint64_t)std::random_device{}() * 0x9E3779B97F4A7C15ull ^
                               (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
    uint64_t a, b;
    {
        std::lock_guard<std::mutex> lk(mx);
        a = rng();
        b = rng();
    }
    uint8_t v[16];
    for (int i = 0; i < 8; i++) { v[i] = (uint8_t)(a >> (8 * i)); v[8 + i] = (uint8_t)(b >> (8 * i)); }
    v[6] = (uint8_t)((v[6] & 0x0F) | 0x40); // version 4
    v[8] = (uint8_t)((v[8] & 0x3F) | 0x80); // variant 1
    std::string h = toHex(v, 16);
    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" +
           h.substr(16, 4) + "-" + h.substr(20);
}

// ISO 8601 UTC with microseconds, the header's `date`.
std::string isoNow() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1000000;
    std::tm tm{};
#ifdef _WIN32
    ::gmtime_s(&tm, &t);
#else
    ::gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%06lldZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, (long long)us);
    return buf;
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            if (start < s.size()) out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    if (out.empty()) out.push_back(s);
    return out;
}

// ---------------------------------------------------------------------------
// ZMTP 3.0 over TCP — as much ZeroMQ as a kernel needs, and no more.
//
// One listening socket per channel, one pump thread behind it: the pump
// accepts peers, walks each one through the greeting and the NULL handshake,
// and cuts the byte stream into frames. Complete messages land in a queue the
// serving thread pops; replies are written straight back to the peer they
// came from. Everything a real ZeroMQ would do beyond that — reconnection,
// queueing high-water marks, the other twelve socket types — is absent on
// purpose: a kernel's peer is one frontend that connects once.
// ---------------------------------------------------------------------------

inline void closeSock(int fd) {
#ifdef _WIN32
    ::closesocket((SOCKET)fd);
#else
    ::close(fd);
#endif
}

enum class Kind { Router, Pub, Rep };

const char* kindName(Kind k) {
    switch (k) {
        case Kind::Router: return "ROUTER";
        case Kind::Pub:    return "PUB";
        case Kind::Rep:    return "REP";
    }
    return "ROUTER";
}

struct Peer {
    int fd = -1;
    std::atomic<bool> greeted{false};   // the peer sent its 64-byte greeting
    std::atomic<bool> dead{false};
    std::string in;                    // bytes read but not yet parsed
    std::vector<std::string> partial;  // frames of a message still MORE-chained
    std::vector<std::string> subs;     // PUB only: the prefixes this peer wants
    std::mutex wmx;                    // one writer at a time down this socket
};
using PeerRef = std::shared_ptr<Peer>;

struct ZMsg {
    PeerRef from;
    std::vector<std::string> frames;
};

// A frame on the wire: flags byte (MORE 0x1, LONG 0x2, COMMAND 0x4), then the
// size — one byte under 256, else eight big-endian — then the body.
std::string frameBytes(const std::string& body, bool more, bool command) {
    std::string out;
    uint8_t flags = (uint8_t)((more ? 1 : 0) | (command ? 4 : 0));
    if (body.size() < 256) {
        out += (char)flags;
        out += (char)(uint8_t)body.size();
    }
    else {
        out += (char)(uint8_t)(flags | 2);
        for (int i = 7; i >= 0; i--) out += (char)(uint8_t)(body.size() >> (i * 8));
    }
    out += body;
    return out;
}

// A ZMTP command: a length-prefixed name, then the command's own body.
std::string commandBytes(const std::string& name, const std::string& body) {
    std::string cmd;
    cmd += (char)(uint8_t)name.size();
    cmd += name;
    cmd += body;
    return frameBytes(cmd, false, true);
}

class ZSock {
public:
    explicit ZSock(Kind k) : kind_(k) {}
    ~ZSock() { stop(); }

    bool bindTcp(const std::string& ip, int port, std::string& err) {
        lfd_ = (int)::socket(AF_INET, SOCK_STREAM, 0);
        if (lfd_ < 0) { err = "socket() failed"; return false; }
        int yes = 1;
        ::setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        addr.sin_addr.s_addr = ip.empty() ? INADDR_ANY : ::inet_addr(ip.c_str());
        if (addr.sin_addr.s_addr == INADDR_NONE) {
            err = "cannot parse ip '" + ip + "' (only dotted-quad addresses are accepted)";
            closeSock(lfd_);
            lfd_ = -1;
            return false;
        }
        if (::bind(lfd_, (sockaddr*)&addr, sizeof addr) < 0 || ::listen(lfd_, 8) < 0) {
            err = "cannot bind " + ip + ":" + std::to_string(port);
            closeSock(lfd_);
            lfd_ = -1;
            return false;
        }
        return true;
    }

    void start() { pump_ = std::thread([this] { pump(); }); }

    // Joined, never detached — the same lesson McpServer's watchdog carries:
    // a thread outliving its condition variable is a Linux-only hang waiting
    // to happen.
    void stop() {
        if (quit_.exchange(true)) return;
        cv_.notify_all();
        if (pump_.joinable()) pump_.join();
        if (lfd_ >= 0) { closeSock(lfd_); lfd_ = -1; }
        std::lock_guard<std::mutex> lk(mx_);
        for (auto& p : peers_)
            if (!p->dead) { p->dead = true; closeSock(p->fd); }
        peers_.clear();
    }

    // Blocks until a message arrives; false once the socket is stopped.
    bool recv(ZMsg& out) {
        std::unique_lock<std::mutex> lk(mx_);
        cv_.wait(lk, [this] { return quit_.load() || !queue_.empty(); });
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void send(const PeerRef& to, const std::vector<std::string>& frames) {
        if (!to || to->dead) return;
        std::string bytes;
        for (size_t i = 0; i < frames.size(); i++)
            bytes += frameBytes(frames[i], i + 1 < frames.size(), false);
        writeAll(to, bytes);
    }

    // PUB: frames[0] is the topic, and a peer sees the message only if one of
    // its subscriptions is a prefix of it. A message published before the
    // frontend has subscribed is dropped — that is what PUB means, and why
    // the protocol has frontends wait for a kernel_info reply first.
    void publish(const std::vector<std::string>& frames) {
        if (frames.empty()) return;
        std::string bytes;
        for (size_t i = 0; i < frames.size(); i++)
            bytes += frameBytes(frames[i], i + 1 < frames.size(), false);
        std::vector<PeerRef> targets;
        {
            std::lock_guard<std::mutex> lk(mx_);
            for (auto& p : peers_) {
                if (p->dead || !p->greeted) continue;
                for (auto& s : p->subs)
                    if (frames[0].compare(0, s.size(), s) == 0) { targets.push_back(p); break; }
            }
        }
        for (auto& p : targets) writeAll(p, bytes);
    }

private:
    // Our half of the greeting: signature, version 3.0, NULL mechanism.
    // Version 3.0 rather than 3.1 keeps the peer on the older subscription
    // form and off ZMTP heartbeats — less protocol for a link that only ever
    // carries one frontend. Both spellings are handled anyway, because
    // "advertise less, accept more" is what keeps this working when libzmq
    // changes its mind.
    static std::string greetingBytes() {
        std::string g;
        g += (char)(uint8_t)0xFF;
        g.append(8, '\0');
        g += (char)(uint8_t)0x7F;
        g += (char)3;   // version major
        g += (char)0;   // version minor
        std::string mech = "NULL";
        mech.resize(20, '\0');
        g += mech;
        g += (char)0;   // as-server: meaningless under NULL
        g.append(31, '\0');
        return g;
    }

    std::string readyBytes() const {
        std::string props;
        auto prop = [&props](const std::string& k, const std::string& v) {
            props += (char)(uint8_t)k.size();
            props += k;
            for (int i = 3; i >= 0; i--) props += (char)(uint8_t)(v.size() >> (i * 8));
            props += v;
        };
        prop("Socket-Type", kindName(kind_));
        return commandBytes("READY", props);
    }

    bool writeAll(const PeerRef& p, const std::string& bytes) {
        std::lock_guard<std::mutex> lk(p->wmx);
        if (p->dead) return false;
        size_t off = 0;
        while (off < bytes.size()) {
#ifdef MSG_NOSIGNAL
            auto n = ::send(p->fd, bytes.data() + off, (int)(bytes.size() - off), MSG_NOSIGNAL);
#else
            auto n = ::send(p->fd, bytes.data() + off, (int)(bytes.size() - off), 0);
#endif
            if (n <= 0) {
                if (n < 0 && (errno == EINTR)) continue;
                p->dead = true;
                return false;
            }
            off += (size_t)n;
        }
        return true;
    }

    void pump() {
        std::string greeting = greetingBytes();
        while (!quit_.load()) {
            std::vector<PeerRef> snap;
            {
                std::lock_guard<std::mutex> lk(mx_);
                snap = peers_;
            }
            std::vector<pollfd> fds;
            fds.push_back(pollfd{lfd_, POLLIN, 0});
            for (auto& p : snap) fds.push_back(pollfd{p->fd, POLLIN, 0});
            int rc = ::poll(fds.data(), (unsigned long)fds.size(), 200);
            if (quit_.load()) break;
            if (rc < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (rc == 0) continue;
            if (fds[0].revents & POLLIN) {
                int fd = (int)::accept(lfd_, nullptr, nullptr);
                if (fd >= 0) {
                    auto p = std::make_shared<Peer>();
                    p->fd = fd;
                    // Speak first: ZMTP lets either side open with its
                    // greeting, and doing it here means a peer that connects
                    // and waits (libzmq's connecting side does exactly that)
                    // never stalls.
                    writeAll(p, greeting);
                    std::lock_guard<std::mutex> lk(mx_);
                    peers_.push_back(p);
                }
            }
            for (size_t i = 1; i < fds.size(); i++) {
                if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
                readPeer(snap[i - 1]);
            }
            reapDead();
        }
    }

    void readPeer(const PeerRef& p) {
        char buf[65536];
        auto n = ::recv(p->fd, buf, (int)sizeof buf, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) return;
            p->dead = true;
            return;
        }
        p->in.append(buf, (size_t)n);
        drain(p);
    }

    void reapDead() {
        std::vector<PeerRef> gone;
        {
            std::lock_guard<std::mutex> lk(mx_);
            for (size_t i = 0; i < peers_.size();) {
                if (peers_[i]->dead) { gone.push_back(peers_[i]); peers_.erase(peers_.begin() + (long)i); }
                else { i++; }
            }
        }
        for (auto& p : gone) closeSock(p->fd);
    }

    // Cut whatever has arrived into frames, delivering every complete
    // message. Returns with the leftovers still in p->in.
    void drain(const PeerRef& p) {
        for (;;) {
            if (!p->greeted) {
                if (p->in.size() < 64) return;
                if ((uint8_t)p->in[0] != 0xFF || (uint8_t)p->in[9] != 0x7F) {
                    logf("peer sent no ZMTP greeting; dropping it");
                    p->dead = true;
                    return;
                }
                std::string mech = p->in.substr(12, 20);
                size_t z = mech.find('\0');
                if (z != std::string::npos) mech.resize(z);
                if (mech != "NULL") {
                    logf("peer wants the " + mech +
                         " security mechanism; this kernel speaks NULL only");
                    p->dead = true;
                    return;
                }
                p->in.erase(0, 64);
                p->greeted = true;
                writeAll(p, readyBytes());
                continue;
            }
            if (p->in.size() < 2) return;
            uint8_t flags = (uint8_t)p->in[0];
            size_t off, size;
            if (flags & 2) {
                if (p->in.size() < 9) return;
                size = 0;
                for (int i = 0; i < 8; i++) size = (size << 8) | (uint8_t)p->in[1 + i];
                off = 9;
            }
            else {
                size = (uint8_t)p->in[1];
                off = 2;
            }
            if (p->in.size() < off + size) return;
            std::string body = p->in.substr(off, size);
            p->in.erase(0, off + size);
            if (flags & 4) { onCommand(p, body); continue; }
            if (kind_ == Kind::Pub) { onSubscriptionMessage(p, body); continue; }
            p->partial.push_back(std::move(body));
            if (flags & 1) continue;  // MORE: the message is not finished
            ZMsg m;
            m.from = p;
            m.frames.swap(p->partial);
            {
                std::lock_guard<std::mutex> lk(mx_);
                queue_.push_back(std::move(m));
            }
            cv_.notify_one();
        }
    }

    void onCommand(const PeerRef& p, const std::string& body) {
        if (body.empty()) return;
        size_t nameLen = (uint8_t)body[0];
        if (body.size() < 1 + nameLen) return;
        std::string name = body.substr(1, nameLen);
        std::string rest = body.substr(1 + nameLen);
        if (name == "READY") return;              // its properties tell us nothing we act on
        if (name == "PING") {
            // ZMTP 3.1 heartbeat: the PONG must echo the PING's context,
            // which follows a two-byte TTL.
            std::string ctx = rest.size() >= 2 ? rest.substr(2) : std::string();
            writeAll(p, commandBytes("PONG", ctx));
            return;
        }
        if (kind_ == Kind::Pub && (name == "SUBSCRIBE" || name == "CANCEL")) {
            setSubscription(p, rest, name == "SUBSCRIBE");
            return;
        }
        if (name == "ERROR") p->dead = true;
    }

    // The ZMTP 3.0 spelling of the same thing: an ordinary message frame
    // whose first byte says subscribe (1) or cancel (0).
    void onSubscriptionMessage(const PeerRef& p, const std::string& body) {
        if (body.empty()) return;
        setSubscription(p, body.substr(1), (uint8_t)body[0] == 1);
    }

    void setSubscription(const PeerRef& p, const std::string& prefix, bool add) {
        std::lock_guard<std::mutex> lk(mx_);
        if (add) { p->subs.push_back(prefix); return; }
        for (size_t i = 0; i < p->subs.size(); i++)
            if (p->subs[i] == prefix) { p->subs.erase(p->subs.begin() + (long)i); return; }
    }

    Kind kind_;
    int lfd_ = -1;
    std::thread pump_;
    std::atomic<bool> quit_{false};
    std::mutex mx_;
    std::condition_variable cv_;
    std::vector<PeerRef> peers_;
    std::deque<ZMsg> queue_;
};

// ---------------------------------------------------------------------------
// The Jupyter message layer: what those frames mean.
//
//   [ident…] "<IDS|MSG>" signature header parent metadata content [buffers…]
//
// The signature is the HMAC of the four JSON blobs in that order. A message
// whose signature does not check out is DROPPED, not answered: it did not
// come from the frontend that owns this kernel.
// ---------------------------------------------------------------------------

const char* kDelim = "<IDS|MSG>";
const char* kProtocolVersion = "5.3";

struct JMsg {
    std::vector<std::string> idents;   // the routing frames, echoed back verbatim
    Json header, parent, metadata, content;
    std::string msgType;
};

bool decodeJupyter(const std::vector<std::string>& fr, const std::string& key,
                   JMsg& out, std::string& err) {
    size_t d = fr.size();
    for (size_t i = 0; i < fr.size(); i++)
        if (fr[i] == kDelim) { d = i; break; }
    if (d == fr.size()) { err = "no <IDS|MSG> delimiter"; return false; }
    if (fr.size() < d + 6) { err = "short message"; return false; }
    const std::string& sig = fr[d + 1];
    if (!key.empty()) {
        std::string want = hmacSha256Hex(key, {fr[d + 2], fr[d + 3], fr[d + 4], fr[d + 5]});
        if (!constantTimeEqual(sig, want)) { err = "bad signature"; return false; }
    }
    out.idents.assign(fr.begin(), fr.begin() + (long)d);
    if (!parse(fr[d + 2], out.header) || !parse(fr[d + 3], out.parent) ||
        !parse(fr[d + 4], out.metadata) || !parse(fr[d + 5], out.content)) {
        err = "malformed JSON in a message part";
        return false;
    }
    out.msgType = out.header.getStr("msg_type");
    return true;
}

std::vector<std::string> encodeJupyter(const std::vector<std::string>& idents,
                                       const std::string& key, const std::string& session,
                                       const std::string& msgType, const Json& parentHeader,
                                       const Json& content, const Json& metadata) {
    Json header = Json::object();
    header.set("msg_id", Json::str(uuid4()));
    header.set("session", Json::str(session));
    header.set("username", Json::str("rakupp"));
    header.set("date", Json::str(isoNow()));
    header.set("msg_type", Json::str(msgType));
    header.set("version", Json::str(kProtocolVersion));

    std::string h = dumps(header);
    std::string p = parentHeader.t == Json::T::Obj ? dumps(parentHeader) : std::string("{}");
    std::string m = dumps(metadata);
    std::string c = dumps(content);

    std::vector<std::string> fr = idents;
    fr.push_back(kDelim);
    fr.push_back(key.empty() ? std::string() : hmacSha256Hex(key, {h, p, m, c}));
    fr.push_back(h);
    fr.push_back(p);
    fr.push_back(m);
    fr.push_back(c);
    return fr;
}

// ---------------------------------------------------------------------------
// The session: one interpreter for the kernel's whole life, so cell 9 sees
// what cell 3 defined. A near-twin of McpServer's Session — same ABI calls,
// same reasons — differing where a notebook does: output is not collected
// into a buffer here but forwarded as it is produced, so a printing loop
// shows its work.
// ---------------------------------------------------------------------------

// Loaded at session start: how a cell's value is rendered. Same fallback
// chain the MCP server uses — .gist, then .raku, then an honest placeholder —
// because a value whose gist dies must not take the cell's result with it.
const char* kPrelude = R"RKJUP(
sub rk-jup-gist($v) { (try $v.gist) // ((try $v.raku) // '(unprintable value)') }
)RKJUP";

using OutputSink = void (*)(void* ud, const char* text, size_t len, int isErr);

class Session {
public:
    std::string start(const std::vector<std::string>& preload, OutputSink sink, void* ud) {
        RkConfig cfg{};
        cfg.size = sizeof cfg;
        cfg.own_stack = 1;  // deep recursion meets the engine's guard, as the CLI's does
        rk_ = rk_new(&cfg);
        if (!rk_) return "rk_new refused: an interpreter is already live in this process";
        c_ = rk_ctx(rk_);
        rk_set_output(rk_, sink, ud);
        // A notebook cell has no keyboard behind it: `get` sees EOF rather
        // than blocking a frontend that may have no way to answer.
        rk_set_input(rk_, "", 0);
        if (rk_eval(rk_, kPrelude, nullptr) != RK_OK)
            return std::string("jupyter prelude failed: ") + lastError();
        for (auto& m : preload) {
            std::string use = "use " + m + ";";
            if (rk_eval(rk_, use.c_str(), nullptr) != RK_OK)
                return "-M " + m + " failed: " + lastError();
        }
        return "";
    }

    RkInterp rk() { return rk_; }
    RkCtx ctx() { return c_; }

    std::string lastError() {
        const char* e = rk_last_error(rk_);
        return e ? e : "unknown engine error";
    }

    // The REPL's rendering of a value, through the prelude's gist chain.
    std::string gist(RkValue v) {
        RkValue rooted = rk_root(c_, v);
        RkValue g = rk_call(c_, "rk-jup-gist", &rooted, 1);
        std::string out;
        if (const char* e = rk_error(c_)) {
            out = e;
            rk_clear_error(c_);
        }
        else {
            size_t len = 0;
            const char* s = rk_str_get(c_, g, &len);
            out.assign(s ? s : "", len);
        }
        rk_unroot(c_, rooted);
        return out;
    }

    // What language_info reports: the Raku the engine implements, not the
    // engine's own version (that is implementation_version).
    std::string languageVersion() {
        RkValue v = nullptr;
        if (rk_eval(rk_, "~$*RAKU.version", &v) != RK_OK || !v) return "6.d";
        size_t len = 0;
        const char* s = rk_str_get(c_, v, &len);
        std::string out(s ? s : "", len);
        return out.empty() ? "6.d" : out;
    }

private:
    RkInterp rk_ = nullptr;
    RkCtx c_ = nullptr;
};

// ---------------------------------------------------------------------------
// The kernel.
// ---------------------------------------------------------------------------

class Kernel {
public:
    int run(const Options& opt);

private:
    // ---- publishing ----
    void publish(const std::string& msgType, const Json& content) {
        Json parent;
        {
            std::lock_guard<std::mutex> lk(parentMx_);
            parent = parent_;
        }
        publishWithParent(msgType, content, parent);
    }
    void publishWithParent(const std::string& msgType, const Json& content, const Json& parent) {
        // The topic frame. Frontends subscribe to everything, so its only job
        // is to be a stable, human-legible label in a packet trace.
        std::vector<std::string> idents{msgType};
        auto fr = encodeJupyter(idents, key_, session_, msgType, parent, content, Json::object());
        iopub_.publish(fr);
    }
    void publishStatus(const char* state) {
        Json c = Json::object();
        c.set("execution_state", Json::str(state));
        publish("status", c);
    }
    void publishStream(const char* name, const std::string& text) {
        Json c = Json::object();
        c.set("name", Json::str(name));
        c.set("text", Json::str(text));
        publish("stream", c);
    }

    void reply(ZSock& sock, const ZMsg& to, const JMsg& req, const std::string& msgType,
               const Json& content) {
        auto fr = encodeJupyter(req.idents, key_, session_, msgType, req.header, content,
                                Json::object());
        sock.send(to.from, fr);
    }

    // ---- output from the engine, forwarded as it is produced ----
    static void onOutput(void* ud, const char* text, size_t len, int isErr) {
        auto* self = (Kernel*)ud;
        if (!len) return;
        self->publishStream(isErr ? "stderr" : "stdout", std::string(text, len));
    }

    // ---- jupyter-display(...) : the host function a cell calls for rich output ----
    static RkValue hostDisplay(RkCtx c, void* ud) {
        auto* self = (Kernel*)ud;
        if (rk_argc(c) < 1) {
            rk_die(c, "jupyter-display expects the data to display, optionally with a MIME type");
            return nullptr;
        }
        auto asText = [&](RkValue v) {
            size_t len = 0;
            const char* s = rk_str_get(c, v, &len);
            return std::string(s ? s : "", len);
        };
        std::string data = asText(rk_arg(c, 0));
        std::string mime = "text/plain";
        if (rk_argc(c) > 1) mime = asText(rk_arg(c, 1));
        else if (RkValue m = rk_named(c, "mime")) mime = asText(m);
        Json d = Json::object();
        d.set(mime, Json::str(data));
        // text/plain is what a frontend falls back to; give it one so the
        // output is never blank in a client that cannot render the MIME.
        if (mime != "text/plain") d.set("text/plain", Json::str("[" + mime + "]"));
        Json content = Json::object();
        content.set("data", std::move(d));
        content.set("metadata", Json::object());
        content.set("transient", Json::object());
        self->publish("display_data", content);
        return rk_bool(c, 1);
    }

    // ---- handlers ----
    Json kernelInfo() {
        Json li = Json::object();
        li.set("name", Json::str("raku"));
        li.set("version", Json::str(langVersion_));
        li.set("mimetype", Json::str("text/x-raku"));
        li.set("file_extension", Json::str(".raku"));
        // 'perl6' rather than 'raku': it is the alias every Pygments and
        // CodeMirror that knows this language answers to, including the ones
        // predating the rename, and the newer ones kept it.
        li.set("pygments_lexer", Json::str("perl6"));
        li.set("codemirror_mode", Json::str("perl6"));
        li.set("nbconvert_exporter", Json::str("script"));

        Json links = Json::array();
        auto link = [&links](const char* text, const char* url) {
            Json l = Json::object();
            l.set("text", Json::str(text));
            l.set("url", Json::str(url));
            links.arr.push_back(std::move(l));
        };
        link("Raku documentation", "https://docs.raku.org/");
        link("Raku++", "https://github.com/ash/rakupp");

        Json c = Json::object();
        c.set("status", Json::str("ok"));
        c.set("protocol_version", Json::str(kProtocolVersion));
        c.set("implementation", Json::str("rakupp"));
        c.set("implementation_version", Json::str(rk_version()));
        c.set("language_info", std::move(li));
        c.set("banner", Json::str(std::string("Raku++ ") + rk_version() +
                                  " — a Raku interpreter in C++ (rakupp --jupyter)"));
        c.set("help_links", std::move(links));
        return c;
    }

    void handleExecute(const ZMsg& zm, const JMsg& req) {
        std::string code = req.content.getStr("code");
        bool silent = false, storeHistory = true;
        if (const Json* v = req.content.find("silent")) silent = v->t == Json::T::Bool && v->b;
        if (const Json* v = req.content.find("store_history"))
            storeHistory = !(v->t == Json::T::Bool && !v->b);
        if (silent) storeHistory = false;
        if (storeHistory) execCount_++;

        if (!silent) {
            Json ei = Json::object();
            ei.set("code", Json::str(code));
            ei.set("execution_count", Json::integer(execCount_));
            publish("execute_input", ei);
        }

        busy_.store(true);
        RkValue v = nullptr;
        int rc = rk_eval(sess_.rk(), code.c_str(), &v);
        busy_.store(false);

        if (rc != RK_OK) {
            std::string msg = sess_.lastError();
            std::vector<std::string> tb = splitLines(msg);
            Json tbj = Json::array();
            for (auto& l : tb) tbj.arr.push_back(Json::str(l));
            // ename/evalue: the engine hands back one message, not a class and
            // a payload, so the first line is the value and the rest is the
            // traceback the frontend renders under it. Inventing a Raku
            // exception class name here would be a lie the engine never told.
            Json err = Json::object();
            err.set("ename", Json::str("Error"));
            err.set("evalue", Json::str(tb.empty() ? msg : tb[0]));
            err.obj.emplace_back("traceback", tbj);
            publish("error", err);

            Json rep = Json::object();
            rep.set("status", Json::str("error"));
            rep.set("execution_count", Json::integer(execCount_));
            rep.set("ename", Json::str("Error"));
            rep.set("evalue", Json::str(tb.empty() ? msg : tb[0]));
            rep.obj.emplace_back("traceback", tbj);
            reply(shell_, zm, req, "execute_reply", rep);
            return;
        }

        // The REPL's rule, deliberately: a cell shows the value of its last
        // statement whether or not it also printed. `say 42` therefore ends
        // in True, exactly as it does at the rakupp prompt — a notebook that
        // hid it would be teaching a different language.
        if (!silent && v) {
            std::string g = sess_.gist(v);
            if (!g.empty() && g != "Nil") {
                Json data = Json::object();
                data.set("text/plain", Json::str(g));
                Json c = Json::object();
                c.set("execution_count", Json::integer(execCount_));
                c.set("data", std::move(data));
                c.set("metadata", Json::object());
                publish("execute_result", c);
            }
        }

        Json rep = Json::object();
        rep.set("status", Json::str("ok"));
        rep.set("execution_count", Json::integer(execCount_));
        rep.set("user_expressions", Json::object());
        rep.set("payload", Json::array());
        reply(shell_, zm, req, "execute_reply", rep);
    }

    void handleShell(const ZMsg& zm, const JMsg& req) {
        {
            std::lock_guard<std::mutex> lk(parentMx_);
            parent_ = req.header;
        }
        publishStatus("busy");
        const std::string& t = req.msgType;
        if (t == "execute_request") { handleExecute(zm, req); }
        else if (t == "kernel_info_request") {
            reply(shell_, zm, req, "kernel_info_reply", kernelInfo());
        }
        else if (t == "is_complete_request") {
            // "unknown" is the protocol's word for "ask your own heuristic":
            // the C ABI has no is-this-source-finished entry point, and a
            // guess made here would disagree with the parser sooner or later.
            Json c = Json::object();
            c.set("status", Json::str("unknown"));
            reply(shell_, zm, req, "is_complete_reply", c);
        }
        else if (t == "complete_request") {
            long long pos = 0;
            if (const Json* p = req.content.find("cursor_pos"))
                if (p->t == Json::T::Int) pos = p->i;
            Json c = Json::object();
            c.set("status", Json::str("ok"));
            c.set("matches", Json::array());
            c.set("cursor_start", Json::integer(pos));
            c.set("cursor_end", Json::integer(pos));
            c.set("metadata", Json::object());
            reply(shell_, zm, req, "complete_reply", c);
        }
        else if (t == "inspect_request") {
            Json c = Json::object();
            c.set("status", Json::str("ok"));
            c.set("found", Json::boolean(false));
            c.set("data", Json::object());
            c.set("metadata", Json::object());
            reply(shell_, zm, req, "inspect_reply", c);
        }
        else if (t == "history_request") {
            Json c = Json::object();
            c.set("status", Json::str("ok"));
            c.set("history", Json::array());
            reply(shell_, zm, req, "history_reply", c);
        }
        else if (t == "comm_info_request") {
            Json c = Json::object();
            c.set("status", Json::str("ok"));
            c.set("comms", Json::object());
            reply(shell_, zm, req, "comm_info_reply", c);
        }
        else if (t == "comm_open" || t == "comm_msg" || t == "comm_close") {
            // Widgets. Nothing here implements them, and the protocol says an
            // unknown comm target is dropped in silence rather than answered.
        }
        else {
            logf("ignoring unknown shell message '" + t + "'");
        }
        publishStatus("idle");
    }

    void handleControl(const ZMsg& zm, const JMsg& req) {
        const std::string& t = req.msgType;
        if (t == "shutdown_request") {
            bool restart = false;
            if (const Json* r = req.content.find("restart")) restart = r->t == Json::T::Bool && r->b;
            Json c = Json::object();
            c.set("status", Json::str("ok"));
            c.set("restart", Json::boolean(restart));
            reply(control_, zm, req, "shutdown_reply", c);
            publishWithParent("status", [] { Json s = Json::object(); s.set("execution_state", Json::str("idle")); return s; }(),
                              req.header);
            // _Exit, not return: the frontend has its answer, and running
            // static destructors while other threads are still inside the
            // interpreter is how the MCP server learned to hang on Linux.
            std::cout.flush();
            std::fflush(nullptr);
            std::_Exit(0);
        }
        else if (t == "interrupt_request") {
            Json c = Json::object();
            c.set("status", Json::str("ok"));
            reply(control_, zm, req, "interrupt_reply", c);
            if (!busy_.load()) return;   // nothing to interrupt: the answer was enough
            Json err = Json::object();
            err.set("ename", Json::str("Interrupt"));
            err.set("evalue", Json::str("rakupp cannot interrupt a running cell"));
            Json tb = Json::array();
            tb.arr.push_back(Json::str(
                "The engine has no interrupt point inside an evaluation, so this kernel "
                "is exiting instead of pretending the cell stopped. The frontend will "
                "restart it — session state (variables, subs, loaded modules) is lost."));
            err.obj.emplace_back("traceback", tb);
            publish("error", err);
            std::cout.flush();
            std::fflush(nullptr);
            std::_Exit(0);
        }
        else if (t == "kernel_info_request") {
            reply(control_, zm, req, "kernel_info_reply", kernelInfo());
        }
        else if (t == "debug_request") {
            Json c = Json::object();
            c.set("status", Json::str("error"));
            c.set("ename", Json::str("NotImplemented"));
            c.set("evalue", Json::str("this kernel has no debug adapter"));
            reply(control_, zm, req, "debug_reply", c);
        }
    }

    ZSock shell_{Kind::Router};
    ZSock control_{Kind::Router};
    ZSock stdin_{Kind::Router};
    ZSock iopub_{Kind::Pub};
    ZSock hb_{Kind::Rep};
    Session sess_;
    std::string key_, session_ = uuid4(), langVersion_ = "6.d";
    long long execCount_ = 0;
    std::atomic<bool> busy_{false};
    std::mutex parentMx_;
    Json parent_ = Json::object();
};

int Kernel::run(const Options& opt) {
    std::ifstream in(opt.connectionFile, std::ios::binary);
    if (!in) {
        logf("cannot read the connection file '" + opt.connectionFile + "'");
        return 4;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    Json cfg;
    if (!parse(ss.str(), cfg) || cfg.t != Json::T::Obj) {
        logf("'" + opt.connectionFile +
             "' is not a JSON object — Jupyter passes this file as {connection_file}");
        return 4;
    }

    std::string transport = cfg.getStr("transport", "tcp");
    if (transport != "tcp") {
        logf("transport '" + transport + "' is not supported; this kernel speaks ZMTP over tcp only");
        return 4;
    }
    std::string ip = cfg.getStr("ip", "127.0.0.1");
    key_ = cfg.getStr("key");
    std::string scheme = cfg.getStr("signature_scheme", "hmac-sha256");
    if (!key_.empty() && scheme != "hmac-sha256") {
        logf("signature scheme '" + scheme + "' is not supported; this kernel signs with hmac-sha256");
        return 4;
    }

    struct Chan { const char* field; ZSock* sock; };
    Chan chans[] = {{"shell_port", &shell_},   {"control_port", &control_},
                    {"stdin_port", &stdin_},   {"iopub_port", &iopub_},
                    {"hb_port", &hb_}};
    for (auto& ch : chans) {
        const Json* p = cfg.find(ch.field);
        if (!p || p->t != Json::T::Int || p->i <= 0 || p->i > 65535) {
            logf(std::string("the connection file has no usable ") + ch.field);
            return 4;
        }
        std::string err;
        if (!ch.sock->bindTcp(ip, (int)p->i, err)) {
            logf(std::string(ch.field) + ": " + err);
            return 4;
        }
    }
    for (auto& ch : chans) ch.sock->start();

    std::string e = sess_.start(opt.preload, &Kernel::onOutput, this);
    if (!e.empty()) {
        logf(e);
        return 4;
    }
    langVersion_ = sess_.languageVersion();
    // The one thing a cell can do that a script cannot: hand the frontend
    // something to render. `jupyter-display($svg, 'image/svg+xml')`.
    rk_register(sess_.rk(), "jupyter-display", &Kernel::hostDisplay, this);

    logf("kernel ready (rakupp " + std::string(rk_version()) + ", Raku " + langVersion_ + ") on " + ip);
    publishStatus("starting");

    std::thread hbThread([this] {
        ZMsg m;
        while (hb_.recv(m)) hb_.send(m.from, m.frames);   // a heartbeat is an echo
    });
    // Nothing here asks the frontend for input, so nothing should arrive on
    // the stdin channel — but the socket must exist for the frontend to
    // connect to, and an undrained queue is a slow leak rather than an idle
    // one. Read and discard.
    std::thread stdinThread([this] {
        ZMsg m;
        while (stdin_.recv(m)) { /* discarded: see rk_set_input above */ }
    });
    std::thread controlThread([this] {
        ZMsg m;
        while (control_.recv(m)) {
            JMsg req;
            std::string err;
            if (!decodeJupyter(m.frames, key_, req, err)) {
                logf("dropping a control message (" + err + ")");
                continue;
            }
            handleControl(m, req);
        }
    });
    hbThread.detach();
    controlThread.detach();
    stdinThread.detach();

    ZMsg m;
    while (shell_.recv(m)) {
        JMsg req;
        std::string err;
        if (!decodeJupyter(m.frames, key_, req, err)) {
            logf("dropping a shell message (" + err + ")");
            continue;
        }
        handleShell(m, req);
    }
    return 0;
}

} // namespace

int runKernel(const Options& opt) {
#ifdef _WIN32
    WSADATA wsa;
    ::WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
#ifdef SIGPIPE
    // A frontend that closes a socket mid-write must not kill the kernel.
    ::signal(SIGPIPE, SIG_IGN);
#endif
    Kernel k;
    return k.run(opt);
}

// ---------------------------------------------------------------------------
// --jupyter-install: the kernelspec.
//
// Jupyter finds kernels by scanning data directories for kernels/<name>/
// kernel.json, so installing one is writing that file — with an ABSOLUTE path
// to this binary, because the frontend that launches it may have any PATH at
// all, and none of them is guaranteed to be the shell's.
// ---------------------------------------------------------------------------

int installKernelspec(const InstallOptions& opt) {
    namespace fs = std::filesystem;
    fs::path base;
    if (!opt.prefix.empty()) { base = fs::path(opt.prefix) / "share" / "jupyter"; }
    else if (const char* d = std::getenv("JUPYTER_DATA_DIR")) { base = d; }
    else {
        const char* home = std::getenv("HOME");
#if defined(_WIN32)
        const char* appdata = std::getenv("APPDATA");
        base = fs::path(appdata ? appdata : ".") / "jupyter";
#elif defined(__APPLE__)
        base = fs::path(home ? home : ".") / "Library" / "Jupyter";
#else
        const char* xdg = std::getenv("XDG_DATA_HOME");
        base = xdg && *xdg ? fs::path(xdg) : fs::path(home ? home : ".") / ".local" / "share";
        base /= "jupyter";
#endif
    }
    fs::path dir = base / "kernels" / opt.name;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::cerr << "rakupp --jupyter-install: cannot create " << dir.string() << ": "
                  << ec.message() << "\n";
        return 4;
    }

    // Written by hand rather than dumped, so the file a user opens reads like
    // one a human wrote — but every string goes through the JSON escaper,
    // because a Windows path is full of backslashes.
    std::string js = "{\n  \"argv\": [";
    rakupp::json::dumpStr(opt.selfExe, js);
    js += ", \"--jupyter\", \"{connection_file}\"],\n  \"display_name\": ";
    rakupp::json::dumpStr(opt.displayName, js);
    js += ",\n  \"language\": \"raku\",\n";
    // "message", not the default "signal": the kernel cannot stop a running
    // evaluation either way, and a message lets it SAY so on iopub before it
    // goes down (see interrupt_request).
    js += "  \"interrupt_mode\": \"message\",\n";
    js += "  \"metadata\": {\"debugger\": false}\n}\n";

    fs::path file = dir / "kernel.json";
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "rakupp --jupyter-install: cannot write " << file.string() << "\n";
        return 4;
    }
    out << js;
    out.close();

    std::cout << "wrote " << file.string() << "\n";
    std::cout << "run a notebook with it:  jupyter console --kernel " << opt.name << "\n";
    std::cout << "                         jupyter lab      (pick \"" << opt.displayName << "\")\n";
    return 0;
}

} // namespace rakupp::jupyter
