# Performance hints (`--hints`)

`rakupp --hints` runs a program as usual and, on stderr, points out constructs
that cost far more than they look, with what to write instead. The same switch
is `RAKUPP_HINTS=1` in the environment.

```
rakupp --hints program.raku
RAKUPP_HINTS=1 rakupp program.raku
```

- **It is off by default.** Rakudo prints nothing in these places, and what a
  program writes to stderr is part of what it does. Hints are something you
  ask for.
- **It changes nothing a program computes.** Stdout, results and exit status
  are the same with and without it; only stderr gains the hint lines.
- **Each hint is printed once per source line,** however many times the line
  runs. In the REPL every input counts afresh, so repeating an input repeats
  its hint.
- **`--hints` sets `RAKUPP_HINTS=1`** in the process environment, as `--color`
  sets `RAKUPP_COLOR`, so a child process started with `run` or
  `Proc::Async` hears it too.

A hint is a single line:

```
hint: line 1: an exact Rat raised to 10000: its numerator and denominator run to about 40000 digits each, and the result becomes a Num anyway; write the base with a Num (1e0 instead of 1) to compute in Num directly
```

There is one hint so far, described below. The static counterpart is the
`exact-power` rule of `--lint` ([LINT.md](LINT.md)), which flags the same
shape without running the program.

---

## An exact Rat power that becomes a Num

In Raku, `1/$n`, `0.9` and `1.0005` are exact rationals (`Rat`), not
floating-point numbers. Raising one to a large integer power therefore
computes the result exactly: `(1 + 1/$n) ** $n` builds (n+1)ⁿ as its
numerator and nⁿ as its denominator. A `Rat`'s denominator must fit in 64
bits, so once it does not, the exact result is converted to a `Num` (a
double). The program has then spent its time building two enormous integers
to hand back one floating-point number.

```raku
for ^10 -> $p { my $n = 10 ** $p; my $e = (1 + 1/$n) ** $n; say "$n\t$e" }
```

Each step here is roughly ten times the work of the one before. On the
machine this was written on, p = 5 takes several seconds in Raku++ and p = 7
or more takes too long to wait for, in Rakudo as well.

### When it fires

The hint fires when all four of these hold at the moment the power is
computed:

1. **the base is a `Rat`** (not an `Int`, a `Num` or a `FatRat`);
2. **the exponent is an integer;**
3. **the result's denominator is sure to pass 64 bits,** so the exact result
   will become a `Num`;
4. **the exact numerator or denominator would exceed about 10,000 digits.**

Below that size the exact computation takes a few milliseconds and is not
worth mentioning.

These are real runs with `--hints`:

| Code | Result | Hint |
|---|---|---|
| `my $n = 10**4; (1 + 1/$n) ** $n` | `2.718145926825225` | fires: about 40,000 digits each |
| `1.0005 ** 10000` (compound interest) | `148.22782029161004` | fires: about 33,000 digits |
| `0.9 ** 20000` | `0` | fires: about 20,000 digits |
| `my $rolls = 50000; (5/6) ** $rolls` (a probability) | `0` | fires: about 39,000 digits |
| `(1/3) ** 50` | `1.3929555690985384e-24` | no: the parts are 24 digits, cheap |
| `0.5 ** 30000` | `0` | no: about 9,000 digits, under the threshold |
| `1.5 ** 40` | `11057332.32094001214227` | no: the denominator is 2⁴⁰, so the result stays an exact `Rat` |
| `(1/3) ** -20000` | an exact `Rat` | no: the result is 3²⁰⁰⁰⁰/1, which stays exact |
| `2 ** 100000` | a 30,103-digit `Int` | no: an `Int` result is exact and nothing is thrown away |
| `FatRat.new(1,3) ** 20000` | a `FatRat` | no: a `FatRat` never becomes a `Num` |
| `(1 + 1e0/$n) ** $n` | `2.7181459268249255` | no: this is already `Num` arithmetic |

### What to do about it

**If a `Num` is what you want,** write the base as a `Num`. `1e0` is the `Num`
spelling of 1 (`1` is an `Int`, `1.0` a `Rat`), and one `Num` operand makes
the whole expression `Num` arithmetic:

```raku
for ^10 -> $p { my $n = 10 ** $p; my $e = (1 + 1e0/$n) ** $n; say "$n\t$e" }
```

That runs all ten iterations in milliseconds. `.Num` works too: `(1 + 1/$n).Num ** $n`.

**But the answers are different ones.** A double keeps about 16 significant
digits, so `1 + 1e-9` loses most of the `1e-9`, and raising it to the
billionth power magnifies the loss. Printing `$e - e` for each row shows it:

| n | exact `Rat` version | `1e0` version |
|---|---|---|
| 10⁵ | `-1.3591284555580785e-05` | `-1.359126674760347e-05` |
| 10⁶ | (too slow to wait for) | `-1.359363291708604e-06` |
| 10⁸ | | `-3.011168736577474e-08` |
| 10⁹ | | `+2.2355251516614771e-07` |

The error stops shrinking after 10⁷ and changes sign at 10⁹. If the program
exists to show `(1 + 1/n)ⁿ` converging to e, the exact version is the honest
one; if it exists to show floating-point error, the `1e0` version shows it
well.

**If the exact value is what you want,** declare it with `FatRat`, whose
denominator may grow without limit. It will still be slow, because the work is
real.

**A declared `Num` variable does not help on its own.** `my Num $e = (1 +
1/$n) ** $n` is a type *constraint*: on the first iteration the right side is
the `Rat` `2.0`, and the assignment dies with `X::TypeCheck::Assignment`, as
it does in Rakudo. `my Num() $e` (a coercion type) accepts it, but it converts
the result only after the exact power has been computed, so it is just as
slow.

### How long the exact route is allowed to run

Raku++ gives every power a budget of work, roughly five seconds. When a power
whose result must become a `Num` exhausts the budget, the engine computes that
`Num` directly instead of finishing the exact integers. That is why in Raku++
the large cases above take about five seconds each rather than minutes. The
hint is printed before any of this, as soon as the size is known.
