# Replacing the method-dispatch if/else chain — measured, not adopted

*2026-07-29. Nothing here landed. This is the record of an experiment so it does
not have to be run twice.*

(For dispatch in **compiled** `--exe` code, a different subject, see
[DISPATCH.md](DISPATCH.md). This is about the interpreter's `methodCallInner`.)

## The question

`methodCallInner` dispatches a method by walking a chain of several hundred
`if (m == "…")` arms. The obvious objection is that this is O(n) in the number of
methods, and that an `std::unordered_map<std::string, handler>` — or a `switch` —
would make it O(1). The proposal was to index on the name first and move the
secondary checks inside the handlers.

Three things were measured: whether the chain **can** be indexed by name at all,
what each mechanism **costs**, and what either does to the **benchmarks**.

## 1. The chain is not shaped like a name lookup

Every top-level arm across the four dispatch files, classified:

| arm shape | count | can it be name-indexed? |
|---|---:|---|
| `if (m == "x")` — name only | 99 | yes |
| `if (m == "x" && …)` — name plus a guard | 55 | only with fall-through on guard failure |
| `if (inv.t == … )` — **invocant type first** | **198** | **no — there is no name to index on** |
| total | 352 | |

**56% of the arms dispatch on the invocant type, not the method name**, and they
are interleaved throughout the chain rather than gathered at one end. An arm like

```cpp
if (inv.t == VT::Hash && inv.hashKind == "Supply") { … }
```

legitimately handles *every* method name for that invocant. A name→handler map
placed in front of the chain would therefore steal calls that this arm must see,
so it is not a semantics-preserving substitution.

Only **6 arms** anywhere form a contiguous run of name-only tests — not enough to
convert in place either.

The same fact rules out the `goto`-into-the-chain trick (jump to the matching
arm, then continue linearly): jumping past a type-guard arm skips an arm that
should have run.

## 2. A map lookup costs about 19 comparisons

`MName` (see [`src/MethodName.h`](../../src/MethodName.h)) caches the name's
length and first eight bytes, so a comparison against a literal is an integer
compare against a compile-time constant — most arms reject on length alone.

Measured directly, three runs on an idle machine:

```
17 MName comparisons : 4.7–5.0 ns/call  (0.28–0.29 ns each)
1 unordered_map find : 5.3–5.5 ns/call
-> a map lookup costs the same as 18–20 comparisons
```

So the map is a **regression** for any method reachable within ~19 arms, which
includes the common ones — `.Str`, `.Int`, `.elems`, `.chars` all sit near the
front. Every call would pay the hash; the chain mostly pays one integer compare
per arm and stops.

This is worth stating plainly because the number that motivates this idea is
still in [OPTIMIZATION.md](../OPTIMIZATION.md): *"name comparison 8.5%"*. That
profile **predates `MName`**. That optimisation is what captured this win, which
is why there is little left for a map to take.

## 3. Both mechanisms are invisible in the benchmarks

Each variant was built and benchmarked back to back on a settled machine. The
probes run on every dispatch but can never fire, so behaviour is unchanged and
the timing is the mechanism's cost alone: a 682-entry `unordered_map::find`, and
a `switch` on `m.pre` (the first eight bytes, which `MName` already computes, so
obtaining the key is free — 658 distinct case labels).

| kernel | base | hash | switch | hash Δ | switch Δ |
|---|---:|---:|---:|---:|---:|
| loopsum | 198.7 | 197.0 | 201.0 | −0.9% | +1.2% |
| fib | 830.1 | 834.2 | 836.2 | +0.5% | +0.7% |
| strcat | 12.7 | 12.7 | 12.6 | ±0.0% | −0.8% |
| arrayops | 118.7 | 118.9 | 117.4 | +0.2% | −1.1% |
| sortnums | 70.8 | 73.2 | 70.6 | +3.4% | −0.3% |
| regex | 83.8 | 85.8 | 86.2 | +2.4% | +2.9% |
| hash | 38.3 | 38.2 | 38.1 | −0.3% | −0.5% |
| bigint | 32.9 | 32.7 | 33.1 | −0.6% | +0.6% |
| streq | 513.6 | 507.1 | 515.7 | −1.3% | +0.4% |

Every delta is inside the noise floor — the Rakudo column, measured in the same
three runs, drifted ±1–3% by itself. Both mechanisms are free here, which cuts
both ways: whatever they would save is equally invisible. **Method dispatch is
not where these workloads spend their time.**

## Conclusion

Not adopted. The map cannot replace the chain without changing semantics, it is
slower than the chain for the common case, and at benchmark level the whole
question is below the noise floor.

## If someone picks this up again

The direction with an actual prize is **not** map-versus-switch. It is
**dispatching on the invocant type first, then the name within that type** —
because the 198 type-guard arms are what give the chain its length, and they are
exactly what a name index cannot touch. That is an architectural change, not a
mechanical one, and it would need the same three measurements before landing.

## A methodology note, learned the hard way

The first pass at these numbers was wrong twice, and both mistakes are easy to
repeat:

1. **A generated probe name collided with a real method.** 300 dummy arms named
   by an alphabet walk happened to include `"flat"` — the very method under
   test — so it was shadowed and returned early. The tell was an *impossible*
   result (adding arms made it faster) and wrong program output. Sanity-check
   the probe's output, not just its timing.

2. **Six stress-test spinners from an unrelated investigation were still pinned
   at 100% CPU**, because `kill %1 %2 …` had not reached them (they were
   subshells). Load average 22. The tell was the **Rakudo column doubling** —
   the one column no local change can affect. Check the yardstick before
   believing the table; if it moved, nothing else in that table means anything.

Reproduce with `tools/run-bench.raku` for the tables and, for the per-comparison
numbers, a standalone C++ harness — the probes themselves were throwaway
injections into `methodCallInner` and are not in the tree.
