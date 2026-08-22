# What Perl 5 does that keeps its constant factors low

A study of the Perl 5 sources (github.com/Perl/perl5, shallow clone at HEAD,
2026-08-21) read against Raku++'s interpreter, looking for mechanisms worth
adopting. The prompt: on the `hashfill` kernel the perl *interpreter* (82 ms)
beats our *compiled binary* (113 ms) on wall clock, and our own interpreter by
3.3× — see [BENCHMARKS.md](../../status/BENCHMARKS.md) "vs Perl 5". Perl has
spent thirty years shaving interpreter constants; most of what it does is
directly legible in five files (`sv.h`, `hv.h`/`hv.c`, `pad.h`, `run.c`,
`pp.h`/`pp_hot.c`).

An honest frame first: Perl's data model is *far* smaller than Raku's — no
containers, no laziness, no junctions, no type objects, no NFG — and a chunk
of its speed is that simplicity, not technique. The items below are the parts
that are technique, ranked by expected payoff for us.

## 1. A scalar is 24 bytes; ours is 344

`sv.h` (`SV_HEAD_`, line ~207): every SV is `{ void* body; U32 refcnt;
U32 flags; 8-byte union }` — 24 bytes. The union holds the IV/NV/PV pointer
directly, so an **integer scalar has no body allocation at all**. Type-specific
fields live behind the body pointer, allocated only for types that need them.

Raku++'s `Value` is **344 bytes** (`sizeof`, this machine), all fields inline:
the string, the BigInt slots, several shared_ptrs, rat/complex fields, flags —
every Value pays for every feature. Every assignment, argument, list element,
and hash node copies 344 bytes; a 2M-entry structure drags ~700 MB through the
allocator. This is measurable today: at the 10×-scale `hashfill` (2M keys) the
native binary wins CPU time against perl (0.88 s vs 1.12 s) but loses wall
clock to page traffic.

**Lever:** a head/body split (small tagged head + typed body pointer), or at
minimum migrating the cold wide fields (rat, complex, BigInt, blob, regex)
behind one lazily-allocated extras pointer, the way `Env::ex` already works.
This is the largest single win available and also the most invasive — it is
effectively the container/binding refactor's data-layout half, and should be
planned with it, not bolted on.

## 2. Lexicals are compile-time array offsets, not hash lookups

`pad.h` (`PAD_SV`, `PAD_SVl`): at compile time every `my` resolves to an
integer offset; at runtime `PL_curpad[po]` is one indexed load. The op carries
the offset in `op_targ` (`pp_hot.c` `pp_padsv`, line ~1526).

Raku++ resolves every variable read through
`unordered_map<string, Value>::find` per frame, walking the parent chain on
miss (`Env::find`, Interpreter.h:245). The node-specialization work already
caches slot *pointers* on hot AST shapes; pads are the systematic version:
resolve `(depth, slot)` at parse time, make a frame a `vector<Value>`. This
composes with the call-frame pool (a pooled frame keeps its vector capacity)
and would retire the map entirely from the hot path. Second-largest win for
interpreted throughput; medium invasiveness (declaration sites are already
statically known to the parser).

## 3. Execution is a flat threaded loop, not tree recursion

`run.c:37`: the whole interpreter is

    while ((PL_op = op = op->op_ppaddr(aTHX))) ;

Each op function does its work and returns `op_next` — a pointer the compiler
filled in once. No recursion, no dispatch on node kind, no per-node virtual
call; total dispatch cost is one indirect call per op.

Raku++ tree-walks: `eval()` recurses per AST node, re-dispatching on node kind
every execution. The `--exe` backend already escapes this by compiling; the
interpreter could linearize statement/expression sequences into a threaded op
array once per block and run perl's loop. Big win, but architectural — this is
"bytecode without inventing a bytecode", and worth a design doc of its own
before any code.

## 4. Ops reuse a preallocated result slot (TARG)

`pp.h` (`SETi`/`TARGi`, line ~585): every arithmetic/string op owns a pad slot
(`op_targ`) allocated once at compile time; `$a + $b` writes its result into
that same SV every execution. Zero allocations per operation.

Raku++'s interpreter constructs a fresh 344-byte `Value` per operator result.
The `-O` codegen lanes already fix this for compiled int statements; the
interpreter pays every time. With pads (item 2) this falls out naturally — a
result slot is just one more pad entry per expression node.

## 5. A scalar caches all three of its forms at once

`sv.h:431`: `SVf_IOK` / `SVf_NOK` / `SVf_POK` are independent bits — after one
numification or stringification the SV holds int *and* num *and* string forms,
each valid until a write invalidates them. `"key$i"` stringifies `$i` once per
distinct value ever.

Raku++ recomputes: `toStr()` builds a fresh `std::string` on every call. The
hashfill fix removed one instance of this by codegen (interpolation literals);
the general mechanism — a memoized string form on Int/Num Values — would need
a mutable cached field (thread care), and pairs naturally with the head/body
split in item 1.

