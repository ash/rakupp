// The E0 smoke host (docs/dev/plans/EMBED-PLAN.md): the smallest program that
// links the static runtime and runs Raku from C++. It exists to be built and
// run by tools/embed-smoke.raku on every batch of ABI/embedding work — if this
// stops compiling or the exit code drifts, the embedding surface broke.
//
// Deliberately calls rakuppRun, not rakuppRunBigStack: a host that wants the
// 1 GiB stack thread can ask for it, and an embedding must work without it.
#include "Runtime.h"

#include <cstdio>

int main() {
    int rc = rakupp::rakuppRun("exit (1..10).grep(*.is-prime).sum",
                               {}, "embed-smoke.raku", "", {});
    if (rc != 17) {
        std::fprintf(stderr, "embed host: expected exit 17, got %d\n", rc);
        return 1;
    }
    std::puts("embed host: ok");
    return 0;
}
