// reloc-probe — which relocation path is `ValueList` actually running?
//
// This file exists because the answer was WRONG for most of the sitting that
// introduced it, and nothing caught it. `RVec` (src/ValueVec.h) grows by
// memcpy when the standard library's `std::string` tolerates a bitwise move
// and by a move-and-destroy loop when it does not. Both are correct, so a
// broken probe costs only speed — which is invisible unless something asks.
//
// The first version of `bitwiseRelocOk()` asked whether a short string's
// `data()` pointer lay inside the string object. It does, on every
// implementation: that is what the small-buffer optimisation IS. So it
// answered "not relocatable" everywhere, libc++ included, and every number
// measured that day came from the fallback.
//
// Build and run:
//
//   c++ -std=c++20 -O2 -DNDEBUG -Isrc -Iinclude tools/reloc-probe.cpp \
//       build/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props,stubs}.a \
//       -o /tmp/reloc-probe && /tmp/reloc-probe
//
// Exit status is 0 when the container is correct, whichever path is live;
// the printed line is what tells you which one that is.
#include "Value.h"

#include <cstdio>
#include <string>

int main() {
    const bool memcpyPath = rakupp::bitwiseRelocOk();
    std::printf("relocation path : %s\n", memcpyPath ? "memcpy (bitwise)" : "move-and-destroy loop");
    std::printf("standard library: %s\n",
#if defined(_LIBCPP_VERSION)
                "libc++ (expects memcpy)"
#elif defined(__GLIBCXX__)
                "libstdc++ (expects the move loop: short strings self-point)"
#else
                "other"
#endif
    );

    // Correctness, on the member that decides it: 5,000 Str elements pushed
    // one at a time, so the buffer is relocated a dozen times, then every
    // element read back. A wrong bitwise move shows up here as garbage or a
    // crash, not as a wrong answer somewhere subtle.
    rakupp::ValueList v;
    for (int i = 0; i < 5000; i++)
        v.push_back(rakupp::Value::str("s-" + std::to_string(i) + std::string(i % 90, 'x')));
    int bad = 0;
    for (int i = 0; i < 5000; i++)
        if (v[i].s.str() != "s-" + std::to_string(i) + std::string(i % 90, 'x')) bad++;

    // …and the self-referential push `std::vector` guarantees.
    rakupp::ValueList w;
    for (int i = 0; i < 8; i++) w.push_back(rakupp::Value::integer(i));
    w.push_back(w[0]);
    if (w.size() != 9 || w[8].i != 0) bad++;

    std::printf("5000 Str elements across ~13 reallocations, plus a self-push: %s\n",
                bad ? "CORRUPT" : "ok");
    return bad ? 1 : 0;
}