## 6. Hash keys are interned once, with their hash stored

`hv.h:52` (`struct hek`): a hash key is stored **with its computed hash**, and
`PL_strtab` (`hv.c`) interns keys process-wide — a key string exists once,
refcounted, shared by every hash that uses it. Lookup compares the stored hash
first (`hv.c:866` — "strings can't be equal") and only then the bytes.

Raku++'s Hash is `std::map<std::string, Value>` (Value.h:328) — a red-black
tree: O(log n) node walks with a full string comparison at every level, a
344-byte Value in every node, and the key copied into every map. The
`rtIndexRef` memcmp samples in the hashfill profile are exactly this cost. An
open-addressing table with stored hashes would help every hash workload —
**but** Raku++ leans on `std::map`'s sorted iteration for deterministic
`.keys`/`.values`/gist ordering in many places and tests; a replacement must
either keep an order side-structure (insertion-order vector, Rakudo-style
randomized iteration is also spec-legal) or be audited against the ordering
assumptions first. Real win, medium risk; do it behind the `hashRef()`
accessor so the container swap is one type change.

## 7. Arguments travel on one contiguous stack

`pp.h` (`dSP`, `PUSHs`, `POPs`): calls push SV pointers on a single growable
stack; a callee pops them in place. No per-call vector, no element copies —
pointers only.

Raku++ builds a `ValueList` (vector of 344-byte Values) per call; the
call-frame commit already moves rather than copies it, and the frame pool
recycles the env. The remaining step in perl's direction is passing pointers
to existing Values instead of copied Values — which is blocked on (and part
of) the container/binding refactor, since Raku's value semantics need to know
what may alias.

## 8. Allocation goes through arenas and free lists

`sv.c` (`S_more_sv`, arena roots): SV heads come from arena blocks chained on
a free list — allocation is "pop head", free is "push head", O(1), no malloc
in the steady state.

Raku++ makes one `make_shared` per heap payload (arr, hash, ObjectData, Env).
The thread-local call-frame pool that just landed is this exact pattern
applied to one type; `arr` buffers (ValueList) and hash nodes are the next
candidates. Cheap to do incrementally, modest but broad payoff.

## What deliberately does not transfer

- **String COW** (`SvIsCOW`): perl shares string buffers on assignment until a
  write. Our `s` is an inline `std::string`; COW only becomes reachable after
  the head/body split, at which point a refcounted string payload gives it for
  free — fold it into item 1 rather than pursuing it alone.
- **Magic/tie, XS specifics, the GV symbol machinery** — solve perl problems
  we don't have.
- The **op checker/peephole** infrastructure is worth revisiting only after
  item 3 exists.

## Applied so far

**Item 1's incremental step is implemented (2026-08-22)**: the cold-block
split from REPRESENTATION-PLAN batch 2 (revised) — `big`, `ratN`, `ratD`,
`fatRat`, `shape`, `pairKey`, `ext`, `im`, the range fields and `ofType`
moved behind one lazily-allocated copy-on-write `shared_ptr<ValueExt>`.
`sizeof(Value)` 344 → **200**; a plain Int/Str/Array Value copies 5
shared_ptr null-checks and a CowStr instead of 11 shared_ptrs, a std::string
and ~148 bytes of cold scalars. Every bench kernel improved (sortnums −26%,
arrayops −20%, hashfill −15%, the rest −3…−13%), JSON::Fast interpreted parse
−8%, grammar capturing parse −15%, hashfill peak RSS −39%. Zero Roast diff.
Numbers and gates in
[REPRESENTATION-PLAN.md](../plans/REPRESENTATION-PLAN.md) batch 2. The full
24-byte head/body split (and by-value BigInt inside the block) remains open,
still coupled to the container/binding refactor.

