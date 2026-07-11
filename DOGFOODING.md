# Dogfooding: Raku++ builds itself with Raku

The tools that build, test, and measure Raku++ are — wherever it makes sense —
written in Raku and executed by `rakupp` itself. That is deliberate: every run
of the toolchain is a real-world workout for the interpreter, and it keeps the
project honest about what the language implementation can actually do.

## The self-hosted tools

| Tool | What it does | Run as |
|---|---|---|
| [`tools/run-roast.raku`](tools/run-roast.raku) | The full Roast harness: runs all ~1,460 spec-test files with timeouts, parses TAP, classifies pass/partial/no-TAP/timeout, prints the per-synopsis table that goes into [ROAST.md](ROAST.md). The entire coverage figure is measured *by* Raku++ — and thanks to the sub-15 ms startup, the whole ~1,460-file suite runs in about 3½ minutes, so refreshing the numbers is a coffee break, not an overnight job. | `build/rakupp tools/run-roast.raku [PATTERN]` |
| [`tools/gen-unicode.raku`](tools/gen-unicode.raku) | The UCD parser: reads `UnicodeData.txt`, `NameAliases.txt` and `DerivedNumericValues.txt` (40k+ lines) and generates the C++ character-name / category / numeric-value tables in `src/`. Raku++ literally generates part of its own source. | `build/rakupp tools/gen-unicode.raku` |
| [`tools/run-bench.raku`](tools/run-bench.raku) | The benchmark harness behind [BENCHMARKS.md](BENCHMARKS.md): times interp / `--exe` / Rakudo as fresh subprocesses. Also runs under Rakudo, so the harness language can't bias the results. | `build/rakupp tools/run-bench.raku` |
| [`tools/run-optbench.raku`](tools/run-optbench.raku) | Same idea for the `--exe -O` optimizer measurements in [OPTIMIZATION.md](OPTIMIZATION.md). | `build/rakupp tools/run-optbench.raku` |
| [`tools/rc-compare.raku`](tools/rc-compare.raku) | The RosettaCode survey ([docs/ROSETTACODE.md](docs/ROSETTACODE.md)): fetches real programs off the wiki and diffs Raku++ against Rakudo on each. Written to run under either engine. | `raku tools/rc-compare.raku` (or rakupp) |

Beyond the repo, the same principle drives validation against real
applications: the [covid.observer](https://github.com/ash/covid.observer) site
builder and the [raku-course](https://github.com/ash/raku-course) static-site
generator (with the real zef-installed `YAMLish` module) are run unmodified
under rakupp — each surfaced gaps that Roast never would have.

## Why bother

**It finds real bugs.** A test suite exercises features one at a time; a real
program exercises them together, at volume, in unplanned combinations. Bugs
found *by these tools themselves* while building Raku++:

- `.kv.reverse.hash` mangled pair re-keying — every element became a key with
  an `Any` value instead of consecutive elements pairing up. Found the day
  `gen-unicode.raku` used the idiom to build a name→index map; fixed, and the
  generator's idiomatic line now regenerates byte-identical tables.
- `my @a` re-evaluated in a loop *condition* wrongly re-initialized the array
  (`until $iter.push-exactly(my @a, 3) =:= IterationEnd` never accumulated) —
  found via Roast's own `Test::Iterator` helper, which the harness runs.
- The Roast harness needed multiple TAP plans per file handled
  first-plan-wins, `run(:timeout)` with subprocess capture, and `@*ARGS` —
  each implemented because the harness required it.
- The RosettaCode comparator needed heredocs (`q:to/…/`), quote-aware regex
  lexing, and `run` with closed stdin before it could even fetch and time its
  first task.

**It proves the claims.** "Raku++ runs real Raku" is easy to say; a
1,400-line harness, a UCD parser chewing a 40k-line data file, and a benchmark
runner all executing under the interpreter every day is harder to fake.

**It keeps speed visible.** The generator takes ~3 minutes under the
tree-walker — a standing, concrete reminder of where interpreter throughput
is spent (and a ready-made profiling workload).

## What is *not* dogfooded (yet)

- The compiler itself is C++ — Raku++ does not compile Raku++. The generated
  Unicode tables are the only part of `src/` produced by Raku.
- The other Unicode generators (`tools/gen_unicode_gb.py`, `_norm`, `_coll`,
  `_props`, `_scripts`, `_blocks`, `_bidi`) are still Python — straightforward
  candidates for porting the same way `gen_unicode.py` was replaced by
  `gen-unicode.raku`.
- CI/packaging scripts (Homebrew tap) are shell.
