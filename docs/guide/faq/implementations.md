# FAQ — three ways to build a Raku

Raku is a *specification* with more than one implementation, and three of them
are in active development: **Rakudo**, the reference; **Raku++**, this one; and
**[mutsu](https://github.com/tokuhirom/mutsu)**, written in Rust. They target
the same language and are all measured against the same test suite, but they are
built on three genuinely different sets of engineering decisions.

This page is about **how they are built** and what follows from it — not a
scoreboard. Where a difference shows up as a number, the number is here with the
machine and date it came from; for the kernel-by-kernel timings see
[BENCHMARKS.md](../../status/BENCHMARKS.md), and for test coverage
[ROAST.md](../../status/ROAST.md).

## One paragraph each

**Rakudo** is the reference implementation and the only complete one. Its
compiler is written in Raku itself plus NQP ("Not Quite Perl", its bootstrap
subset); source is compiled through an intermediate tree (QAST) into bytecode for
**MoarVM**, a register-based virtual machine built specifically for it. MoarVM
carries a dynamic optimiser (`spesh`) that specialises hot code on observed types
and inlines through it, a JIT behind that, and a generational, precise, moving
garbage collector. Rakudo also has a JVM backend. Practically everything written
about "what Raku does" is a description of Rakudo's behaviour, and both other
implementations treat it as the oracle.

**Raku++** is a hand-written C++17 implementation with no third-party
dependencies. A hand-rolled lexer feeds a recursive-descent statement parser with
a Pratt expression core; the result is one uniform AST that both the interpreter
and the code generator walk. There is no bytecode and no VM: the interpreter
walks the tree directly. What it has instead is **four output modes** — interpret,
`--bundle` (embed the source), `--aot` (embed the rebuilt tree), and `--exe`,
which transpiles the program to C++ and links a native binary with no interpreter
inside it. Memory is `shared_ptr` reference counting with no collector. See
[ARCHITECTURE.md](../../internals/ARCHITECTURE.md).

**mutsu** is a Raku implementation in Rust, started in February 2026 and
developed at a high tempo. It parses to an AST, compiles that to bytecode
(~340 opcodes), and runs it on its own VM — the tree-walking engine it started
with was deliberately removed, so there is exactly one execution path. Underneath
sit a **Cranelift JIT** (on by default; `MUTSU_JIT=off` opts out), a
**Bacon–Rajan cycle collector** layered on Rust's reference counts, and an
8-byte NaN-boxed value representation. Its stated goal is a *batteries-included*
Raku: the release bundles the real Zef package manager as `mzef` plus a set of
vendored community modules, with the policy of adopting each module unchanged and
growing the interpreter until it runs.

## The structural differences

| | **Raku++** | **mutsu** | **Rakudo** |
|---|---|---|---|
| Written in | C++17 | Rust | Raku + NQP |
| Front end | hand-written lexer + recursive descent / Pratt | hand-written parser | NQP grammar → QAST |
| Execution | tree-walking interpreter | bytecode VM (~340 opcodes) | bytecode VM (MoarVM) |
| Runtime optimiser | none | Cranelift JIT, default on | `spesh` type specialisation + inlining, then JIT |
| Memory | `shared_ptr` refcounting, no collector | refcounting + Bacon–Rajan cycle collector | generational, precise, moving GC |
| Value repr | one struct, cold fields behind a copy-on-write block | NaN-boxed (8-byte target) | MoarVM object model |
| Third-party deps | none | ~25 crates (num-bigint, ICU, Cranelift, libffi, pcre2) | its own toolchain (NQP, MoarVM) |
| Regex engine | own | own | own (NQP-compiled) |
| Unicode | own generated UCD tables | ICU + `unicode-*` crates | own tables |
| Ships a binary? | `--exe`: yes, standalone native | no | no |
| In a browser | Raku.js (WebAssembly) | WebAssembly build | — |

Two things on that table are worth pointing out because they cut against the
obvious story. **All three wrote their own regex engine** — Raku's regex and
grammar semantics are far enough from PCRE that nobody could take one off the
shelf. And **Raku++ and mutsu implement Roast's `fudge` directives inside the
engine** rather than by running Roast's Perl preprocessor, independently, for the
same reason: it preserves line numbers. Convergent evolution is a good sign that
a decision was forced rather than chosen.

## What each choice buys, and costs

### Tree-walk versus bytecode

Walking the AST means every node re-dispatches on every execution — a switch on
the node kind, a virtual call, and the operands fetched through pointers, each
time round the loop. A bytecode VM pays that dispatch cost once, at compile time,
and then runs a flat instruction stream. On paper the VM wins, and that is why
mutsu deleted its own tree-walker.

What a tree-walker keeps is **nothing between you and the program**: no compile
step, no bytecode cache, no VM image to load. That is most of why Raku++ starts
in single-digit milliseconds. It is also why `--exe` exists — if you want the
dispatch overhead gone, the answer here is not to build a VM but to stop
interpreting altogether and emit C++.

### Making it faster

Three different answers to the same question:

- **Rakudo** optimises at run time. `spesh` watches which types actually flow
  through a routine, generates a specialised version, inlines into it, and hands
  the result to the JIT. This is the most powerful approach and the reason
  Rakudo wins the kernels dominated by object and method dispatch.
- **mutsu** JITs hot methods with Cranelift — a pure-Rust backend chosen over
  LLVM specifically to protect startup time, since a heavyweight code generator
  would have cost the fast start the project treats as a feature.
- **Raku++** compiles ahead of time or not at all. `--exe` turns the whole
  program into C++ and hands it to the system compiler, which has as long as it
  likes to optimise. The cost is that this only helps code the generator can
  express; time spent inside the runtime's own methods is unchanged, and the
  interpreter itself gets no help at all.

### Reclaiming memory

Raku++ frees a value the moment its last reference goes, which buys a ~1.5 MB
floor and no stop-the-world pause, and costs you **reference cycles, which are
never reclaimed** — see [garbage-collection.md](garbage-collection.md). mutsu
starts from the same refcounting base and adds a Bacon–Rajan collector that
finds exactly those cycles, so it pays a little memory and some bookkeeping to
close the leak. Rakudo's moving generational collector is the most capable of the
three and the reason its baseline process is ~90 MB rather than ~1.5 MB.

### Dependencies

Raku++ links nothing: the BigInt, the Unicode tables, the regex engine, the FFI
and the collation are all in-tree. mutsu takes `num-bigint`, ICU, `libffi` and
Cranelift off the shelf. Neither is the right answer in general. Ours means one
binary, no supply chain and no version skew, paid for by having to write and
maintain every one of those ourselves. Theirs means large, mature components for
free, paid for in build time and in a dependency graph to keep current.

This page used to predict that `bigint` would be the kernel where a well-tuned
external library simply beats what we hand-rolled, and for a while it was:
measured at **3.5× interpreted and 3.3× compiled**, the one kernel of the sixteen
where mutsu beat both Raku++ modes. Two passes on our own multiply have since
reversed it — 7.4 ms interpreted and 6.2 compiled against mutsu's 11.2, on the
box where all three were measured together. The honest version of the prediction
is narrower than the one that was written here: what an off-the-shelf library
buys you is *everything at once*, not any particular kernel. `num-bigint`'s
base-2^64 limbs are still about 10× ahead of our base-1e9 ones on a general
n×n product — we simply have not needed that shape enough to pay for it, and
base 1e9 is what makes printing a large integer O(n) instead of O(n²). Every one
of those trades is ours to make and to get wrong; that is the actual cost of
taking no dependencies.

### What you can ship

This is the sharpest divergence. `--exe` produces a standalone native binary that
needs no Raku on the target machine — no other implementation does this. mutsu
ships something different and equally deliberate: one binary that already
contains a package manager and a working module set, so a fresh install can write
a real program without a network round trip. Rakudo ships the complete language
and the whole ecosystem behind `zef`.

## What you actually notice

Measured on this machine (macOS Darwin 25.5, Apple M1), 2026-08-31 — rakupp
3.23.0, mutsu 0.23.0, Rakudo v2026.08 on MoarVM 2026.08. These are start-up and
footprint figures, which are stable across machines in *shape* if not in exact
value; the throughput tables — which as of the 2026-08-31 sitting carry a
mutsu column on all sixteen kernels — live in
[BENCHMARKS.md](../../status/BENCHMARKS.md) and are measured on a different,
dedicated box.

| running `say 1` | Raku++ | mutsu | Rakudo |
|---|---:|---:|---:|
| wall clock | 4.1 ms | 6.0 ms | 102.8 ms |
| peak memory footprint | 1.5 MB | 4.3 MB | 91.1 MB |
| max RSS | 4.2 MB | 10.4 MB | 129.8 MB |
| installed size | 11.9 MB (one binary) | 32.6 MB (one binary) | 74 MB + MoarVM |

The two-orders-of-magnitude startup gap is not an efficiency difference so much
as an architectural one: Rakudo loads a large precompiled `CORE.setting` on every
run, and neither of the other two has an equivalent to load. It is what makes
Raku usable in a shell pipeline or an editor hook, and it is the one thing both
newer implementations optimised for from the start.

## Coverage

Speed comparisons only mean anything on the subset of the language all three run
identically, and that subset is set by the least complete implementation. On
Roast — the official suite, 1,464 files — **Rakudo runs essentially all of it**,
and it is the oracle both other engines check themselves against.

Between the two newer implementations, **mutsu is well ahead of Raku++ on
coverage**. Both rows below were produced by the same harness on the same day,
same Roast revision, same timeout, same counting rules:

| | files fully passing | assertions, all declared |
|---|---:|---:|
| **mutsu** 0.23.0 | **1,419 / 1,464 (96.9%)** | **216,807 / 218,173 (99.4%)** |
| **Raku++** 3.23.0 | 643 / 1,464 (43.9%) | 198,939 / 218,773 (90.0%) |

The shape of that gap is worth reading. Raku++ passes ~90% of assertions almost
everywhere but leaves a residue in most files, so the all-or-nothing file bar
stays low; mutsu has cleaned up that tail across nearly every synopsis. Six
months of very high-tempo development got them there, and it shows.

The rules for comparing any two of these numbers — which is easy to get wrong —
are in [COUNTING.md](../../status/COUNTING.md). The short version: a Roast figure
means nothing without the bar it was measured on, because whether an engine
honours the suite's `#?rakudo skip` directives moves the file count by hundreds.
Our own per-synopsis breakdown is in [ROAST.md](../../status/ROAST.md).

## Further reading

- [ARCHITECTURE.md](../../internals/ARCHITECTURE.md) — how Raku++ itself is put
  together, mode by mode.
- [garbage-collection.md](garbage-collection.md) — refcounting, what it buys,
  and the cycles it leaks.
- [compiling.md](compiling.md) — `--exe` vs `--aot` vs `--bundle`.
- [differences.md](differences.md) — where Raku++ and Rakudo differ in
  *behaviour* rather than construction.
- [COUNTING.md](../../status/COUNTING.md) — how Roast results are counted here,
  and how to compare them with someone else's.
