// The micro:bit backend (MICROBIT-PLAN.md): a Raku program to one C++ file
// for the BBC micro:bit V2 on CODAL, Lancaster University's runtime, which
// MakeCode runs on too. The emitter is the boards' shared one (BoardEmitter.h);
// this is what the micro:bit adds to it:
//   - `use MicroBit`'s objects and routines, by MicroPython's names spelt as
//     Raku (display.scroll, button-a.is-pressed, pin0.read-analog,
//     accelerometer.x), as CODAL's own calls on its global `uBit`.
//   - pictures: 25 brightness levels, 0 to 9, made while building (`image`,
//     Image.HEART) and kept in flash, shown through MicroPython's ten levels.
//   - text on the display: literals, integers and Bools, joined as it runs
//     into one of CODAL's ManagedStrings.
//   - say / print / note: the serial port over USB.
//   - the program's shape: main(), the mainline after uBit.init(), the
//     top-level `loop { }` a C loop in place.
#include "MicroBit.h"
#include "BoardEmitter.h"

namespace rakupp {
namespace {
using namespace board;

// MicroPython's built-in pictures (micropython-microbit-v2's
// microbit_constimage.c, MIT licence), by its names spelt as Raku; the same
// list as rakulib/MicroBit.rakumod's Image.
struct Pic { const char* name; const char* levels; };
const Pic kPictures[] = {
    {"HEART", "09090:99999:99999:09990:00900"},
    {"HEART-SMALL", "00000:09090:09990:00900:00000"},
    {"HAPPY", "00000:09090:00000:90009:09990"},
    {"SMILE", "00000:00000:00000:90009:09990"},
    {"SAD", "00000:09090:00000:09990:90009"},
    {"CONFUSED", "00000:09090:00000:09090:90909"},
    {"ANGRY", "90009:09090:00000:99999:90909"},
    {"ASLEEP", "00000:99099:00000:09990:00000"},
    {"SURPRISED", "09090:00000:00900:09090:00900"},
    {"SILLY", "90009:00000:99999:00909:00999"},
    {"FABULOUS", "99999:99099:00000:09090:09990"},
    {"MEH", "09090:00000:00090:00900:09000"},
    {"YES", "00000:00009:00090:90900:09000"},
    {"NO", "90009:09090:00900:09090:90009"},
    {"CLOCK12", "00900:00900:00900:00000:00000"},
    {"CLOCK1", "00090:00090:00900:00000:00000"},
    {"CLOCK2", "00000:00099:00900:00000:00000"},
    {"CLOCK3", "00000:00000:00999:00000:00000"},
    {"CLOCK4", "00000:00000:00900:00099:00000"},
    {"CLOCK5", "00000:00000:00900:00090:00090"},
    {"CLOCK6", "00000:00000:00900:00900:00900"},
    {"CLOCK7", "00000:00000:00900:09000:09000"},
    {"CLOCK8", "00000:00000:00900:99000:00000"},
    {"CLOCK9", "00000:00000:99900:00000:00000"},
    {"CLOCK10", "00000:99000:00900:00000:00000"},
    {"CLOCK11", "09000:09000:00900:00000:00000"},
    {"ARROW-N", "00900:09990:90909:00900:00900"},
    {"ARROW-NE", "00999:00099:00909:09000:90000"},
    {"ARROW-E", "00900:00090:99999:00090:00900"},
    {"ARROW-SE", "90000:09000:00909:00099:00999"},
    {"ARROW-S", "00900:00900:90909:09990:00900"},
    {"ARROW-SW", "00009:00090:90900:99000:99900"},
    {"ARROW-W", "00900:09000:99999:09000:00900"},
    {"ARROW-NW", "99900:99000:90900:00090:00009"},
    {"TRIANGLE", "00000:00900:09090:99999:00000"},
    {"TRIANGLE-LEFT", "90000:99000:90900:90090:99999"},
    {"CHESSBOARD", "09090:90909:09090:90909:09090"},
    {"DIAMOND", "00900:09090:90009:09090:00900"},
    {"DIAMOND-SMALL", "00000:00900:09090:00900:00000"},
    {"SQUARE", "99999:90009:90009:90009:99999"},
    {"SQUARE-SMALL", "00000:09990:09090:09990:00000"},
    {"RABBIT", "90900:90900:99990:99090:99990"},
    {"COW", "90009:90009:99999:09990:00900"},
    {"MUSIC-CROTCHET", "00900:00900:00900:99900:99900"},
    {"MUSIC-QUAVER", "00900:00990:00909:99900:99900"},
    {"MUSIC-QUAVERS", "09999:09009:09009:99099:99099"},
    {"PITCHFORK", "90909:90909:99999:00900:00900"},
    {"XMAS", "00900:09990:00900:09990:99999"},
    {"PACMAN", "09999:99090:99900:99990:09999"},
    {"TARGET", "00900:09990:99099:09990:00900"},
    {"TSHIRT", "99099:99999:09990:09990:09990"},
    {"ROLLERSKATE", "00099:00099:99999:99999:09090"},
    {"DUCK", "09900:99900:09999:09990:00000"},
    {"HOUSE", "00900:09990:99999:09990:09090"},
    {"TORTOISE", "00000:09990:99999:09090:00000"},
    {"BUTTERFLY", "99099:99999:00900:99999:99099"},
    {"STICKFIGURE", "00900:99999:00900:09090:90009"},
    {"GHOST", "99999:90909:99999:99999:90909"},
    {"SWORD", "00900:00900:00900:09990:00900"},
    {"GIRAFFE", "99000:09000:09000:09990:09090"},
    {"SKULL", "09990:90909:99999:09990:09990"},
    {"UMBRELLA", "09990:99999:00900:90900:09900"},
    {"SNAKE", "99000:99099:09090:09990:00000"},
    {"SCISSORS", "99009:99090:00900:99090:99009"},
};
const Pic* pictureOf(const std::string& n) {
    for (const Pic& p : kPictures) if (n == p.name) return &p;
    return nullptr;
}

// The gestures accelerometer.is-gesture and was-gesture know: MicroPython's
// names for CODAL's events.
struct Gesture { const char* name; const char* event; };
const Gesture kGestures[] = {
    {"up", "ACCELEROMETER_EVT_TILT_UP"},       {"down", "ACCELEROMETER_EVT_TILT_DOWN"},
    {"left", "ACCELEROMETER_EVT_TILT_LEFT"},   {"right", "ACCELEROMETER_EVT_TILT_RIGHT"},
    {"face up", "ACCELEROMETER_EVT_FACE_UP"},  {"face down", "ACCELEROMETER_EVT_FACE_DOWN"},
    {"freefall", "ACCELEROMETER_EVT_FREEFALL"}, {"3g", "ACCELEROMETER_EVT_3G"},
    {"6g", "ACCELEROMETER_EVT_6G"},             {"8g", "ACCELEROMETER_EVT_8G"},
    {"shake", "ACCELEROMETER_EVT_SHAKE"},
};

// pin0 … pin16, pin19, pin20: the edge connector's pins, as MicroPython names them
bool pinNumber(const std::string& s, int& n) {
    if (s.size() < 4 || s.compare(0, 3, "pin") != 0) return false;
    for (size_t i = 3; i < s.size(); i++) if (s[i] < '0' || s[i] > '9') return false;
    if (s.size() > 5 || (s.size() == 5 && s[3] == '0')) return false;
    n = std::stoi(s.substr(3));
    return n <= 16 || n == 19 || n == 20;
}
bool isObject(const std::string& s) {
    int n;
    return s == "display" || s == "button-a" || s == "button-b" || s == "pin-logo" || s == "accelerometer" ||
           s == "compass" || s == "Image" || pinNumber(s, n);
}
bool isRoutine(const std::string& s) {
    return s == "temperature" || s == "running-time" || s == "pause" || s == "sleep" || s == "randint" || s == "image";
}

// The helpers a program may carry (board::Piece, runtimeFor): the shared
// arithmetic, and the micro:bit's own: where an error is written, the end of
// the program, pictures, the display's text, randint and the gestures.
const std::vector<Piece>& kPieces() {
    static const std::vector<Piece> p = boardPieces({
{"RK_FN", false, nullptr},
{"RK_ERR", true, R"RK(#ifdef RAKUPP_MICROBIT_SIM
#define RK_ERR rk_sim::err
#else
#define RK_ERR rk_out
#endif
)RK"},
{"F", true, R"RK(#ifndef F
#define F(s) (s)   // the Arduino core's flash-string marker: a micro:bit keeps a literal in flash anyway
#endif
)RK"},
{"RK_FLASH", false, nullptr},
// The serial port over USB, at CODAL's 115200 baud: what say and print write.
{"rk_out rk_port", false, R"RK(#ifdef RAKUPP_MICROBIT_SIM
#define rk_out rk_sim::out
#else
struct rk_port {
    void print(const char* s) { uBit.serial.send((uint8_t*)s, (int)strlen(s)); }
};
static rk_port rk_out;
#endif
)RK"},
// The program has ended: the board keeps what its display shows, and idles.
{"rk_halt", false, R"RK(#ifdef RAKUPP_MICROBIT_SIM
#define rk_halt(code) rk_sim::halt(code)
#else
RK_FN void rk_halt(int) { release_fiber(); }
#endif
)RK"},
{"rk_flash rk_print_int", false, nullptr},
// An integer as the display's text, all 64 bits of it: CODAL's own
// ManagedString(int) takes 32.
{"rk_ms_int", false, R"RK(RK_FN ManagedString rk_ms_int(int64_t v) {
    char b[21]; char* p = b + 20; *p = 0;
    uint64_t u = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
    do { *--p = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) *--p = '-';
    return ManagedString(p);
}
)RK"},
// A board seldom has a serial console open, so after the message it shows a
// sad face and scrolls the line, over and over, until it is reset.
{"rk_die_at", false, R"RK(RK_FN void rk_die_at(int line) {
    RK_ERR.print("\n  at line "); rk_print_int(RK_ERR, line); RK_ERR.print("\n");
#ifndef RAKUPP_MICROBIT_SIM
    static const uint8_t sad[25] = {0, 0, 0, 0, 0,  0, 255, 0, 255, 0,  0, 0, 0, 0, 0,  0, 255, 255, 255, 0,  255, 0, 0, 0, 255};
    for (;;) {
        uBit.display.print(MicroBitImage(5, 5, sad));
        uBit.sleep(1000);
        uBit.display.scroll(ManagedString("line ") + rk_ms_int(line));
    }
#endif
    rk_halt(1);
}
)RK"},
{"rk_die_div rk_tab", false, nullptr},
// MicroPython's ten brightness levels as CODAL's 0 to 255, in visually
// distinct steps; a level read back is a value's highest bit (255: 9).
{"rk_bright rk_level", false, R"RK(RK_FN uint8_t rk_bright(int64_t b, int line) {
    static const uint8_t map[10] = {0, 1, 2, 4, 8, 16, 32, 64, 128, 255};
    if (b < 0 || b > 9) {
        RK_ERR.print("Brightness out of range. Is: "); rk_print_int(RK_ERR, b);
        RK_ERR.print(", should be in 0..9"); rk_die_at(line);
    }
    return map[b];
}
RK_FN int64_t rk_level(int v) {
    if (v >= 255) return 9;
    int64_t l = 0;
    while (v > 0) { l++; v >>= 1; }
    return l;
}
)RK"},
// A picture: 25 levels, a table in flash or an array the program fills.
{"rk_show", false, R"RK(template<typename T> RK_FN void rk_show(const T (&lv)[25], int line) {
    uint8_t px[25];
    for (int i = 0; i < 25; i++) px[i] = rk_bright((int64_t)rk_flash(&lv[i]), line);
    uBit.display.print(MicroBitImage(5, 5, px));
}
)RK"},
{"rk_xy rk_set_pixel rk_get_pixel", false, R"RK(RK_FN void rk_xy(int64_t x, int64_t y, int line) {
    if (x < 0 || x > 4) rk_oob(x, 5, line);
    if (y < 0 || y > 4) rk_oob(y, 5, line);
}
RK_FN void rk_set_pixel(int64_t x, int64_t y, int64_t b, int line) {
    rk_xy(x, y, line);
    uint8_t v = rk_bright(b, line);
    uBit.display.image.setPixelValue((int16_t)x, (int16_t)y, v);
}
RK_FN int64_t rk_get_pixel(int64_t x, int64_t y, int line) {
    rk_xy(x, y, line);
    return rk_level(uBit.display.image.getPixelValue((int16_t)x, (int16_t)y));
}
)RK"},
{"rk_pause", false, R"RK(RK_FN void rk_pause(int64_t ms) { if (ms > 0) uBit.sleep((uint32_t)ms); }
)RK"},
{"rk_randint", false, R"RK(RK_FN int64_t rk_randint(int64_t a, int64_t b, int line) {
    if (b < a) {
        RK_ERR.print("randint's range "); rk_print_int(RK_ERR, a); RK_ERR.print("..");
        rk_print_int(RK_ERR, b); RK_ERR.print(" is empty"); rk_die_at(line);
    }
    uint64_t n = (uint64_t)b - (uint64_t)a + 1u;
    if (n == 0 || n > 2147483647u) { RK_ERR.print("randint's range is wider than the board's random numbers"); rk_die_at(line); }
#ifdef RAKUPP_MICROBIT_SIM
    return a + (int64_t)rk_sim::random(n);
#else
    return a + (int64_t)uBit.random((int)n);
#endif
}
)RK"},
// The gestures: is-gesture asks the accelerometer now; was-gesture whether
// it has raised one since that one was last asked about, as rk_on_gesture
// hears them (main() registers it, to run as the event is raised). On the
// board each first takes a sample, which is when CODAL works gestures out.
{"rk_is_gesture rk_was_gesture rk_on_gesture rk_gestures_seen", false, R"RK(static uint32_t rk_gestures_seen = 0;
RK_FN void rk_on_gesture(MicroBitEvent e) { if (e.value < 32) rk_gestures_seen |= 1u << e.value; }
RK_FN bool rk_was_gesture(int g) {
#ifndef RAKUPP_MICROBIT_SIM
    (void)uBit.accelerometer.getX();
#endif
    bool seen = (rk_gestures_seen >> g) & 1u;
    rk_gestures_seen &= ~(1u << g);
    return seen;
}
RK_FN bool rk_is_gesture(int g) {
#ifndef RAKUPP_MICROBIT_SIM
    (void)uBit.accelerometer.getX();
#endif
    return uBit.accelerometer.getGesture() == g;
}
)RK"},
    });
    return p;
}

