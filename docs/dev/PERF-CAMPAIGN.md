# Interpreter performance — where the time goes, and what has been tried

A running log. Each entry says what was measured, what was changed, and what it
was worth — including the attempts that turned out to be worth nothing, so they
do not get retried.

Companions: [OPTIMIZATION.md](../OPTIMIZATION.md) is about what `-O` does to
*compiled* code; [DISPATCH.md](DISPATCH.md) is dispatch in `--exe` output;
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
matches the split already in [OPTIMIZATION.md](../OPTIMIZATION.md): **heap
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
