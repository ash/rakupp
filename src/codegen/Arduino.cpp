// The Arduino backend (ARDUINO-PLAN.md, phase A0 with A1's constants): a
// Raku program to one Arduino sketch. The emitter is the boards' shared one
// (BoardEmitter.h); this is what the Arduino adds to it:
//   - the `Arduino` module's routines and constants: the core's own names.
//   - say / print / note: Serial writes, string literals kept in flash (F()).
//   - the program's shape: the mainline before the top-level `loop { }` is
//     setup(), one iteration of that loop is one call of loop().
//   - the Uno R4 WiFi's LED matrix (`use Arduino::LEDMatrix`) and network
//     (`use Arduino::WiFi`), from its core's libraries.
#include "Arduino.h"
#include "BoardEmitter.h"

namespace rakupp {
namespace {
using namespace board;

struct Api { const char* name; std::vector<const char*> args; bool result; };
const std::vector<Api>& apis() {
    static const std::vector<Api> a = {
        {"pinMode", {"uint8_t", "uint8_t"}, false},
        {"digitalWrite", {"uint8_t", "uint8_t"}, false},
        {"digitalRead", {"uint8_t"}, true},
        {"analogRead", {"uint8_t"}, true},
        {"analogWrite", {"uint8_t", "int"}, false},
        {"delay", {"unsigned long"}, false},
        {"delayMicroseconds", {"unsigned int"}, false},
        {"millis", {}, true},
        {"micros", {}, true},
    };
    return a;
}
const Api* apiOf(const std::string& n) {
    for (auto& a : apis()) if (n == a.name) return &a;
    return nullptr;
}
bool isBoardConst(const std::string& n) {
    static const std::set<std::string> s = {"HIGH", "LOW", "INPUT", "OUTPUT", "INPUT_PULLUP", "LED_BUILTIN",
                                            "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7"};
    return s.count(n) > 0;
}
// wl_status_t of the WiFiS3 library, which `use Arduino::WiFi` names
bool isWiFiConst(const std::string& n) {
    static const std::set<std::string> s = {"WL_NO_MODULE", "WL_IDLE_STATUS", "WL_NO_SSID_AVAIL", "WL_CONNECTED",
                                            "WL_CONNECT_FAILED", "WL_CONNECTION_LOST", "WL_DISCONNECTED"};
    return s.count(n) > 0;
}

// The helpers a sketch may carry (board::Piece, runtimeFor). The functions
// live in a namespace because the Arduino IDE writes a prototype for each
// file-scope function of a .ino above the first one, and drops a template's
// `template<...>` when it does: `static T rk_flash(const T* p);` does not
// compile. Functions in a namespace it leaves alone.
const std::vector<Piece>& kPieces() {
    static const std::vector<Piece> p = boardPieces({
{"RK_FN", false, nullptr},
{"RK_ERR", true, R"RK(#ifdef RAKUPP_ARDUINO_SIM
#define RK_ERR rk_sim::err
#else
#define RK_ERR Serial
#endif
)RK"},
{"RK_FLASH", false, nullptr},
{"rk_halt", false, R"RK(#ifdef RAKUPP_ARDUINO_SIM
#define rk_halt(code) rk_sim::halt(code)
#else
RK_FN void rk_halt(int) { Serial.flush(); for (;;) delay(1000); }
#endif
)RK"},
{"rk_flash rk_tab", false, nullptr},
// WiFi.begin's answer, held until the board has an address: WiFiS3 answers
// WL_CONNECTED once it has joined, and the router's address comes after, so
// a localIP() straight after it is 0.0.0.0 (an Uno R4 WiFi printed that).
// It asks every 100 ms for the library's 10 s; with no address it leaves, so
// status() agrees. The simulated network brings the address as it joins.
{"rk_wifi_joined", false, R"RK(#ifdef RAKUPP_ARDUINO_SIM
#define rk_wifi_joined(s) (s)
#else
RK_FN int rk_wifi_joined(int s) {
    if (s != WL_CONNECTED) return s;
    for (unsigned long t = millis(); millis() - t < 10000UL; delay(100))
        if ((uint32_t)WiFi.localIP() != 0) return WL_CONNECTED;
    WiFi.disconnect();
    return WL_CONNECT_FAILED;
}
#endif
)RK"},
    });
    return p;
}

class ArduinoEmitter : public Emitter {
public:
    ArduinoEmitter(Program& p, const ArduinoOptions& o) : Emitter(p, o.srcName, o.version) {}

protected:
    bool useArduino_ = false, serialBegun_ = false;
    bool useMatrix_ = false, matrixUsed_ = false;   // `use Arduino::LEDMatrix`, and a call that needs the library
    bool useWiFi_ = false, wifiUsed_ = false;       // `use Arduino::WiFi`, and a name that needs the library

    const char* useHint() const override { return "`use Arduino`"; }
    void boardUse(const std::string& module) override {
        if (module == "Arduino") useArduino_ = true;
        if (module == "Arduino::LEDMatrix") useMatrix_ = true;
        if (module == "Arduino::WiFi") useWiFi_ = true;
    }
    std::string lit(const std::string& c) const override { return "F(" + c + ")"; }
    const char* outStream() const override { return "Serial"; }
    bool loopIsFunction() const override { return true; }

