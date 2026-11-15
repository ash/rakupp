// The simulated micro:bit a `rakupp --target=microbit --sim` build compiles a
// program against, in place of CODAL: the part of CODAL's API the emitter
// names (src/codegen/MicroBit.cpp), run on this computer. Embedded in rakupp
// (cmake/EmbedTools.cmake) and written beside the program as MicroBit.h when
// it is built; nothing in rakupp includes it.
//
// It writes what rakulib/MicroBit.rakumod writes, by the same rules, so a
// program run by the interpreter and its C++ built with --sim can be compared
// byte for byte. stdout is the serial port. stderr is the board: each change
// of what the LEDs show (`[1200 ms] display 09090:99999:99999:09990:00900`,
// MicroPython's own picture format, a brightness 0 to 9 per LED), each text
// shown or scrolled (`[0 ms] scroll "HELLO"`, then the time CODAL takes), and
// each pin written. The clock is real by default; with RAKUPP_MICROBIT_STOP=MS
// it is virtual — pause and sleep advance it, every other board call advances
// it one microsecond, and past MS the program stops. Inputs come from the
// environment (RAKUPP_MICROBIT_BUTTONS, _ACCEL, _GESTURES, _HEADING, _TEMP,
// _LIGHT, _PINS, _SEED: docs/guide/MICROBIT.md). Change one of the two and the
// other changes with it.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <vector>

#define RAKUPP_MICROBIT_SIM 1
#define DEVICE_ID_GESTURE 13
#define DEVICE_EVT_ANY 0
#define MESSAGE_BUS_LISTENER_IMMEDIATE 0x0030
#define ACCELEROMETER_EVT_NONE 0
#define ACCELEROMETER_EVT_TILT_UP 1
#define ACCELEROMETER_EVT_TILT_DOWN 2
#define ACCELEROMETER_EVT_TILT_LEFT 3
#define ACCELEROMETER_EVT_TILT_RIGHT 4
#define ACCELEROMETER_EVT_FACE_UP 5
#define ACCELEROMETER_EVT_FACE_DOWN 6
#define ACCELEROMETER_EVT_FREEFALL 7
#define ACCELEROMETER_EVT_3G 8
#define ACCELEROMETER_EVT_6G 9
#define ACCELEROMETER_EVT_8G 10
#define ACCELEROMETER_EVT_SHAKE 11

struct MicroBitEvent { uint16_t source, value; };

