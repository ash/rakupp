# Book notes: pads and TARG (raw material, not a chapter yet)

## TARG addendum (same day): the cost of a result

The decomposition ladder that scoped the TARG batch belongs in the book —
six one-line programs pricing an interpreter's per-iteration anatomy (loop
floor 65 ns, a pad read +37, a constant STORE +108, the specialized add
only +28). The punchline: the assignment CEREMONY cost four times the
arithmetic, and perl's literal TARG (result slots) was the wrong translation
for a value-returning tree-walk — the right one was a decided-once
simple-assign lane plus feeding the chapter-19 shapes from the pad
(`padPtr`). asg went −40% on top of the pads batch; the typedness lesson is
chapter-worthy on its own: a typed `my Int $x` leaves NO mark on its slot
Value — the constraint lives in `varDefault`, so the lane's eligibility had
to be a per-slot bit computed from the DECLARATION at layout-build time,
not anything inspectable at the store site.

The four follow-up items (same day) each carry a book-shaped moral:

- The `op=` arms show the cost of RE-DERIVING per evaluation: the full
  compound path allocates a `substr` of the operator string and runs a
  cascade of string compares on every single `+=` — the lane's verdict does
  it once and stores an op class on the node.
- The native-int lane's rule fell out of READING the full path's predicate:
  only uppercase types register an assignment check, so "lowercase declared
  type" is lane-eligible BY THE ENGINE'S OWN DEFINITION — parity by
  construction beats parity by testing.
- The binder's fast-accept has the pattern worth teaching: cache what is a
  property of the NAME (its accept class), re-check per call what can
  change under you (whether a user subset shadows the name — two hash
  counts). Fast-accept only, never fast-reject, so every error message
  stays byte-identical with the full matcher's. The falsifier from the
  plan was then satisfiable mechanically: a `sample` with zero frames in
  the type matchers.
- Cond-to-bool: the first iteration deliberately takes the slow path so
  chapter 19's shape verdict gets decided by the code that owns it; the
  fast path only ever reads the verdict. Layers stay in their lanes.

Combined batch effect: subcall −19.5% (predicted −15–25%), strpass −19.5%,
strscan −12%, loopsum −8%; the `my int` while-shape −45% in isolation.


Working notes collected while the pads batch lands — the material the
Internals book needs when chapters 13 (the tree-walk), 14 (calls) and 19
(node specialization) get their update, or when pads earn a chapter of their
own between 13 and 14. Keep observations here as they happen; polish later.
Plan and design rationale: [../dev/plans/PADS-PLAN.md](../dev/plans/PADS-PLAN.md).

## The story so far (for the chapter opening)

- Until this batch, every variable in Raku++ lived in an
  `unordered_map<string, Value>` on an `Env`, and every read hashed the name
  at every level of the parent chain until it hit. Perl 5 has not done that
  since 1994: a `my` is an integer offset into the pad, decided at compile
  time (`pad.h`, `PAD_SVl`), and a variable read is one indexed load.
- The reason Raku++ could not copy that directly is that `Env` is doing three
  jobs at once: the lexical scope, the CLOSURE environment (a block value
  captures the `shared_ptr<Env>` chain), and the dynamic-lookup spine
  (`CALLER::`, `$*dyn` walks). Perl's pads only do the first job. Any pad
  design here has to keep the other two working through the same object.
- Key sizing fact: the perf-guard kernels showed the cost is NOT mostly in
  deep chains — `asg` is a two-level walk — it is the string hash itself,
  twice per `$x = $x + 1` iteration.

## Design decisions worth explaining in the book

- **Layout keyed by BODY, not by Callable.** `.assuming` wrappers share the
  AST body with the original; slot annotations live on shared AST nodes, so
  the slot assignment must be a function of the body alone.
- **The owner-identity compare** (`pf->layout.get() == ve->padOwner`) is the
  load-bearing safety idea: the fast path re-proves, on every execution, that
  this node's annotation belongs to the frame it is about to index. A missed
  edge case in frame maintenance degrades to the old map path instead of
  reading someone else's slot. This is the same shape as the payload-slot
  batch's kind byte: self-describing state beats a global invariant you have
  to prove over the whole interpreter.
- **Map first, layout second in `find`** — the opposite order would tax every
  non-pad name that passes through a layout frame (all the builtin and
  special lookups that walk to `global_`).
- **Liveness bits, not existence** — a pad slot exists from frame entry but
  answers only after its `my` executed, preserving
  outer-visible-before-inner-declaration. Declaration is an EVENT in the
  scope's timeline, and the mask is that event made cheap.
- **`define` erases the map twin.** Lenient mode lets `$x = 5` precede
  `my $x`; the early write lands in the map, the declaration claims the
  slot; without the erase the name exists in two places and lookup order
  becomes semantics.

## Gotchas hit while implementing (grow this list as they happen)