class MicroBitEmitter : public Emitter {
public:
    MicroBitEmitter(Program& p, const MicroBitOptions& o) : Emitter(p, o.srcName, o.version) {}

protected:
    bool useMicroBit_ = false;
    bool listenGestures_ = false;                  // was-gesture: main() registers rk_on_gesture
    std::map<std::string, std::string> pictures_;  // Image.NAME → its table's C name

    const char* useHint() const override { return "`use MicroBit`"; }
    void boardUse(const std::string& module) override { if (module == "MicroBit") useMicroBit_ = true; }
    std::string lit(const std::string& c) const override { return c; }
    const char* outStream() const override { return "rk_out"; }
    bool loopIsFunction() const override { return false; }

    void needUse(const Node* n, const std::string& name) {
        if (!useMicroBit_) refuse(n, name + " is not declared — did you mean to `use MicroBit`?");
    }

    bool boardName(NameTerm* n, Ex& out) override {
        const std::string& s = n->name;
        if (s == "temperature" || s == "running-time") { out = routine(n, s, {}, true); return true; }
        if (isRoutine(s)) { needUse(n, s); refuse(n, s + " needs its arguments, as in `" + s + "(…)`"); }
        if (isObject(s)) {
            needUse(n, s);
            if (s == "Image") refuse(n, "Image used as a value — name one of its pictures, as in Image.HEART");
            refuse(n, s + " used as a value — call one of its methods");
        }
        return false;
    }

