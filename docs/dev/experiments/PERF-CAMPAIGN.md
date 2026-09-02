# Interpreter performance — where the time goes, and what has been tried

A running log. Each entry says what was measured, what was changed, and what it
was worth — including the attempts that turned out to be worth nothing, so they
do not get retried.

Companions: [OPTIMIZATION.md](../../internals/OPTIMIZATION.md) is about what `-O` does to
*compiled* code; [DISPATCH.md](../../internals/DISPATCH.md) is dispatch in `--exe` output;
[METHOD-DISPATCH-EXPERIMENT.md](METHOD-DISPATCH-EXPERIMENT.md) is the hash-map /
switch experiment that was measured and rejected.

## The profile

Sampling `fib(32)` and a 3M-method-call loop, consistently:

```
sizeof(Value) = 376 bytes   — 5 std::strings (120B) + 11 shared_ptrs (176B) + scalars
sizeof(Env)   = 256 bytes   — 5 associative containers, one make_shared per call
```

`Value::~Value()` and `Value::Value(const Value&)` are the top two rakupp frames
in every profile, and the malloc family is the largest cluster overall. That
matches the split already in [OPTIMIZATION.md](../../internals/OPTIMIZATION.md): **heap
allocation 31%, Value copy 11%** — about 42% of runtime — against the ~8.5% that
name comparison used to be before `MName` took it.

Every `Value` copy is a 376-byte memcpy, up to eleven **atomic** refcount
increments, and five string copies.

## Ranked candidates

1. **Stop redundant by-value copies on the dispatch path** — done, see below.
2. **Shrink `Value`** — the 31%. Most of the eleven pointers and four of the five
   strings are empty for any given value; an `Int` carries all of them.
3. **Lazy `Env` sub-containers** — `rwLinks`/`rwSynced`/`rwDead` are empty for
   almost every call but constructed every time. 256 bytes → ~80.
4. **Slot-indexed locals** — variables are looked up by hashing a `std::string`
   at runtime. Resolving names to slots at parse time turns `Env` into a
   `vector<Value>`. Biggest architectural win, touches scoping/closures/`state`/
   `rw` write-through, wants its own campaign.

## 1. Invocant by const reference (2026-07-29)

`methodCall`/`methodCallInner` took `Value inv` **by value** — 376 bytes and up
to eleven atomic refcount bumps per method call — to serve the arms that rewrite
the invocant. There are **four such arms out of 352**: the package-relative
class-alias rewrite, a Supply drain, a Bridge/Numeric coercion that re-dispatches,
and two mutable cursors that descend into nested containers (multi-dim
`ASSIGN-POS`, and push through a key path).

Each takes its own copy where it needs one. The cursor cases are safe because the
descent reaches the caller's data through `inv`'s `arr`/`hash` shared_ptr, which a
copy **shares** rather than duplicates — `@a[1;0] = 9` and `%h<a><b>.push(1)`
still write through, and both are checked.

**Measured** on a 3M-method-call workload, alternating builds over three rounds
to control for drift:

| build | run 1 | run 2 | run 3 |
|---|---:|---:|---:|
| main (by value) | 2192 ms | 2210 ms | 2210 ms |
| const reference | 2116 ms | 2143 ms | 2131 ms |

**−3.4%**, no overlap between the groups. Invisible on `perf-guard` and
`run-bench` — their kernels are arithmetic and assignment loops that barely
dispatch a method. That is a coverage gap in the guard, not an absence of effect.

Worth having independently of the timing: `inv` is now a const reference through
the whole chain, so an arm that quietly mutates the invocant **no longer
compiles**. Before, those four mutations were indistinguishable from the 348
non-mutations.

### Two things that went wrong

**The first cut ate its own saving.** The copy-on-write shim declared a plain
`Value invCopy;` in the preamble — which default-constructs 376 bytes on *every*
call. Measured exactly neutral until it became a `std::optional`, whose empty
state is a flag.

**perf-guard cried wolf.** It first reported `fib` +27% and `asg` +28%. That was
`ecosystemanalyticsd`, `WindowServer` and `trustd` saturating the machine
mid-run; re-measured quiet it is 833 ms against 828 baseline. A single failing
run is not a regression — check the machine, then re-run, before believing it.

