# The FORTRAN-LESSONS kernels against gfortran, with self-packing arrays

*2026-11-07, on an Apple M4, macOS 26.6.2, load average 3–4. Raku++
5.6.3-66 (self-packing arrays, merged in 402828e2) with the undo-log change
below. gfortran 16.2.0 is from the native arm64 Homebrew in `/opt/homebrew`:
the `brew` on this machine's PATH is the Intel one in `/usr/local`, under
Rosetta, and its gfortran would not be a fair comparison. C is Apple clang 21.*

*Each program builds its arrays once, then times five passes of its kernel. A
figure is the fastest pass of two runs. Every Raku and C checksum equals
gfortran's to the last digit, except where `-Ofast` reorders the dot product's
sum. The sources are in [`fortran-packing-2026-11/`](fortran-packing-2026-11/).*

---

## The short answer

- **Compiled with `--exe -O`, untyped Raku now runs sum and dot at gfortran's
  speed.** It takes 5.2 and 5.5 ms against gfortran's 6.0 and 6.3, level with
  C.
- **Matmul is at 3.3× gfortran's loop and axpy at 5.4×.**
- **Without packing, the same untyped programs take 23–136× gfortran's time.**
  Interpreted with packing they take 4–31×.
- **gfortran's `matmul` intrinsic is a class of its own:** 0.34 ms, 5.5× its
  own loop and 18× Raku's.
- **The store undo log now copies the arrays at 1/16 of their size**, where it
  logged every store before. Compiled axpy went 26.4 → 13.8 ms; the kernels
  that store nothing are unchanged.

The FORTRAN-LESSONS note (2026-10-15) had no Fortran compiler to measure
against. These are the same kernels: sum, dot and `c = 2a + b` over ten
million doubles, and a 200×200 naive matmul.

## The numbers

ms for one pass of the kernel:

| | sum | dot | axpy | matmul |
|---|--:|--:|--:|--:|
| Fortran, gfortran `-O3`, loops | 5.97 | 6.34 | 2.55 | 1.86 |
| Fortran, gfortran `-O3`, intrinsic | 5.95 | 6.34 | 2.62 | **0.34** |
| Fortran, gfortran `-Ofast`, loops | 3.06 | 4.28 ¹ | 2.54 | 2.24 |
| C, clang `-O3` | 5.15 | 5.51 | 2.56 | 2.73 |
| **Raku untyped, `--exe -O`, packing on** | **5.22** | **5.52** | **13.76** | **6.18** |
| Raku untyped, `--exe -O`, packing off | 136.6 | 248.8 | 326.7 | 252.2 |
| Raku native types, `--exe -O` | 5.95 | 6.75 | 15.37 | 6.19 |
| Raku untyped, interpreted, packing on | 25.0 | 48.0 | 71.4 | 57.9 |
| Raku untyped, interpreted, packing off | 707 | 1,026 | 2,303 | 1,643 |
| Raku native types, interpreted | 37.4 | 59.3 | 83.8 | 85.9 |

¹ `-Ofast` reassociates the sum, and the checksum differs in its last digits.

- **sum and dot** are a chain of dependent adds when nothing may reorder them:
  each add waits for the one before. gfortran, clang and the Raku lane all run
  that chain, at about 0.5–0.6 ns an element. Only `-Ofast` vectorizes it.
- **axpy** has no chain, so gfortran and clang vectorize it at `-O3`. The Raku
  lane checks three subscripts against their lengths and notes each store for
  a bail, so its loop does not vectorize. Hoisting those checks out of the
  loop, where the range and the lengths are known at entry, is the next step.
- **matmul**: gfortran's own loop is 1.9 ms and its blocked intrinsic 0.34 ms.
- **Native types** (`num @a`, `int` loop counters, C-style `loop`) compile to
  about what the untyped packed form does. Interpreted they are slower: 37–86
  ms against 25–71. That difference was not looked into here.

## The undo log

A compiled loop that bails reruns from its start on the boxed path, so it
first puts back every array element it stored. It logged each store until the
log held more entries than the arrays held words, and only then switched to a
copy of the arrays. A loop that stores each element of a big array once never
reached that point. axpy logged all ten million stores, at 16 bytes each.

The log now switches to a copy at 1/16 of the arrays' size (`RtWordUndo` in
`src/Interpreter.h`). The interpreter's loop kernels use the same log.

Measured on a 10-million-element store loop, the best of 10 passes in one
process:

| when the log switches to a copy | ms a pass |
|---|--:|
| when it outgrows the arrays (before) | 32–37 |
| at half the arrays' size | 30–32 |
| at 1/16 (now) | 16–22 |
| at the first store | 14–19 |

At the first store would copy a whole array even for a loop that stores one
element of it. At 1/16 a loop that stores a few elements still only logs.

In the kernels above, before → after:

- axpy, untyped `--exe -O`: 26.4 → 13.8 ms.
- axpy, native `--exe -O`: 27.3 → 15.4 ms.
- axpy, untyped interpreted: 82.3 → 71.4 ms.
- Every other row: within 5%.

## Reproduce

```bash
docs/dev/findings/fortran-packing-2026-11/run.sh            # every row above, with build/rakupp
docs/dev/findings/fortran-packing-2026-11/run.sh OTHER/rakupp
GFORTRAN=/path/to/gfortran docs/dev/findings/fortran-packing-2026-11/run.sh
```

`run.sh` builds the Fortran and C programs, compiles the two Raku programs
with `--exe -O`, and checks each checksum against gfortran's. It takes about
five minutes, most of it spent building the boxed ten-million-element lists of
the packing-off rows.
