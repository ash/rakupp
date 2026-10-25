# The stale element pointer — two fixes prototyped and measured

*2026-10-24. Nothing here landed. Both prototypes lived in the working tree
under `STALE-PROTO` markers and came out again after the measurements. This is
the record, so they need not be run twice.*

## The hole

The indexed-grow fix (90bc6e23, same day; `growToIndex` in
`src/InterpreterParts.h`) put an array's growth under push's stripe. What it
left: `lvalue()` hands out `&arr[i]`, and the store happens later
(`SlotStripe ws(*this, lv); *lv = rhs;`) under the *element's* stripe, not
the array's. A sibling's growth in between moves the buffer, and the store
writes freed memory. The damage surfaces later as SIGSEGV, SIGTRAP, a realloc
abort, or a hang.

The window between the two also runs user code, so it cannot just be held
under one lock as written: `coerceElemValue`, `checkElemType` (subset `where`
blocks), `proxyStore` and a Proxy's STORE, `coerceToType`, an `is default`
trait, `coerceVarValue`, `coerceViaSubset`.

Probes, all racy programs (the contract promises survival, not values):

| probe | shape |
|---|---|
| p1-blocks-int | ub-indexed-grow's block-per-worker phase, Int values, 20 times |
| p1-blocks-str | the same with Str values |
| p2-push-vs-write | one worker pushes 30k, another stores `@a[$_ % 4]` in bounds, 20 times |
| p3-push-vs-read | one pushes, one reads in bounds |
| p4-pop-vs-write | one pops, one stores in bounds |
| p5-ub-indexed-grow | `t/stress/ub-indexed-grow.raku` as it is |
| p6-typed-grow | its `my Int @typed` phase alone, 20 times |

p2 is the largest hole found. It predates the indexed-grow fix: an in-bounds
store racing a push.

## The two candidates

**A, the plain-element lane.** `@a[KEY] = RHS` resolves the element and stores
it inside one hold of the array's stripe (`ParStripe(*this, list)`), the stripe
every structural mutator of an array already takes. It runs only where no user
code can run inside:

- `=` only;
- KEY pure (`packedIndexShape`) and a non-negative Int;
- RHS a plain Int, Num, Str or Rat (not native, itemized, an enum, a list or a hash kind);
- the array untyped, with no `is default`, shape or ext;
- the current element not a bound container, readonly, native, an object or a Proxy;
- no rw links.

Anything else keeps the old path. The lane engages only while workers run. The
code is in the appendix.

**B, buffer retirement.** Growing an array while workers run copies its
elements into a fresh buffer and keeps the old buffer alive. A stale pointer
then writes into a retired buffer instead of freed memory, and the write is
lost, which a racy program is allowed. Each element is copied under a lock, so
that a stale writer cannot tear the copy.

## Crash rates

Interleaved rounds. **base** is the indexed-grow fix as it stands. **null** is
the prototype binary with both variants off.

| probe | base | null | A | B |
|---|---:|---:|---:|---:|
| p1-blocks-int | 2/50 | 1/50 | 0/50 | 0/50 |
| p1-blocks-str | 2/50 | 3/50 | 0/50 | 0/50 |
| p2-push-vs-write | 16/50 | 31/130 | 0/50 | 0/80 |
| p3-push-vs-read | 0/50 | 0/50 | 0/50 | 0/50 |
| p4-pop-vs-write | 0/50 | 0/50 | 0/50 | 0/50 |
| p5-ub-indexed-grow | — | 1/30 | 0/30 | 0/30 |
| p6-typed-grow | — | 0/30 | 0/30 | 0/30 |

How the failures died: SIGSEGV and SIGTRAP, plus SIGKILL, which is the
harness's 120 s timeout killing a hung process.

B's first p2 run was 19/50. The fast push lane in `Builtins.cpp` had not been
hooked, and leaving out that one growth site left the hole as open as before.

## Cost

Seven interleaved rounds with the order rotated; medians shown. **base2** is a
byte-identical copy of base, the noise floor.

The probes:

