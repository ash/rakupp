/* The C++ guide's second example: Raku as a library, no grammar involved.
 * Evaluate source, call subs with C++ values, read results back. Unlike the
 * other hosts, C++ LINKS against librakupp instead of dlopen'ing it. From a
 * checkout, on macOS:
 *
 *   c++ -std=c++17 -Iinclude bindings/examples/calc.cpp \
 *       build/librakupp.dylib -Wl,-rpath,$PWD/build -o calc && ./calc
 *
 * On Linux:  c++ -std=c++17 -Iinclude bindings/examples/calc.cpp \
 *       -Lbuild -lrakupp -Wl,-rpath,$PWD/build -lpthread -o calc
 *
 * Against an INSTALLED rakupp the include line below is unchanged and the
 * flags are just -lrakupp.
 *
 * Two C++ habits worth copying from this file: bind each result to a NAMED
 * Tree before reading it (list() and map() hand back references INTO the
 * Tree, so a temporary would dangle), and name a container argument as a
 * Tree too — brace elision would otherwise flatten {vector{1,2,3}} into
 * three separate arguments.
 */
#include <rakupp/raku.hpp>

#include <fstream>
#include <iostream>
#include <sstream>

int main() {
    // eval runs source in the mainline scope and keeps it, like the REPL.
    std::cout << "2 + 2 = " << rakupp::eval("2 + 2").int_() << "\n";

    // So loading a file of subs is just an eval — they stay callable below.
    std::ifstream in("bindings/examples/calc.raku");
    std::stringstream ss;
    ss << in.rdbuf();
    rakupp::eval(ss.str());

    // call passes C++ values as arguments and returns a Tree.
    std::cout << "area(3, 4) = " << rakupp::call("area", {3, 4}).int_() << "\n";

    // a Raku list -> a Tree holding a vector
    rakupp::Tree primes = rakupp::call("primes-below", {30});
    std::cout << "primes below 30:";
    for (const auto& p : primes.list()) std::cout << " " << p.int_();
    std::cout << "\n";

    // a C++ vector -> a Raku list; a Raku hash -> a Tree holding a map
    rakupp::Tree nums = std::vector<rakupp::Tree>{3, 1, 4, 1, 5, 9, 2, 6};
    rakupp::Tree s = rakupp::call("stats", {nums});
    std::cout << "stats: count=" << s.map().at("count").int_()
              << " sum="  << s.map().at("sum").int_()
              << " mean=" << s.map().at("mean").str()
              << " max="  << s.map().at("max").int_() << "\n";

    rakupp::Tree who = std::map<std::string, rakupp::Tree>{{"name", "Ada"}, {"age", 36}};
    std::cout << "greet: " << rakupp::call("greet", {who}).str() << "\n";

    // Raku integers do not overflow; past 64 bits this one hands back digits.
    std::cout << "30! = " << rakupp::call("factorial", {30}).str() << "\n";

    // A die inside Raku crosses as RakuError, the host's own exception type.
    try {
        rakupp::call("checked-div", {10, 0});
    }
    catch (const rakupp::RakuError& e) {
        std::cout << "died: " << e.what() << "\n";
    }
    return 0;
}
