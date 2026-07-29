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