    bool boardCall(Call* c, const std::vector<Expr*>& args, bool value, Ex& out) override {
        if (!isRoutine(c->name)) return false;
        out = routine(c, c->name, args, value);
        return true;
    }

    // temperature, running-time, pause(ms), sleep(seconds), randint(a, b)
    Ex routine(const Node* at, const std::string& n, const std::vector<Expr*>& args, bool value) {
        needUse(at, n);
        auto arity = [&](size_t want) {
            if (args.size() != want) refuse(at, n + " takes " + std::to_string(want) + " argument" + (want == 1 ? "" : "s") + ", not " + std::to_string(args.size()));
        };
        std::string L = lineOf(at);
        if (n == "image") refuse(at, "image runs while building — make the picture a constant, as in `constant SMILE = image '…'`");
        if (n == "temperature") { arity(0); return {"((int64_t)uBit.thermometer.getTemperature())", K::Int}; }
        if (n == "running-time") { arity(0); return {"((int64_t)uBit.systemTime())", K::Int}; }
        if (n == "randint") { arity(2); return {"rk_randint(" + asInt(expr(args[0]), at) + ", " + asInt(expr(args[1]), at) + ", " + L + ")", K::Int}; }
        if (value) refuse(at, "the result of " + n + ", which answers nothing");
        arity(1);
        Ex a = expr(args[0]);
        if (n == "pause") return {"rk_pause(" + asInt(a, at) + ")", K::Int};
        // sleep: Raku's, in seconds, to the nearest millisecond
        if (a.k == K::Num) return {"rk_pause(rk_to_int(floor(" + a.c + " * 1000.0 + 0.5), " + L + "))", K::Int};
        return {"rk_pause(rk_mul(" + asInt(a, at) + ", " + cInt(1000) + "))", K::Int};
    }

