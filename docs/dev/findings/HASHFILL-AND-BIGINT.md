# Two benchmarks answered: `hashfill` against perl, `bigint` against mutsu

Ten days apart, two outside comparisons said Raku++ was slower than something
else at something. Neither said *where*. Both turned out to be the same shape of
problem, and both were fixed the same way — which is the reason to write them up
together rather than separately.

This is a companion piece. The mechanism lives in
[internals/OPTIMIZATION.md](../../internals/OPTIMIZATION.md), the measurements
in [status/BENCHMARKS.md](../../status/BENCHMARKS.md), and the fuller narrative
of the first episode in
[book chapter 40](../../book/ch/40-the-speed-campaign.md). What is here is the
pair, and what they have in common.

---

## The shape both had

1. **An outside comparison gives a number, not a place.** "Twice slower than
   Perl 5." One red row in a table against another implementation. Neither is
   actionable, and neither is wrong.
2. **Build the smallest program that can carry the claim**, and keep it. Not to
   win it — to have something to measure.
3. **Measure where the time actually is**, rather than fixing the thing the
   comparison appeared to be about.
4. **Both times a large share of the cost was not in the operation at all.** It
   was in copying data into and out of the operation — most of the gap for
   `hashfill`, a bit under half of it for `bigint`. These are copies no line of
   source asks for, and that a profile names only as time under `memmove`,
   `memset` and allocator churn.

That last point is the finding, and it is why these two sit in one document.

---

## Episode one: `hashfill` against perl (2026-08-21)

### The trigger

perlancar — a prolific CPAN author, and the author of Bencher — starred the
repo and remarked publicly that Raku++ had made him interested in Raku for
the first time in about eighteen years: "milliseconds startup time, only
twice slower than Perl 5 (native compiled)".

That is praise, and it is also a bug report with the location field left blank.
No program was named. Twice slower *at what*?

### The response

Rather than guess, a program was built to carry the claim — a **twin pair**,
[`tools/bench/hashfill.raku`](../../../tools/bench/hashfill.raku) and
[`hashfill.pl`](../../../tools/bench/hashfill.pl), the same work line for line,
with byte-identical output:

```raku
my %h;
for 1 .. 200_000 -> $i { %h{"key$i"} = $i * 2; }
my $sum = 0;
for %h.values -> $v { $sum += $v }
my $s = '';
for 1 .. 50_000 { $s ~= 'x' }
say $sum, ' ', $s.chars;
```

It is deliberately made of what a scripting language is asked to do all day:
interpolated hash keys, a values sweep, a string built up by appending. And
`perl` won it in every mode we had — the native binary at 113 ms of wall clock
against perl's 82, the interpreter 3.3× behind.

The reproduction was close enough to trust: timing the programs directly, the
native binary spent **0.15 s of CPU against perl's 0.07** — the reported factor
of two, arrived at independently.

### What was actually wrong

Three of the four fixes were not about hashing at all. They were about work
being done *around* the work:

1. **`%h.values` snapshotted the whole hash.** Reading the values built a
   complete list of `Pair`s — 200,000 of them — and then threw it away, once
   per sweep. Same for `.keys`, `.kv`, `.pairs` and `.antipairs`.
2. **Assigning a freshly built list into an `@`-array copied it element by
   element**, when the list was a temporary that nobody else could see and whose
   buffer could simply be taken.
3. **Compiled string interpolation built a `Value` per literal part, per
   evaluation.** `"key$i"` has a constant `"key"` in it; that constant was being
   constructed 200,000 times.
4. And then the hash itself: the payload's `std::map<std::string, Value>` was
   replaced by `ValueHash` — a compact, insertion-ordered open hash that stores
   each key's hash alongside it, in the perl mould. That came out of reading the
   Perl 5 sources rather than guessing at them; see
   [engines/PERL5-TECHNIQUES.md](engines/PERL5-TECHNIQUES.md).

Only (4) is a hash change. (1)–(3) are the cost of moving data, and they were
most of the gap.

### Where it landed

After the first three fixes, timing the programs directly: **0.06 s of CPU
against perl's 0.08**, and 0.07 s of wall clock against 0.09 — from twice
perl's time to slightly under it. `ValueHash` then put the compiled row clearly
ahead.

Current standing, all engines in one harness run on the benchmark machine
(M3, 2026-08-31):

