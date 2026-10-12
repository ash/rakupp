# Plan: freeing objects that refer to each other (issue #151)

*Written 2026-10-12, before any engine change.*

rakupp frees memory by reference counting alone. Two objects that refer to
each other keep each other's count at one after the program has let go of
both, so neither is ever freed. Rakudo's tracing collector frees them. This
plan adds a **backup cycle collector** that keeps reference counting as the
primary mechanism and catches only what it cannot free.

## The number a stranger can re-measure

```raku
# cycle.raku: run as `rakupp cycle.raku cycle` and `rakupp cycle.raku nocycle`
my atomicint $freed = 0;
class N { has $.other is rw; has @.pad; submethod DESTROY { $freed⚛++ } }
sub footprint() {
    (run('footprint', '-p', $*PID, :out, :err).out.slurp(:close)
        ~~ /'Footprint:' \s* (\S+ \s* \w+)/)[0] // '?'
}
my $cycle = @*ARGS[0] eq 'cycle';
for ^8 -> $round {
    for ^20_000 {
        my $a = N.new(pad => (^50).list);
        my $b = N.new(other => $a, pad => (^50).list);
        $a.other = $b if $cycle;
    }
    say "round $round: footprint {footprint()}, freed $freed of {($round + 1) * 40_000}";
}
```

`footprint` is macOS's tool and counts compressed pages. RSS does not, so on
macOS RSS drifts and plateaus while leaked memory still grows. On Linux, read
`VmRSS` from `/proc/$*PID/status`.

Measured 2026-10-12, Darwin 25.5 / arm64, rakupp at c87cceb7, Rakudo 2026.09:

| | objects freed after 8 rounds | footprint, round 0 → 7 |
|---|---:|---|
| rakupp, with the cycle | **0** of 320,000 | 238 MB → 1,882 MB (+~235 MB per round) |
| rakupp, without the cycle | 319,061 of 320,000 | 9.7 MB → 9.8 MB |
| Rakudo, with the cycle | 319,252 of 320,000 | 143 MB → 148 MB |

Without cycles rakupp holds a fifteenth of Rakudo's memory: reference
counting frees each pair at the end of its iteration. The plan must keep
that; a collector that made the common case pay would be a bad trade.

---

## Why scope exit alone cannot decide

`$a` and `$b` *are* released at the end of the loop body. What survives is
the objects, each held by the other's attribute. Freeing an object because
the variable that named it died is wrong whenever the object escaped: pushed
onto an outer array, captured by a closure, stored in another object's
attribute, returned. Proving that nothing escaped means escape analysis
through `new`, `BUILD`, `is rw` accessors and every method call, and in Raku
that analysis would answer "might escape" almost everywhere.

What scope exit *can* supply is the trigger. A garbage cycle forms at the
moment its last outside reference goes away. When a variable drops a
reference and the object's count stays above zero, the object is a
**candidate**: it may have just become unreachable garbage held up only by
references from inside its own group.

## The algorithm: local trial deletion

This is the synchronous form of Bacon and Rajan's cycle collection
("Concurrent Cycle Collection in Reference Counted Systems", ECOOP 2001),
done without touching the live counts:

1. **Collect.** From the candidates, walk the references that container
   bodies hold, up to a node budget, gathering the reachable nodes.
2. **Count internal references.** For each gathered node, count the edges
   that point at it from other gathered nodes.
3. **Find the externally held.** A node whose reference count exceeds its
   internal count is held from outside the group. Mark it live, and
   everything reachable from it within the group.
4. **What is left is garbage.** Every remaining node is referenced only from
   inside the group, so nothing outside can reach it.
5. **Finalize, then recheck.** Run `DESTROY` on every garbage object first,
   while every link is still intact. A `DESTROY` may resurrect an object by
   storing `self` somewhere, so repeat steps 2–4 over the garbage set.
6. **Break.** Clear the garbage containers' contents: an object's
   attributes, an array's elements, a hash's entries. The ordinary reference
   counts then fall to zero and the normal release path frees everything.

In the #151 loop the walk from `$a` sees `$a` and `$b`, each with count 1,
each count explained by the other. Nothing is externally held, so both are
garbage, and memory stays flat.

### The safety invariant

**A node is freed only when every reference its count records comes from a
node in the garbage set.** Each way the walk can be wrong falls on the safe
side, except one:

