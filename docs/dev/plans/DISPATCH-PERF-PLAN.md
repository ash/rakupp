# Plan: dispatch — method calls and attributes stop allocating

*Written 2026-08-30, before any code. Prompted by Anton Antonov's issue #47:
once `Graph` computed the right answer (commits 945285c, 38a0d90) it was 4x
Rakudo, and the fixes in ece4e87 bought 11% of that back. This plan is about the
other 89%. Sibling of [PADS-PLAN.md](PADS-PLAN.md), which did for lexicals what
phase 4 here does for attributes.*

## The measurement that frames everything

Per 300k iterations, best of three, Rakudo 2026.08 vs rakupp at ece4e87:

| | Rakudo | rakupp | |
|---|---|---|---|
| empty loop | 0.035 | 0.026 | rakupp **1.3x faster** |
| operator `+` | 0.031 | 0.042 | 1.4x slower |
| builtin method on `Str` | 0.034 | 0.094 | 2.8x |
| user method, own class | 0.041 | 0.236 | **5.8x** |
| user method, inherited 2 deep | 0.033 | 0.251 | 7.6x |
| attribute read | 0.062 | 0.240 | **4x** |
| private method | 0.050 | 0.409 | **8x** |
| multi method | 0.039 | 0.410 | **10x** |

We win the loop and lose the call. Two readings matter more than the ratios:

**Depth is nearly free.** Two levels of inheritance cost 0.015s on top of a
direct hit — so the MRO walk in `ClassInfo::findMethod` is NOT the problem, and a
method cache would buy almost nothing. This killed the first hypothesis.

**A built-in method is faster than a user one.** `$s.chars` (0.094) beats
`$obj.own` (0.236) even though `chars` walks a long `m == "..."` ladder and the
user method is now dispatched at the top of `methodCallInner` (ece4e87). So the
ladder is not the problem either. What separates them is that the built-in
*answers* while the user method *builds a frame*.

## Where a method call actually goes

`sample` over a tight `$k.m(1)` loop, leaf attribution, non-idle only:

| | samples | |
|---|---|---|
| `_xzm_free` + `mach_absolute_time` + `_xzm_xzone_malloc*` + `_free` + `malloc_type_malloc` | **~1500** | allocate/free |
| `_tlv_get_addr` + `__tls_init` | **921** | thread-local access |
| `eval` / `invokeMethod` / `execBlock` / `bindParams` | ~750 | the actual work |
| string hashing + `memcmp` + `strlen` | ~400 | name lookup |

`mach_absolute_time` is not one of our clocks — it is called from inside
`_xzm_free`, macOS's zone bookkeeping. So it counts as free(), and allocation is
~40% of a method call. `Env::~Env()` is the single heaviest named frame under
`invokeMethod` (237), which says what is being allocated: the call frame.

A call currently allocates, at minimum: the `shared_ptr<Env>` control block, the
`Env` itself (which carries an `unordered_map` and a `vector<Value>` pad), the
`ValueList` for arguments, and whatever the argument `Value`s own.

That a sub call costs ~2x and a method call ~5.8x is the sharpest number here:
whatever `invokeMethod` does *beyond* `callCallableRaw` is worth as much as
everything else in this plan. Phase 0 is one piece of it that is already visible.

## Design, in the order worth doing it

### 0. Stop resolving the same method three times

A user method call now does `findMethodForCall` at the top of `methodCallInner`
(the ece4e87 hoist), then `invokeMethodChain` does `findMethod(name, &owner)`
again, then `nextStart->findMethod(name)` to decide whether a redispatch frame is
needed — three MRO walks and three name hashes for one call. The hoist already
holds the resolved `Value*` and the owning `ClassInfo*`; passing them through
removes two. Half an hour, no design risk, and it should be done first so the
later phases measure against an honest baseline.

### 1. Frame pooling — stop allocating an Env per call

Keep a free list of `Env` objects on the interpreter; a call takes one, resets
it, and returns it on unwind instead of destroying it. Two details make it
work rather than merely look clean:

- The `unordered_map vars` must not be *destroyed and rebuilt* — that is the
  allocation we are removing. `clear()` keeps the buckets, so a pooled frame
  reuses them. Frames that grew pathologically large get dropped rather than
  pooled (a size cap keeps one outlier from pinning memory).
- `Env` is handed out as `shared_ptr` and a closure can capture it, so a frame
  is only returnable to the pool when its use count says nobody kept it. The
  pool hands back `shared_ptr<Env>` with a custom deleter that recycles instead
  of freeing; a captured frame simply never comes back.

Expected: the two biggest allocations per call, and the `Env::~Env` line with
them.

### 2. Make `tctx_` one load

`static thread_local ExecContext tctx_` is a non-trivial type, so every access
compiles to a `_tlv_get_addr` call plus a lazy-init guard — 921 samples, and the
codebase already notes the cost at Interpreter.h:472 for the gather deadline.
Two candidate fixes, cheapest first:

- **Cache it per frame.** The hot functions (`invokeMethod`, `callCallableRaw`,
  `execBlock`, `eval`) touch `tctx_` dozens of times each. One `ExecContext& t =
  tctx_;` at entry turns N wrapper calls into one. Purely local, no semantic
  risk, and it can be done function by function with the guard measuring each.
- **Change the storage.** A `thread_local ExecContext*` with
  `tls_model("initial-exec")` pointing at a heap context is a single load with
  no guard. Bigger blast radius (every `tctx_.` site), so only if the first
  pass leaves the line high.

