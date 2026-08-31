# Independent replication: the Raku++ release benchmark sweep

Measures every released Raku++ build (28 tags, v0.5.1 → v3.23.0) against the
Rakudo that was current on that release's date, and writes one TSV.

Nothing is installed system-wide. Everything lives under `--workdir`
(default `./rakupp-sweep`). Delete that directory to undo the whole thing.

## Linux / macOS

```sh
./rakupp-bench-sweep.sh
```

Builds Rakudo 2026.06, 2026.07 and 2026.08 from source (~20 min each, once) so
each release is compared against its contemporary. To skip that and use one
Rakudo for every tag:

```sh
./rakupp-bench-sweep.sh --rakudo-mode=single --rakudo=$(command -v raku)
```

Useful flags: `--kernels=all` (all 16 kernels instead of the 3 charted ones),
`--tags=v3.22.0,v3.23.0` (a subset), `--passes=N` (interleaved passes for the
per-era Rakudo measurement, default 4), `--workdir=PATH`, `--jobs=N`.

**Requires:** `curl`, `tar`, `git`, a C++ compiler (`c++`/`g++`/`clang++` — the
`native` lane compiles each kernel with `rakupp --exe`), plus `perl` and `make`
if building Rakudo.

**Linux is x86_64-only** — there is no aarch64 Raku++ release asset, and the
script refuses to run on ARM rather than measure an emulated binary.

## Windows

```powershell
.\rakupp-bench-sweep.ps1 -Rakudo 'C:\rakudo\bin\raku.exe'
```

This one does **not** build Rakudo — a from-source build there needs an MSVC or
MinGW toolchain configured by hand, which a measurement script has no business
attempting. Install Rakudo from <https://rakudo.org/downloads> or via
`rakubrew`, then point the script at it. For era-matching:

```powershell
.\rakupp-bench-sweep.ps1 -RakudoEra @{
    '2026.06' = 'C:\rakudo-2026.06\bin\raku.exe'
    '2026.07' = 'C:\rakudo-2026.07\bin\raku.exe'
    '2026.08' = 'C:\rakudo-2026.08\bin\raku.exe' }
```

Add `-Mingw` to use the MinGW builds instead of the MSVC ones.

## Send back

- `<workdir>/series.tsv` — one row per (tag, kernel), min and median ms for the
  `interp`, `native` and `rakudo` lanes. **Its `rakudo` column is the
  correctness oracle, not a chartable reference** — see below.
- `<workdir>/rakudo-eras.tsv` — the Rakudo reference: one value per Rakudo
  release per kernel, plus a `converge_pct` column.
- `<workdir>/environment.txt` — CPU, compiler, Rakudo versions, and the
  repeatability probe

## Why Rakudo is measured once per RAKUDO release

Rakudo ships monthly. It cannot change between two Raku++ tags cut on the same
day, so any difference in a per-tag Rakudo column is run-to-run noise. On the
reference machine that noise reached **19% on `sortby`** (whose Rakudo lane is
bimodal there, ~168 ms or ~198 ms with nothing between) and 2–10% elsewhere,
and it drew on the dashboard as if Rakudo had got faster and slower between
releases cut hours apart.

So the script measures each Rakudo **once**, keyed by date: 20 timed runs per
kernel after a discarded warm-up, every era interleaved inside each round,
`--passes=N` such passes (default 4), and the minimum across all of them.

`converge_pct` compares the first half of the passes against the second.
**Above ~2% means the floor has not converged** — raise `--passes` or quiet the
machine. Two passes is not enough *and is not a check either*: run as sequential
blocks they track the machine's drift rather than the engine, and on the
reference machine they disagreed by up to 10% (`objects` consistently ~10%
faster in the second pass, `fib` ~7% slower, in all three eras) while the
four-pass minimum settled to within 1.8%.

One thing that survives even a bad sitting: the **era-to-era ratios** reproduce
to 1–2% because interleaving protects the comparison. It is the absolute floor
that drifts. If your numbers differ from the reference machine's in absolute
terms but agree on the ratios between Rakudo releases, that is the expected
result, not a discrepancy.

## Things that will bite, and why they are handled

- **Kernels are pinned** to one repo ref (`--repo-ref`, default `v3.23.0`) and
  used for *every* tag. Letting each tag bring its own kernels would confound
  engine changes with benchmark changes.
- **Version numbers are not chronological.** v3.14.0 shipped 2026-08-11,
  v3.5.0 on 2026-08-20. The tag table is in date order; do not re-sort it by
  version.
- **The repeatability probe** runs nine consecutive kernels and reports
  `max/min`. The reference machine reads ~1.03 when quiet. If yours reads much
  above that, the box was busy and the numbers are correspondingly soft — send
  it anyway, it is recorded so the comparison can account for it.
- **MoarVM's JIT is x86_64-only.** On x86_64 (any Linux/Windows box) the rakudo
  column has the JIT; on arm64 macOS MoarVM compiles `src/jit/stub.o` and has
  no JIT backend at all. So a Linux `rakudo` column is *not* directly
  comparable with an arm64 macOS one — that difference is the interesting part,
  not noise.
- **Two releases report the wrong version.** `v3.20.0`'s binary says `3.7.0`
  and `v3.0.0`'s says `2.0.0`. They are genuine distinct builds (different
  hashes and sizes); only the embedded version string was never bumped. Rows
  are keyed by tag, never by `--version`.
- **The harness leaks its `--exe` output** into `/tmp` and never unlinks it;
  both scripts clean up after each tag.
- **A non-zero exit from the harness** means an engine disagreed on output. It
  is not fatal here — the affected row carries the reason in its `flags`
  column, which is exactly what an old release failing on a newer kernel should
  look like.