- **The best bug of the batch, and the design change it forced.** First
  design: a tracked register `tctx_.padFrame`, set by `run()` and
  `callCallableRaw`, restored RAII-style. It survived every targeted edge
  test (shadowing, closures, EVAL, temp, recursion) and then the Forth
  showcase said "stack underflow". Minimal repro:
  `sub f($n is rw, $d) { $d > 0 ?? f($n, $d - 1) !! ($n = 42) }` — the rw
  write-through evaluates the CALLER's argument expression after swapping
  `tctx_.cur` to the caller's scope… but not the pad register. The caller's
  `$n` node is the SAME AST node as the callee's (same sub, recursive), its
  owner-compare passed against the innermost frame's layout, and the write
  landed on the wrong activation. The interpreter has ~109 places that
  re-point `tctx_.cur` temporarily; each was this bug waiting.
  Fix: **stop tracking, start deriving** — the pad frame is the nearest
  layout-carrying ancestor of `tctx_.cur`. A register is a claim about
  global program state; a derivation is a per-use proof from the state you
  actually hold. Every swap site became correct simultaneously, and both
  RAII guards were deleted. Chapter-worthy as a general principle: when a
  cached register needs a discipline every call site must remember, derive
  it from what call sites already maintain.
- **Restructuring a multi-exit path drops the tail.** Turning the declare
  path's `define(...); …; return vars[name]` shape into early
  `return define(...)` calls skipped the `is dynamic` registration that sat
  between them — caught by t/regression/is-dynamic-and-sum.raku within
  minutes. The regression suite's whole point, again: minimal repros of
  once-broken behavior run on every build.
- **Two homes for one name is the recurring hazard class.** Any
  `env->vars[name] = …` site that predates pads can silently create a map
  twin of a pad variable (reads then answer from whichever store `find`
  checks first). The audit had to touch: the binder's rw-sync snapshots,
  temp/let's owner-scope walk, Proxy FETCH/STORE closures, the mainline
  pre-declare guards, `dynVarRef`, and the `is dynamic`/`is default`
  declaring-scope walks — all moved to a scope-`local()` helper that sees
  both stores. The forEachVar helper covers the three enumeration walkers
  (self-closure cycle breaking, REPL completion, the `__stash__` dump).

## The falsified prediction, and the second lever it exposed

Chapter material: the plan predicted asg/loopsum ≥10% from pads alone, and
the first A/B measured asg FLAT (+0.8%) with fib at −5%. The plan's own
falsifier clause said what to do — check whether the annotation fires before
concluding — and a `sample` of the asg loop answered better than any
theorizing: the samples were not in `Env::find` at all. They were in
`__emplace_unique_key_args` and `execBlock` — the per-iteration topic
INSERT and the iteration-scope machinery. Pads had already removed the
lookups; the kernels' remaining cost was the scope itself, two million
times.

That is the other half of perl's foreach lesson: perl aliases the loop
variable to ONE pad cell for the whole loop — no per-iteration binding at
all. The equivalent here: when a static scan proves the body cannot DEFINE
anything into the iteration scope (no declares, no CATCH/phaser, no
EVAL/use in scope-sharing positions — nested if/loop bodies don't count,
they build child envs), the loop keeps one scope and overwrites the topic
Value in place. Capture safety stays DYNAMIC: a closure taking the scope
bumps use_count and the next iteration forks it, per-iteration freshness
exactly as before. The redo-rebind lambda also stopped being constructed
per iteration (std::function built once, loop index captured by
reference).

Numbers moved from noise to the predicted band and past it — the lesson
for the book: measure, believe the falsifier, and profile before adding
machinery. The pads infrastructure was NOT wasted work — the flat-loop
lever lands on top of it (the `$x` in the body is a pad hit; the topic is
one map node written in place), and TARG (result slots) builds on the same
frame.

## Measurements (this box, interleaved best-of-3, pre-pads vs pads+flat-loop)

| kernel | pre | after | |
|---|---:|---:|---:|
| perf-guard asg | 545.1 ms | 406.7 ms | −25% |
| perf-guard loopsum | 226.5 ms | 149.5 ms | −34% |
| perf-guard hash | 34.0 ms | 26.0 ms | −24% |
| perf-guard fib | 575.1 ms | 546.0 ms | −5% |
| perf-guard strscan/strpass/subcall | — | — | −2% (while-loop bodies; cost is the body work) |
| bench hashfill (interp) | 184.0 ms | 155.2 ms | −16% |
| bench strcat | 17.2 ms | 13.1 ms | −24% |
| bench regex | 50.3 ms | 45.6 ms | −10% |
| bench streq | 494.9 ms | 459.2 ms | −7% |
| bench fib | 600.6 ms | 550.5 ms | −8% |
| bench sortnums/arrayops/bigint | — | — | −1…−4% (not variable-bound) |
| JSON::Fast parse (347 KB) | 475 ms | 463 ms | −2.5% |
| grammar capturing parse | 149 ms | 150 ms | flat |