    // The board's objects: their methods, as values and as statements.
    std::string objectOf(MethodCall* m) {
        if (!m->inv || m->inv->kind != NK::NameTerm) return "";
        const std::string& s = static_cast<NameTerm*>(m->inv.get())->name;
        int n;
        if (!isObject(s) && s.size() > 3 && s.compare(0, 3, "pin") == 0 && s.find_first_not_of("0123456789", 3) == std::string::npos && !pinNumber(s, n))
            refuse(m, s + " is not on the edge connector — the micro:bit has pin0 to pin16, pin19 and pin20");
        return isObject(s) ? s : "";
    }
    bool boardMethod(MethodCall* m, const std::vector<Expr*>& args, bool value, Ex& out) override {
        std::string obj = objectOf(m);
        if (obj.empty()) return false;
        out = objectCall(m, obj, args, value);
        return true;
    }
    bool boardStmt(MethodCall* m, std::ostream& o, int in) override {
        std::string obj = objectOf(m);
        if (obj.empty()) return false;
        o << ind(in) << objectCall(m, obj, argsOf(m->args), false).c << ";\n";
        return true;
    }

    Ex objectCall(MethodCall* m, const std::string& obj, const std::vector<Expr*>& args, bool value) {
        needUse(m, obj);
        const std::string& n = m->method;
        const std::string what = obj + "." + n;
        auto arity = [&](size_t want) {
            if (args.size() != want) refuse(m, what + " takes " + std::to_string(want) + " argument" + (want == 1 ? "" : "s") + ", not " + std::to_string(args.size()));
        };
        auto answers = [&](const std::string& c, K k) { return Ex{c, k}; };
        auto none = [&](const std::string& c) {
            if (value) refuse(m, "the result of " + what + ", which answers nothing");
            return Ex{c, K::Int};
        };
        std::string L = lineOf(m);
        if (obj == "display") {
            if (n == "show") {
                arity(1);
                std::string pic;
                if (picture(args[0], m, pic)) return none(pic);
                return none("uBit.display.print(" + text(args[0]) + ")");
            }
            if (n == "scroll") {
                arity(1);
                std::string pic;
                if (picture(args[0], m, pic)) refuse(m, "display.scroll of a picture — it scrolls text; show a picture with display.show");
                return none("uBit.display.scroll(" + text(args[0]) + ")");
            }
            if (n == "clear") { arity(0); return none("uBit.display.clear()"); }
            if (n == "set-pixel") {
                arity(3);
                return none("rk_set_pixel(" + asInt(expr(args[0]), m) + ", " + asInt(expr(args[1]), m) + ", " + asInt(expr(args[2]), m) + ", " + L + ")");
            }
            if (n == "get-pixel") { arity(2); return answers("rk_get_pixel(" + asInt(expr(args[0]), m) + ", " + asInt(expr(args[1]), m) + ", " + L + ")", K::Int); }
            if (n == "read-light-level") { arity(0); return answers("((int64_t)uBit.display.readLightLevel())", K::Int); }
            refuse(m, what + " — the display has show, scroll, clear, set-pixel, get-pixel and read-light-level");
        }
        if (obj == "button-a" || obj == "button-b") {
            std::string b = obj == "button-a" ? "uBit.buttonA" : "uBit.buttonB";
            if (n != "is-pressed" && n != "was-pressed" && n != "get-presses")
                refuse(m, what + " — a button has is-pressed, was-pressed and get-presses");
            arity(0);
            if (n == "is-pressed") return answers("(" + b + ".isPressed() != 0)", K::Bool);
            if (n == "was-pressed") return answers("(" + b + ".wasPressed() > 0)", K::Bool);
            return answers("((int64_t)" + b + ".wasPressed())", K::Int);
        }
        if (obj == "pin-logo") {
            if (n != "is-touched") refuse(m, what + " — the logo has is-touched");
            arity(0);
            return answers("(uBit.logo.isPressed() != 0)", K::Bool);
        }
        if (obj == "accelerometer") {
            if (n == "x" || n == "y" || n == "z") {
                arity(0);
                return answers(std::string("((int64_t)uBit.accelerometer.get") + (char)toupper(n[0]) + "())", K::Int);
            }
            if (n == "is-gesture" || n == "was-gesture") {
                arity(1);
                std::string g;
                if (!buildString(args[0], g)) refuse(m, what + " of something other than a gesture's name the build knows, as in 'shake'");
                const char* ev = nullptr;
                std::string known;
                for (const Gesture& k : kGestures) {
                    if (g == k.name) ev = k.event;
                    known += std::string(known.empty() ? "" : ", ") + "'" + k.name + "'";
                }
                if (!ev) refuse(m, "'" + g + "' is no gesture — the accelerometer knows " + known);
                if (n == "was-gesture") { listenGestures_ = true; return answers(std::string("rk_was_gesture(") + ev + ")", K::Bool); }
                return answers(std::string("rk_is_gesture(") + ev + ")", K::Bool);
            }
            refuse(m, what + " — the accelerometer has x, y, z, is-gesture and was-gesture");
        }
        if (obj == "compass") {
            if (n != "heading" && n != "calibrate") refuse(m, what + " — the compass has heading and calibrate");
            arity(0);
            if (n == "heading") return answers("((int64_t)uBit.compass.heading())", K::Int);
            return none("uBit.compass.calibrate()");
        }
        if (obj == "Image") {
            if (pictureOf(n)) refuse(m, what + " used as a value — show it (`display.show(" + what + ")`), or make it a constant");
            refuse(m, "Image has no picture " + n + " — its pictures are MicroPython's, spelt as Raku: Image.HEART, Image.ARROW-N…");
        }
        int pin = 0;
        pinNumber(obj, pin);
        std::string p = "uBit.io.P" + std::to_string(pin);
        if (n == "read-digital") { arity(0); return answers("((int64_t)" + p + ".getDigitalValue())", K::Int); }
        if (n == "read-analog") { arity(0); return answers("((int64_t)" + p + ".getAnalogValue())", K::Int); }
        if (n == "write-digital") { arity(1); return none(p + ".setDigitalValue((int)" + asBool(expr(args[0]), m) + ")"); }
        if (n == "write-analog") {
            arity(1);
            return none(p + ".setAnalogValue((int)rk_min<int64_t>(rk_max<int64_t>(" + asInt(expr(args[0]), m) + ", " + cInt(0) + "), " + cInt(1023) + "))");
        }
        if (n == "is-touched") {
            arity(0);
            if (pin > 2) refuse(m, obj + " is not a touch pin — pin0, pin1 and pin2 are");
            return answers("(" + p + ".isTouched() != 0)", K::Bool);
        }
        refuse(m, what + " — a pin has read-digital, write-digital, read-analog, write-analog and is-touched");
    }