- A reference the walk cannot see (a C++ temporary, a `std::function`
  capture, a `shared_ptr<void>` handle, another thread's copy) leaves the
  count above the internal count, so the node looks externally held and
  nothing is freed. Missing edges cost collections, never correctness.
- A walk cut short by the budget leaves edges from unvisited nodes
  uncounted, which again only makes nodes look externally held.
- **The one unsafe error is counting an edge that holds no reference.**
  Every edge the walker reports must correspond to exactly one owning handle
  (`PayRef`, `PRef`, `Ref`, `shared_ptr`) that contributes exactly one count.
  The edge enumeration in Phase 1 is therefore the part to review line by
  line, and the poison mode below exists to catch a mistake in it.

## What the engine gives us, and what it complicates

**Payload bodies already carry what the collector needs.** A Value's payload
is a `Body<T>` with an intrusive atomic count, a spare per-body flags word
`pflags_` and a vtable (`src/Ref.h:108`). A virtual `forEachEdge(visitor)` per
`Body<T>` enumerates edges, and two `pflags_` bits give "buffered as a
candidate" and "registered for DESTROY". Payloads that can hold Values:
`ValueList`, `ValueHash`, `ObjectData` (`src/Value.h:2029`: `attrs`, plus the
boxed value), `MatchData`, `Callable`, and `Value` itself (Pair values and
container cells).

**Envs are reached through `std::shared_ptr`.** A closure holds its scope as
`Callable::closure` (`src/Value.h:571`) and its `state` env separately
(`StateSlot`, `src/Value.h:577`). An Env holds Values in `vars` and its pad
and points at `parent` (`src/Interpreter.h:743`). A cycle through a closure
(an object whose attribute is a block that mentions the object) runs object
→ Callable → Env → variable slot → object. Envs must be walk nodes too, with
`shared_ptr::use_count` as their count. That is Phase 3.

**Opaque holders stay opaque.** `ValueContExt::ext` (`src/Value.h:755`,
`shared_ptr<void>`: promises, channels, lazy-sequence generators, a regex's
closed-over scope) and every `std::function` capture hold references the walk
cannot enumerate. They are treated as external, which is safe. Nothing they
hold is collected, and a cycle that passes through them leaks as it does
today.

**Aliased Value bodies.** A `Body<Value>` may alias a Value that lives
elsewhere (`ValueBodyBase::target`, `src/Ref.h:139`: a hash entry handed out
as a live Pair). The edge belongs to the owning body only; the alias must not
be counted as an edge to the target's contents.

**The DESTROY registry holds a strong reference.** An object whose class
declares `DESTROY` is kept in `destroyReg_` (`maybeRegisterDestroy`,
`src/Interpreter.cpp:3503`), and `runPendingDestroys` (`:3533`) runs
`DESTROY` once the registry is the sole owner. Without care, every such
object looks externally held and can never be collected. A `pflags_` bit set
at registration lets the walk count the registry's reference as internal.
Finalization then goes through the same `DESTROY` chain logic, taking the
object out of the registry first so it is never finalized twice.

**Parallel mode.** rakupp is parallel by default, and other threads change
counts while a walk reads them. The walk runs only when no workers are live
(`liveWorkers_ == 0`, the condition `.receive` already uses). Otherwise
candidates wait in a bounded buffer and are processed at the next quiet
point. A buffer that overflows drops candidates, which means a missed
collection, not a wrong one.

## Where candidates come from

A garbage group is entirely reachable from the reference whose drop made it
garbage. Anything in it not reachable from that drop was garbage *before*,
and should have been caught at an earlier drop. So the collector is complete
exactly when every drop that can strand a group is a candidate. Two trigger
strategies:

**A. Named drops (proposed first).** Hook the places a program lets go of a
value by name:

- scope exit: an Env destroyed, a loop's iteration scope reused
  (`Env::clearBindings` / `clearBindingsKeeping`, `src/Interpreter.h:825`),
  a routine frame returned to `FramePool` (`src/InterpreterCore.cpp:1823`);
- assignment over a variable, an attribute or an element (`$x = Nil`,
  `$a.other = $c`, `@a[0] = 1`), and `:delete`;
- a statement value that is sunk (a sub that builds a cycle and returns it
  to a caller that ignores it).

Cheap: only these sites pay, and only for container payloads whose count
stays above zero. It can miss a group stranded by a drop no hook sees, such
as a C++ temporary that outlives the scope exit and is released later.

**B. Every non-final release (Bacon–Rajan in full).** In `PayRef::release`
and `PRef::release` (`src/Ref.h:158`, `:197`), when the count stays above
zero and the body is a container not yet buffered, buffer it. This is
complete, but it adds a flag test to every release of every container body,
the hottest path in the engine. The comment at `src/Ref.h:40` records a
+2.5% regression from code size alone there. Kept as Phase 4, behind
measurement.

## The cost risk, and how it is bounded

A candidate walk visits everything reachable from the candidate. The
dangerous case: a local variable in a hot routine names the root of a large
live structure (`my $node = $tree.root`, in a tree with parent links, so the
structure *is* cyclic). Every return of that routine would walk the whole
tree and find it externally held. Bounds, in order:

1. **Node budget per walk** (start at 1,000). A walk that exceeds it gives up
   and frees nothing.
2. **The candidate must lie on a cycle.** A garbage group is reachable from
   its candidate, and the candidate's remaining count comes from inside the
   group, so the candidate is reachable from itself. A walk that cannot
   return to the candidate within budget cannot have found cyclic garbage,
   and stops.
3. **A "recently found live" stamp** in `pflags_`, with an epoch, so a
   structure found externally held is not re-walked from the same root on
   every call. This is purely an optimization: a stale stamp can only skip a
   collection.
4. **Batching.** Candidates go to a buffer; the buffer is processed at the
   end of the scope exit that filled it, or when it reaches a threshold.

Whether these bounds suffice is what Phase 0 measures.

## Phases

**Phase 0 — measure, change nothing.** An instrumented mode
(`RAKUPP_CYCLE_STATS=1`) records candidates at the named-drop sites and runs
steps 1–4 as a dry run: walk sizes, budget overruns, garbage found, never
freed. Run it on the benchmark suite, Roast, `t/run.raku` and the leak corpus
below. Decide from the numbers: the budget, whether named drops find the
corpus's garbage, and the walk cost per benchmark. If the dry run's cost is
already visible on the benchmarks (measured as interleaved A/B pairs: a
sequential before-and-after on this machine is noise), the trigger sites get
narrowed before anything is freed.

**Phase 1 — objects, arrays, hashes, pairs, cells; scope-exit trigger.**
`forEachEdge` on each payload kind; the walk, finalization, recheck and
break; the DESTROY registry bit; the quiet-point rule for parallel mode.
Gate: leak corpus flat, Roast unchanged, `t/run.raku` unchanged, benchmarks
within 2%.

**Phase 2 — assignment, `:delete` and sink triggers.** Covers a group made
in one routine and dropped by its caller. Same gate.

**Phase 3 — Envs and closures as walk nodes.** Object → block → scope →
object cycles, and a block that refers to itself through its own scope.
Envs referenced by active frames are external through `tctx_` and are never
collected; the case to get right is a scope kept alive only by a closure.
Same gate.

**Phase 4 — only if Phases 0–2 show gaps that matter:** strategy B behind a
build or runtime switch, with its release-path cost measured on its own.

## Gates and tools

- **Leak corpus, `t/leak/`.** Each case counts `DESTROY` calls, a
  deterministic signal unlike RSS, and asserts the freed count. Cases: the
  #151 pair; an object referring to itself; a doubly linked list; a tree
  with parent links; an array containing itself; a hash containing itself; a
  cycle built in a sub, returned, and dropped by the caller; an object
  holding a closure that mentions it. And the cases that must **not** be
  collected: a cycle still reachable from an outer variable, from an array
  element, from a running closure, and from a C++ temporary mid-collection
  (a cycle passed as an argument while a scope exits). Plus a `DESTROY` that
  resurrects its object, which must keep it alive and finalize it once, and
  a graph larger than the budget, which must leak quietly rather than stall.
  Each case is checked against Rakudo for the freed counts.
- **Poison mode, `RAKUPP_CYCLE_POISON=1`.** Instead of clearing a collected
  object, swap its class for a sentinel whose every method call and
  attribute access dies loudly, naming the object's original class. Roast
  and `t/run.raku` run clean under it, or the edge enumeration has counted a
  reference that does not exist. This is the check for the one unsafe error.
- **Roast** in an isolated worktree, before and after, every phase.
- **Benchmarks:** interleaved A/B pairs against a null control, all kernels.

## Alternatives considered

- **Escape analysis at compile time.** Rejected above: Raku's dynamism makes
  "might escape" the answer almost everywhere.
- **Weak references.** Raku has no standard weak reference, and they would
  need user code to change.
- **A tracing collector replacing reference counting.** A rewrite of memory
  management, and it would give up prompt freeing, which is why rakupp holds
  a fifteenth of Rakudo's memory on the acyclic loop above.
- **Tracking every container, as CPython does.** CPython keeps all container
  objects on a list and periodically trial-deletes the whole set, which needs
  no trigger sites. The cost is a list insertion and removal per container
  allocation. In a parallel engine that is an atomic or a lock on the
  hottest allocation path. It stays the fallback if both trigger strategies
  fall short.

## Not in scope

Cycles through opaque holders (promises, channels, lazy generators, regex
scopes, `std::function` captures) leak as they do today. Compiled code
(`--exe`, `--jit`) reaches the same payloads, so collection applies there
too. But its own frames are not Envs, so its scope-exit trigger is a
separate small job, after Phase 1.
