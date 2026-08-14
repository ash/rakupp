# Infinity: what an endless list can still answer

`1..Inf` has no last element, so anything that would have to walk to the end of
it has to do something other than walk. A fold like `[+]` is written to consume
its list element by element, which for an endless one simply does not return.
Raku++ walked a bounded prefix instead — and that produced the bug this page
exists because of ([#19](https://github.com/ash/rakupp/issues/19)):

```
$ rakupp -e 'say [+] 1..Inf'
50005000
```

That is the sum of `1..10000`, the prefix `Value::flatten()` caps an endless
range at. It is a correct answer to a question nobody asked, and nothing in the
output said so. A wrong number is worse than an error, and — for the cases below
— worse than the right one, which was available all along.

## The rule

> **Answer with the limit of the partial folds when the range's bounds fix it.
> Otherwise say it cannot be done. Never fold a prefix and present it as the
> whole.**

A fold that never terminates has no *value*, but its partial results often have
a *limit*, and that limit follows from the endpoints without touching a single
element. `[+] 1..Inf` runs 1, 3, 6, 10, … — unbounded, so `Inf`. `[*] ^Inf`
runs 0, 0, 0, … — every partial is 0 the moment the walk crosses zero, so `0`.
`[~] 1..Inf` runs `"1"`, `"12"`, `"123"`, … — the strings grow without ever
approaching a string, so there is nothing to name and it throws.

Where the table below says Rakudo does not terminate, the Raku++ answer is a
deliberate divergence: nothing is being copied from another implementation,
because an expression that does not return has no observable value to copy.

## What each operator answers

| Expression | Before | Now | Rakudo |
|---|---|---|---|
| `[+] 1..Inf`, `[+] 1..*` | `50005000` | `Inf` | does not terminate |
| `[*] 1..Inf` | all of `10000!`, after ~40 s | `Inf` | does not terminate |
| `[*] ^Inf`, `[*] -3..Inf` | `0` | `0` | does not terminate |
| `[*] -0.5..Inf` | `0` | `-Inf` | does not terminate |
| `[-] 1..Inf` | `-50004998` | `-Inf` | does not terminate |
| `[min] 1..Inf` / `[max] 1..Inf` | `1` / `10000` | `1` / `Inf` | does not terminate |
| `[~] 1..Inf` | a 38,894-character string | `X::Cannot::Lazy` | does not terminate |
| `(1..Inf).reduce(&[+])` | `50005000` | `Inf` | does not terminate |
| `(1..Inf).sum` / `sum(1..Inf)` | threw / `50005000` | `Inf` | `Inf` |
| `(-Inf..0).sum` | `-92233720368547708085000` | `-Inf` | `-Inf` |
| `(-Inf..Inf).sum` | threw | `NaN` | `NaN` |
| `(1..10**100).sum` / `sum(…)` | threw / `50005000` | the exact Gauss sum | same |
| `[+]` over an endless **lazy** list | `0` or a prefix sum | `X::Cannot::Lazy` | does not terminate |
| `(1..*).gist` / `.raku` | `1..9223372036854775807` | `1..Inf` | `1..Inf` |
| `(1..*).Str` | `1..9223372036854775807` | `1..*` | `1..*` |
| `(^Inf).gist` | `^9223372036854775807` | `0..^Inf` | `0..^Inf` |
| `(-Inf..0).Str` | `-9223372036854775808..0` | `*..0` | `*..0` |

The reasoning behind the four arithmetic cases:

- **`+`** — the arithmetic series. `Inf` when the range runs up, `-Inf` when it
  runs down, and `NaN` for `-Inf..Inf`, where the two ends cancel. These are
  Rakudo's own answers for `.sum`, which is where the value comes from.
- **`*`** — zero is absorbing. If stepping by 1 from the low endpoint lands
  exactly on 0, every partial product from there on is 0, and so is the answer
  (`[*] -3..Inf` is `0`; `[*] -2.5..Inf` is not, because that walk goes
  `-2.5, -1.5, -0.5, 0.5, …` and never touches zero). Otherwise the magnitudes
  grow without bound and only the sign is left to settle: a range is bounded
  below, so it has finitely many negative elements, and their count decides it —
  three for `-2.5..Inf`, hence `-Inf`. (The two `0` rows in the table were right
  before the change as well, but by accident: zero happened to be inside the
  10,000-element prefix.)
- **`-`** — the first element minus a tail that grows without bound: `-Inf`.
- **`min` / `max`** — the partial minima settle on the low bound immediately;
  the partial maxima chase the high one. Both are the range's own endpoints, the
  same values `.min` and `.max` report.

Everything else refuses. `~` has no limit in the string domain; a user-supplied
operator has no known one at all; and for an endless **lazy** list (`1, 2, 4 ...
*`, or a `.map` over an endless range) even `+` refuses, because a lazy list's
elements do not follow from any bounds — there is nothing to compute the limit
from.

```
$ rakupp -e 'say [+] (1, 2, 3 ... *)'
Cannot reduce a lazy list
```

## Which doors this covers

The answer must not depend on how the reduce was spelled, so every entry point
asks the same question first: the `[op]` metaoperator, `prefix:<[op]>(…)`, an
`&prefix:<[op]>` reference, `.reduce`, and `rtReduce` — the one the `--exe`
native code generator emits, so a compiled binary agrees with the interpreter.

Alongside them, `.sum` on a Range and the `sum` builtin answer from the bounds
rather than from `toList`, which would have handed back the same truncated
prefix.

## Merely lazy is not endless

A lazy list holds only the prefix something has already pulled from it, so
folding what happens to be cached is its own way of answering a different
question — `[+] (1..Inf).grep(* %% 2)` used to be `0`, the sum of nothing. A
lazy operand is therefore **drained** before it is folded, and refused only if
it will not drain.

Whether it drains cannot be decided up front. A `.map` over an endless source is
endless, always. A `.grep` over one may still end — Roast's own
`S32-list/grep.t` has

```raku
(^Inf).grep({ last if $_ > 5; True }).eager.join   # 012345
```

— so the grep view is not marked endless when it is built. Instead, if it ever
runs into an endless source's materialization ceiling, it marks itself endless
at that point, and a reduce over it refuses from then on rather than folding the
million elements it managed to pull.

## The other thing the sentinel means

A Range keeps its endpoints in two `long long`s, and an endless one parks
`±LLONG_MAX` there. So does a range whose endpoint is merely too big for a
`long long`:

```raku
say (1..10**100).sum
```

Both look identical in `rFrom`/`rTo`, which is why that sum came out wrong too.
The written endpoint decides: a big-Int bound means the range is finite, just
very long, and it sums by Gauss — `count × (lo + hi) / 2` — instead of by
walking. Ordinary integer ranges take the same path, so `(1..10).sum` is still
`55`, an `Int`. That is Roast's `S03-operators/range-int.t`, which now passes
507 of 507.

## What still walks a prefix

- `[\+] 1..Inf` — the triangular (scan) form still yields the 10,000-element
  prefix rather than a lazy sequence of partial sums. It is a list-producing
  operation, not a fold, and it is left alone deliberately.
- `[+] (1..Inf, 5).flat` — `.flat` materializes the prefix and does not mark its
  result endless, so the reduce never sees that anything ran away.
- `Value::flatten()` itself still caps at 10,000 elements. It is the backstop for
  every eager consumer that has no better answer, and this page is about the ones
  that do.