    // A string the build knows: a literal, or a constant.
    bool buildString(Expr* a, std::string& out) {
        if (a->kind == NK::StrLit) { out = static_cast<StrLit*>(a)->v; return true; }
        if (a->kind == NK::InterpStr) {
            auto* is = static_cast<InterpStr*>(a);
            if (is->parts.size() == 1 && is->parts[0]->kind == NK::StrLit) { out = static_cast<StrLit*>(is->parts[0].get())->v; return true; }
            return false;
        }
        std::string n = a->kind == NK::NameTerm ? static_cast<NameTerm*>(a)->name : a->kind == NK::VarExpr ? static_cast<VarExpr*>(a)->name : "";
        if (const Const* c = n.empty() ? nullptr : findConst(n)) if (!c->table && c->k == K::Str) { out = c->s; return true; }
        return false;
    }

    // The display's text, as a ManagedString: the pieces of an interpolated
    // string, each a literal, an integer or a Bool, joined as it runs.
    std::string text(Expr* a) {
        if (a->kind == NK::Ternary) {   // `$ok ?? "yes" !! "no"`: either text
            auto* t = static_cast<Ternary*>(a);
            return "(" + asBool(expr(t->cond.get()), t->cond.get()) + " ? " + text(t->then.get()) + " : " + text(t->els.get()) + ")";
        }
        std::vector<Expr*> parts;
        if (a->kind == NK::InterpStr) for (auto& p : static_cast<InterpStr*>(a)->parts) parts.push_back(p.get());
        else parts.push_back(a);
        std::string c;
        for (Expr* p : parts) {
            Ex e = expr(p);
            std::string piece;
            if (e.k == K::Str) { if (e.c == "\"\"") continue; piece = "ManagedString(" + e.c + ")"; }
            else if (e.k == K::Int) piece = "rk_ms_int(" + e.c + ")";
            else if (e.k == K::Bool) piece = "ManagedString(" + e.c + " ? \"True\" : \"False\")";
            else refuse(p, "a num on the display — the board cannot write a num the way Raku does yet; show an integer (.round, .Int) instead");
            c += (c.empty() ? "" : " + ") + piece;
        }
        return c.empty() ? "ManagedString(\"\")" : c;
    }