## 2. Shrinking `Value` — attempted, abandoned (2026-07-29)

The free part was tried first: `Value`'s flags (`fatRat`, `rExFrom`/`rExTo`/
`rNum`, `natBits`/`natSigned`/`natFloat`) sat between 8- and 16-byte members and
forced **23 bytes of padding**. Grouping them took `sizeof(Value)` from 376 to
**360** with no behaviour change and no risk.

It measured **2.5% SLOWER**, consistently, over six alternating rounds:

| build | runs |
|---|---|
| 376 bytes (original) | 2115 / 2103 / 2145 / 2152 ms |
| 360 bytes (packed) | 2175 / 2166 / 2188 / 2204 ms |

Hot field offsets were *identical* — `t`/`b`/`i`/`n`/`im`/`s`/`hashKind` did not
move; only `arr` and the pointers after it shifted by 8 bytes, staying in the
same cache line. So this is not locality. The likeliest cause is the copy
constructor: `Value` has a user-defined copy (eleven shared_ptrs interleaved with
scalars), not a memcpy, and reordering changed how that code schedules.

**Reverted.** The useful conclusion is that **struct size is not the lever here**
— the relationship between `sizeof(Value)` and speed is not even monotonic.

The prize is real, though. Profiling the method-heavy probe:

```
Value ctor/dtor/assign : 22.0%   (Value::~Value alone is ~10%)
malloc family          : 21.5%
```

But the planned route to it — moving rare fields behind a `shared_ptr<Extras>` —
would touch ~600 access sites and swap five or six null-pointer copies for one
pointer copy **plus an indirection on every access to those fields**. After the
reordering result, that is not obviously a win, and it is far too large a change
to make on a hunch.

If someone picks this up: the profile points at `Value::~Value()` specifically
(destroying eleven shared_ptrs and five strings, nearly all empty), not at the
byte count. A discriminated union over the pointers that can never be set
together — `arr`/`hash`/`code`/`obj` — would cut the destructor's work without
changing field-access syntax at all. Prototype that narrowly and measure before
committing to anything wider.

## 3. Lazy `Env` extras (2026-07-29)

`sizeof(Env)` was 256 bytes, 192 of which were eight associative containers that
are empty in almost every scope — `rwLinks`, `rwSynced`, `rwDirect`, `rwDead`,
`tempRestores`, `letRestores`, `varDefault`, `varDynamic`. They serve `is rw`
write-through, `temp`/`let` restoration, `is default` and `is dynamic`. An Env is
built for every routine call *and* every block, so that was eight container
constructions and destructions per scope for features most scopes never use.

They moved into an `EnvExtras` allocated on first write: **256 → 72 bytes**.

Reads had to stay allocation-free or the change defeats itself — the `temp`/`let`
restore checks run on every scope exit. Guards test the pointer directly
(`e->ex && !e->ex->letRestores.empty()`); other reads go through `xr()`, which
returns a shared empty instance; only writes call `x()`.

**Measured** on `fib(29)`, which builds an Env per call, alternating builds:

| build | runs |
|---|---|
| before | 850 / 844 / 846 ms |
| after | 831 / 828 / 822 ms |

**−2.4%**, no overlap. Unlike item 2, this one is a straight win — the difference
being that it removes *work* (sixteen container constructor/destructor calls per
scope), not merely bytes.

## 4. Slot-indexed locals — measured, NOT attempted (2026-07-29)

Profiling `fib(32)`, which is as variable-lookup-heavy as these kernels get:

```
hash-table lookup    :  66 samples  (2.8%)
strlen/memcmp/string :  27          (1.2%)
Env / bindParams     :  60          (2.6%)
```

**The ceiling is about 4%** — for the riskiest change on the list, one that
touches scoping, closures, `EVAL`, dynamic `$*vars`, `MY::` introspection,
`state`, and the `rw` write-through machinery (which is keyed by *name*). It was
not attempted. If it is ever revisited, note that the ranked list had it as "the
biggest architectural win", and the measurement says otherwise.

The same profile said where the time really is:

```
malloc family                    : 567 samples  (24.2%)
Value ctor / dtor / move-assign  : 433          (18.5%)
```

So the question became *who allocates*, not *what ought to be slow*. Tallying the
callers of `operator new` in the sample: **`evalCall`, 514 of ~580**. That is one
call site, and it led straight to item 5.