    bool boardName(NameTerm* n, Ex& out) override {
        const std::string& s = n->name;
        if (isBoardConst(s)) {
            if (!useArduino_) refuse(n, s + " is not declared — did you mean to `use Arduino`?");
            out = {"((int64_t)" + s + ")", K::Int, true};
            return true;
        }
        if (isWiFiConst(s)) {
            if (!useWiFi_) refuse(n, s + " is not declared — did you mean to `use Arduino::WiFi`?");
            wifiUsed_ = true;
            out = {"((int64_t)" + s + ")", K::Int, true};
            return true;
        }
        if (s == "Serial" || s == "LEDMatrix" || s == "WiFi") refuse(n, s + " used as a value — call one of its methods");
        return false;
    }

    bool boardCall(Call* c, const std::vector<Expr*>& args, bool value, Ex& out) override {
        const std::string& n = c->name;
        const Api* api = apiOf(n);
        if (!api) return false;
        if (!useArduino_) refuse(c, n + " is not declared — did you mean to `use Arduino`?");
        arityOf(c, args, api->args.size());
        if (value && !api->result) refuse(c, "the result of " + n + ", which answers nothing");
        std::string a;
        for (size_t i = 0; i < args.size(); i++) a += std::string(i ? ", " : "") + "(" + api->args[i] + ")(" + asInt(expr(args[i]), args[i]) + ")";
        if (api->result) out = {"((int64_t)" + n + "(" + a + "))", K::Int};
        else out = {n + "(" + a + ")", K::Int};
        return true;
    }

    bool boardMethod(MethodCall* m, const std::vector<Expr*>& args, bool, Ex& out) override {
        if (isNamed(m->inv, "WiFi")) { out = wifiCall(m, args, false); return true; }
        if (m->inv && m->inv->kind == NK::NameTerm &&
            (static_cast<NameTerm*>(m->inv.get())->name == "Serial" || static_cast<NameTerm*>(m->inv.get())->name == "LEDMatrix"))
            refuse(m, static_cast<NameTerm*>(m->inv.get())->name + "." + m->method + " used as a value — it answers nothing");
        return false;
    }

    bool boardPrint(Expr* a, std::ostream& o, int in, const std::string& stream) override {
        if (a->kind == NK::MethodCall && isNamed(static_cast<MethodCall*>(a)->inv, "WiFi") &&
            static_cast<MethodCall*>(a)->method == "localIP" && argsOf(static_cast<MethodCall*>(a)->args).empty()) {
            o << ind(in) << stream << ".print(" << wifiCall(static_cast<MethodCall*>(a), {}, true).c << ");\n";
            return true;
        }
        return false;
    }

    bool boardStmt(MethodCall* m, std::ostream& o, int in) override {
        if (m->inv && m->inv->kind == NK::NameTerm && static_cast<NameTerm*>(m->inv.get())->name == "Serial") {
            if (!useArduino_) refuse(m, "Serial is not declared — did you mean to `use Arduino`?");
            auto args = argsOf(m->args);
            if (m->method == "begin") {
                if (args.size() != 1) refuse(m, "Serial.begin takes the baud rate");
                o << ind(in) << "Serial.begin((unsigned long)(" << asInt(expr(args[0]), m) << "));\n";
                serialBegun_ = true;
                return true;
            }
            if (m->method == "print" || m->method == "println") {
                if (args.empty() && m->method == "print") refuse(m, "Serial.print with nothing to print");
                prints_ = true;
                emitPrints(o, in, "Serial", args, m);
                if (m->method == "println") o << ind(in) << "Serial.print(F(\"\\r\\n\"));\n";
                return true;
            }
            refuse(m, "Serial." + m->method + " — the board's Serial has begin, print and println");
        }
        if (m->inv && m->inv->kind == NK::NameTerm && static_cast<NameTerm*>(m->inv.get())->name == "LEDMatrix") {
            matrixCall(m, o, in);
            return true;
        }
        return false;
    }

