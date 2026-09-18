# Junction benchmarks

The programs behind the numbers in
[`docs/dev/plans/JUNCTION-PLAN.md`](../../../docs/dev/plans/JUNCTION-PLAN.md).
Each runs under both engines unchanged, which is the point — run it twice and
compare:

```sh
./build/rakupp tools/bench/junction/width.raku
raku          tools/bench/junction/width.raku
```

| program | answers |
|---|---|
| `width.raku` | cost per eigenstate, at W = 3 … 10000. The junction is built once with the n-ary constructor, so this times the autothread and not the build. Small-W rows carry the loop's own per-iteration cost; read the W=1000/10000 rows for the asymptote. |
| `shortcircuit.raku` | whether the collapse short-circuits: one W=2000 `any`, matched against a needle at the front, the middle, the end, and absent. Flat = no short-circuit, linear in position = short-circuit. |
| `dispatch.raku` | where the per-eigenstate cost comes from: `==` against `~~` against a 3-wide junction match, net of the loop. If `~~` costs what `==` costs, the dispatch is not the problem. |
| `literal.raku` | `5 ~~ 1 \| 3 \| 5` as written, split into build, match, and both — the left-assoc build cost against the autothread cost. |
| `scale.raku` | the parallel ceiling on this box: the same total work as N `start` blocks and as a serial loop, at N = 1, 2, 4, 8. Not junction-specific; it bounds what a parallel autothread could ever return. |
| `pool.cpp` | the parallel floor, with no interpreter in the picture: a warm thread pool's fan-out/join round trip, and a bare `std::thread` spawn+join for contrast. `c++ -O2 -std=c++17 pool.cpp -o pool && ./pool` |

`scale.raku` needs parallel mode, which is the default; `RAKUPP_GIL=1` is the
control that should show no speed-up at all.

Timings are taken from inside the program with `now`, so interpreter startup
does not dilute them. See
[`docs/guide/PARALLEL-SPEEDUP.md`](../../../docs/guide/PARALLEL-SPEEDUP.md) for
the interleaving discipline the parallel numbers want.
