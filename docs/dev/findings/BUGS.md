# Bugs and divergences found while writing the article series

Found on 17 July 2026 by cross-checking every article snippet under
`build/rakupp` and Rakudo (v2026.06) while drafting the
[raku-brochure](https://github.com/ash/raku-brochure) series. Each entry is
self-contained: reproduction, expected vs. actual, and where to look.

## 1. Quantified-capture arity — FIXED, committed (506ba3c)

A quantified subrule capture with exactly one match came back as a bare
`Match` (whose `.list` is empty and which has no `.map`); with zero matches it
was absent. Rakudo gives an `Array` in both cases (`[]` for zero).

```raku
grammar G { token TOP { <p>* 'x' } token p { 'q' } }
G.parse('qx')<p>.elems;   # was: no .list/.map path — now 1, as in Rakudo
G.parse('x')<p>.raku;     # was: Any — now [], as in Rakudo
```

**Fix (in the working tree, not yet committed):** `src/Regex.h`,
`src/Regex.cpp`, `src/Interpreter.cpp`. Quantified subrule capture keys are
collected at pattern-parse time (`Regex::collectListNames`, called from
`markListCap` in `parseQuant`), carried on `ParseNode`/`RxMatch`/`MemoEntry`
as a shared set (`listNames`), and honoured at Match-building time in all
three conversion sites in `Interpreter.cpp` (grammar `build`, plain-regex
`build`, substitution `build`). `?` correctly stays bare-or-Nil; direct
assignment captures (`$<c>=\w ** 3`) correctly stay bare.

Full Roast after the fix: **513 files fully pass (35.1%)**, **175,161 /
213,203 declared assertions (82.2%)** — no regressions (previously published:
501 / ~80%). All grammar showcases verified. The `as-list` workaround in
`showcase/json/json.raku` (lines 37–42) is now unnecessary and could be
retired.

**Remaining to do:** ~~commit the fix~~ (506ba3c); rebuild + redeploy Raku.js to
raku.online (a rebuilt, browser-verified `rakujs/playground/` already sits in
the working tree — the A3 article's grammar links depend on it).

## 2. Blob range subscripts return empty — FIXED 18 Jul

```raku
my $b = "abcd".encode('utf-8');
say $b[1 ..^ 3];   # rakupp: ()   — Rakudo: (98 99)
say $b[1 .. 2];    # rakupp: ()   — Rakudo: (98 99)
say $b[1];         # 98 on both (plain indexing works)
```

Array range slices work correctly, so compare the Array positional-subscript
path with the Blob/Buf one (likely `src/Builtins.cpp` or the subscript
handling in `src/Interpreter.cpp`). Also check list subscripts (`$b[0, 2]`)
and `$b[^2]`. Check Roast S32 buf tests, then run
`build/rakupp tools/run-roast.raku` on the relevant files.

Workaround used meanwhile: index one byte at a time
(`raku-brochure/tools/mklink.raku` does this — can be reverted).

**Fix:** the positional-subscript slice path in `src/Interpreter.cpp` treated a
Blob as a one-item Str list; Blob/Buf slices now index the bytes, and `*`-based
lengths (`$b[*-1]`) resolve against the byte count.

## 3. `rule` trailing sigspace does not match a newline after `\N*` — FIXED 18 Jul

```raku
grammar E1 { rule TOP { \w+ '=' \N* } }
say E1.parse("a = x").defined;    # True on both
say E1.parse("a = x\n").defined;  # rakupp: False — Rakudo: True
```

In Rakudo the rule's implicit `<.ws>` after the final atom consumes the
trailing `"\n"`. Likely area: how sigspace injects `<.ws>` around atoms in
`src/Regex.cpp` (`parseSeq`, the `sigspace_` branch) and whether a trailing
`<.ws>` is emitted after the last atom at all. Related failure: a token
calling `rule pair { $<key>=\w+ '=' $<value>=\N* }` via `<pair>*` fails to
parse `"host = x\n"` lines that Rakudo accepts. Check Roast S05-mass/rules.t.

**Fix:** `parseSeq` only injected `<.ws>` BETWEEN atoms; it now also emits a
trailing `<.ws>` when the sequence ends after whitespace (end of pattern, `|`,
`)`, `]`). The word-word zero-width rule is unaffected
(`'foobar' !~~ /:s foo bar/` still fails, `'foo bar'` matches).

## 4. Smartmatch against a lexical regex as a Callable — FIXED 18 Jul

```raku
my token key   { \w+ }
my token value { \S+ }
my regex pair  { <key> \s* '=' \s* <value> }
say 'port = 443' ~~ &pair;   # rakupp: no match — Rakudo: matches (｢port｣…)
```

Composing via `/ <pair> /` works on both and is the workaround. Low priority.

**Fix:** `my token/regex/rule name {…}` now also defines `&name` — a Callable
running the regex unanchored over its argument — and `~~ Callable` returns the
Match (with subcaptures) instead of a Bool when the callable yields one.

## 5. `is-prime` wrong on large Mersenne prime — FIXED 18 Jul (Miller-Rabin, int64 + BigInt)

```raku
say (2 ** 127 - 1).is-prime;   # rakupp: False — Rakudo: True (M127 is prime)
```

Found 17 July 2026 while drafting A2. Small inputs agree
(`(1..100).grep(*.is-prime)` is correct), so suspect the Miller–Rabin (or
trial-division cutoff) path for bignum arguments in `src/BigInt.cpp` /
wherever `is-prime` dispatches for `big` values. Check witnesses/rounds and
any int64 truncation of the base or modulus. Roast: S32-num/isprime.t.

**Fix:** it WAS trial division on a truncated `toInt()`. Replaced with
Miller-Rabin (witnesses 2..37 — deterministic below 3.3e24): `__int128` mulmod
for int64 inputs (BigInt fallback on MSVC), full BigInt modpow for big inputs.

## 5b. Hash variables with non-Latin names are not assignable — OPEN

```raku
my %цены; %цены<хлеб> = 3;      # rakupp: "Target is not assignable" — Rakudo: works
my %цены = (хлеб => 3);         # rakupp: "Target is not assignable" — Rakudo: works
my %h = хлеб => 3; say %h<хлеб>; # works on both (non-Latin KEYS are fine)
my $Δ = 42;                      # works on both (non-Latin SCALAR names are fine)
```

Found 18 July 2026 while testing Unicode-article examples. Verified: `@`-sigil
arrays with non-Latin names work (`my @данные = 1,2,3; say @данные[1]` → 2),
so the problem is specific to `%`-sigil variables with multi-byte identifiers —
likely `src/Lexer.cpp` identifier scanning or the assignable-container
resolution for hashes in `src/Interpreter.cpp`.

## 5c. Ideograph numerals parsed as literals shadow legal identifiers — OPEN

```raku
say ⅷ + 三;          # rakupp: 11 — Rakudo: compile error (三 is a letter)
my \三 = 5; say 三;   # rakupp: "Target is not assignable" — Rakudo: 5
```

Found 18 July 2026. Standard Raku accepts Nl/No numerals (`ⅷ`, `½`, `Ⅻ`) as
literals — both implementations agree there — but `三` is category Lo, i.e. an
identifier letter: Rakudo lets you declare `my \三 = 5`. rakupp's lexer eats
ideographs with a numeric value as number literals, which is a nice extension
but breaks legal programs that use them as identifiers. Either drop the
extension or only apply it where an identifier interpretation is impossible.

## 6. Numeric coercion of non-numeric strings silently returns 0 — OPEN, minor

```raku
say +'+';   # rakupp: 0e0 — Rakudo: Failure (dies if used)
```

Makes `+$x // $fallback` idioms behave differently (found via a `+$/ // ~$/`
actions idiom, 17 July 2026). Probably the Str→Num coercion path should
produce a Failure for unparsable input instead of 0e0.

## 7. Cosmetic output divergences — OPEN, minor

Noticed while making article outputs agree; each is a display difference,
not a semantics bug, but worth aligning with Rakudo eventually:

- ~~`(0.1 + 0.2).nude` prints `[3 10]`; Rakudo prints `(3 10)`.~~ FIXED 18 Jul.
- ~~`say 5 == 3|5|7` prints `True`; Rakudo prints the unthreaded junction
  `any(False, True, False)`. (`so`/`if` forms agree on both.)~~ FIXED 18 Jul —
  and it was not cosmetic after all: comparisons collapsed junctions EAGERLY.
  They now autothread into a preserved Junction (collapse only in boolean
  context, via Value::truthy/boolify); `~~` with a junction RHS keeps ACCEPTS'
  Bool; junction LHS `~~ /rx/` autothreads; any/all/one/none follow the
  one-arg rule (a later flattening duplicate registration shadowed the
  constructors); junction ARGUMENTS autothread plain calls recursively.
  S03-junctions: misc.t 105→129, associative.t 10/10, autothreading.t 3→5.
