# Values in a pool, variables as indices: measured, not pursued

*2026-11-10. An experiment: this file and the probe are all there is of it,
and nothing in the engine changed. It follows the day `Value` went from 64 to
32 bytes ([VALUE32-PLAN.md](../plans/VALUE32-PLAN.md)).*

**The question:** keep every `Value` in one big pre-allocated array, and have
a variable or an array element hold a 16- or 32-bit index into it instead of
the 32-byte `Value` itself. Is that a way to make values smaller and faster?

**The answer:** no. The pool is a cost on everything it touches. One part of
the idea is a large win, a small Int held in the index itself, and that part
does not need the pool. rakupp already has the form of it that fits: packed
arrays.

## What was measured

[tools/value-pool-probe.cpp](../../../tools/value-pool-probe.cpp), a C++ probe
on the real 32-byte `rakupp::Value`. **It prices the representation. It is not
an engine measurement, nothing in the engine is built this way, and none of
these numbers belongs in BENCHMARKS.md.** Converting the engine was not
attempted: every array, hash entry and frame slot holds a `Value` by value.

```bash
c++ -std=c++20 -O2 -DNDEBUG -Isrc -Iinclude tools/value-pool-probe.cpp \
    build/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props,stubs}.a \
    -liconv -o /tmp/value-pool-probe && /tmp/value-pool-probe
```

Three shapes of the same data:

- **inline:** a `Value` where it is used, in a `ValueList` or a variable. What
  rakupp does.
- **pool:** a 32-bit handle to a slot in a pool of `Value`s. Address space for
  2^26 slots is reserved up front. A slot has a count, so a copy of a handle
  is a count and the last drop frees the slot. Counts stay, as everywhere in
  this work.
- **pool+imm:** the same, and a handle with its low bit set *is* a 31-bit Int,
  with no slot and no count. This is the idea at its best.

Each pooled shape was run with a plain count and with an atomic one. A
threaded engine needs the atomic one; the plain one is the ceiling.

The pool was run **fresh** (slots handed out in address order, a program's
first moments) and **aged** (two million slots taken and given back in random
order first, so the free list visits them at random).

Darwin 25.6 / arm64, Apple clang, load average 2–3, best of 7 inside one
binary. Times are against inline: under 1 is faster.

## The numbers

A million elements, or twenty million turns of a loop. Atomic counts.

| | pool, fresh | pool, aged | pool+imm, fresh | pool+imm, aged |
|---|---:|---:|---:|---:|
| build an array of Ints | 0.83x | **2.47x** | 0.50x | 0.43x |
| copy that array | 1.65x | 1.90x | 0.31x | 0.29x |
| read it, in order | 1.16x | **6.36x** | 0.71x | 0.67x |
| read it, shuffled | 5.46x | 6.26x | 0.74x | 0.67x |
| sort it | 0.82x | 0.95x | 0.59x | 0.57x |
| `$x = $x + 1`, Int | 1.07x | 1.09x | 1.87x | 1.70x |
| the same, `$x` also held elsewhere | 2.68x | 2.57x | 0.95x | 0.89x |
| `$c = $a` or `$b`, Int | 1.39x | 1.38x | 0.35x | 0.36x |
| build an array of Nums | 0.89x | **2.75x** | 0.87x | **2.95x** |
| read it, in order | 1.01x | **4.16x** | 1.00x | **4.06x** |
| sort it | 0.86x | 0.98x | 0.87x | 1.00x |
| `$x = $x + 1`, Num | 1.92x | 1.90x | 1.61x | 1.59x |
| the same, `$x` also held elsewhere | 2.99x | 2.99x | 3.45x | 3.41x |
| build an array of short Strs | 1.00x | **2.49x** | 1.00x | **2.48x** |
| read it, in order | 1.07x | **5.80x** | 1.06x | **5.87x** |
| a million references to 100 values | 0.41–0.66x | 0.37–0.66x | 0.42–0.66x | 0.38–0.67x |

