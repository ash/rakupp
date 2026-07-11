# Examples

Self-contained Raku programs that run under `rakupp`. Each is a single file of
roughly the same size, with a header comment explaining what it does and which
language features it leans on. None of them need arguments, a network
connection, or any setup — run one with:

```sh
./build/rakupp examples/mandel.raku
```

(from the repository root, after building — see the top-level [README](../README.md)).
Every output shown below is what the program actually prints.

| File | What it shows | Feature focus |
|------|---------------|---------------|
| **Graphics & fractals** | | |
| [`mandel.raku`](mandel.raku) | The Mandelbrot set as ASCII | loops, complex-plane arithmetic |
| [`sierpinski.raku`](sierpinski.raku) | The Sierpinski triangle | Rule 90 cellular automaton, bit ops |
| [`life.raku`](life.raku) | Conway's Game of Life on a torus | 2-D arrays, `given`/`when` |
| **Grammars & parsing** | | |
| [`calculator.raku`](calculator.raku) | Evaluates arithmetic expressions | grammars + an actions class |
| [`json.raku`](json.raku) | A JSON parser → native data structure | grammars + actions |
| [`rpn.raku`](rpn.raku) | A postfix (RPN) calculator | stack machine, exact `Rat` |
| **Numbers & maths** | | |
| [`primes.raku`](primes.raku) | A short number-theory tour | lazy sequences, big `Int` |
| [`rationals.raku`](rationals.raku) | Exact rational arithmetic | the `Rat` tower, continued fractions |
| [`fibonacci.raku`](fibonacci.raku) | Fibonacci three ways, cross-checked | lazy sequences, bignums |
| [`factorize.raku`](factorize.raku) | Prime factorization, gcd/lcm | number theory, reductions |
| [`pascal.raku`](pascal.raku) | Pascal's triangle & what it encodes | combinatorics, `Z+`, formatting |
| [`matrix.raku`](matrix.raku) | Linear algebra by hand | nested arrays, reductions |
| **Algorithms** | | |
| [`quicksort.raku`](quicksort.raku) | Quicksort, merge sort, one-liner | recursion, `.grep`, multi-dispatch |
| [`hanoi.raku`](hanoi.raku) | Towers of Hanoi | recursion, formatted output |
| [`nqueens.raku`](nqueens.raku) | The 8-Queens puzzle | backtracking |
| [`brainfuck.raku`](brainfuck.raku) | A tiny Brainfuck interpreter | tape/pointer loop, dispatch |
| **Text & strings** | | |
| [`anagrams.raku`](anagrams.raku) | Groups words into anagram classes | hashes, `.classify` |
| [`wordcount.raku`](wordcount.raku) | Word-frequency bar chart | `Bag`, regex, sort by value |
| [`cipher.raku`](cipher.raku) | ROT13 / Caesar / Vigenère | `.trans`, modular arithmetic |
| [`roman.raku`](roman.raku) | Roman numerals, both directions | `given`/`when`, table-driven logic |
| **Concurrency & I/O** | | |
| [`parallel.raku`](parallel.raku) | Prime counting across threads | `start`/`await`, `Channel` |
| [`sleep-sort.raku`](sleep-sort.raku) | The sleep-sort joke algorithm | `start`, `Channel`, `sleep` |
| [`echo-server.raku`](echo-server.raku) | A TCP echo server and its client | `IO::Socket::INET` |
| **Curiosities** | | |
| [`quine.raku`](quine.raku) | A program that prints its own source | string self-reference |

---

## Graphics & fractals

### `mandel.raku`

The classic ASCII Mandelbrot set, a faithful modernisation of the ~2004 Parrot
`examples/mandel.p6`. The header explains the two language changes since then
that the original tripped over; [docs/MANDEL.md](../docs/MANDEL.md) has the full
story. It is the smallest program here that exercises tight numeric loops.

### `sierpinski.raku`