    // The Uno R4 WiFi's matrix (`use Arduino::LEDMatrix`): one global
    // ArduinoLEDMatrix of the core's Arduino_LED_Matrix library. A frame is
    // copied into a local array of three words, so a table of any element
    // type, or a shaped array the board computes as it runs, can be shown.
    void matrixCall(MethodCall* m, std::ostream& o, int in) {
        if (!useMatrix_) refuse(m, "LEDMatrix is not declared — did you mean to `use Arduino::LEDMatrix`?");
        auto args = argsOf(m->args);
        const std::string& n = m->method;
        matrixUsed_ = true;
        if (n == "begin" && args.empty()) { o << ind(in) << "rk_matrix.begin();\n"; return; }
        if (n == "clear" && args.empty()) { o << ind(in) << "{ uint32_t rk_z[3] = {0, 0, 0}; rk_matrix.loadFrame(rk_z); }\n"; return; }
        if (n == "show" && args.size() == 1 && args[0]->kind == NK::VarExpr) {
            const std::string& vn = static_cast<VarExpr*>(args[0])->name;
            std::string L = lineOf(m), w[3];
            if (const Var* v = findVar(vn); v && v->array && v->n == 3 && isIntT(v->t)) {
                for (int i = 0; i < 3; i++) w[i] = "(uint32_t)rk_at(" + v->c + ", " + std::to_string(i) + ", " + L + ")";
            } else if (const Const* c = findConst(vn); c && c->table && c->n == 3 && isIntT(c->et)) {
                for (int i = 0; i < 3; i++) w[i] = "(uint32_t)rk_tab(" + c->c + ", " + std::to_string(i) + ", " + L + ")";
            } else {
                refuse(m, "LEDMatrix.show of " + vn + " — a frame is a constant list or a shaped integer array of three "
                          "32-bit words; make one with `frame`");
            }
            o << ind(in) << "{ uint32_t rk_f[3] = {" << w[0] << ", " << w[1] << ", " << w[2] << "}; rk_matrix.loadFrame(rk_f); }\n";
            return;
        }
        refuse(m, "LEDMatrix." + n + " — the matrix has begin, show(@frame) and clear");
    }

    // The Uno R4 WiFi's network (`use Arduino::WiFi`): the global `WiFi` of
    // the core's WiFiS3 library. The network's name and password are strings
    // the build knows — literals or constants — as the board has no others,
    // and `begin` answers once the board has an address (rk_wifi_joined);
    // the address it answers can be printed (`printed`), not kept. getTime
    // is the WiFi module's own NTP time, seconds since 1970, 0 until it has it.
    Ex wifiCall(MethodCall* m, const std::vector<Expr*>& args, bool printed) {
        if (!useWiFi_) refuse(m, "WiFi is not declared — did you mean to `use Arduino::WiFi`?");
        const std::string& n = m->method;
        wifiUsed_ = true;
        if (n == "begin" && (args.size() == 1 || args.size() == 2)) {
            std::string a;
            for (Expr* x : args) {
                Ex e = expr(x);
                if (e.k != K::Str)
                    refuse(x, "WiFi.begin of something other than a string the build knows — the network's name and "
                              "password are constants, as in `constant SSID = %*ENV<WIFI_SSID>`");
                a += (a.empty() ? "" : ", ") + e.c;
            }
            return {"((int64_t)rk_wifi_joined(WiFi.begin(" + a + ")))", K::Int};
        }
        if ((n == "status" || n == "disconnect" || n == "getTime") && args.empty()) return {"((int64_t)WiFi." + n + "())", K::Int};
        if (n == "localIP" && args.empty()) {
            if (!printed) refuse(m, "WiFi.localIP used as a value — the board can print the address (`say WiFi.localIP`), not keep it");
            return {"WiFi.localIP()", K::Str};   // an IPAddress, which the core's print takes
        }
        refuse(m, "WiFi." + n + " — the board's WiFi has begin(SSID, PASS), status, localIP, getTime and disconnect");
    }

    // A sketch: setup() and loop(), and the runtime they name.
    std::string assemble(bool sawLoop) override {
        std::ostringstream out;
        out << "// Generated by `rakupp --target=arduino` (Raku++ " << version_ << ") from " << srcName_ << ".\n"
            << "// An Arduino sketch: open it in the Arduino IDE, or build it with arduino-cli.\n"
            << "// `rakupp --target=arduino --sim` builds it for this computer instead, on a simulated board.\n"
            << "#include <Arduino.h>\n#include <stdint.h>\n#include <string.h>\n#include <math.h>\n";
        if (matrixUsed_) out << "#include \"Arduino_LED_Matrix.h\"   // the Uno R4 WiFi's matrix, from its core\n";
        if (wifiUsed_) out << "#include <WiFiS3.h>   // the Uno R4 WiFi's network, from its core\n";
        std::ostringstream body;   // the program; the runtime above it is what it calls
        if (matrixUsed_) body << "\nstatic ArduinoLEDMatrix rk_matrix;\n";
        if (!globals_.str().empty()) body << "\n" << globals_.str();
        if (!protos_.str().empty()) body << "\n" << protos_.str();
        body << "\nvoid setup() {\n";
        if (prints_ && !serialBegun_) body << "    Serial.begin(9600);\n";
        body << begin_.str() << setup_.str() << "}\n\nvoid loop() {\n";
        if (sawLoop) body << loop_.str();
        else body << "    rk_halt(0);   // the program has no top-level loop: it ran once, in setup()\n";
        body << "}\n";
        if (!funcs_.str().empty()) body << "\n" << funcs_.str();
        std::string rt = runtimeFor(body.str(), kPieces());
        if (!rt.empty()) out << "\n" << rt;
        out << body.str();
        return out.str();
    }
};

} // namespace

std::string transpileToArduino(Program& prog, const ArduinoOptions& opt) {
    ArduinoEmitter em(prog, opt);
    return em.run();
}

} // namespace rakupp