### 3. Small-buffer argument lists

`ValueList args` is a heap `vector<Value>` per call, and
`__emplace_back_slow_path` shows up in every profile taken this session. Almost
every call passes 0-3 arguments. A small-buffer vector (inline capacity 4)
removes the allocation for the common arity. `ValueList` is a typedef, so this
is one type swap plus whatever `std::vector`-specific API the codebase leans on.

### 4. Attribute slots — PADS-PLAN for `has`

`ObjectData::attrs` is a `ValueHash` keyed by name, so `@!array` is a string
hash per read, and an object's attribute set is fixed at composition. This is
exactly the shape pads solved for lexicals:

- `ClassInfo` gains an attribute layout (name → index), built once at
  composition, inherited attributes flattened in.
- `ObjectData` carries a `vector<Value>` sized from the layout. The map stays
  for the dynamic cases (mixins add attributes at runtime; `.^attributes` and
  the JSON:: unmarshallers walk names) — layout first, map as fallback, the
  same two-rule shape `Env::find` uses.
- Attribute-access AST nodes get a `(ClassInfo*, index)` annotation, re-proved
  against the invocant's class the way pads re-prove frame identity.

Measured target: attribute read from 4x toward parity.

### 5. Call-site inline cache — BUILT, MEASURED NEUTRAL, REVERTED (2026-08-30)

The design was: annotate each `MethodCall` node with the last `(ClassInfo*,
resolved Value*)` pair, so a monomorphic site becomes a pointer compare and skips
`methodCallInner`'s preamble; invalidate on a generation counter that
`noteSymbolMutation` bumps (which turned out to be exactly the right hook — every
method-table change already goes through it).

It was built that way — one atomic word on the node pointing at an immutable,
interpreter-owned record, so a racing reader sees a whole entry or none — and it
passed all 595 checks. Then, measured against the same binary with the cache
switched off at run time:

| kernel | ic off | ic on | |
|---|---|---|---|
| method | 0.25 | 0.25 | 1.00x |
| privmeth | 0.49 | 0.48 | 0.98x |
| multimeth | 0.53 | 0.54 | 1.02x |
| Graph 12x12 | 1.80 | 1.78 | 0.99x |

Neutral, and reverted. The reason is phase 0: once the resolution is made once
and handed to `invokeMethodChain`, all a cache can save is a single ValueHash
probe on the class's own method table plus a short preamble — and a probe on a
table of a dozen names with a cached hash is not measurable. The plan already
suspected this ("depth being cheap means this is worth less than it looks"); what
it got wrong was expecting the private and multi rows to show a win, since their
extra cost is in `requirePrivateCallScope` and `scoreCandidate`, neither of which
a dispatch cache touches.

Worth re-testing only if profiling ever puts `findMethodForCall` back near the
top — for instance after the built-in ladder gets a real dispatch table and the
class probe stops being dwarfed by everything around it.

### 6. Compact slot containers

`BIND-POS` and every element binding allocate a proxy that is a Hash plus two
`Callable`s plus two `std::function`s — about seven allocations for what is
conceptually a pointer and an index. Measured at 4-8x Rakudo for the binding
primitives. Give slot proxies a compact representation that `deproxy` and
`proxyStore` recognise ahead of the generic FETCH/STORE path, keeping the
`hashKind == "Proxy"` surface so the ~40 existing test sites are untouched.

## Gates and measurement

`tools/perf-guard.raku` guards SUB calls well — `fib` is recursive calls and
`subcall` is a typed `is rw` signature 200k times — but **not one kernel calls a
METHOD, reads an attribute, or dispatches a multi.** That is exactly the delta
that went unnoticed: a sub call is ~2x Rakudo and guarded, a method call is 5.8x
and unguarded. Before phase 1, add kernels:

- `method` — a user method call in a loop
- `attr` — an attribute read in a loop
- `privmeth` — a private method call
- `multimeth` — a multi-method call
- `objnew` — construction

Per phase: the guard must not regress any existing kernel, the new kernels move
in the expected direction, `t/run.raku` stays green, and the two dist suites
(Math::NumberTheory, Graph) keep their current pass counts. The end-to-end
number to watch is `Graph::Grid.new(20,20).diameter`: 17.8s at ece4e87 against
Rakudo's 4.1s.

## What this does NOT do

Not a JIT, and not a bytecode VM — this is all inside the tree-walking
interpreter. Not the string-keyed *lexical* path (pads did that). Not multi
dispatch's `scoreCandidate` scoring itself, only the frame cost around it; if
the multi row stays high after phase 5, that is its own plan. Not `--exe`, whose
codegen has separate characteristics and its own baseline.

## What would falsify the design

Phase 1 is the load-bearing bet: that allocation is ~40% of a call and pooling
recovers most of it. If a pooled-frame prototype does not move the `method`
kernel by at least a quarter, the profile is being misread — most likely the
allocations are the argument `Value`s and their `shared_ptr` payloads rather
than the frame, which would promote phase 3 and demote phase 1. Build the
prototype crudely and measure it before committing to the deleter/pool design.

Second falsifier: if caching `tctx_` per function (phase 2, first pass) does not
dent the `_tlv_get_addr` line, then the TLS cost is coming from the many small
`thread_local` scalars (`t_isWorker`, `t_safePtCtr`, `t_gatherDeadline`,
`t_gatherTickCtr`) that the loop and safepoint paths read, not from `tctx_` —
and the fix is to fold them into one context struct rather than to change how
`tctx_` is reached.