| engine | `hashfill` | vs perl |
|---|---:|---:|
| Raku++ `--exe` | 38.6 ms | **2.7× faster** |
| Perl 5 | 103.2 ms | — |
| Raku++ interp | 109.5 ms | 1.1× slower |
| Rakudo | 284.1 ms | 2.8× slower |
| mutsu | 392.9 ms | 3.8× slower |

The interpreted row is the one worth watching over time: 1.6× slower than perl
on 2026-08-21, within 10% on 2026-08-24, 6% off it now.

The kernel stayed. It is in the harness permanently, with a `perl` column beside
it, because the whole point was that we had no way to see this workload before
somebody outside told us about it. `textsplit` was later added as a second twin,
and it is the one perl still wins — which is more informative than one twin
agreeing with itself.

---

## Episode two: `bigint` against mutsu (2026-08-31 → 09-01)

### The trigger

[mutsu](https://github.com/tokuhirom/mutsu), a Raku implementation in Rust, was
measured against Raku++ across all fifteen benchmark kernels. The interpreter
won eleven, drew two and lost two. `bigint` was the clear loss — and the only
kernel mutsu led even against `--exe`, which was the interesting part: compiling
the program bought nothing, so the time was inside the runtime's own multiply
rather than in the loop around it.

The kernel is a running product:

```raku
my $f = 1;
$f *= $_ for 1 .. 5000;
say $f.chars;
```

`5000!` is 16,326 decimal digits. Raku++ stores a big integer as a list of
**limbs** — pieces of nine decimal digits each, so about 1,814 of them — and
multiplying by a small number walks the limbs, multiplying each and carrying,
exactly like long multiplication on paper.

The obvious explanation was correct as far as it went: mutsu links
`num-bigint`, a mature Rust library, and we hand-roll ours because we take no
dependencies. The FAQ had *predicted* this loss before it was measured — and the
prediction was right about the row and wrong about the reason.

### First pass: one limb at a time

The general multiply routed every limb's carry through the result array — a
store the next iteration has to load back — and needed a second inner pass to
place it. A dedicated one-limb path that keeps the carry in a register, with the
limb product split off the carry chain, took `bigint` from 45.2 ms to 11.4
compiled and 60.4 to 12.6 interpreted, level with mutsu.

Level is not ahead. The second pass is where it got interesting.

### Second pass: the loop couldn't get out of its own way

Each limb's carry feeds the next one, so however wide the processor is, a
single-chain loop retires one limb per trip through that dependency. Measured:
about five cycles per limb, for work whose instruction count says one and a
half. The chip was mostly idle.

So the magnitude is cut into **eight segments**, each multiplied with its own
carry starting from zero — a lie, but a cheap one. Afterwards, whatever fell off
the top of segment one is added to the bottom of segment two, and so on. That
addition almost always stops immediately; it only cascades if the digits happen
to be all nines. Seven small additions at the end buy eight times as much work
in flight.

Then something counterintuitive fell out. **The clever code became the slow
code.** The existing step was carefully arranged to keep a slow division *off*
the carry chain, at a cost of twelve machine instructions per limb. With eight
chains there is no waiting left to protect, so the arrangement was pure
overhead — and the naive form, which folds the carry in and pays one division,
is six instructions. All three candidates in one harness at eight chains:

| step form | 5000-step run |
|---|---:|
| fold the carry in first (**shipped**) | **2.46 ms** |
| reciprocal-of-multiplier | 2.80 ms |
| split the product first (the old one) | 3.48 ms |

Eight chains is measured too, not chosen: four and six are within 3%, two is
1.5× worse, sixteen gives the fold-back more segments than the loop saves. Below
32 limbs a single chain wins outright and that is what runs.

### And then the copies, again

Every one of the 5,000 steps was:

1. copy the entire running total into the multiply routine (`toBig()` returns by
   value),
2. build a whole new magnitude for the answer,
3. copy that answer back into the variable (`make_shared<BigInt>(const&)`).

Three full passes over thousands of limbs to move data around, wrapped around
one pass of actually multiplying. Two go away with a reference and a move
overload; the third needs the destination and the left operand named *once*
instead of twice, which is what `applyArithInto` is — so when the box provably
owns its magnitude alone, the multiply writes over it and allocates nothing.

*Provably* is two separate ownership checks, and each catches a case the other
misses — a plain copy of the value shares one thing, a `Range` built over the
number as an endpoint shares a different thing. It also declines entirely while
worker threads are live, because growing a magnitude reallocates and a racing
reader would be left pointing at freed memory.

Counted with a `malloc` shim over the whole benchmark, against a control that
runs the same loop without the bignum:

| | allocations | bytes copied |
|---|---:|---:|
| before | 32,163 | 34,019,701 |
| after | **2,297** | **131,852** |

Seventy-four allocations for 4,966 bignum steps — one vector growing
geometrically — and 33.9 MB of copying gone.

### The road not taken

The obvious fix was to do what the competitor does: stop storing decimal digits
and store raw binary limbs. That is about 3.4× faster on this multiply and about
10× on the general big-by-big product.

It was measured and rejected. Base 10⁹ is what makes printing a big integer
**linear**; every power-of-two base makes it quadratic. A 16,326-digit number
converts to decimal in 0.087 ms as we store it, against 0.73 ms from base 2⁶⁴
*even with* a divide-and-conquer conversion written for the comparison — and at
261,211 digits it is 1.3 ms against 129. This is a language, so `say $n` has to
stay fast for real programs, not just for a benchmark. Base 10¹⁸ was measured
too and is the worst of the three: the same total rewrite for 1.16×.

So the speedup had to come from inside the existing representation, and it did.

### Where it landed

Measured through `tools/run-bench.raku` against a purpose-built binary of the
commit before, one interleaved sitting at load average 2.3 (M1, so **not**
comparable with the `hashfill` table above, which is the M3 bench box):

| lane | before | after | mutsu |
|---|---:|---:|---:|
| interp | 13.0 ms | **7.4 ms** | 11.2 ms |
| `--exe` | 11.1 ms | **6.2 ms** | 11.2 ms |

Five control kernels — `loopsum`, `fib`, `hash`, `streq`, `rats`, both lanes
each — all landed within ±2%, which is the run-to-run spread on that box.

---

## What the two have in common

- **The report named a number, not a place, and that was fine.** Both times the
  useful first move was to build or run the smallest program that could carry
  the claim, and then measure it. Neither report was actionable as written;
  neither was wrong.

- **A large share of each gap was copying, not computing** — most of it for
  `hashfill`, a bit under half for `bigint`, whose other half was the carry
  chain. `.values` snapshotting a hash it discards; a list copied element-wise
  when its buffer could be taken; a literal rebuilt 200,000 times; a magnitude
  copied three times per multiply. None of these appears in the source as work,
  and none is named by a symbol anyone would think to suspect.

- **Both fixes are constant factors, not algorithms.** No asymptotic complexity
  changed in either episode. `hashfill` is still a hash fill and a sweep;
  `bigint` is still schoolbook long multiplication.

- **The competitor's answer was the wrong answer both times.** perl's speed on
  `hashfill` came from a hash design worth reading and adopting — but three of
  the four wins were not about hashing. mutsu's speed on `bigint` came from a
  base we measured, priced, and declined, because the same choice that makes
  their multiply fast would make our printing quadratic.

- **The kernel stays afterwards.** `hashfill` and its perl twin are permanent
  fixtures with their own column; `bigint` gained
  `t/regression/bigint-multiply-kernel.raku` and `tools/optbench/bigmul.raku`,
  the latter because nothing else in that directory leaves `int64` — so the
  compound-assign path had no four-lane agreement check at all until this work
  added one.

## What is still open

- **`tools/perf-baseline.raku` has no bignum kernel**, so the performance gate
  still cannot see a `BigInt` regression. `bigmul` closes the *correctness* hole
  in gate 4, not the performance one in gate 3.
- **The eight-chain technique is applied in exactly one place** — a magnitude
  times a single limb. `addMag` and `subMag` have the same strictly serial carry
  chain and would take the same treatment; nothing measured has needed it yet,
  so it has not been done.
- **The general big-by-big product is untouched**, and there the interesting
  prize is a better algorithm (Karatsuba), not more chains.
- **`bigint` is now dominated by process startup**, which at ~3.9 ms of a 7.4 ms
  run is the largest single line item left and has nothing to do with bignums.
- The `hashfill` interpreted row is 6% off perl. Closing that is a different
  kind of work than either episode above.
