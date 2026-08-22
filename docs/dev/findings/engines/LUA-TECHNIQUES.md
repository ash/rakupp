# What Lua and LuaJIT teach Raku++

Sixth in the series ([PERL5-](PERL5-TECHNIQUES.md), [PHP7-](PHP7-TECHNIQUES.md),
[PYTHON3-](PYTHON3-TECHNIQUES.md), [RUBY-](RUBY-TECHNIQUES.md),
[MOARVM-TECHNIQUES.md](MOARVM-TECHNIQUES.md)). Primary source, read in full
2026-08-22: Ierusalimschy, de Figueiredo & Celes, *The Implementation of
Lua 5.0* (JUCS, 2005) — the cleanest written account of interpreter economy
in the literature. The LuaJIT material is from Mike Pall's public design
notes (lua-l and elsewhere), cited from memory — the LuaJIT wiki was not
fetchable at time of writing.

Lua is the minimal pole of this series: eight types, no classes, one data
structure, and a stated goal of the "simplest C code" that implements the
language. The honest frame from the perl doc applies double — a large part
of Lua's speed *is* that surface, and LuaJIT's peak numbers are unreachable
for any engine carrying containers, laziness and a numeric tower. What
survives the discount is craft: several mechanisms here are the sharpest
statements of ideas the series keeps meeting, and one — upvalues — is a
design input the container/binding refactor should not be written without.

## 1. TValue: the fourth confirmation of the head/body shape

A Lua value is `{int tag; union {GCObject*, void*, double, int}}` — 12–16
bytes; nil/boolean/number unboxed in the union, the five heap types behind
one pointer whose targets share a common GC header. The paper's own
alternatives analysis is worth keeping: pointer-tag tricks were rejected as
non-portable ANSI C (their constraint, not ours), and heap-allocating
numbers "would make the language quite slow" — the Python base design,
named as the thing to avoid, in 2005. They also concede the cost that
remains: copying a 12–16-byte tagged value is "a little expensive" at 3–4
words per move — the same per-copy arithmetic our 128-byte `Value` faces at
16 words. Series tally for the head/body endgame: perl SV head 24 B, zval
16 B, TValue 12–16 B, JSC's NaN-boxed 8 B
([JSC-TECHNIQUES.md](JSC-TECHNIQUES.md) item 5). With containers and
allomorphs to carry, our realistic landing zone stays 16–24 — but the
bracket is now confirmed from four independent directions.

## 2. Tables: the array+hash hybrid, with the sizing algorithm written down

Lua has one data structure doing both jobs, so 5.0 grew the hybrid: an
array part for integer keys 1..n, a hash part for the rest. The boundary
algorithm on rehash: pick the largest n such that more than half the slots
1..n are used (and some slot in n/2+1..n is), so the array part is never
less than half full — sparse tables degrade gracefully to hash. The hash
part is a chained scatter table with Brent's variation: a colliding element
is guaranteed to sit in its own main position, so there are no secondary
collisions and the table runs at 100% load factor. Globals are an ordinary
table, fast because every string carries its hash — interned once,
hashed once (the fifth engine in the series to land on that pair).
Measured: the array optimization cut the array-heavy benchmarks (sieve,
heapsort, matrix) by up to 40%.

Us: the Array/Hash split at the language level means — for the third time
after PHP and Python — the *packed* insight is structurally ours already.
Two residues worth keeping: Brent's variation is the alternative if our
linear-probe `ValueHash` ever shows probe-chain pathologies (it permits
100% load where we rehash at 75%); and the never-less-than-half-full
boundary rule is the right starting point should a sparse-array
representation ever be needed for `my @a; @a[1_000_000] = 1`.

## 3. Upvalues: per-variable capture — the missing input to the container refactor

The item this study is worth doing for. Lua closures do not capture
frames; they capture **variables**, through upvalue cells: while the
variable is live on the stack, the upvalue points *into the stack slot*
("open"); when the scope exits, the value migrates into the upvalue itself
("closed") — transparently, because access is always through the pointer.
Uniqueness is enforced (a per-stack list of open upvalues, reused on
capture), so two closures over the same `x` share one cell and aliasing
semantics come out right. *Flat closures* complete it: a reference to a
grandparent local is routed through the enclosing function's own closure,
so capture only ever looks one level out.

Us: a closure captures the **whole frame** — `Callable.closure` is a
`shared_ptr<Env>` — which is why closed-over frames stay alive in their
entirety (the reason `breakSelfClosures` exists), why the capture graph can
cycle, and why outer reads walk an Env chain. The pads work made frames
cheap; Lua's design is the answer to what *capture* should look like after
the container/binding refactor: a heap cell **per captured variable**,
created only when a closure actually references it, shared for aliasing
correctness — which is precisely the "container cell only where binding
demands one" rule (PHP7 item 4, `zend_reference`) arriving from a third
direction, this time for closures instead of `is rw`. The parser already
knows each block's referenced-outer-variables; the open/closed distinction
even has a natural analog (cell points into the pad while the frame is
live; migrates on frame exit). One caution, cross-linked: per-variable
capture makes *lazy body parsing* harder, because a skipped body must still
reveal its outer references — V8 hit exactly this and the sequencing note
lives in [V8-LAZY-PARSING.md](V8-LAZY-PARSING.md) item 3.