    // A picture to show: a constant of 25 levels (from `image`, or a list),
    // one of Image's, or a shaped integer array of 25 the program fills. False:
    // not a picture, so text.
    bool picture(Expr* a, const Node* at, std::string& c) {
        std::string L = lineOf(at);
        if (a->kind == NK::Ternary) {   // `$x > 0 ?? Image.ARROW-E !! Image.ARROW-W`: either picture
            auto* t = static_cast<Ternary*>(a);
            std::string yes, no;
            bool py = picture(t->then.get(), at, yes), pn = picture(t->els.get(), at, no);
            if (!py && !pn) return false;
            if (!py || !pn) refuse(a, "a choice between a picture and text — show both as pictures, or both as text");
            c = "(" + asBool(expr(t->cond.get()), t->cond.get()) + " ? " + yes + " : " + no + ")";
            return true;
        }
        if (a->kind == NK::MethodCall && isNamed(static_cast<MethodCall*>(a)->inv, "Image")) {
            auto* m = static_cast<MethodCall*>(a);
            const Pic* pic = pictureOf(m->method);
            if (!pic) refuse(a, "Image has no picture " + m->method + " — its pictures are MicroPython's, spelt as Raku: Image.HEART, Image.ARROW-N…");
            if (!argsOf(m->args).empty()) refuse(a, "Image." + m->method + " takes no arguments");
            std::string& t = pictures_[pic->name];
            if (t.empty()) {
                t = uniqueGlobal("t_Image_" + mangle(pic->name));
                globals_ << "static const uint8_t " << t << "[25] RK_FLASH = {";
                int i = 0;
                for (const char* p = pic->levels; *p; p++) {
                    if (*p == ':') continue;
                    globals_ << (i ? ", " : "") << (*p - '0');
                    i++;
                }
                globals_ << "};   // Image." << pic->name << "\n";
            }
            c = "rk_show(" + t + ", " + L + ")";
            return true;
        }
        std::string vn = a->kind == NK::VarExpr ? static_cast<VarExpr*>(a)->name : a->kind == NK::NameTerm ? static_cast<NameTerm*>(a)->name : "";
        if (vn.empty()) return false;
        const std::string why = " — a picture is 25 brightness levels, 0 to 9: a constant from `image`, one of Image's, or a shaped integer array of 25";
        if (const Var* v = findVar(vn)) {
            if (!v->array) return false;
            if (v->n != 25 || !isIntT(v->t)) refuse(at, "display.show of " + vn + why);
            c = "rk_show(" + v->c + ", " + L + ")";
            return true;
        }
        if (const Const* k = findConst(vn)) {
            if (!k->table) return false;
            if (k->n != 25 || !isIntT(k->et)) refuse(at, "display.show of " + vn + why);
            for (size_t i = 0; i < k->ints.size(); i++)
                if (k->ints[i] < 0 || k->ints[i] > 9)
                    refuse(at, "the picture " + vn + " has a brightness of " + std::to_string(k->ints[i]) + " at LED " + std::to_string(i) + " — a level is 0 to 9");
            c = "rk_show(" + k->c + ", " + L + ")";
            return true;
        }
        return false;
    }

