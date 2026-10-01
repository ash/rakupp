# The `i`/`n` audit build

VALUE32-PLAN design A, batch 1, puts `Value::i` and `Value::n` in one 8-byte
slot. That is only safe if no code reads one of them after the other was
written and expects the value it held before. This build finds those reads.

With `-DRAKUPP_IN_AUDIT`, `i` and `n` become proxy fields
([src/Value.h](../src/Value.h)). Their storage stays separate, so the engine
behaves exactly as a normal build does. Every write records which of the two
was written last. A read of the other one checks whether a shared slot would
have answered different bits from the ones stored. If it would have, the
caller's address is counted. The counts are appended at exit to
`$RAKUPP_IN_AUDIT_OUT` (stderr when unset), one line per (kind, address):

    read-n	0x1000a3f2c	1234

Build it in a tree of its own, with line tables so the addresses symbolize:

```bash
cmake -S . -B build-inaudit -DCMAKE_BUILD_TYPE=Release \
      "-DCMAKE_CXX_FLAGS=-DRAKUPP_IN_AUDIT -gline-tables-only"
cmake --build build-inaudit --target rakupp -j4
```

Run anything under it with `RAKUPP_IN_AUDIT_OUT=/some/file`, then sum the
lines per address and symbolize them:

```bash
awk -F'\t' '{c[$1"\t"$2]+=$3} END {for (k in c) print c[k]"\t"k}' /some/file |
  sort -rn > hits.tsv
cut -f3 hits.tsv | atos -o build-inaudit/rakupp -l 0x100000000
```

Each address is a read that the union would change. It is either a real
bug in the union plan, which the batch must fix, or a read whose result is
never used.

`--exe` programs cannot run under this build: the generated C++ includes
`Value.h` without the define.