## 4. The register VM: the tightest spec for the eventual threaded loop

The numbers and mechanisms, for the day PERL5 item 3's design doc gets
written. 35 instructions; fixed 32-bit format (OP 6 bits, A 8, B and C 9);
**RK operands** — a B/C value below ~250 names a register, above it a
constant — so `a = a + 1` is one `ADD` and `a = b.f` one `GETTABLE`, no
loads. Branches: a test instruction conceptually *skips* the following
jump, and the interpreter executes test+jump **fused in one dispatch** —
the third independent appearance of smart branch (PHP, our conds-as-bool,
now Lua). Calls use **register windows**: arguments are evaluated into
consecutive registers at the top of the caller's frame, and those registers
simply *become* the callee's activation record — args-into-frame (PHP7
item 6, MoarVM item 1) in its purest form, published two years before
phpng's authors were hired. Two parallel stacks: frame-info entries and one
big value array the registers live in. Measured against the 4.0 stack VM:
the pure-loop kernel more than 2× faster (1.23 s → 0.54 s), and the paper
is candid that other kernels moved less because dispatch wasn't their
bottleneck — the same "know where the time is" discipline BENCHMARKS.md
practices.

## 5. The one-pass compiler, and the compiler you can delete

Lua compiles source to bytecode in one pass with **no AST at all** —
hand-written scanner, recursive-descent parser, code emitted during
parsing, with delayed emission for base expressions so constants and locals
fold into RK operands. The compiler is ~30% of a ~100 KB core, and can be
omitted entirely: chunks precompile offline and a tiny loader runs them —
the extreme point of the compile-speed spectrum the original "why is perl's
compiler fast" question opened. Us: one notch up by necessity — our
one-pass parser *produces* the AST because the AST is the execution format;
and the deletable-compiler trick is exactly the `--bundle`/`AstSerial`
lane, plus the embedded-prelude idea indexed in PYTHON3 item 6. Recorded as
convergence, not work.

## 6. Stackful coroutines and the C-stack rule

Lua coroutines are stackful and first-class; the enabling discipline is
that the interpreter performs Lua-level calls **without C recursion** (the
paper explicitly contrasts "stackless" Python) — only `resume` nests one C
level, and `yield` unwinds to it. Flat closures kill the cactus-stack
problem of variables living in other coroutines' stacks. This is the same
rule MoarVM states as "no nested runloops"
([MOARVM-TECHNIQUES.md](MOARVM-TECHNIQUES.md) item 6); two VMs arriving at
it independently upgrades it from implementation habit to design law:
resumable control flow and a C-stack-recursive interpreter do not coexist.
Our tree-walk recurses C per call, so full `gather` resumability waits on
the threaded loop — already so noted in the MoarVM doc; this is the second
witness.

## 7. LuaJIT: what hand-craft buys, and which residue transfers

(From Mike Pall's public notes, from memory.) The LuaJIT 2 *interpreter* —
before any JIT — beats PUC-Lua by 2–4×: NaN-tagged 8-byte values; the whole
interpreter hand-written in assembly (via DynASM) with VM state pinned in
registers; fixed 8-bit-field bytecode decoded in two or three instructions;
and — the load-bearing trick — **dispatch replicated at the tail of every
handler** rather than jumping back to a central loop, giving each
opcode-pair its own indirect-branch-predictor entry. The trace JIT on top
records hot loops into linear SSA traces; brilliant on numeric loops,
famously fragile on branchy code (trace explosion) — part of why the
method/block JITs (YJIT's BBV, [RUBY-TECHNIQUES.md](RUBY-TECHNIQUES.md)
item 3) won the argument for irregular workloads.

The honest sieve: an assembly interpreter is outside this project's
maintainability budget, and NaN-boxing is calibration (item 1). What
transfers intact is the dispatch residue for the threaded-loop design:
computed-goto with **per-handler replicated dispatch** captures most of the
branch-prediction win in portable C (Ertl & Gregg's measurements, which the
Lua paper itself cites, say the same), and "keep the interpreter's working
set in registers" survives as: make the op loop's hot state few enough
words that the compiler *can* (the CPython 3.14 tail-call build —
PYTHON3 doc, non-transfer section — is the modern packaging of the same
goal). File both under PERL5 item 3's future design doc.

## What deliberately does not transfer

- **The semantic surface itself** — metatables-instead-of-classes, no type
  tower, no containers: the discount named in the frame. LuaJIT's ceiling
  is not our ceiling.
- **Doubles-only numbers** (5.0-era) — the numeric tower forbids it; our
  TARG int lanes already occupy the practical middle.
- **The trace JIT** — wrong shape for branchy Raku code; the series' JIT
  position stays with BBV (Ruby doc) if ever.

## Suggested order

| # | change | payoff | cost | depends on |
|---|---|---|---|---|
| 3 | per-variable capture cells (open/closed) as the closure half of the container refactor | high — memory (frame liveness), cycles, outer-read cost | large | container/binding design doc; sequence with V8 lazy-parse note |
| 4+7 | register-window calls, RK folding, fused test+jump, replicated dispatch → the threaded-loop spec | large, deferred | large | PERL5 item 3 design doc |
| 2 | Brent-variation + array-boundary rule in the pocket | contingency | — | only on ValueHash/sparse-array evidence |
| 1, 5, 6 | calibration and convergence records | — | — | — |