    // main(): the mainline after uBit.init(), then the top-level loop, and
    // the runtime they name.
    std::string assemble(bool sawLoop) override {
        std::ostringstream out;
        out << "// Generated by `rakupp --target=microbit` (Raku++ " << version_ << ") from " << srcName_ << ".\n"
            << "// A program for the BBC micro:bit V2 on CODAL: put it in microbit-v2-samples/source/main.cpp,\n"
            << "// run `python3 build.py` there, and copy MICROBIT.hex onto the MICROBIT drive.\n"
            << "// `rakupp --target=microbit --sim` builds it for this computer instead, on a simulated board.\n"
            << "#include \"MicroBit.h\"\n#include <stdint.h>\n#include <string.h>\n#include <math.h>\n\nMicroBit uBit;\n";
        std::ostringstream body;   // the program; the runtime above it is what it calls
        if (!globals_.str().empty()) body << "\n" << globals_.str();
        if (!protos_.str().empty()) body << "\n" << protos_.str();
        body << "\nint main() {\n    uBit.init();\n";
        if (listenGestures_) body << "    uBit.messageBus.listen(DEVICE_ID_GESTURE, DEVICE_EVT_ANY, rk_on_gesture, MESSAGE_BUS_LISTENER_IMMEDIATE);\n";
        body << begin_.str() << setup_.str();
        if (sawLoop) body << "    for (;;) {   // the top-level loop\n" << loop_.str() << "    }\n";
        body << "    rk_halt(0);   // " << (sawLoop ? "`last` left the top-level loop" : "the program has no top-level loop: it ran once") << "\n}\n";
        if (!funcs_.str().empty()) body << "\n" << funcs_.str();
        std::string rt = runtimeFor(body.str(), kPieces(), "no exceptions");
        if (!rt.empty()) out << "\n" << rt;
        out << body.str();
        return out.str();
    }
};

} // namespace

std::string transpileToMicroBit(Program& prog, const MicroBitOptions& opt) {
    MicroBitEmitter em(prog, opt);
    return em.run();
}

} // namespace rakupp
