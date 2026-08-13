# tools/bench/diagnose/ — finding out *where* the time goes

The `.raku` files one directory up are **kernels**: fixed programs that
[run-bench.raku](../../run-bench.raku) times three ways, and that
[perf-guard.raku](../../perf-guard.raku) gates a release on. They answer *did
this get slower*.

These four answer a different question — *why, and in which line* — and they are
the programs that produced every number in
[STRING-SCAN-QUADRATICS.md](../../../docs/dev/findings/STRING-SCAN-QUADRATICS.md)
§5–§6. They are kept because the investigation they came from will happen again:
the bug they found (per-call O(length) work) is invisible in the shape of the
code and survived a release.

All five run under **rakupp and Rakudo unchanged**, which is deliberate — for
everything except `call-cost.raku` the cross-engine comparison *is* the
measurement. That holds even for `json-native.raku`, because the module it uses
degrades to `JSON::Fast` rather than refusing to load; it just says so.

```bash
L=<battery>/dists/JSON--Fast-0.19/lib
R=./build-arm64/rakupp
```

## The five

| | question it answers |
|---|---|
| [json-gen.raku](json-gen.raku) | builds the corpus — deterministic, so both engines and every run see identical bytes |
| [json-parse.raku](json-parse.raku) | how long does `from-json` take, best-of-N, across a size ladder |
| [json-native.raku](json-native.raku) | the same corpus through **`JSON::Native`**, the native extension module — what the C ABI is worth against the interpreted parse |
| [string-scale.raku](string-scale.raku) | **is a string op's per-call cost proportional to the string's length?** |
| [call-cost.raku](call-cost.raku) | where does the time in a *call* go — the frame, each parameter, `is rw`, the body |

## The usual sequence

```bash
for n in 200 400 800 1600; do $R json-gen.raku --out=d$n.json $n; done
$R   -I$L json-parse.raku --reps=3 d200.json d400.json d800.json d1600.json
raku -I$L json-parse.raku --reps=5 d200.json d400.json d800.json d1600.json
```

and, for the third configuration — the native extension module, which needs
[JSON::Native](../../../docs/guide/EXTENSIONS.md) with its library built:

```bash
M=~/raku-modules/JSON-Native
$R -I$M/lib json-native.raku --reps=7 d200.json d400.json d800.json d1600.json
```

The three sit on one corpus, so they subtract. Measured 2026-08-09, `d800.json`
(278 KB), same machine:

| | ms |
|---|---:|
| Rakudo + `JSON::Fast` | 34.0 |
| rakupp + `JSON::Fast` | ~440 |
| rakupp + `JSON::Native` | **4.5** |

`json-native.raku` **prints the backend it used** on every run, and that is not
decoration. `JSON::Native` falls back to `JSON::Fast` when its library is
missing, on Rakudo, or after a compiler upgrade it has not been rebuilt for — so
a run that quietly measured the fallback would look like a catastrophic
regression, and one on Rakudo would look like a triumph. Read the label before
the number.

**Read the scaling column, not the improvement factor.** ×2 per doubling is
linear and fine. ×4 is the bug. That distinction is the whole reason this
directory exists: fixing four of seven sites of a quadratic leaves a quadratic,
and the ratio between two builds cheerfully reports progress while the shape
stays wrong.

Then, to find out *which* op:

```bash
$R string-scale.raku      # flat = correct, rising = per-call O(length)
$R call-cost.raku         # rakupp deltas only — Rakudo optimises these away
```

and for line-level attribution, a `RelWithDebInfo` build plus `sample`:

```bash
cmake -S . -B build-dbg -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-dbg -j8
```

Sample the running process and aggregate self-time per `file:line`; without the
debug build everything inlines into `Interpreter::eval` and says nothing.

## Two rules, learned the hard way here

- **Never measure while anything else runs.** A Roast run in the background
  moved these numbers by tens of percent and produced one round of conclusions
  that had to be thrown away. `perf-guard` refuses a loaded machine for exactly
  this reason; these scripts do not, so check `uptime` yourself.
- **A/B on the same machine, in the same session.** Absolute times here are
  machine-specific and drift between builds for reasons as dull as linker
  layout. Only the paired difference means anything — build the comparison
  binary from `HEAD` in a worktree rather than trusting a number from earlier
  in the day.

## What they found, so the shape is recognisable

Both bugs were the *same shape*: a cost that is fine once, paid per call.

- `.substr`/`.index` each opened with `inv.toStr()` — a full copy of the
  invocant — and then an uncached scan of it. 5,000 `.substr` calls cost 29 ms
  on a 50 KB string and 135 ms on a 400 KB one, for identical work. Now flat at
  5 ms.
- A signature's fast-path eligibility, its arity bounds, its parameters' native
  widths and type-name resolvability were all recomputed on every call. Caching
  them on the AST took 22% off the parse.