namespace rk_sim {
struct Port {
    FILE* f;
    void print(const char* s) { fputs(s, f); }
};
static Port out = {stdout}, err = {stderr};
static bool virt = false;
static uint64_t now_us = 0, stop_us = 0;
static std::chrono::steady_clock::time_point start;

// A timeline: each entry holds from its time on. "V" alone holds from 0.
struct Entry { uint64_t at; std::string v; };
typedef std::vector<Entry> Line;
static Line accel, gestures, heading, temp, light;
// A press of button A or B, of the logo, or a touch of pin 0, 1 or 2.
struct Press { std::string who; uint64_t from, to; };
static std::vector<Press> presses;
static std::map<long, long> pinIn, level, analog;
static uint32_t rng = 1;
static void (*gestureListener)(MicroBitEvent) = 0;
static uint64_t dispatched = 0;
static bool anyDispatched = false;

// CODAL's 5x5 font (codal-core's pendolino3), ASCII 32 to 126: five rows a
// glyph, two hex digits a row, the leftmost LED the row's bit 4.
static const char* font =
    "000000000008080800080a0a0000000a1f0a1f0a0e190e130e19120409130c120c120d0808000000"
    "04080808040804040408000a040a0000040e0400000000040800000e000000000008000102040810"
    "0c1212120c040c04040e1c020c101e1e0204120c060a121f021f101e011e02040e110e1f02040810"
    "0e110e110e0e110e0408000800080000040004080204080402000e000e0008040204080e11060004"
    "0e1115130c0c121e12121c121c121c0e1010100e1c1212121c1e101c101e1e101c10100e1013110e"
    "12121e12121c0808081c1f0202120c1214181412101010101e111b15111111191513110c1212120c"
    "1c121c10100c12120c061c121c12110e100c021c1f04040404121212120c1111110a041111151b11"
    "12120c1212110a0404041e0408101e0e0808080e10080402010e0202020e040a000000000000001f"
    "0804000000000e12120f10101c121c000e10100e02020e120e0c121c100e06081c08080e120e020c"
    "10101c12120800080808020002020c10141814120808080806001b151111001c121212000c12120c"
    "001c121c10000e120e02000e101010000608041808080e0807001212120f0011110a04001111151b"
    "00120c0c1200110a0418001e04081e06040c0406080808080818080c081800000c0300";

static uint64_t clock_us() {
    if (virt) return now_us;
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
}
static void stamp() { fprintf(stderr, "[%llu ms] ", (unsigned long long)(clock_us() / 1000)); }
static void halt(int code) { fflush(stdout); fflush(stderr); exit(code); }

static int gestureCode(const std::string& g) {
    static const char* names[] = {"", "up", "down", "left", "right", "face up", "face down", "freefall", "3g", "6g", "8g", "shake"};
    for (int i = 1; i < 12; i++) if (g == names[i]) return i;
    return 0;
}
// The accelerometer raises a gesture as it begins: each entry of the gesture
// timeline that began up to now, once, to the program's listener.
static void dispatch() {
    if (!gestureListener) return;
    uint64_t now = clock_us();
    for (size_t i = 0; i < gestures.size(); i++) {
        const Entry& e = gestures[i];
        if (e.at > now || (anyDispatched && e.at <= dispatched)) continue;
        MicroBitEvent ev = {DEVICE_ID_GESTURE, (uint16_t)gestureCode(e.v)};
        if (ev.value) gestureListener(ev);
    }
    dispatched = now;
    anyDispatched = true;
}
static void advance(uint64_t us) {
    if (virt) {
        now_us += us;
        if (now_us > stop_us) {
            fflush(stdout);
            fprintf(stderr, "[%llu ms] stop\n", (unsigned long long)(stop_us / 1000));
            halt(0);
        }
    }
    dispatch();
}
static void wait_ms(uint64_t ms) {
    if (virt) advance(ms * 1000);
    else { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); dispatch(); }
}

static Line timeline(const char* name) {
    Line l;
    const char* s = getenv(name);
    if (!s || !*s) return l;
    std::string all = s;
    size_t p = 0;
    while (p <= all.size()) {
        size_t q = all.find(';', p);
        std::string item = all.substr(p, q == std::string::npos ? std::string::npos : q - p);
        size_t c = item.find(':');
        if (c == std::string::npos) l.push_back(Entry{0, item});
        else l.push_back(Entry{(uint64_t)strtoull(item.c_str(), 0, 10) * 1000, item.substr(c + 1)});
        if (q == std::string::npos) break;
        p = q + 1;
    }
    return l;
}
static std::string at(const Line& l, const char* dflt) {
    uint64_t now = clock_us();
    std::string v = dflt;
    for (size_t i = 0; i < l.size(); i++) if (l[i].at <= now) v = l[i].v;
    return v;
}
static long number(const Line& l, long dflt) {
    std::string v = at(l, "");
    return v.empty() ? dflt : strtol(v.c_str(), 0, 10);
}
static bool pressed(const char* who) {
    uint64_t now = clock_us();
    for (size_t i = 0; i < presses.size(); i++)
        if (presses[i].who == who && presses[i].from <= now && now < presses[i].to) return true;
    return false;
}
// Presses that began after `since` (-1: before the first ask) and up to now.
static int pressesSince(const char* who, int64_t since) {
    uint64_t now = clock_us();
    int n = 0;
    for (size_t i = 0; i < presses.size(); i++)
        if (presses[i].who == who && (int64_t)presses[i].from > since && presses[i].from <= now) n++;
    return n;
}
static uint32_t random(uint64_t n) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (uint32_t)(rng % n);
}

