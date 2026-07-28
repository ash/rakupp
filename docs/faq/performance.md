# FAQ — my program is slow

The short answer, in order of how much it usually buys:

1. **Compile it** — `rakupp --exe -O prog.raku -o prog`
2. **Check it is actually CPU-bound** before doing anything else
3. **Avoid rebuilding big values in a loop**

## Compile it

```bash
rakupp --exe -O prog.raku -o prog && ./prog
```

Measured on the benchmark kernels (2026-07-29), interpreter against `--exe -O`:

| kernel | interpreter | `--exe` | `--exe -O` |
|---|---:|---:|---:|
| 5M integer accumulation | 1342ms | 296ms | **33ms** |
| `fib(32)`, recursive calls | 3809ms | 637ms | **170ms** |
| 1M `** 3` then `% 1000` | 847ms | 578ms | **54ms** |
| primes < 200k by trial division | 11579ms | 1206ms | **23ms** |

That is where the big factors are: arithmetic in a loop, called functions, integer
work. `-O` turns those into native operations instead of boxed values.

The middle column matters: **`--exe` alone is worth a lot, and `-O` again on top
of it.** Compiling without `-O` already removes the tree-walk; `-O` then turns
the arithmetic into native operations.

What neither speeds up much:

| kernel | interpreter | `--exe` | `--exe -O` |
|---|---:|---:|---:|
| 400k string appends | 99ms | 24ms | 23ms |

String building, IO, regex and hash work are already doing the same underlying
operations in both modes. If that is your program, compiling changes little —
and `--exe` without `-O` is not the fast mode; the `-O` is what matters.

## Check what you are actually measuring

```raku
my $t0 = now;
# … the part you suspect …
note "elapsed: ", (now - $t0).round(0.001), "s";
```

Startup is ~2ms, so for anything short you are timing the work, not the launch.
If elapsed time is dominated by a `run`/`shell` call, a network round trip or
reading a file, none of the above applies — you are waiting on something else.

## Things that are slow in any Raku

**Rebuilding a big value inside a loop.** Appending to a string with `~=` is
in-place and cheap; `$s = $s ~ $x` is not, and neither is `@a = @a, $x`.

**Sorting with a comparator when a key would do.** `.sort({ $^a.foo cmp $^b.foo })`
calls your block on every comparison — O(n log n) times. `.sort(*.foo)` extracts
the key once per element:

```raku
my @words = <delta alpha charlie bravo>;
say @words.sort(*.chars);       # key extraction — one call per element
```

**Regexes rebuilt per iteration.** Hoist a regex out of the loop if the pattern
is constant.

## Measuring a change

The repo has the harnesses used for its own numbers:

```bash
rakupp tools/perf-guard.raku       # interpreter hot path, ~10s
rakupp tools/run-bench.raku        # interpreter / --exe / Rakudo, 3-way
rakupp tools/run-optbench.raku     # --exe vs --exe -O, and verifies they agree
```

`run-optbench` checks that all four modes produce *identical output* before it
reports a timing, so it catches an optimisation that changed an answer.

If you have a program where Raku++ is much slower than you expect — especially
slower than Rakudo — that is worth reporting. Include the program.

---

Deeper detail: [OPTIMIZATION.md](../OPTIMIZATION.md) for what `-O` does,
[BENCHMARKS.md](../BENCHMARKS.md) for the full measured set.

Back to the [FAQ index](README.md).
