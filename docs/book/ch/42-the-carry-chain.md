# The Carry Chain

Chapter 41 closed with a prediction: the shelf will still be there when the
profiles point back. They pointed back ten days later, and this chapter is
what happened — the campaign's method run once more, end to end, against a
different competitor and a different kind of cost. Chapter 40's batches were
about copying and lookup; this episode's first half is about *latency*, a
cost no instruction count names. Its second half is the copies again, because
it is always the copies. And its centrepiece decision is one Chapter 41
prepared: the competitor's fastest design, measured, priced, and declined.

## The trigger

On 2026-08-31 mutsu — a Raku implementation in Rust, with a bytecode VM and a
Cranelift JIT — was measured against Raku++ across all fifteen benchmark
kernels. The interpreter won eleven, drew two and lost two. `bigint` was the
clear loss, and it came with the one detail that made it actionable:
`bigint` was the only kernel mutsu led *even against `--exe`*. Compiling the
program bought nothing, so the time was not in the loop around the multiply —
dispatch, scopes, assignment, everything Chapter 40 worked on — it was inside
the runtime's own multiply routine, which the interpreter and the native
binary share.

Chapter 40's trigger was a number without a place. This one named the row and
not the reason, which is nearly as blank: *slower at big integers* covers the
representation, the algorithm, the loop, and everything either side of it.

The kernel is a running product:

```raku
my $f = 1;
$f *= $_ for 1 .. 5000;
say $f.chars;
```

`5000!` is 16,326 decimal digits. Chapter 11 described the representation
that has to carry it: a magnitude of **limbs** base 10⁹ — nine decimal digits
each, about 1,814 of them by the end — and multiplying by a small number
walks the limbs, multiplying each and carrying, exactly like long
multiplication on paper.

There was even a prediction on file. The FAQ had named `bigint` as the row a
dependency-taking implementation should win before anyone measured it: mutsu
links `num-bigint`, a mature Rust library, and Raku++ hand-rolls its bignums
because it takes no dependencies. The prediction was right about the row and
wrong about the reason — the library's advantage is real (its base, below),
but the measured gap was mostly ours to close without touching the
representation at all.

## First pass: one limb at a time

The general multiply had one schoolbook loop for every shape of operand. For
the running-product shape — thousands of limbs times one small limb — that
loop routed every limb's carry through the result array, a store the next
iteration had to load back, and needed a second inner pass to place the
carry. A dedicated one-limb path keeps the carry in a register, and splits
the limb product *before* folding the carry in, so the split — a
multiply-high and a shift, five-odd cycles — runs ahead of the carry chain
instead of on it.

That took `bigint` from 45.2 ms to 11.4 compiled and 60.4 to 12.6
interpreted: level with mutsu. Level is not ahead, and the second pass is
where the episode earned its chapter.

## Second pass: the loop could not get out of its own way

Each limb's carry feeds the next one. However wide the processor is, a
single-chain loop retires one limb per round trip through that dependency —
and measured, the loop cost about five cycles per limb for work whose
instruction count says one and a half. Nothing was being computed slowly;
almost nothing was being computed at all. The chip was waiting for its own
previous answer.

The fix is to stop having one chain. The magnitude is cut into **eight
contiguous segments**, each multiplied with its own carry starting from
zero — a lie, but a cheap one. The chains are independent, so they issue in
parallel and the loop becomes throughput-bound. Afterwards the lie is paid
for: segment *j*'s carry-out belongs at the first limb of segment *j+1*,
which was computed as if nothing came in from below. Paying it back is one
add with a ripple that stops at the first limb that does not overflow — it
cascades only when the digits happen to be all nines — and the payments
commute, so their order does not matter. Seven small additions at the end
buy eight times as much work in flight.

