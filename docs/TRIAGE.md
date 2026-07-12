# Triage — behavioural quirks to cover later

Interpreter gaps found outside the Roast harness (see [ROAST-GAPS.md](ROAST-GAPS.md)
for the Roast-derived classification). These surfaced while using rakupp to write
real programs, so they aren't yet mapped to specific failing test files — but each
is a real behavioural gap with a minimal repro. All verified on HEAD.

## From writing the `examples/` programs (2026-07-11)

| # | Symptom (repro) | Correct behaviour | Workaround used |
|---|---|---|---|
| 1 | `my @a = 1,2,3; my @b = @a; @b.push(4)` mutates **`@a`** too | `=` should copy the array, not alias it | `my @b = @a.clone` |
| 2 | `(1..20).grep(5 <= * <= 10)` returns **all** 20 | chained compare with `*` should curry one predicate | `.grep({ 5 <= $_ <= 10 })` |
| 3 | `gather { for 1..* -> $n { take $n } }` yields **`()`** | infinite `for` over a Range inside `gather` should stream | `loop` with a manual counter |
| 4 | `.map(-> $n { start {…} })` may never launch the threads (lazy) | mapping that spawns `start` should still run | append `.eager` |
| 5 | a `sub` in a `class {}` body → `Undefined routine` when called unqualified from a method | lexical subs should resolve from methods | define the helper at file scope |
| 6 | `"$x-1"` interpolates as the subtraction `$x - 1` | `-1` after `$x` is literal text | `"{$x}-1"` |
| 7 | `gcd(a, b)` / `lcm(a, b)` (call form) return empty | should equal the infix result | infix `a gcd b`, or `[gcd]` |
| 8 | `[|@a, 3]` builds `[[1 2] 3]` — the slip doesn't flatten in a literal | `|@a` should flatten into the array | `.clone` + `.push`, or build then flatten |
| 9 | `rule TOP { \d+ }` fails to parse `"42 "` (trailing space) | `TOP` should allow trailing whitespace like Rakudo | `.trim` the input first |
| 10 | `~(355/113)` → `3.141593` (truncated to ~6 places) | `Rat.Str` should not lose precision | `.nude` / `.numerator` / `.denominator` |
| 11 | `sprintf("%{$w}d", 7)` → `"d"` — `%{…}` read as hash interpolation | should be a dynamic field-width format | `sprintf('%*d', $w, 7)` |
| 12 | `constant N = 8; N` in term position → `Undefined routine 'N'` | a sigilless constant should be usable bare | `constant \N = 8` |

## From the native-`--exe` / parallel-harness round (2026-07-12)

| # | Symptom (repro) | Correct behaviour | Workaround used |
|---|---|---|---|
| 13 | `my @o = <a b>.map({ [1,2,3] })` → **6** elements — assignment deep-flattens the map result, even itemized `[…]` elements (`$(…)` doesn't protect them either) | 2 elements, each an itemized array (Rakudo: 2) | `@o.push(f($_)) for <a b>` — `push` keeps each tuple one item |
| 14 | `next` inside `.map({ next if …; $_ })` escapes to the **enclosing loop** — `for 1..3 { @r.push: (1..5).map({ next if $_ == 2; $_ }).elems }` leaves `@r` **empty** | `next` skips the map element; `@r` = `[4 4 4]` | `.grep` the elements away instead of `next` |
| 15 | `return` inside `CATCH` yields **Nil**: `sub f { die "x"; CATCH { default { return 42 } } }; f()` → Nil | returns 42 (Rakudo) | set a result variable in CATCH, return after |
| 16 | `1, 4, 9 ... 100` silently guesses a step from the last difference (21 elements, ends at 99) | Rakudo dies: "Unable to deduce arithmetic or geometric sequence" | give the generator explicitly: `1, 4, 9, { … } ... 100` |
