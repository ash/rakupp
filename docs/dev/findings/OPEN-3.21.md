# What v3.21.0's gates found, and left open

Six findings from the v3.21.0 release sitting (2026-08-29). One is an engine
bug the release SHIPS; the rest are about the gates themselves — a gate that
cannot be read, or that reports green while leaving damage, is on its way to
becoming a ritual. All are v3.22.0's, and each is written with the repro so
nobody has to re-derive it.

## 1. `flat` over a BOUND array does not spread — RIPEMD hashes wrong

**Shipped in v3.21.0.** `Digest`'s `rmd160` returns incorrect digests for every
input, including the empty string. Nothing warns; it simply hashes wrong.

Bisected to **`0ae9387`** (".flat asks what slot an element sits in, not what
the element is"), which changed the spread gate in two places from
`(x.isList || !ofArray)` to `!ofArray`, in `src/Builtins.cpp` and
`src/MethodCallTail.cpp`. `326870f` passes, `0ae9387` fails, with a full build
and the dist's own suite at each of six points across the window.

The reasoning in that commit is right about *assignment* — `my @a = (1,2),(3,4)`
itemises each element into a Scalar, so `@a.flat` is two — and fifteen forms it
checked do now agree with Rakudo. What it misses is that **signature
destructuring BINDS**, and binding never itemises, so Rakudo still spreads
those elements:

```raku
sub f([&a, $b, @c, $d]) { (flat @c »xx» 2).elems }
say f([{;}, 1, (7,8,9), 2]);     # Rakudo 6, rakupp 3
```

A plain `my @c = 1,2,3; (flat @c »xx» 4).elems` is 3 on both — the divergence
needs the bound slot. `Digest::RIPEMD` builds its 80-entry constant table as
`[&a,$b,@c,$d]` destructuring followed by `flat @c »xx» 16`, so `@K` comes out
with 5 entries instead of 80 and every round after the fourth reads off the
end.

**Candidate fix, untested:** the elements already carry `itemized`, which
assignment sets and binding does not — so the gate may want to be `!x.itemized`
alone, dropping `ofArray` entirely. That gives `my @a = (1,2),(3,4); @a.flat`
→ 2, `((1,2),(3,4)).flat` → 4, and the destructured case → 6. It must be
checked against all nineteen forms `0ae9387` enumerates, not just these three.

**Why nothing else caught it:** it needs `flat` over a bound array of
iterables. 578 local checks, 643 Roast files and 954 documentation examples all
stayed green. The 59-dist battery was the only gate that saw it — which is what
`23cd3de` said the battery is for, one release earlier.

**Add a regression case with the fix**, not before: `t/regression/` has no
known-failure marker (only `#?requires`), so a failing file there turns gate 2
red for everyone.

## 2. The perf baseline moved, and nobody knows why

Five of nine kernels moved up against the 2026-08-27 record — fib +7.8%,
strscan +10.0%, subcall +11.0%, rats +26.0%, regexloop +18.5% — and four did
not (asg, loopsum, hash, strpass held or improved). It is **not** this
release's code: eight commits across the window were built arm64 and measured
one at a time on a settled machine, and no kernel moves outside noise at any of
them. The control is the finding — `d19263b`, the commit that RECORDED the old
baseline, rebuilt, measures fib 377.7 / rats 237.0 / regexloop 134.8, which are
today's numbers and not the ones it wrote.

Ruled out by measurement: load (identical at 2.5 and 4.0, before and after a
reboot, with and without `mediaanalysisd`/`mds` running), OS and toolchain
(unchanged since February), power and thermal state, and the kernels themselves
(`perf-guard.raku` byte-identical since the record). A different machine does
not explain it either — four of nine kernels still match.

The baseline was re-recorded so the release could ship. `best` keeps every
earlier figure, so `vs best` still reads fib +12.2%, strscan +11.8%, subcall
+13.5%, rats +31.7%, regexloop +19.2%. **Treat this as open, not closed** — and
note the trap: if the cause is environmental and the box returns to its old
form, the gate will read ~25% FASTER than baseline on `rats` and say nothing.

## 3. The Roast harness interleaves its own status lines

Under `--workers=4` the children's diagnostics are spliced mid-line into the
parent's per-file status output, consistently four corrupted lines a run:

```
  [PASS]    4/4  S03-smar# Failed test '$obj ~~ Pair, nonexistent, dies (1)' at …
```

The tallies are computed from data and are sound, but RELEASING.md calls the
**file list** the gate, and that list is parsed from those lines — so four
files per run silently lose their path and read as regressions. Line-buffering
the status writes would fix it.

## 4. No `roast.txt` was archived from v3.20.1

RELEASING.md says to keep it, precisely so the file-list diff has a baseline.
None existed, so v3.21.0's diff fell back to a development run found in
`rc-work/` (2026-08-28). The release should archive its own.

## 5. A compiled binary ignores `-e`, and `slim-diff` does not reap

`t/regression/anton-batch-round2.raku` does `run $*EXECUTABLE, '-e', $code`.
Under the interpreter that is `rakupp -e …`. Compiled, `$*EXECUTABLE` is the
compiled binary, which **accepts and silently ignores `-e`** and re-runs its
own embedded program — reaching the same line and spawning another copy. Each
generation names its temp dylib after the previous PID, which is how the chain
was identified:

```
87130 → 87134 → 87139 → …    (rk-cstruct-87118, -87130, -87134, …)
```

It reached **1,253 processes and load average 450** during gate 4b. `slim-diff`
reported `2 timed out (not judged)`, then `ALL SLIM-DIFF CHECKS PASSED`, and
left the chain running. Two fixes: a compiled binary should refuse `-e` rather
than run something else, and slim-diff's timeout should kill the process tree.

## 6. `perf-guard` picks the wrong binary by default

It takes the first of `build/`, `build-arm64/`, `./rakupp`. On the machine of
record `build/` is the **x86_64** build, so the gate command exactly as
RELEASING.md writes it reports `INCONCLUSIVE — … is x86_64 on a arm64 host`
instead of measuring. Every run in this sitting needed
`RAKUPP=build-arm64/rakupp`. Either prefer a native build or make the doc's
command the one that works.
