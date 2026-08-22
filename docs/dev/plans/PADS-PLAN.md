# Plan: pads — lexicals stop being hash lookups

*Written 2026-08-22, before any code. PERL5-TECHNIQUES item 2 (with item 4, the
result slots, deliberately deferred to a follow-up batch once this one's frame
machinery exists). Book notes for the Internals chapters accumulate in
[../../book/NOTES-pads.md](../../book/NOTES-pads.md) as this lands.*

## What perl does, and what we do instead

Perl resolves every `my` to an integer pad offset at compile time; at runtime
`PL_curpad[po]` is one indexed load (`pad.h`, `pp_hot.c:pp_padsv`). Raku++
resolves every variable read by hashing its NAME into the current `Env`'s
`unordered_map` and walking the parent chain on miss — per read, per level.
The read fast path (Interpreter.cpp, `case NK::VarExpr`) and the write path
(`lvalue`) both end in `Env::find`; a `$x = $x + 1` in a loop body pays the
hash-and-walk twice per iteration, and the perf-guard kernels are exactly this
shape — `asg`, `loopsum`, `hash`, `strscan` mutate MAINLINE `my`s from inside
loop bodies; only `fib` is routine-frame-bound.

## Design

One new compile-time object and three Env fields, everything else is the two
resolution rules.

**`PadLayout`** — per pad OWNER, built once, keyed by the owner's BODY address
(two Callables sharing one body — `.assuming` wrappers — must agree on slots,
so the body, not the Callable, is the key). Owners in v1: the main program's
mainline, and every Callable body at its first call. NOT owners in v1: EVAL'd
ASTs, module mainlines, REPL lines — their code runs against frames whose
layout belongs to someone else, so they stay on the map path entirely.

**Slots** — a name gets a slot if it is a PARAM of the owner, or a plain `my`
declared as a DIRECT top-level statement of the owner's body (the same set a
reader of the body sees without entering a block). Inner-block `my`s keep
their per-block map semantics — a slot in the owner pad would break
per-iteration freshness for closures. Excluded by name class: twigils,
specials, `$_`, `&`-sigil names, `our`/`state`/`is dynamic`/shaped/pkg-scoped
declarations. Owners with more than 64 candidates get no layout (the liveness
mask is one word).

**Env** grows `shared_ptr<const PadLayout> layout` (owning — a captured frame
can outlive its Callable), `vector<Value> pad` (sized once at frame setup,
NEVER grows — `lvalue()` hands out `Value*` into it, the same stability
contract ValueHash honors), and a `uint64_t padLive` mask — a slot answers
lookups only after its declaration has executed, which keeps the
outer-variable-visible-before-inner-declaration semantics.

**The two resolution rules:**

1. *Names*: `Env::find` checks the map FIRST (protects every non-pad lookup
   passing through a layout frame from paying a second hash), then the layout
   (live slots only). `Env::define` redirects layout names into the pad, sets
   the live bit, and ERASES any stale map entry of that name — a write that
   happened before the declaration executed (lenient mode) leaves a map
   duplicate otherwise. Every existing `define`/`find` caller becomes
   pad-correct with no site changes; sites touching `->vars` raw are the audit
   list below.

2. *Sites*: the resolution pass annotates a `VarExpr` with `(padSlot,
   padOwner)` — DecidedOnce fields, relaxed atomics like every other
   decided-once AST annotation — only when the reference is DOMINATED by the
   declaration inside the owner: walking the owner's statement list in order,
   declarations activate their name; references resolve while active; inline
   statement blocks (if/while/for/loop/given bodies) are entered with the
   active set (pointy/loop vars shadow within); expression blocks that become
   Callables (map/grep blocks, sub decls) are NOT entered — their references
   resolve at their own first call, against their own layout, and outer
   captures stay on the map path in v1.

**The fast path** (eval `VarExpr` read, `lvalue` write): the pad frame is
DERIVED, not tracked — the nearest layout-carrying ancestor of `tctx_.cur`
(zero to a few pointer hops; inline block scopes carry no layout). The read
is then:

    pf = nearest layout ancestor of cur
    pf->layout.get() == ve->padOwner  &&  live(ve->padSlot)
        ? &pf->pad[ve->padSlot] : slow path      (never skip past pf)

*As first built, this was a tracked register set by `run()`/`callCallableRaw`
— and the Forth showcase found the hole on landing day: the rw write-through
re-points `tctx_.cur` at the CALLER's scope to evaluate the caller's argument
expression, and in a recursive `is rw` sub that expression is the same AST
node as the callee's, so the owner compare passed against the wrong
ACTIVATION. The interpreter re-points `tctx_.cur` in ~109 places; a register
made each one a proof obligation. Deriving from `tctx_.cur` makes them all
correct at once.*

The owner-identity compare keeps the fast path SELF-VALIDATING: an annotated
node executing under any frame chain it does not belong to — a module
mainline, an EVAL, a foreign context — fails the pointer compare and takes
today's path. Mistakes degrade to slow, never to corruption. That property is
what makes this batch shippable without proving a global invariant about
every execution path in the interpreter.