Then something counterintuitive fell out: **the clever code became the slow
code.** The split-first step above exists for exactly one purpose — keeping
the division off the carry chain — and pays twelve machine instructions per
limb to do it. With eight chains there is no waiting left to protect, so the
arrangement is pure overhead, and the naive form, which folds the carry in
first and pays one division, is six instructions — load, `umaddl`, `umulh`,
shift, `msub`, store:

```cpp
static inline void mulStep(uint32_t* d, const uint32_t* s, std::size_t i,
                           uint32_t m, uint32_t& carry) {
    uint64_t p = (uint64_t)s[i] * m + carry;
    uint64_t q = p / BigInt::BASE;            // a multiply-high and a shift
    d[i] = (uint32_t)(p - q * BigInt::BASE);  // one fused multiply-subtract
    carry = (uint32_t)q;
}
```

All three candidate step forms were run in one harness on the factorial
kernel at eight chains:

| step form | 5000-step run |
|---|---:|
| fold the carry in first (**shipped**) | **2.46 ms** |
| reciprocal-of-multiplier | 2.80 ms |
| split the product first (the first-pass form) | 3.48 ms |

The reciprocal form — two multiply-highs in place of the divide — spends the
same three multiply-class operations, which is the real floor here, plus four
more scalar ones. And eight is measured, not chosen: four and six chains land
within 3%, two is 1.5× worse, sixteen gives the fold-back more segments than
the loop saves. Below 32 limbs a single chain wins outright, and that is what
runs.

An optimisation that was correct becomes overhead when the constraint it
served disappears — Chapter 40's batches never hit that shape, because
removing a cost there never changed what the remaining code was *for*.
Removing the latency did.

## And then the copies, again

Every one of the 5,000 steps was doing this around its one multiply:

1. copy the entire running total into the multiply routine — `toBig()`
   returns by value;
2. build a whole new magnitude for the answer;
3. copy that answer back into the variable, through
   `make_shared<BigInt>(const BigInt&)`.

Three full passes over thousands of limbs to move data around, wrapped
around one pass of actually multiplying — and no line of source asks for any
of them. Two go away with a reference (`toBigRef`) and a move overload
(`Value::bigint(BigInt&&)`). The third needs the destination and the left
operand named *once* instead of twice, which is what `applyArithInto` is:
the compound-assign form of `applyArith`, emitted by the code generator in
place of `lhs = applyArith(op, lhs, rhs)` and called by the interpreter's
`op=` paths. When the box provably owns its magnitude alone, the multiply
writes over it and allocates nothing.

*Provably* is two ownership counts, not one, and each catches a shape the
other misses. Chapter 8's cold block is copy-on-write, so a plain copy of a
`Value` shares the block and leaves the *magnitude's* use count at one while
two Values plainly reach it. And a `Range` built over a big endpoint splices
the same magnitude into a *different* cold block, so the block can be
unshared while the magnitude is not. Both counts must be one. The rest of
the guard is the fields a fresh `Value::bigint(...)` would have reset and an
in-place write would instead preserve — an enum identity, a native width, a
readonly binding, an itemized tag. And it refuses outright while worker
threads are live: growing a magnitude *reallocates*, so a racing reader that
has already loaded the limb pointer would read freed memory rather than a
stale-but-valid limb.

Counted with a `malloc` shim over the whole benchmark, against a control
that runs the same loop without the bignum:

| | allocations | bytes copied |
|---|---:|---:|
| before | 32,163 | 34,019,701 |
| after | **2,297** | **131,852** |

Seventy-four allocations for 4,966 bignum steps — one vector growing
geometrically — and 33.9 MB of copying gone.

## The road not taken

The obvious fix was on the table the whole time: do what the competitor
does. `num-bigint` stores raw binary limbs, base 2⁶⁴; measured against base
10⁹ that is about 3.4× on this multiply and about 10× on the general
big-by-big product.

