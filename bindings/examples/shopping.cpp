/* The C++ guide's example: compile a grammar from a .raku file, parse text,
 * and move the results into C++. Unlike the other hosts, C++ LINKS against
 * librakupp instead of dlopen'ing it. From a checkout, on macOS:
 *
 *   c++ -std=c++17 -Isrc bindings/examples/shopping.cpp \
 *       build/librakupp.dylib -Wl,-rpath,$PWD/build -o shopping && ./shopping
 *
 * On Linux:  c++ -std=c++17 -Isrc bindings/examples/shopping.cpp \
 *       -Lbuild -lrakupp -Wl,-rpath,$PWD/build -lpthread -o shopping
 *
 * Against an INSTALLED rakupp the include is <rakupp/grammar.hpp> and the
 * flags are just -lrakupp.
 */
#include "grammar.hpp"

#include <iostream>

int main() {
    auto g = rakupp::Grammar::from_file("bindings/examples/shopping.raku",
                                        "Shopping", "ShoppingActions");

    // parse returns std::nullopt if the grammar does not match
    auto m = g.parse("milk=2\nbread=1\neggs=12\n");
    if (!m) {
        std::cerr << "no match\n";
        return 1;
    }

    auto items = (*m)["item"];
    std::cout << items.size() << " items\n";
    for (size_t i = 0; i < items.size(); i++)   // lazy: one engine call per leaf
        std::cout << items[i]["name"].str() << " x " << items[i]["qty"].int_() << "\n";

    // ShoppingActions made this
    std::cout << "total, computed in Raku: " << m->made().int_() << "\n";

    rakupp::Tree all = m->tree();               // the whole match, eagerly
    std::cout << "items in the tree: " << all.map().at("item").list().size() << "\n";

    // parse gives std::nullopt on a non-match; parse_or_throw diagnoses it
    try {
        g.parse_or_throw("milk=2\nbread=lots\n");
    }
    catch (const rakupp::ParseError& e) {
        std::cout << e.what() << "\n";
    }
    return 0;
}   // m's destructor unroots the engine value