- **d2**: a pre-sized shared array, each of 4 workers storing 1M Ints into its own range. A correct program.
- **d7**: d2 with a few operations of work per element.
- **d3**: each worker pushes 1M onto its own array.
- **d4**: 4 workers push 250k onto one array (a race, timed anyway).
- **d5**: each worker auto-extends its own array to 1M.
- **d6**: the main thread stores 2M into a private array while an idle worker lives.

Wall, s:

| probe | base | base2 | null | A | B |
|---|---:|---:|---:|---:|---:|
| d2 partitioned shared | 0.62 | 0.63 | 0.63 | **0.89** | 0.62 |
| d7 partitioned + work | 1.31 | 1.29 | 1.25 | **1.47** | 1.29 |
| d3 push private | 0.35 | 0.34 | 0.29 | 0.34 | 0.39 |
| d4 push shared | 0.34 | 0.32 | 0.33 | 0.32 | 0.37 |
| d5 grow private | 0.46 | 0.45 | 0.44 | **0.34** | **0.57** |
| d6 main, idle worker | 0.64 | 0.65 | 0.67 | 0.63 | 0.64 |

CPU, s:

| probe | base | base2 | null | A | B |
|---|---:|---:|---:|---:|---:|
| d2 | 2.11 | 2.11 | 2.10 | 2.45 | 2.12 |
| d7 | 4.66 | 4.66 | 4.70 | 5.01 | 4.79 |
| d3 | 1.16 | 1.05 | 1.03 | 1.10 | 1.27 |
| d4 | 0.88 | 0.89 | 0.97 | 0.91 | 1.01 |
| d5 | 1.71 | 1.70 | 1.67 | 1.29 | 1.76 |
| d6 | 0.61 | 0.61 | 0.62 | 0.60 | 0.62 |

Peak RSS, MB:

| probe | base | base2 | null | A | B |
|---|---:|---:|---:|---:|---:|
| d2 | 316.9 | 316.8 | 316.9 | 316.9 | 317.0 |
| d3 | 481.9 | 482.9 | 481.9 | 481.7 | 513.2 |
| d4 | 87.6 | 87.6 | 87.6 | 87.6 | 166.4 |
| d5 | 482.9 | 483.2 | 482.1 | 483.1 | 516.9 |
| d6 | 9.2 | 9.3 | 9.4 | 9.3 | 9.4 |

Readings:

- Base against base2 differs by up to 0.11 s CPU on d3, so the push rows are 10% noise. d2, d6 and d7 agree to 0.02 s.
- **A on d2 is +0.27 s wall (+44%) and +16% CPU.** All four workers store into one array, so they take that array's one stripe, where before each store took its own element's stripe. A correct program is paying. With a little work per element (d7) the cost is +13% wall and +7% CPU.
- **A on d5 is −0.12 s wall and −25% CPU.** The lane is shorter than the general path it replaces, and each private array has its own stripe. On d6 (the lane on, the data private) A matches base. The prototype had no privacy check, and these two probes say it doesn't need one.
- **B on d5 is +0.11 s wall (+24%).** d4 is +14% CPU and d3 is inside the noise.
- **B's RSS** is +31 MB on d3, +34 MB on d5 and +79 MB on d4. The prototype never frees a retired buffer.
- With no workers (a single-threaded 1M-store loop) every variant is equal, since both gate on live workers.

## What each needs to become real

**A:**

- **Coverage.** Everything outside the lane keeps the window: compound assignment, `++`, typed arrays, objects or containers as RHS, impure keys, multi-dim and nested subscripts, attribute arrays. None of these reproduced here (p6, the typed phase alone, was 0/30 for every variant), but the window is the same one. Widening is incremental: each added shape is one more condition, not a new mechanism.
- **The array's storage must be pinned** against a concurrent whole-array assignment that replaces the body; the prototype held a raw `ValueList*`. The general path has the same exposure today.
- **The d2 price.** The lever is a reader-writer array lock: lane stores take the shared side, structural mutators the exclusive side. The engine has no recursion-safe reader-writer stripe, so that is its own piece of work, to be measured against d2.
- **Size.** The hook is two lines in `evalAssignInner`, which sits at its function ceiling (3,805), so it needs a cut there first.
- **Soundness** rests on one invariant the engine already keeps: every structural mutation of an array takes that array's stripe. The indexed-grow fix relies on the same one.
- **Forced on every store** (the gate on live workers removed), `t/run.raku` passed 1792/1793. The one failure was that function ceiling.