## The audit list (raw `->vars` sites)

- `breakSelfClosures` (Interpreter.cpp:2251) walks a dying frame's vars for
  self-closure cycles — must also walk the pad (a `my $f = sub {…}` at owner
  level is a slot).
- `replNames` (4719) and the `__stash__` pseudo-package dump (20493) —
  iteration sites; a `forEachVar` helper covers map + live pad slots.
- `temp`/`let` (19566, 20281): locate the owning scope of a name — must see
  pad names (via find/layout, not `vars.count`).
- The EVAL operator-seeding scan (4604) and the `our`-qualified global scan
  (MethodCallPart2:3664) are unaffected by name class (`&…:<…>`, qualified).
- The declare paths (eval ~22533, lvalue ~11836) and `hoistExprDecls` (2089)
  route through `define` or get the layout probe added.
- The lenient undeclared-assign fallthroughs (11906/11912) create map entries
  the declare-time erase cleans up.

## What this does NOT do (v1)

Inner-block `my` slots, placeholder params, `&`-name slots (sub lookup), `$_`,
result slots (item 4 — next batch, one more pad region per owner), binder
direct-slot writes (params go through the `define` redirect this batch; the
per-param hash can come out later if the profile still shows it).

## Gates and measurement

The standing batch gates: `t/run.raku` 499/499 including the `--exe` goldens
(codegen is untouched — compiled code has its own locals — but the goldens are
cheap), full Roast per-file diff vs a same-day pre-batch run with serial
re-verification of any flap, parmap + t/stress/parallel-map under
`RAKUPP_PARALLEL=1`, TSan leg over both, `-DRAKUPP_PTR_CENSUS` still compiles.

Perf protocol, so the change is measurable in one sitting (the batch-4
procedure): stash the pre-batch release binary, interleaved best-of-N A/B on
the quiet machine — perf-guard's seven kernels, the bench kernels, JSON::Fast
interpreted parse, the grammar capturing parse, hashfill RSS. The benchmarks
machine refresh picks the numbers up separately.

**Prediction:** `asg`/`loopsum` are two find-walks per iteration and should
move the most — if pads land and neither moves at least ~10%, the resolution
pass is not annotating the kernels' shapes (check `padSlot` on the loop-body
VarExprs before concluding anything else). `fib` gains the param-read path
but keeps its per-call map inserts, so it should move less. `hash` reads
`%c` per iteration through two frame levels today.

## LANDED (2026-08-22), and what the falsifier caught

The batch shipped in two levers, because the prediction below FAILED first
and the falsifier clause did its job:

1. **Pads as designed** — layouts, slots, the derived frame (see the design
   note above for the tracked-register bug the Forth showcase caught). First
   A/B: fib −5%, asg FLAT. Per the falsifier, the next step was not
   redesign but a `sample` of the asg loop — which showed the remaining
   per-iteration cost in `__emplace_unique_key_args` and `execBlock`, not
   in `Env::find`. Pads had removed the lookups; the topic re-insert and
   the iteration-scope machinery were the cost.
2. **Flat loop bodies** — perl's other foreach lesson: when a static scan
   (`flatLoopBody`, cached on the Block) proves the body cannot define into
   the iteration scope, the loop keeps ONE scope and overwrites the topic
   map node in place; capture safety stays dynamic (use_count fork, exactly
   the old semantics). The redo-rebind std::function is built once per
   loop, not per iteration.

Measured (this box, interleaved best-of-3, pre-pads HEAD vs both levers):
asg **−25%**, loopsum **−34%**, hash **−24%**, fib −5…−8%, bench hashfill
interpreted **−16%**, strcat −24%, regex −10%, streq −7%; JSON::Fast −2.5%,
grammar parse flat; the while-shaped kernels (strscan/strpass/subcall) −2%
— their cost is the body work, and their lever is item 4 (TARG) plus the
threaded loop, not scopes. Full tables in
[../../book/NOTES-pads.md](../../book/NOTES-pads.md).

Gates: t/run.raku 499/499 (the `is dynamic` ordering slip and the rw
write-through activation bug were both caught by the suite/showcases before
any Roast run); full Roast per-file diff vs a same-day pre-pads baseline —
jitter only, every non-RNG differing file re-verified serially identical
under both binaries (advent2012-day13 23<->24 is the documented flapper);
parmap correct and t/stress/parallel-map PASS under RAKUPP_PARALLEL=1 on
the release build, and the ThreadSanitizer build runs both with ZERO
reports through the pad accessors, the derived-frame walk and the
flat-loop scope reuse.

## What would falsify the design

- A Roast diff traced to lookup order — the map-first rule exists exactly so
  a name can never be found in two places; if one shows up, the stale-entry
  erase in `define` has a hole.
- A perf-guard kernel slower: the layout probe added to `define`, or the pad
  resize at frame setup, costing more than the lookups save on short frames.
  The frame pool keeps pad capacity across calls to keep that cheap.
