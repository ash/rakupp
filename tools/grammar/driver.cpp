/* The C++ half of the byte-identical gate: the same walk as driver.py,
 * through rakupp_grammar.hpp (header-only over librakupp). Run by
 * grammar-smoke.raku; outputs are compared byte for byte against the Raku
 * reference driver.
 *
 *   driver-cpp <grammar-file> <input-file>
 *
 * (No library path argument: the driver is LINKED against librakupp — the
 * C++ story — where the ctypes/FFI hosts dlopen it.)
 */
#include "grammar.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

static std::string canon(const rakupp::Tree& t) {
    if (t.is_list()) {
        std::string out = "[";
        bool first = true;
        for (auto& e : t.list()) { if (!first) out += ","; first = false; out += canon(e); }
        return out + "]";
    }
    if (t.is_map()) { /* std::map iterates sorted, matching sorted() in the twins */
        std::string out = "{";
        bool first = true;
        for (auto& kv : t.map()) { if (!first) out += ","; first = false; out += kv.first + ":" + canon(kv.second); }
        return out + "}";
    }
    if (t.is_null()) return "";
    if (std::holds_alternative<long long>(t.v)) return std::to_string(t.int_());
    if (std::holds_alternative<bool>(t.v)) return std::get<bool>(t.v) ? "True" : "False";
    return t.str();
}

static std::string slurp(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 3) { std::cerr << "usage: driver-cpp <grammar> <input>\n"; return 2; }

    auto g = rakupp::Grammar::from_file(argv[1], "Log", "LogActions");
    auto m = g.parse(slurp(argv[2]));
    if (!m) { std::cerr << "gate: the log corpus did not parse\n"; return 1; }

    auto lines = (*m)["line"];
    size_t n = lines.size();
    std::cout << "lines " << n << "\n";
    std::cout << "islist " << (lines.is_list() ? "True" : "False") << "\n";

    for (size_t i = 0; i < n; i++)
        std::cout << lines[i]["ip"].str() << " " << lines[i]["status"].str() << "\n";

    std::cout << "made " << canon((*m)["line"][0]["size"].made()) << "\n";
    std::cout << "req.str " << (*m)["line"][42]["req"].str() << "\n";
    std::cout << "size.int " << (*m)["line"][42]["size"].int_() << "\n";
    std::cout << "missing " << ((*m)["nope"].truthy() ? "True" : "False") << "\n";
    std::cout << "tree " << canon((*m)["line"][999].tree()) << "\n";

    auto one = g.parse("7.7.7.7 - - [x] \"GET / HTTP/1.1\" 200 5\n", "line");
    std::cout << "rule-parse " << (*one)["status"].str() << "\n";

    auto bad = g.parse("this is not a log line");
    std::cout << "failed-parse " << (bad ? "Match" : "None") << "\n";

    try {
        g.parse_or_throw("this is not a log line");
        std::cout << "diag none\n";
    } catch (const rakupp::ParseError& e) {
        std::cout << "diag line " << e.line << " col " << e.col << " rule " << e.rule << "\n";
    }
    return 0;
}