static void init() {
    start = std::chrono::steady_clock::now();
    const char* s = getenv("RAKUPP_MICROBIT_STOP");
    if (s && *s) { virt = true; stop_us = strtoull(s, 0, 10) * 1000; }
    accel = timeline("RAKUPP_MICROBIT_ACCEL");
    gestures = timeline("RAKUPP_MICROBIT_GESTURES");
    heading = timeline("RAKUPP_MICROBIT_HEADING");
    temp = timeline("RAKUPP_MICROBIT_TEMP");
    light = timeline("RAKUPP_MICROBIT_LIGHT");
    if (const char* b = getenv("RAKUPP_MICROBIT_BUTTONS")) {
        std::string all = b;
        size_t p = 0;
        while (p < all.size()) {
            size_t q = all.find(',', p);
            std::string item = all.substr(p, q == std::string::npos ? std::string::npos : q - p);
            size_t a = item.find('@');
            if (a != std::string::npos) {
                Press pr;
                pr.who = item.substr(0, a);
                char* e;
                pr.from = (uint64_t)strtoull(item.c_str() + a + 1, &e, 10) * 1000;
                pr.to = *e == '-' ? (uint64_t)strtoull(e + 1, 0, 10) * 1000 : pr.from + 100000;
                presses.push_back(pr);
            }
            if (q == std::string::npos) break;
            p = q + 1;
        }
    }
    if (const char* p = getenv("RAKUPP_MICROBIT_PINS")) {
        while (*p) {
            char* q;
            long pin = strtol(p, &q, 10);
            p = q;
            if (*p == '=') { pinIn[pin] = strtol(p + 1, &q, 10); p = q; }
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
    }
    const char* seed = getenv("RAKUPP_MICROBIT_SEED");
    rng = seed && *seed ? (uint32_t)strtoul(seed, 0, 10) : virt ? 1u : (uint32_t)time(0);
    if (!rng) rng = 1;
}

// What the LEDs show, in MicroPython's format: a brightness 0 to 9 per LED,
// row by row; CODAL keeps 0 to 255, and a level is its highest bit (255: 9).
static std::string shown = "00000:00000:00000:00000:00000";
static int levelOf(int v) { if (v >= 255) return 9; int l = 0; while (v) { l++; v >>= 1; } return l; }
static std::string frameOf(const uint8_t* px) {
    std::string f;
    for (int y = 0; y < 5; y++) {
        if (y) f += ':';
        for (int x = 0; x < 5; x++) f += (char)('0' + levelOf(px[y * 5 + x]));
    }
    return f;
}
// A picture that changes what the LEDs show is a line; one that does not is not.
static void drawn(const uint8_t* px) {
    std::string f = frameOf(px);
    if (f == shown) return;
    shown = f;
    stamp();
    if (f == "00000:00000:00000:00000:00000") fputs("display clear\n", stderr);
    else fprintf(stderr, "display %s\n", f.c_str());
}
static void glyph(uint8_t* px, unsigned char c) {
    if (c < 32 || c > 126) c = ' ';
    const char* g = font + (c - 32) * 10;
    for (int y = 0; y < 5; y++) {
        char h[3] = {g[y * 2], g[y * 2 + 1], 0};
        int row = (int)strtol(h, 0, 16);
        for (int x = 0; x < 5; x++) px[y * 5 + x] = (row >> (4 - x)) & 1 ? 255 : 0;
    }
}
} // namespace rk_sim

class ManagedString {
public:
    ManagedString() {}
    ManagedString(const char* str) : s(str) {}
    int16_t length() const { return (int16_t)s.size(); }
    const char* toCharArray() const { return s.c_str(); }
    friend ManagedString operator+(const ManagedString& a, const ManagedString& b) { ManagedString r; r.s = a.s + b.s; return r; }
private:
    std::string s;
};