It was measured and declined, and the reason is Chapter 11's, now with
prices attached. Base 10⁹ is what makes printing a big integer **linear**;
every power-of-two base makes it quadratic. A 16,326-digit number converts
to decimal in 0.087 ms as stored, against 0.73 ms from base 2⁶⁴ — and the
0.73 is *with* a divide-and-conquer conversion written specially for the
comparison. At 261,211 digits it is 1.3 ms against 129. This is a language, so `say $n` has to stay fast for
real programs, not just for a benchmark that prints once. A base change
would also silently move the boundary in `Value.cpp`'s `dblExact`, whose
one-limb test currently means "below 10⁹, hence exact in a double" and would
come to mean "below 2⁶⁴" — a change to Rat→Num rounding that no test named.
Base 10¹⁸ was measured too and is the worst of the three: the same total
rewrite for 1.16×, because dividing a 128-bit product by 10¹⁸ exactly costs
three multiplies where base 2⁶⁴ costs none.

This is Chapter 41's discipline pointed at a tenth engine: read the design,
ground the comparison in measured local numbers, and say what does not
transfer and why. mutsu's base is the right choice for a library and was
priced as the wrong one here — so the speedup had to come from inside the
existing representation, and it did.

## Where it landed

Measured through `tools/run-bench.raku` against a purpose-built binary of
the commit before, one interleaved sitting at load average 2.3 — on the M1
development machine, so the milliseconds are not comparable with Chapter
40's tables, only the ratios are:

| lane | before | after | mutsu |
|---|---:|---:|---:|
| interp | 13.0 ms | **7.4 ms** | 11.2 ms |
| `--exe` | 11.1 ms | **6.2 ms** | 11.2 ms |

Five control kernels — `loopsum`, `fib`, `hash`, `streq`, `rats`, both lanes
each — all landed within ±2%, which is that box's run-to-run spread. Roast
gated the change as Chapter 39 requires; the five files that differed from
the baseline run behaved identically on a binary built from the prior
commit, so all five were the machine's evening load, not the change.

And the episode left coverage behind, because there was none.
`t/regression/bigint-multiply-kernel.raku` sweeps magnitude lengths 28–257
limbs across the eight-segment boundaries, cross-checks the one-limb kernel
against the general schoolbook one, and probes the shared-magnitude hazard
six ways. `tools/optbench/bigmul.raku` exists because nothing else in that
directory leaves `int64` — so the compound-assign lane the code generator
emits for `*=` had no four-lane agreement check at all until this work added
one. The honest leftover is stated with them: the performance gate still has
no bignum kernel, so it can gate this work's correctness but not its speed.

## What the two episodes share

The full pairing lives in the repository's findings shelf, in
`docs/dev/findings/HASHFILL-AND-BIGINT.md`; the short form belongs here,
because it is the campaign's closing evidence.

Both reports named a number, not a cause, and both times the useful first
move was to build or run the smallest program that could carry the claim.
Both gaps were dominated by cost that appears nowhere in the source as
work — most of `hashfill`'s in copying, `bigint`'s split between copying and
a dependency chain the instruction count cannot see. Both fixes are constant
factors: `hashfill` is still a hash fill and a sweep, `bigint` is still
schoolbook long multiplication. Both times the competitor's own answer was
measured and turned out to be the wrong answer here — perl's hash design was
worth adopting but was one fix of four; mutsu's limb base would have bought
the multiply by selling the printing. And both kernels stayed: the point of
each episode was that nothing in the harness could see the workload until
someone outside pointed at it, and the permanent fix for that is a kernel,
not a recollection.

What remains open is the same list Chapter 40 ended on, one item longer.
The eight-chain technique is applied in exactly one place — a magnitude
times a single limb; addition and subtraction have the same strictly serial
chain and would take the same treatment, and nothing measured has needed it
yet. The general big-by-big product is untouched, and there the interesting
prize is a better algorithm, not more chains. And `bigint` is now dominated
by process startup — at ~3.9 ms of a 7.4 ms run it is the largest single
line item left, and it has nothing to do with bignums at all.
