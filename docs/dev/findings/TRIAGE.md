# Triage — behavioural quirks to cover later

Gaps found outside the Roast harness (see [ROAST-GAPS.md](ROAST-GAPS.md) for the
Roast-derived classification). These surfaced while using rakupp to write real
programs, so they aren't yet mapped to specific failing test files — but each is
a real behavioural gap with a minimal repro. All verified on HEAD. Most are
interpreter gaps; the dated TODO sections at the end record gaps in the native
`--exe` compiler, where the fix that landed was a fallback and the rest is still
to do.

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

## TODO — native `--exe` cannot dispatch most multi methods (2026-08-20)

Not a quirk with a workaround: a **compiler**-side gap, currently paid for with
a fallback rather than solved. Recorded here so the remaining half is not lost.

**What was wrong.** `Codegen::classRegister` emits a multi-method dispatcher
whose guard sees positional arity and nominal type and nothing else, yet it
decided every call anyway. Three things it cannot decide were being decided
wrongly:

- a REQUIRED named is invisible to the guard, so `multi method g(:$size!)`
  matched a call passing none and bound `$size` to `Any`;
- a `where` clause or a `:D`/`:U` smiley never enters the guard;
- a candidate declared in an ANCESTOR is unreachable, though in Rakudo a multi's
  candidate set spans the MRO (the interpreter defers up the chain — the
  `parentNext` branch in `Interpreter::invokeMethod`).

```raku
class P { multi method g(UInt:D $s = 1) { self.g(:size($s)) }
          multi method g(UInt:D :$size = 1) { !!! } }
class K is P { multi method g(UInt:D :$size) { "k$size" } }
say K.new.g;     # interpreter and Rakudo: k1 — compiled: k
```

**What landed.** Codegen now refuses such a group, so the program falls back to
AOT bundling and both faces agree. This is the call `Codegen::multiDef` already
made for multi SUBS (`"a multi candidate with a where/:D constraint"`); multi
METHODS simply never had the equivalent bail, which is why the above compiled at
all. Guarded by `t/regression/multi-method-compiled-dispatch.raku`.

**What it costs** (measured over 1,822 files — `examples/`, `showcase/`,
`t/regression/`, `tools/`, and the Roast checkout; 1,143 of them transpile
natively today):

| | files |
|---|---:|
| currently-native files that use a multi method at all | 23 |
| …now falling back to AOT | 21 |
| — because of a `where`/`:D` constraint | 5 |
| — because a candidate is (or may be) in a parent class | 16 |

The parent-class bails are dominated by `multi method new` on a class with a
built-in parent (`class NotComplex is Cool`, the S32-trig files) — where the
parent's `new` genuinely exists and genuinely must be reachable, so the bail is
earning its keep rather than being paranoid.

**Still open.** Native dispatch for these forms. Note the measurement rules out
the obvious cheap answer: teaching the emitted guard about named parameters wins
back **zero** files, because every currently-native file with a named-parameter
multi method also carries a `where`/`:D` constraint and bails anyway. Reproducing
`scoreCandidate` in generated C++ — smileys, `where` bodies, MRO deferral into
built-in parents — is the whole interpreter dispatcher.

The promising design is the one `MAIN` already uses: ship the **signatures** as
metadata and keep the **bodies** compiled. `mainSigBlob` serializes MAIN's
signatures (bodies detached, via the AstSerial module-cache serializer) into the
binary, and `RT.runCompiledMain` feeds them through the same `mainProtocol` the
interpreter uses. The same shape here — emit each candidate as a real `Callable`
carrying its `Param` list, register the group as a multi dispatcher — would let
the interpreter's own `scoreCandidate` and `parentNext` pick the candidate while
every body stays native code. That wins back all 21 files without a second
dispatcher to keep in sync with the first.

## Role BODY lexicals are shared across composers (2026-08-31)

Found while making `ML::TriesWithFrequencies` install (issue #53). A role's body
runs **once**, and every class that composes the role closes over that one pad;
Rakudo instantiates the body per composition, so each composer gets its own copy.

```raku
role R { my $x = 'ROOT'; method get { $x }; method set($v) { $x = $v } }
class A does R { }
class B does R { }
A.new.set('A!');
say A.new.get, " ", B.new.get;   # Rakudo: A! ROOT   ·   rakupp: A! A!
```

The same shows through the `my $.x` class-level accessor that issue #53 added
(`Parser::desugarDotDecl`), since the accessor reads exactly such a lexical:
after `C.rl = 'X'` on a class composing `role R { my Str $.rl = 'ROOT'; … }`,
Rakudo still answers `ROOT` for `R.rl` and rakupp answers `X`.

Nothing measured depends on it — the divergence needs two classes composing one
role *and* a mutated body lexical, and no distribution in the sweep does that —
which is why it is recorded rather than fixed. The fix is not local: the
ClassDecl path in `Interpreter.cpp` builds one `bodyEnv` per role declaration and
the composition loop copies the method `Value`s with their closures intact, so a
faithful version has to re-run (or clone) the body per composition and repoint
each composed `Callable`'s `closure` at the fresh env.
