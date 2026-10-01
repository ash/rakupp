# Plan: packed native arrays — `my int @a` as machine words

*Written 2026-10-01, before any code. INTERP-SPEED-PLAN tier 2, item 7; V6-PLAN
P3 item 6. The number to move: a million-element `my int @a` is 83 bytes an
element (an 80-byte `Value` each, after realloc growth), against ~10 under
Rakudo.*

## Where it stands

`my int @a` is an ordinary Array whose `ofType` says `int`: a `ValueList` of
80-byte `Value`s. Nothing is packed anywhere. The engine's semantics for it,
as they are today (and as a packed form has to keep them, since a change of
representation must not change an answer):

- **List assignment is strict.** `my int @a = …` refuses a Str, a Num, an Int
  past a machine word, and wraps a sized native (`my int8 @b = 200` stores
  -56) — the loop in evalAssignInner's `@`-target arm.
- **Element stores are lax.** `@a[0] = 2**64 + 5` stores the bignum, and
  `@a[1] = "x"` stores the Str; `my num @n; @n[0] = 3` keeps an Int. Rakudo
  refuses all three. That divergence is not this plan's to fix, but the packed
  form must not silently change it either: a store it cannot hold falls back
  to the boxed form and stores as today.
- **Reads** hand out the element Value. A packed read builds the Int (or Num)
  the boxed element would have been.

## The design: a packed payload that unpacks itself on demand

A new payload kind, `PK::Packed`, holds the elements as `int64_t` or `double`
(one kind per array, from its `ofType`: `int`/`int64` or `num`/`num64`; sized
and unsigned natives stay boxed in v1).

The seam is `Value::arr()`. It has 3,129 call sites in 38 files, and none of
them changes: on a packed payload `arr()` **materialises** — builds the
`ValueList` once, inside the shared payload body, and from then on the payload
is boxed for every holder of it. So every path that does not know about
packing is correct by construction, and costs a one-time unpack.

What keeps an array packed is a short list of fast paths that read the packed
words directly:

1. construction by list assignment (`my int @a = ^N`, `= 1, 2, 3`), after the
   existing strictness checks pass;
2. an element read with an in-range Int subscript;
3. `.elems` and `.end`;
4. an element store of a plain machine Int (or Num) at an in-range subscript —
   anything else unpacks and stores as today;
5. `for @a` iteration with a read-only loop variable;
6. `--cnp`'s `idxget`/`idxset`/`alen` on a packed register;
7. `.sum`, `.min`, `.max` over the words.

A program that stays on these keeps its array at 8 bytes an element. A program
that leaves them pays one unpack and then runs exactly as it does today.

## Hazards, named

- **References into elements.** `lvalue()` on an element hands out a `Value&`
  (binding, `is rw` arguments, `:=`, `\@a[0]`). Packed words have no `Value`
  to point at, so every such path goes through `arr()` and unpacks. That is
  the design working, not an exception to it.
- **Concurrency.** Two threads may call `arr()` on one packed payload. The
  materialisation runs under a once-flag in the payload.
- **Copies.** A packed payload is shared by copies of the container the way a
  `ValueList` is (`PRef`); copy-on-write paths that clone the list must clone
  the packed words.
- **`--exe` and Codegen.** Generated C++ reaches arrays through the runtime's
  helpers, which call `arr()`; they stay correct and unpack.

## Batches

Each one gated on Roast (per-file), `t/run.raku`, the `--cnp` gate, the budget,
an interleaved perf-guard A/B, and the memory row.

1. The payload, `arr()` materialisation, construction (1), reads (2),
   `.elems` (3). Measure: a 1M `my int @a = ^N` read back by index.
2. Element stores (4) and `for` iteration (5).
3. `--cnp` (6) and the reductions (7).

## What would falsify this plan

- If ordinary programs over `my int @a` unpack on their first few statements —
  `say @a`, a slice, a method this plan did not list — the memory never
  materialises as a saving. Batch 1's measurement includes a census of which
  paths unpacked, over the examples and the `types/` benchmarks.
