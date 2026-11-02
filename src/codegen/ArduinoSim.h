// The simulated board a `rakupp --target=arduino --sim` build compiles a
// sketch against, in place of an Arduino core: the same API, run on this
// computer. Embedded in rakupp (cmake/EmbedTools.cmake) and written beside
// the sketch as Arduino.h when it is built; nothing in rakupp includes it.
//
// It writes what rakulib/Arduino.rakumod writes, by the same rules, so a
// program run by the interpreter and its sketch built with --sim can be
// compared byte for byte: Serial on stdout, each pin change on stderr as
// `[1500 ms] pin 13 HIGH`. The clock is real by default; with
// RAKUPP_ARDUINO_STOP=MS it is virtual — delay advances it, every other board
// call advances it one microsecond, and past MS the program stops.
// RAKUPP_ARDUINO_PINS="2=1,A0=512" fixes what digitalRead and analogRead see.
// Change one of the two and the other changes with it.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <chrono>
#include <map>
#include <thread>

#define RAKUPP_ARDUINO_SIM 1
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define LED_BUILTIN 13
#define A0 14
#define A1 15
#define A2 16
#define A3 17
#define A4 18
#define A5 19
#define A6 20
#define A7 21
#define PROGMEM
typedef uint8_t byte;
typedef bool boolean;
class __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper*>(s))

namespace rk_sim {
struct Port {
    FILE* f;
    void begin(unsigned long) {}
    void print(const char* s) { fputs(s, f); }
    void print(const __FlashStringHelper* s) { fputs(reinterpret_cast<const char*>(s), f); }
    void print(char c) { fputc(c, f); }
    void flush() { fflush(f); }
};
static Port err = {stderr};
static bool virt = false;
static uint64_t now_us = 0, stop_us = 0;
static std::chrono::steady_clock::time_point start;
static std::map<long, long> mode, level, pwm, input;

static uint64_t clock_us() {
    if (virt) return now_us;
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
}
static void stamp() { fprintf(stderr, "[%llu ms] ", (unsigned long long)(clock_us() / 1000)); }
static void halt(int code) { fflush(stdout); fflush(stderr); exit(code); }
static void advance(uint64_t us) {
    if (!virt) return;
    now_us += us;
    if (now_us > stop_us) {
        fflush(stdout);
        fprintf(stderr, "[%llu ms] stop\n", (unsigned long long)(stop_us / 1000));
        halt(0);
    }
}
static long at(const std::map<long, long>& m, long k, long dflt) {
    std::map<long, long>::const_iterator f = m.find(k);
    return f == m.end() ? dflt : f->second;
}
static void init() {
    start = std::chrono::steady_clock::now();
    const char* s = getenv("RAKUPP_ARDUINO_STOP");
    if (s && *s) { virt = true; stop_us = strtoull(s, 0, 10) * 1000; }
    if (const char* p = getenv("RAKUPP_ARDUINO_PINS")) {
        while (*p) {
            char* q;
            long pin = *p == 'A' ? 14 + strtol(p + 1, &q, 10) : strtol(p, &q, 10);
            p = q;
            if (*p == '=') { input[pin] = strtol(p + 1, &q, 10); p = q; }
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
    }
}
static const char* modeName(long m) { return m == OUTPUT ? "OUTPUT" : m == INPUT_PULLUP ? "INPUT_PULLUP" : "INPUT"; }
} // namespace rk_sim

static rk_sim::Port Serial = {stdout};

inline void pinMode(uint8_t pin, uint8_t m) {
    rk_sim::advance(1);
    rk_sim::mode[pin] = m;
    rk_sim::stamp();
    fprintf(stderr, "pin %d mode %s\n", pin, rk_sim::modeName(m));
}
inline void digitalWrite(uint8_t pin, uint8_t value) {
    rk_sim::advance(1);
    long l = value ? HIGH : LOW;
    if (rk_sim::at(rk_sim::level, pin, LOW) == l) return;
    rk_sim::level[pin] = l;
    rk_sim::stamp();
    fprintf(stderr, "pin %d %s\n", pin, l ? "HIGH" : "LOW");
}
inline int digitalRead(uint8_t pin) {
    rk_sim::advance(1);
    if (rk_sim::input.count(pin)) return rk_sim::input[pin] ? HIGH : LOW;
    long m = rk_sim::at(rk_sim::mode, pin, INPUT);
    return m == OUTPUT ? (int)rk_sim::at(rk_sim::level, pin, LOW) : m == INPUT_PULLUP ? HIGH : LOW;
}
inline int analogRead(uint8_t pin) {
    rk_sim::advance(1);
    return (int)rk_sim::at(rk_sim::input, pin, 0);
}
inline void analogWrite(uint8_t pin, int value) {
    rk_sim::advance(1);
    long v = value < 0 ? 0 : value > 255 ? 255 : value;
    if (rk_sim::at(rk_sim::pwm, pin, -1) == v) return;
    rk_sim::pwm[pin] = v;
    rk_sim::stamp();
    fprintf(stderr, "pin %d pwm %ld\n", pin, v);
}
inline void delay(unsigned long ms) {
    if (rk_sim::virt) rk_sim::advance((uint64_t)ms * 1000);
    else std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
inline void delayMicroseconds(unsigned int us) {
    if (rk_sim::virt) rk_sim::advance(us);
    else std::this_thread::sleep_for(std::chrono::microseconds(us));
}
inline unsigned long millis() { rk_sim::advance(1); return (unsigned long)(rk_sim::clock_us() / 1000); }
inline unsigned long micros() { rk_sim::advance(1); return (unsigned long)rk_sim::clock_us(); }

void setup();
void loop();
int main() {
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
    rk_sim::init();
    setup();
    for (;;) loop();
}
