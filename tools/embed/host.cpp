// The E0 smoke host (docs/dev/plans/EMBED-PLAN.md): the smallest program that
// links the static runtime and runs Raku from C++. It exists to be built and
// run by tools/embed-smoke.raku on every batch of ABI/embedding work — if this
// stops compiling or the exit code drifts, the embedding surface broke.
//
// The host runs under a COMMA-DECIMAL locale when the system has one: real
// embedders have a locale set (CPython coerces LC_CTYPE; GUI hosts call
// setlocale(LC_ALL, "")), and the engine's semantics must not notice
// (src/AsciiCtype.h, src/CNumeric.h). Under de_DE, an unfixed engine reads
// `2.5e0` as 2, throws on '4.25'.Num, and sprintfs "1,50" — each drops one
// count below and the check fails. When no such locale is installed the host
// still runs, in "C", guarding the embedding surface alone.
//
// Deliberately calls rakuppRun, not rakuppRunBigStack: a host that wants the
// 1 GiB stack thread can ask for it, and an embedding must work without it.
#include "Runtime.h"

#include <clocale>
#include <cstdio>

int main() {
    bool comma = false;
    for (const char* loc : {"de_DE.UTF-8", "de_DE", "fr_FR.UTF-8", "fr_FR"}) {
        if (std::setlocale(LC_ALL, loc)) {
            comma = std::localeconv()->decimal_point[0] == ',';
            break;
        }
    }
    int rc = rakupp::rakuppRun(
        "my $ok = 0;"
        "$ok++ if 2.5e0 == 5/2;"                    // Num literal (lexer strtod)
        "$ok++ if 2.5e0.Str eq '2.5';"              // Num.Str round-trip loop
        "$ok++ if sprintf('%.2f', 3/2) eq '1.50';"  // sprintf float conversions
        "$ok++ if '4.25'.Num == 17/4;"              // Str→Num validate + parse
        "$ok++ if (1..10).grep(*.is-prime).sum == 17;"
        "exit $ok",
        {}, "embed-smoke.raku", "", {});
    if (rc != 5) {
        std::fprintf(stderr, "embed host: expected exit 5, got %d%s\n", rc,
                     comma ? " (under a comma-decimal locale)" : "");
        return 1;
    }
    std::puts(comma ? "embed host: ok (comma-decimal locale)" : "embed host: ok");
    return 0;
}
