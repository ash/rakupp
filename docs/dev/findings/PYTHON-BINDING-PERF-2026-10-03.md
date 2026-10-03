# Python binding: grammar benchmark at v5.2.0, and a slower per-leaf path

Measured 2026-10-03 on the machine of record (Apple M3, macOS), with the
library that `pip install rakulang` installs. The numbers lived in
[bindings/python/README.md](../../../bindings/python/README.md) until this
date; they moved here so that the README is a user guide.

**Finding:** parsing and eager tree conversion from Python cost what they cost
in `rakupp` itself, or close to it. Lazy, field-by-field access to a `Match`
got slower between v4.0.1 and v5.2.0, by a quarter to a half depending on the
run. The engine-side work
of that walk did not get slower, so the extra time is spent crossing the C
interface. The cause is not known yet; the open item is in
[TODO.md](../plans/TODO.md).

## The benchmark

`bindings/python/bench.py` parses a generated 2000-line (168 KB) access log
with the seven-token grammar in `tools/grammar/log.raku`, and times three
phases, each the best of three:

- **parse:** `Grammar.parse` of the whole corpus, one crossing.
- **tree:** eager conversion of the whole `Match` to Python data, one crossing.
- **selective:** two fields of every line, read lazily
  (`m["line"][i]["ip"].str()`), so 4000 crossings.

Each phase is reported three ways: `rakupp` running the same work directly,
the same work through the grammar shim but timed inside the engine, and the
work driven from Python.

```bash
python3 bindings/python/bench.py <dir> 2000
```

`<dir>` must hold a `rakupp` binary and `librakupp.dylib` (`.so` on Linux).
For the numbers below it held the v5.2.0 release's `rakupp` (from
`rakupp-macos-universal.tar.gz`) and the library from the
`rakulang-5.2.0-py3-none-macosx_11_0_universal2.whl` wheel, which is
byte-identical to the release archive's `librakupp.5.2.0.dylib`. Python was
CPython 3.13.2, arm64.

## v5.2.0

Median of five runs:

| phase | rakupp direct | via shim, engine-side | Python host | host/direct |
|---|---:|---:|---:|---:|
| parse | 9.4 ms | 8.5 ms | 8.4 ms | **0.9×** |
| tree (eager) | 55.0 ms | 55.6 ms | 82.9 ms | **1.5×** |
| selective (2 fields × 2000 lines) | 1.7 ms | 16.1 ms | 65.8 ms | **~39×** |

Parse is engine-bound: the host boundary adds nothing. Eager conversion adds
half again on top of the engine's own `tree`. Selective access costs about
16 µs per leaf, of which about 4 µs is the engine's walk and the rest is the
C interface and ctypes.

Selective access exists because converting everything nobody asked for is
usually the worse deal. If a profile shows the per-leaf cost dominating a real
workload, that is GRAMMAR-PLAN G4's cue (a native Match walker), not a reason
to grow the Python layer.

## The earlier figures (2026-08-11)

The same benchmark at the G0 gate, one run of best-of-three, from the
`build-shared/` directory of that day:

| phase | rakupp direct | via shim, engine-side | Python host | host/direct |
|---|---:|---:|---:|---:|
| parse | 11.6 ms | 10.7 ms | 10.5 ms | **1.0×** |
| tree (eager) | 68.0 ms | 68.2 ms | 93.0 ms | **1.4×** |
| selective (2 fields × 2000 lines) | 1.7 ms | 25.9 ms | 52.6 ms | **~31×** |

Parse and tree got faster since then, both in the engine and from Python. The
selective column from Python went the other way.

## Ruling out noise

`build-shared/` still holds `librakupp.4.0.1.dylib` (2026-09-17; it reports
`4.0.1`) beside a `rakupp` binary from 2026-08-13 that reports 3.14.0. The
benchmark ran against that directory and against the v5.2.0 directory,
interleaved, two rounds each, with the same checkout of the Python package
(unchanged since the v5.2.0 tag) driving both libraries:

| run | engine-side selective | Python selective |
|---|---:|---:|
| OLD, round 1 | 25.5 ms | 54.6 ms |
| NEW, round 1 | 18.0 ms | 74.4 ms |
| OLD, round 2 | 25.9 ms | 51.1 ms |
| NEW, round 2 | 16.0 ms | 79.1 ms |

The Python column compares like with like: one Python package, one machine,
library 4.0.1 against library 5.2.0. It is consistently slower on 5.2.0, here
by 35–55%, and by about 25% at the five-run median.

The engine-side column does not compare like with like. In the OLD runs it
comes from the 3.14.0 binary, not the 4.0.1 library. So the per-leaf split
(about 7 µs of host cost per leaf on OLD against about 12 µs on NEW) is an
estimate. What it shows is that the engine's own walk did not cause the
slowdown.

The machine was not idle (desktop applications, load average about 4 on eight
cores), but the benchmark is single-threaded, every phase takes the best of
three, and OLD and NEW were interleaved.

## Where to look

Every selective leaf is one `rk_call` into the shim's walk sub, plus rooting
and releasing the result and converting a `Str` back to Python. Something on
that path costs more in 5.2.0 than in 4.0.1:

- the embed API's per-call work in `src/` (`rk_call`, value rooting and
  release, string conversion, any locking added for threads since 4.0.1);
- `_call_raw` and `_root` in `bindings/python/rakulang/__init__.py`.

The first step is a profile of a tight loop of `rk_call`, from Python and from
C, on both libraries. A bisect between v4.0.1 and v5.2.0 then needs
shared-library builds (`-DRAKUPP_BUILD_SHARED=ON`) in a separate build
directory.
