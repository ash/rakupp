# Junction benchmarks

The programs behind the numbers in
[`docs/dev/plans/JUNCTION-PLAN.md`](../../../docs/dev/plans/JUNCTION-PLAN.md).
Each runs under both engines unchanged, which is the point — run it twice and
compare:

```sh
./build/rakupp tools/bench/junction/width.raku
raku          tools/bench/junction/width.raku
```

For a before/after table across engines, let `drive.raku` do the alternating and
the merging — build the older engine in a worktree so the working tree is left
alone:

```sh
git worktree add /tmp/jbefore <commit>
cmake -S /tmp/jbefore -B /tmp/jbefore/build -DCMAKE_BUILD_TYPE=Release
make -C /tmp/jbefore/build -j6 rakupp
raku tools/bench/junction/drive.raku before=/tmp/jbefore/build/rakupp after=./build/rakupp rakudo=raku
git worktree remove --force /tmp/jbefore
```

| program | answers |
|---|---|
| `width.raku` | cost per eigenstate, at W = 3 … 10000. The junction is built once with the n-ary constructor, so this times the autothread and not the build. Small-W rows carry the loop's own per-iteration cost; read the W=1000/10000 rows for the asymptote. |
| `shortcircuit.raku` | whether the collapse short-circuits: one W=2000 `any`, matched against a needle at the front, the middle, the end, and absent. Flat = no short-circuit, linear in position = short-circuit. |
| `dispatch.raku` | where the per-eigenstate cost comes from: `==` against `~~` against a 3-wide junction match, net of the loop. If `~~` costs what `==` costs, the dispatch is not the problem. |
| `literal.raku` | `5 ~~ 1 \| 3 \| 5` as written, split into build, match, and both — the left-assoc build cost against the autothread cost. |
| `scale.raku` | the parallel ceiling on this box: the same total work as N `start` blocks and as a serial loop, at N = 1, 2, 4, 8. Not junction-specific; it bounds what a parallel autothread could ever return. |
| `compare.raku` | the before/after table: every junction shape at once — collapse by hit position, `all`/`one`/`none`, boolification, the width-2-4 shapes real code writes, `grep`/`first`. Interleaves the cases inside one run and prints a checksum per case, so two binaries that disagree on an answer cannot be compared on time. TAB-separated, because case names contain commas and pipes. |
| `drive.raku` | runs `compare.raku` under two or more engines, ALTERNATING them round by round, checks their checksums agree before reporting any timing, and prints the before/after table. This is what produced the table in JUNCTION-PLAN.md. |
| `pool.cpp` | the parallel floor, with no interpreter in the picture: a warm thread pool's fan-out/join round trip, and a bare `std::thread` spawn+join for contrast. `c++ -O2 -std=c++17 pool.cpp -o pool && ./pool` |

`scale.raku` needs parallel mode, which is the default; `RAKUPP_GIL=1` is the
control that should show no speed-up at all.

Timings are taken from inside the program with `now`, so interpreter startup
does not dilute them. See
[`docs/guide/PARALLEL-SPEEDUP.md`](../../../docs/guide/PARALLEL-SPEEDUP.md) for
the interleaving discipline the parallel numbers want.

## A warm-up trap

`shortcircuit.raku` runs its W=2000 rows only 500 times. That is plenty for
rakupp, which interprets each iteration the same way, but too few for MoarVM to
optimise the loop — it reported Rakudo at 1.8 us for a case `compare.raku`
measures at 0.37 us once warm, and a comparison drawn from the cold number
flatters us by 5x. When the Rakudo column matters, use `compare.raku` (2000+
reps x 5 rounds) or raise the rep counts; a cross-engine ratio taken from a few
hundred iterations is measuring JIT warm-up, not the engine.