Bytes an element of a million-element array costs:

| the array holds | inline | pool | pool+imm |
|---|---:|---:|---:|
| distinct small Ints | 32 | 40 | **4** |
| distinct Nums or Strs | 32 | 40 | 40 |
| a million references to 100 values | 32 | 4 | 4 |

A pooled value is its 4-byte handle, its 32-byte slot and a 4-byte count.

## What the numbers say

**1. A pool of every value is larger and slower.** 40 bytes an element where
inline is 32. Once the pool has aged, building is 2.5x slower and reading in
order is 4x to 6x slower. An inline array's values are in the array, so
walking it is one stream; a pooled array's values are wherever the free list
put them. Fresh, the pool keeps up, and no program stays fresh.

**2. Arithmetic pays for a slot.** `$x = $x + 1` on a Num is 1.9x slower even
when the slot can be written in place, and 3x when the old value is also held
elsewhere, since each result then takes a slot and gives one back. Inline, a
result is 32 bytes written where the variable is.

**3. What the pool is good at is small.** Sorting moves 4 bytes where inline
moves 32, and that is worth 5% to 18%, because the comparison still has to
reach the value. Copying an array is faster only with a plain count; with the
atomic count the engine needs, it is 1.6x to 1.9x slower, where an inline Int
copies with no count at all.

**4. Sharing is real, and rakupp has it where it matters.** A million
references to a hundred values is 4 bytes each and builds 1.5x to 2.7x faster.
That is what a counted body already gives a string, an array, a hash or an
object. The values that are *not* shared today are the ones that fit in the
`Value`: Ints, Nums, short strings. Sharing those saves nothing unless they
repeat, and costs a count on every copy.

**5. The whole win is the immediate Int, and it is not the pool's.** With
small Ints in the handle, an Int array is 4 bytes an element, builds in half
the time, copies in a third, sorts in 0.57x. Every one of those rows is a row
where the pool was not used. The Num and Str rows of the same shape, which do
use it, are the pool's rows again: 2.5x to 3x on an aged build, 4x to 6x on a
read.

Its one cost is the scalar loop: `$x = $x + 1` at 1.7x to 1.9x, for the tag,
the untag and the range check on each turn (1.3 ns a turn against 0.7).

**6. A 16-bit index** reaches 65,535 values, in the whole program. It was not
measured.

## What the probe does not see

Each of these makes the engine's case worse than the probe's, not better.

- **A `Value` is also its container.** `readonly`, `itemized`, a native width
  and `ofType` are on the Value, so `$a = $b` cannot share a slot: it must
  copy into `$a`'s own. The probe's `$c = $a` row shares, which is why it
  reads as a win.
- **Threads.** The probe's free list is one thread's. A real one is
  `BodyPool`'s, with lists per thread and a lock behind them, and a value made
  on one thread and dropped on another.
- **Every read is a load of the pool's base and an add,** as the 40-byte step
  measured for payloads: +2.9% instructions on the call kernels, for the
  payload and the cold block alone.
- **Temporaries.** An operator's result, an argument, a return value: each
  would take a slot or need a second, unpooled form of value beside the
  pooled one.

## Verdict

Not pursued. The pool makes values larger, scatters them, and puts a slot
allocation where an inline write was.

What the experiment does support is the compact array of plain numbers, and
rakupp has one: a packed array (`PK::Packed`, PACKED-ARRAY-PLAN) holds a
native int or num array, and an untyped Array of Nums, as machine words, 8
bytes an element, with none of a handle's indirection. If element size is the
goal, widening what packs itself is the direction this data points in: an
untyped Array of Nums does today (SELFPACK-EXPERIMENT), and an untyped Array
of plain Ints is the obvious next question. That is a separate piece of work
and was not started here. (It was done the same day for the interpreter:
[SELFPACK-EXPERIMENT.md](../experiments/SELFPACK-EXPERIMENT.md), *Ints*.)

`Value` stays inline at 32 bytes.