**B:**

- **Every growth site hooked.** There are 914 `arr()->push_back / emplace_back / insert / reserve / resize / append` calls in 28 files, plus growth through references. Missing one gives p2's 19/50.
- **Lock order.** The first version took `lockStripe(&a[k])` for each element while holding the array's stripe. Two workers, each growing its own array, took the same two stripes in opposite orders and deadlocked: the 300k-per-worker versions of d3 and d5 hung 7/7 until the 300 s timeout. The measured version takes the copy locks from a separate leaf pool. That costs the same, but it excludes stale writers only if every element store moves onto the leaf pool too, which is a change to `SlotStripe` under the torn-copy contract.
- **Reclamation.** The engine has no point at which no thread can still hold a pointer into a retired buffer. B needs epoch-based reclamation, with quiescent points per interpreter thread.
- **Lost writes** land in a retired buffer with no trace.

## Recommendation

**A.** It closes every measured hole with one lane and no new mechanism, and
it is sound by an invariant the engine already keeps. Its price is contention
on a shared array (+44% on a store-only loop, +13% with light work), and it is
faster on private growth.

B closes the same holes only after the following, and it costs growth and
memory on every array while workers run:

- every growth site is hooked;
- the copy gets a new lock order;
- retired buffers get a reclamation scheme the engine does not have.

## Found on the way

A store through a bound element did not reach the container:
`my @b = 1,2,3; @b[1] := my $c; @b[1] = 9; say $c` printed `(Any)` (Rakudo: 9).
Unrelated to threads. Fixed the same day in 84add3b1, together with slice and
list assignments that replaced a bound element instead of storing into it.

## Appendix: the lane as prototyped

```cpp
bool Interpreter::sharedElemStore(Index* ix, const Value& rhs, Value& out) {
    if (!(parallelMode_ && liveWorkers_.load(std::memory_order_acquire) > 0)) return false;
    if (anyRwLinks_ || !packedIndexShape(ix)) return false;
    if (!(rhs.t == VT::Int || rhs.t == VT::Num || rhs.t == VT::Str || rhs.t == VT::Rat) || rhs.natBits ||
        rhs.itemized || rhs.isList || !rhs.enumName.empty() || !rhs.enumType.empty() || !rhs.hashKind.empty())
        return false;
    auto* bve = static_cast<VarExpr*>(ix->base.get());
    Value* bp = padPtr(bve);
    if (!bp) bp = tctx().cur->find(bve->name);
    if (!bp || bp->t != VT::Array || bp->readonly || bp->isList || !bp->hashKind.empty() ||
        !bp->ofType().empty() || bp->elemDefault() || bp->shape() || bp->ext())
        return false;
    Value kv = subscriptKey(ix->index.get());
    if (kv.t != VT::Int || kv.big() || kv.i < 0 || !kv.hashKind.empty()) return false;
    ValueList* l = bp->arr();
    if (!l) return false;
    Value v = rhs;
    v.readonly = v.immutableBind = false;
    ParStripe ws(*this, l);
    const size_t i = (size_t)kv.i;
    if (i < l->size()) {
        const Value& cur = (*l)[i];
        if (cur.isCell() || cur.readonly || cur.immutableBind || cur.natBits || cur.t == VT::Object ||
            (cur.t == VT::Hash && cur.hashKind == "Proxy"))
            return false;   // a bound or special element: the general path decides
    }
    else while (l->size() <= i) l->push_back(Value::any());
    (*l)[i] = v;
    out = std::move(v);
    return true;
}
```

Hooked in `evalAssignInner`'s general path, after the RHS is evaluated and
before `lvalue()`:

```cpp
if (elemTarget && opEq(a->op, "="))
    if (Value out; sharedElemStore(static_cast<Index*>(a->target.get()), rhs, out)) return sink ? Value::any() : out;
```