The Sierpinski triangle, drawn by the simplest fractal-making cellular
automaton — Wolfram's Rule 90. Each cell in the next row is the exclusive-or
(`+^`) of its two upper neighbours; starting from a single live cell, the live
cells trace out the Sierpinski gasket (equivalently, Pascal's triangle mod 2).

```
               #
              # #
             #   #
            # # # #
           #       #
          # #     # #
         #   #   #   #
        # # # # # # # #
```

### `life.raku`

Conway's Game of Life on a 30×16 grid that wraps at the edges (a torus). The
starting field is a fresh random "soup" every run — each cell alive with
probability 0.28 — so no two runs evolve the same way; the frame header tracks
the live population as it settles into still lifes and blinkers. It animates in
place in the terminal (a `\e[H` cursor-home before each frame instead of
scrolling); pass `--delay=0.2` to slow it down, `--delay=0` to run flat out.

```
generation 0 (142 alive):
....#..#.#.....#....##....#..#
......#.#...##............#.#.
.#..#.......#...#.##.##......#
...
generation 7 (121 alive):
...
```

## Grammars & parsing

### `calculator.raku`

A four-function calculator whose parser *is* a grammar: precedence and
associativity come from the grammar's structure, and an actions class turns each
match into a number as it reduces.

```
1 + 2 * 3              = 7
(1 + 2) * 3            = 9
10 / 4                 = 2.5
2 * (3 + 4) - 5 / 2    = 11.5
-3 + 4 * -2            = -11
```

Because arithmetic is done in `Rat`, `10 / 4` is the exact `5/2`, not a rounded
float.

### `json.raku`

A JSON parser whose grammar describes the format and whose actions class turns
each match into a native Raku value as the parse reduces: objects become a
`Hash`, arrays an `Array`, and scalars land as `Str` / `Int` / `Rat` / `Bool` /
`Any`. Integers stay `Int` while decimals become an exact `Rat` rather than a
float.

```
Parsed JSON into a live Raku structure:
active: True (Bool)
distance_au: 163.9 (Rat)
instruments:
  [0] ISS (Str)
  ...
distance stayed exact: 1639/10
```

### `rpn.raku`

A Reverse Polish Notation (postfix) calculator built as a stack machine, in
deliberate contrast to the grammar-based `calculator.raku`. It reads each token
left to right, pushes numbers onto an `Array` used as a stack, and on an operator
pops two operands and pushes the result. Arithmetic stays exact, so `1 3 /`
yields a true `1/3`.

```
3 4 + 5 *              = 35
15 7 1 1 + - / 3 *     = 9
1 3 /                  = 1/3
100 5 / 3 -            = 17
```

## Numbers & maths

### `primes.raku`

A tour through some number theory: a lazy Sieve-of-Eratosthenes sequence,
factorials and Mersenne primes computed in arbitrary precision (no 64-bit
ceiling), and Collatz stopping times.

```
First 20 primes:
(2 3 5 7 11 13 17 19 23 29 31 37 41 43 47 53 59 61 67 71)
...
Mersenne primes 2^p - 1 (p prime, p < 32):
  p=31  2^31-1 = 2147483647
```

### `rationals.raku`

Raku's decimal literals are exact rationals, not floating point, so `0.1 + 0.2`
is exactly `0.3`. This program shows that, then computes exact harmonic numbers
and reconstructs π's best rational approximations from its continued fraction.

```
  0.1 + 0.2 - 0.3  = 0
  (0.1 + 0.2).nude = 3/10
...
  355/113   = 3.1415929204  (error 2.67e-07)
```

### `fibonacci.raku`

Reaches the Fibonacci numbers three ways and checks they agree: the lazy
self-referential sequence `1, 1, * + * ... *`, a fast-doubling recursion that
jumps to F(n) in about log₂(n) big-integer steps, and full arbitrary-precision
printouts. Closes by watching consecutive ratios close in on the golden ratio.

```
Big Fibonacci numbers, in full:
  F(100) has 21 digits: 354224848179261915075
  F(500) has 105 digits: 139423224561697880139724382870407283950070256587697307264108962948325571622863290691557658876222521294125

Ratios F(n+1)/F(n) approach the golden ratio phi:
  F(40+1)/F(40) = 1.618033988750   (phi - ratio = 0.00e+00)
```

### `factorize.raku`

A small number-theory workshop: trial-division prime factorization rendered as
prime powers, the divisor count derived from the exponents, and gcd/lcm shown
three ways — the built-in infix `gcd` / `lcm` operators, their `[gcd]` / `[lcm]`
reductions across a list, and a hand-written Euclid that cross-checks the
built-in.

```
  360            = 2^3 * 3^2 * 5           24 divisors
  5040           = 2^4 * 3^2 * 5 * 7       60 divisors
  600851475143   = 71 * 839 * 1471 * 6857  16 divisors

  [gcd] 24 60 36 90 = 6
  [lcm] 24 60 36 90 = 360
```

### `pascal.raku`

Builds 12 rows of Pascal's triangle — each row grows from the one above by
adding neighbouring entries with the zip operator `Z+` — and prints it centred.
It then checks row 11 against the binomial coefficients `C(11, k)` computed
directly, and redraws the triangle keeping only the odd entries, which trace out
the Sierpinski fractal.

```
                   1
                 1   1
               1   2   1
             1   3   3   1
           1   4   6   4   1
...
Odd entries only (the Sierpinski triangle):
    #
   # #
  #   #
 # # # #
```

### `matrix.raku`

Basic linear algebra written out by hand on arrays-of-arrays — no library, just
nested loops and reductions. It multiplies a 2×3 by a 3×2 matrix (an inner `[+]`
reduces each row·column dot product), transposes a matrix, builds an identity,
confirms that `A × I == A`, and expands a 3×3 determinant by cofactors.

```
A * B (2x2):
    58  64
   139 154

A square matrix, times I, is itself: True

det of [[2,0,1],[3,5,4],[1,1,0]] = -10
```

## Algorithms

### `quicksort.raku`

The textbook recursive quicksort in idiomatic Raku: pick a pivot, `.grep` the
rest into the elements below and above it, recurse on each half, and glue the
pieces back. For contrast it also runs a merge sort and a one-line closure
version, and confirms all three agree with the built-in `.sort`.

```
Input:      [5 3 8 1 9 2 7 4 6 0]
quicksort:  [0 1 2 3 4 5 6 7 8 9]
merge-sort: [0 1 2 3 4 5 6 7 8 9]

quicksort matches .sort?  True
merge-sort matches .sort? True
```

### `hanoi.raku`

Towers of Hanoi, the canonical recursion demo: to move N disks, move the top
N-1 aside, move the biggest disk, then move the N-1 back on top. It prints the
full sequence of moves for a 4-disk stack and checks that the total matches the
provably-minimal 2^N - 1.

```
Towers of Hanoi with 4 disks:

  move  1: disk 1   A -> B
  move  2: disk 2   A -> C
  move  3: disk 1   B -> C
  ...
Solved in 15 moves.
Optimal? True
```

### `nqueens.raku`

The classic N-Queens puzzle solved by backtracking: place one queen per row and
extend a partial placement column by column, pruning any square that clashes on
a column or diagonal with a queen already down. For the 8×8 board it counts all
92 solutions and prints the first one it finds.

```
Solutions for N=8: 92

One solution:
Q . . . . . . .
. . . . Q . . .
. . . . . . . Q
. . . . . Q . .
. . Q . . . . .
. . . . . . Q .
. Q . . . . . .
. . . Q . . . .
```

### `brainfuck.raku`

A compact interpreter for Brainfuck's eight instructions, operating on a byte
tape with a single data pointer and matched-bracket loops. It runs the embedded
classic "Hello World!" program, building up the ASCII codes on the tape and
printing them.

```
Hello World!
```

## Text & strings

### `anagrams.raku`

Two words are anagrams when they share the same multiset of letters, so each
word is keyed by its letters sorted into order (`.comb.sort.join`) and
`.classify` drops matching words into the same bucket. The non-trivial groups
are printed largest-first, ties broken by key so the output never changes.

```
Anagram groups (largest first):

  opst       opts post pots spot stop tops
  eilnst     enlist listen silent tinsel
  eilv       evil live veil vile
  ...
Found 10 anagram classes among 31 words.
```

### `wordcount.raku`

Word-frequency analysis of an embedded paragraph (Sandburg's "Fog"): the text is
lower-cased, chopped into words with a regex, and tallied into a `Bag` — Raku's
built-in counting multiset. The ten most common words are drawn as an ASCII bar
chart, ties broken alphabetically so the ranking is stable.

```
Most frequent words:
  the        10  ##########
  fog         6  ######
  and         5  #####
  on          5  #####
  ...
```

### `cipher.raku`

Three classic substitution ciphers, each shown round-tripping a message back to
the original. ROT13 and the Caesar shift are one-liners built on `.trans`
(mapping character ranges onto rotated ranges); the Vigenère cipher drops down
to per-character `.ord`/`.chr` modular arithmetic because its key runs along the
text.

```
ROT13
  encrypt: Nggnpx ng Qnja!
  decrypt: Attack at Dawn!
  round-trips: True

Vigenere (key LEMON)
  encrypt: Lxfopv ef Rnhr!
  round-trips: True
```

### `roman.raku`

Roman numerals in both directions. The forward pass is table-driven: a
descending list of (value, symbol) pairs — subtractive forms included — is
greedily subtracted from the number. The reverse walks the letters with a
`given`/`when`. Round-tripping a spread of numbers proves the two conversions
agree.

```
  n     roman              back   ok
  ----  -----------------  -----  --
     4  IV                    4   True
  1984  MCMLXXXIV          1984   True
  3888  MMMDCCCLXXXVIII    3888   True
```

## Concurrency & I/O

### `parallel.raku`

Counts the primes below 100,000 by splitting the range across eight threads with
`start`, then `await`-ing and summing the results — and separately runs a
producer/consumer pipeline over a `Channel`. The work runs concurrently but the
merged output is always the same.

```
Primes below 100000, counted on 8 threads:
  [     2 ..  12499] -> 1492 primes
  ...
  total = 9592
```

### `sleep-sort.raku`

The famous sleep sort: launch one thread per number, have each sleep for a time
proportional to its value, and collect them as they wake. Small numbers wake
first, so the values come out sorted — the scheduler does the comparing. Useless
as a real sort, but a compact demonstration of `start`, `Channel`, and `await`.

```
in:  5 1 8 2 9 3 7 4 6
out: 1 2 3 4 5 6 7 8 9
```

### `echo-server.raku`

A TCP echo server and its client in one program, talking over the loopback
interface — real `IO::Socket::INET`, no internet required. The server runs on
its own thread and upper-cases whatever the client sends.

```
Client -> server -> client, over TCP on 127.0.0.1:
  sent 'hello'          got back HELLO
  sent 'raku sockets'   got back RAKU SOCKETS
  sent 'goodbye'        got back GOODBYE
Server stopped cleanly.
```

## Curiosities

### `quine.raku`

A quine: a program whose output is its own source code, exactly, without reading
any file. The entire source is held once in the string `$s`; `printf` feeds `$s`
back into itself, using `%c` (codepoint 39) for the quote marks that wrap it and
`%s` for the text. `diff <(./build/rakupp examples/quine.raku) examples/quine.raku`
shows no differences.

---

These are complete programs. `rakupp` can also compile one to a standalone
binary (`--bundle`, `--aot`, or native `--exe`); see the top-level
[README](../README.md) and [GUIDE.md](../GUIDE.md) for how that works and which
constructs the native path supports today. For a broader, snippet-sized tour of
the language — short `-e` one-liners rather than whole programs — see
[COOKBOOK.md](../COOKBOOK.md).
