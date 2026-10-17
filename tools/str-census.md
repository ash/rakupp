# The string census build

VALUE32-PLAN leaves one question open about `Value`'s string. Design B keeps
every string in a shared body. Design C keeps strings of up to 15 bytes inline
and reads them as `std::string_view`. The plan says the choice is one
measurement on real programs. This build takes that measurement.

With `-DRAKUPP_STR_CENSUS`, `CowStr` ([src/Value.h](../src/Value.h)) counts
five things, each in six length bands: empty, 1-7, 8-15, 16-22, 23-63 and 64
or more bytes.

- **made**: every string `take()` makes.
- **copies, inline**: copies of a string held inline.
- **copies, shared**: copies of a string held in a shared body.
- **written inline**: inline texts that an instance wrote itself, whether by
  `take`, `mut()`, an append or a prepend. Each one is a body that design B
  would allocate and that today costs nothing. A write to a string that has
  been copied counts again, because B would detach there. A string that grows
  past 22 bytes and is promoted does not count, because today pays for that
  body too.
- **literal evals**: strings made while a string literal or a bareword Pair key
  is being evaluated. B could share these from one body kept on the AST node.

`CowStr` and `ExecContext` grow in this build, so their size asserts are off.
Nothing else changes. At exit each process appends one line to
`$RAKUPP_STR_CENSUS_OUT`, or to stderr when it is unset. The line holds the
pid, then `$RAKUPP_STR_CENSUS_LABEL` (or the last argument when there is no
label), then the 30 counts.

Build it in a tree of its own:

```bash
cmake -S . -B build-strcensus -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-DRAKUPP_STR_CENSUS
cmake --build build-strcensus --target rakupp -j4
```

Put the per-process malloc counter
([malloc-census.c](malloc-census.c), in its `MALLOC_CENSUS_OUT` mode) beside
it, so each design's extra allocations can be priced against what the program
allocates today:

```bash
cc -dynamiclib -O2 tools/malloc-census.c -o /tmp/malloc-census.dylib
```

Then run any program, or all of Roast, under both. The harness is the census
binary itself, so every test file it starts is counted:

```bash
RAKUPP_STR_CENSUS_OUT=/tmp/sc.tsv MALLOC_CENSUS_OUT=/tmp/mc.tsv \
DYLD_INSERT_LIBRARIES=/tmp/malloc-census.dylib \
build-strcensus/rakupp tools/run-roast.raku -j=8
```

Sum the result:

```bash
build/rakupp tools/str-census.raku --top=10 /tmp/sc.tsv /tmp/mc.tsv
```

A process started through `/bin/sh` loses `DYLD_INSERT_LIBRARIES`, because
macOS strips it for protected binaries. Such a process has a census line but
no malloc count, and the totals leave it out. Run a single program through
`perl -e 'alarm 120; exec @ARGV'` only if you set `DYLD_INSERT_LIBRARIES` inside
that Perl, for the same reason.

The results of 2026-10-15 are in
[VALUE32-PLAN.md](../docs/dev/plans/VALUE32-PLAN.md), "B versus C, measured".