**Item 1's second step is implemented (2026-08-22, batch 4)**: the five
payload pointers (`arr`, `hash`, `code`, `pairVal`, `obj`) collapsed into ONE
kind-tagged slot, with the Match family — the only licensed co-occurrence —
behind a combined `MatchData{pos, named, made}` body. `sizeof(Value)` 200 →
**128**; a plain Int/Str copy is down to 2 shared_ptr null-checks and a
CowStr. Interleaved A/B on the same machine: perf-guard's seven kernels
−7.5…−10%, sortnums −17%, arrayops −14%, hashfill −13% interpreted and
**−20% compiled**, JSON::Fast interpreted parse −13%, hashfill peak RSS −32%,
grammar parse −4% (the Match body's extra allocation does not bite). Roast
per-file diff: jitter only. One census blind spot surfaced and moved to the
cold block: `is default` containers carried their element default in
`pairVal` alongside the payload — the census bounds what typical programs
do, not what the language allows. Details in REPRESENTATION-PLAN batch 4.
What remains of item 1 is the endgame: CowStr (40 bytes) and the i/n pair
behind a perl-style body union, the ~64-byte head — genuinely coupled to the
container/binding refactor now, since everything separable from it has been
banked.

### Item 6 (worktree `perl5-techniques`, 2026-08-21)

**Item 6 is implemented**: `src/ValueHash.h` replaces the payload's
`std::map<std::string, Value>` with a compact insertion-ordered hash — stored
key hashes, compare-hash-before-bytes, deque-backed append-only entries so
`Value&` references stay stable across inserts (the contract rtIndexRef
relies on), tombstone deletes, and a converting constructor so the deliberate
sorted `std::map` locals keep their semantics. Rendering sites that used to
get sorted output free from the tree (`Hash.gist`, quanthash gist, `.Str`)
sort explicitly now — which is also what Rakudo does. Measured (same machine,
same day, release builds):

| kernel | std::map | ValueHash | |
|---|---:|---:|---:|
| hashfill, interp (user) | 0.26 s | 0.21 s | −19% |
| hashfill, `--exe -O3` (wall, warm) | 0.103 s | **0.078 s** | −24% |
| 500k lookups on a 100k-key hash, interp (user) | 0.41 s | 0.33 s | −20% |
| bench `hash` kernel, interp (user) | 0.06 s | 0.04 s | −30% |

perl runs hashfill in 0.082 s wall / 0.07 s user — the native binary is now
marginally ahead of it on this kernel. Gates: local suite 499/499 including
the golden outputs, full Roast unchanged (see the run in this worktree's
history).

The full benchmark sweep, A/B against the pre-swap release build: perf-guard's
seven interpreter kernels all at or below the old build (fib 731.6→693.8 ms,
asg 684.9→657.2, hash 48.8→44.2, the rest within noise); the five `-O`
optbench kernels produce their documented speedup shapes and the one apparent
outlier (fibcalls) compiles to a byte-equivalent-speed binary under both
builds — doc-vintage drift, not the container; the parmap parallel kernel is
flat (211→209 ms at 4 workers); a 300×-scaled grammar JSON parse (Match
captures are hash-backed) is flat (0.26→0.25 s). The wins stay confined to
hash-touching paths, with small broad gains where method/attr tables are hot.

The ordering audit the item warned about came to exactly three files.
`S09-hashes/objecthash.t` lost two assertions that only ever passed by
coincidence: the typed-object-hash constraints they sit on are unimplemented,
the hash's *content* is already wrong, and sorted iteration happened to put a
conforming entry where the test looked. `S02-types/mix.t`/`mixhash.t` exposed
a real latent bug — `.total` summed fractional weights in a `double`, so its
rounding depended on iteration order; it now sums exactly through the numeric
tower (Rat stays Rat, which is also Rakudo's answer), recovering both
assertions honestly. S32's apparent −1 was run-to-run flap; a direct
section diff is empty.

Two observations from applying it:

- **String COW already exists here**: `CowStr` in Value.h promotes ≥64-byte
  strings to a shared immutable body with cached scan state — the "COW"
  bullet below overstated the gap; what remains is only folding the short
  inline case into a head/body Value split.
- A `sample` of the object-construction kernel on the new build puts
  malloc/free at ~9% — so item 8 (arena pooling) is no longer the cheap next
  step it looked like; the top remaining costs are `Value` copy/destroy (item
  1) and method-name string comparison in dispatch (the interning story,
  docs/book/ch/10-interning.md). Both are the design-doc items.

## Suggested order

Re-ranked 2026-08-22, after item 6 landed, item 1 banked its two separable
steps (344 → 200 → 128), and the post-ValueHash profile demoted item 8
(malloc/free ~9% of the object-construction kernel; the frame pool already
took the big case). Done: **6** (ValueHash), **1a/1b** (cold block + payload
slot). Remaining:

| # | change | payoff | cost | depends on |
|---|---|---|---|---|
| 2 | pad-style lexical slots | high, interp-wide | medium | parser knows decls (yes) |
| 4 | per-node result slots | medium | low once 2 exists | do as 2's second half |
| 5 | cached string form on numerics | medium | low-medium | unblocked: the inline CowStr is empty on an Int — IOK/POK almost literally; needs the write-on-read thread audit |
| 1 | head/body endgame (CowStr + i/n into the body, ~64-byte head) | largest remaining | large | plan WITH the container/binding refactor |
| 7 | args as pointers on one stack | medium | large | same refactor as 1 |
| 3 | threaded-op execution loop | large | large | design doc first; sequence after 2/4, when dispatch is the top cost |
| 8 | pool remaining payload allocations | small now | low | opportunistic only |

(Method-name interning in dispatch — not a perl technique, but named by the
same profile as a top-two remaining cost — slots between 4 and 1;
docs/book/ch/10-interning.md carries the story.)

Items 1–3 are the same lesson at three layers: **stop paying per-use for what
can be paid per-compile** (slots, offsets, op order) **and stop carrying
per-value what only some values need** (the 344-byte everything-struct). Perl's
speed is mostly those two decisions, made everywhere, thirty years ago.
