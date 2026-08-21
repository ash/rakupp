# What perl 5 does that keeps its constant factors low

A study of the perl 5 sources (github.com/Perl/perl5, shallow clone at HEAD,
2026-08-21) read against Raku++'s interpreter, looking for mechanisms worth
adopting. The prompt: on the `hashfill` kernel the perl *interpreter* (82 ms)
beats our *compiled binary* (113 ms) on wall clock, and our own interpreter by
3.3× — see [BENCHMARKS.md](../../status/BENCHMARKS.md) "vs Perl 5". Perl has
spent thirty years shaving interpreter constants; most of what it does is
directly legible in five files (`sv.h`, `hv.h`/`hv.c`, `pad.h`, `run.c`,
`pp.h`/`pp_hot.c`).

An honest frame first: perl's data model is *far* smaller than Raku's — no
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

## Suggested order

| # | change | payoff | cost | depends on |
|---|---|---|---|---|
| 8 | pool `arr`/hash payload allocations | modest, broad | low | — |
| 6 | hash table with stored hashes behind `hashRef()` | high on hash work | medium (ordering audit) | — |
| 2 | pad-style lexical slots | high, interp-wide | medium | parser knows decls (yes) |
| 4 | per-node result slots | medium | low once 2 exists | 2 |
| 5 | cached string form on numerics | medium | low once 1 exists | 1 (cleanly) |
| 1 | head/body Value split | largest | large | plan with container refactor |
| 3 | threaded-op execution loop | large | large | design doc first |

Items 1–3 are the same lesson at three layers: **stop paying per-use for what
can be paid per-compile** (slots, offsets, op order) **and stop carrying
per-value what only some values need** (the 344-byte everything-struct). Perl's
speed is mostly those two decisions, made everywhere, thirty years ago.