class MicroBitImage {
public:
    MicroBitImage() { clear(); }
    MicroBitImage(int16_t, int16_t) { clear(); }
    MicroBitImage(int16_t, int16_t, const uint8_t* bitmap) { memcpy(px, bitmap, 25); }
    void clear() { memset(px, 0, 25); }
    int setPixelValue(int16_t x, int16_t y, uint8_t v) {
        rk_sim::advance(1);
        px[y * 5 + x] = v;
        if (onDisplay) rk_sim::drawn(px);
        return 0;
    }
    int getPixelValue(int16_t x, int16_t y) { rk_sim::advance(1); return px[y * 5 + x]; }
    uint8_t px[25];
    bool onDisplay = false;
};

class MicroBitDisplay {
public:
    MicroBitDisplay() { image.onDisplay = true; }
    MicroBitImage image;
    int print(const MicroBitImage& i) {
        rk_sim::advance(1);
        memcpy(image.px, i.px, 25);
        rk_sim::drawn(image.px);
        return 0;
    }
    // CODAL's print: one character at once; more, each for 400 ms after a
    // first 400, then a blank, ending blank, (n + 2) × 400 ms in all.
    int print(const ManagedString& s) {
        rk_sim::advance(1);
        int n = s.length();
        if (n == 1) {
            uint8_t px[25];
            rk_sim::glyph(px, (unsigned char)s.toCharArray()[0]);
            memcpy(image.px, px, 25);
            std::string f = rk_sim::frameOf(px);
            if (f == rk_sim::shown) return 0;
            rk_sim::shown = f;
            rk_sim::stamp();
            fprintf(stderr, "show \"%s\"\n", s.toCharArray());
            return 0;
        }
        rk_sim::stamp();
        fprintf(stderr, "show \"%s\"\n", s.toCharArray());
        rk_sim::wait_ms((uint64_t)(n + 2) * 400);
        image.clear();
        rk_sim::shown = rk_sim::frameOf(image.px);
        return 0;
    }
    // CODAL's scroll: 120 ms a column, six columns a character (five and a
    // space), and six more to clear the last: 6 × (n + 1) × 120 ms, ending blank.
    int scroll(const ManagedString& s) {
        rk_sim::advance(1);
        rk_sim::stamp();
        fprintf(stderr, "scroll \"%s\"\n", s.toCharArray());
        rk_sim::wait_ms((uint64_t)6 * (s.length() + 1) * 120);
        image.clear();
        rk_sim::shown = rk_sim::frameOf(image.px);
        return 0;
    }
    void clear() { rk_sim::advance(1); image.clear(); rk_sim::drawn(image.px); }
    int readLightLevel() { rk_sim::advance(1); return (int)rk_sim::number(rk_sim::light, 0); }
};

class Button {
public:
    explicit Button(const char* w) : who(w) {}
    int isPressed() { rk_sim::advance(1); return rk_sim::pressed(who); }
    // CODAL counts a press as it begins: the presses since the last ask.
    int wasPressed() {
        rk_sim::advance(1);
        int n = rk_sim::pressesSince(who, last);
        last = (int64_t)rk_sim::clock_us();
        return n;
    }
private:
    const char* who;
    int64_t last = -1;
};