## 5. Move the argument vector into a call (2026-07-29)

`evalCall` built a `ValueList args`, then passed it to `callCallable` — which
takes a `ValueList` **by value** — as an *lvalue*. That copied the vector and
every `Value` in it on every sub call: one allocation plus N × 376-byte copies,
for a local about to die. `callCallable` already moved into `callCallableRaw`, so
the whole copy was at the hand-off.

Four sites, all `return` statements, so the move is safe even where `args`
appears later in the enclosing function. The two loop sites that reuse `args`
across iterations are deliberately left alone.

| build | fib(29) |
|---|---|
| before | 827 / 844 / 835 ms |
| after | 764 / 760 / 755 ms |

**−9.0%**, no overlap — the largest win of the campaign, from four lines.

### What this campaign actually taught

The three changes that worked (−3.4% dispatch, −2.4% call, −9% call) all removed
**work or allocation**. The one that failed removed **bytes** (item 2, a smaller
`Value` that ran 2.5% slower), and the one with the best reputation going in
(item 4, slot locals) turned out to have a 4% ceiling.

Ask the profile who allocates. Do not reason from what ought to be expensive.

## Measuring this kind of change

`perf-guard`'s four kernels (fib/asg/loopsum/hash) are call- and
arithmetic-dominated. They will not show a dispatch or value-copy change at all.
Use a method-heavy probe as well:

```raku
my $s = "abcdef"; my $n = 0;
for ^3_000_000 { my $x = $s.item; $n = $n + 1 }   # ~100% method dispatch
say $n;
```

Alternate the two builds round by round rather than measuring one then the other
— absolute times drift by several percent over minutes. And always sanity-check
the *output*, not just the timing: a probe that accidentally shadows a real
method will happily report a large speedup.

Include a **control kernel** — one the change provably cannot touch — in the
same alternating run. Without it a table of improvements is equally consistent
with the machine having been quieter the second time.

### Measure instructions, not milliseconds, on a machine you cannot quiet

Added 2026-09-02, after a container change read as a 2.4% regression on `fib`
by wall clock and turned out to retire **0.14% more instructions**. The whole
difference was code layout in a translation unit whose object code had moved.

`/usr/bin/time -l` reports **instructions retired** and **cycles elapsed** on
macOS 12+. Instructions retired does not move with machine load, which makes
it the honest metric on a developer box with other work on it — where
`perf-guard --check` refuses to report at all, and where interleaving only
narrows the error rather than removing it. Read the two together:

- **instructions flat, cycles down** — the change did the same work more
  efficiently (bulk `memcpy` instead of a move loop, better locality). Real.
- **instructions down** — the change removed work. Real, and the size of the
  drop is the size of the win.
- **instructions up, wall clock down** (or the reverse) — suspect layout, and
  say so rather than quoting the wall clock.

Two cautions. First, build a **control binary** from the same tree with only
the line under test reverted, and compare against that rather than against
the last release: a rename or a header move changes layout by itself.

Second, and this is the one that bit: **instruction counts are not equally
repeatable across kernels, and a single run of a noisy one will lie to you.**
Measured spreads on one unchanged binary, eight runs each:

| kernel | spread |
|---|---|
| `fib` | 6.4093-6.4119 G, ±0.02% |
| grammar JSON parse | 1.8493-1.8839 G, ±1.9% |
| `streq` | 4.90-5.00 G, ±2% |

A sitting in 2026-09 measured the grammar parse once per build and read an
8.9% improvement from a change that repeats at 0.1%. Two consecutive single
runs had agreed, which felt like confirmation and was not: they had agreed on
a value ~12% above the kernel's own floor. The variance is the parallel
runtime's workers doing variable work, so it lands on kernels that allocate
and thread, not on tight arithmetic loops. **Measure the spread of the kernel
on one binary before trusting any ratio from it, and quote the minimum of at
least five runs.**

## Follow-on: node specialization

The largest win since this campaign came from applying its own lesson (remove
work and allocation, per the profile) to the shapes hot loops are made of:
`$a OP $b`, `$n OP literal`, `@a[$i]`. Up to −18%, with a flat control.
Written up separately in [NODE-SPECIALIZATION.md](../../internals/NODE-SPECIALIZATION.md).