class MicroBitPin {
public:
    explicit MicroBitPin(int n) : pin(n) {}
    int getDigitalValue() {
        rk_sim::advance(1);
        if (rk_sim::pinIn.count(pin)) return rk_sim::pinIn[pin] ? 1 : 0;
        std::map<long, long>::const_iterator f = rk_sim::level.find(pin);
        return f == rk_sim::level.end() ? 0 : (int)f->second;
    }
    int setDigitalValue(int v) {
        rk_sim::advance(1);
        long l = v ? 1 : 0;
        std::map<long, long>::const_iterator f = rk_sim::level.find(pin);
        if (f != rk_sim::level.end() && f->second == l) return 0;
        rk_sim::level[pin] = l;
        rk_sim::stamp();
        fprintf(stderr, "pin %d %s\n", pin, l ? "HIGH" : "LOW");
        return 0;
    }
    int getAnalogValue() {
        rk_sim::advance(1);
        return rk_sim::pinIn.count(pin) ? (int)rk_sim::pinIn[pin] : 0;
    }
    int setAnalogValue(int v) {
        rk_sim::advance(1);
        std::map<long, long>::const_iterator f = rk_sim::analog.find(pin);
        if (f != rk_sim::analog.end() && f->second == v) return 0;
        rk_sim::analog[pin] = v;
        rk_sim::stamp();
        fprintf(stderr, "pin %d analog %d\n", pin, v);
        return 0;
    }
    int isTouched() {
        rk_sim::advance(1);
        char who[4];
        snprintf(who, sizeof who, "%d", pin);
        return rk_sim::pressed(who);
    }
private:
    int pin;
};

struct MicroBitIO {
    MicroBitPin P0{0}, P1{1}, P2{2}, P3{3}, P4{4}, P5{5}, P6{6}, P7{7}, P8{8}, P9{9}, P10{10},
                P11{11}, P12{12}, P13{13}, P14{14}, P15{15}, P16{16}, P19{19}, P20{20};
};

class MicroBitAccelerometer {
public:
    int getX() { return axis(0); }
    int getY() { return axis(1); }
    int getZ() { return axis(2); }
    uint16_t getGesture() { rk_sim::advance(1); return (uint16_t)rk_sim::gestureCode(rk_sim::at(rk_sim::gestures, "")); }
private:
    // RAKUPP_MICROBIT_ACCEL: "x,y,z" in milli-g; flat, face up, unless it says otherwise
    int axis(int k) {
        rk_sim::advance(1);
        std::string v = rk_sim::at(rk_sim::accel, "0,0,-1024");
        const char* p = v.c_str();
        long a[3] = {0, 0, -1024};
        for (int i = 0; i < 3 && *p; i++) {
            char* e;
            a[i] = strtol(p, &e, 10);
            p = *e == ',' ? e + 1 : e;
        }
        return (int)a[k];
    }
};

class MicroBitCompass {
public:
    int heading() { rk_sim::advance(1); return (int)rk_sim::number(rk_sim::heading, 0); }
    int calibrate() { rk_sim::advance(1); rk_sim::stamp(); fputs("compass calibrated\n", stderr); return 0; }
};

class MicroBitThermometer {
public:
    int getTemperature() { rk_sim::advance(1); return (int)rk_sim::number(rk_sim::temp, 21); }
};

class MicroBitSerial {
public:
    int send(const uint8_t* b, int n) { fwrite(b, 1, (size_t)n, stdout); return n; }
};

class MicroBitMessageBus {
public:
    // The one listener a program registers: rk_on_gesture, for was-gesture.
    int listen(int id, int, void (*handler)(MicroBitEvent), uint16_t = 0) {
        if (id == DEVICE_ID_GESTURE) { rk_sim::gestureListener = handler; rk_sim::dispatch(); }
        return 0;
    }
};

class MicroBit {
public:
    MicroBitDisplay display;
    Button buttonA{"A"}, buttonB{"B"}, logo{"logo"};
    MicroBitIO io;
    MicroBitAccelerometer accelerometer;
    MicroBitCompass compass;
    MicroBitThermometer thermometer;
    MicroBitSerial serial;
    MicroBitMessageBus messageBus;
    int init() { setvbuf(stdout, NULL, _IOLBF, BUFSIZ); rk_sim::init(); return 0; }
    void sleep(uint32_t ms) { rk_sim::wait_ms(ms); }
    unsigned long systemTime() { rk_sim::advance(1); return (unsigned long)(rk_sim::clock_us() / 1000); }
};

inline void release_fiber() { rk_sim::halt(0); }
