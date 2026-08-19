# Raku 6.e — what it adds to 6.d

Raku the *language* is versioned (`6.c`, `6.d`, `6.e`, …) independently of Rakudo
the *compiler* (`2026.07`). A given compiler release implements one or more
language revisions, and a program picks one with a `use v6.X` pragma on its first
line. Rakudo 2026.07 defaults to **6.d** and offers **6.e** behind a `PREVIEW`
marker; Raku++ does the same.

This page is the missing changelog: everything `6.e` changes relative to `6.d`,
with both outputs shown for each item, plus where Raku++ stands on each.

> Verified on Rakudo **v2026.07** (MoarVM) and Raku++ **build-arm64, 2026-08-18**,
> against rakudo `main` @`050230cb2` and roast @`b2cbe8a42`. Every snippet below
> was run on both engines; the outputs are pasted, not predicted.

---

## 1. The short version

6.e is not a redesign. It is roughly four kinds of change:

1. **New things** — about a dozen subs/terms/operators and ~25 methods that
   simply do not exist in 6.d (`snip`, `snitch`, `nomark`, `stem`, prefix `//`,
   term `nano`, `rotor` as a sub, …).
2. **Fixes that could not be made without breaking code** — `sqrt` of a negative
   number returns a `Complex` instead of `NaN`, `so (5..1)` is finally `False`,
   string ranges iterate by `.succ` instead of a per-position cross product,
   `sprintf` sign/flag handling follows C.
3. **Object-model changes** — a role's submethods are no longer composed into
   the class; a package no longer silently replaces an enclosing package of the
   same name; shaped hashes default to `Mu` rather than `Any`.
4. **Tightenings** — things that were silently ignored are now compile-time
   errors (unknown regex boundaries, `sub foo;` without `unit`, macros).

Nothing is *removed* except `.pm` as a module file extension, and the multi-path
forms of `unlink`/`rmdir`/`chmod`/`chown` (deprecated, still working).

---

## 2. Turning it on

```raku
use v6.e.PREVIEW;
```

The rules, all verified:

| Rule | Rakudo 2026.07 | Raku++ |
|---|---|---|
| `use v6.e.PREVIEW;` | works | works |
| `use v6.e;` (no PREVIEW) | `Raku v6.e requires PREVIEW modifier` | accepted (divergence) |
| Not the first statement | `Too late to switch language version. Must be used as the very first statement.` | same |
| Default for a file with no pragma, `-e`, REPL | 6.d | 6.d |
| `EVAL` can raise the revision | yes — `EVAL q[use v6.e.PREVIEW; 42]` works | yes |
| Revision is per compilation unit | a module keeps its own revision | same |

`$*RAKU.version` reports the revision in force (`v6.d` / `v6.e`), which is the
easiest runtime check.

---

## 3. Where the documentation is — and isn't

There is no complete, official "6.e changelog". What exists:

- **[roast/docs/announce/6.e.md](https://github.com/Raku/roast/blob/master/docs/announce/6.e.md)**
  — the official announcement *template*. It is explicitly marked "to be filled
  up upon actual release": only the pseudo-package section has content; *New
  Routines and Operators*, *New Types*, *Deprecations*, *Version-Controlled
  Changes* are all empty headings.
- **[docs.raku.org](https://docs.raku.org)** — tags individual routines with
  *"Available as of 6.e language version (early implementation exists in Rakudo
  compiler 2022.07+)"* — `snip`, `snitch`, `nano`, `Format`, `Formatter`,
  `Complex.sqrt`, `RakuAST` and others carry it. Accurate where present, but
  scattered across hundreds of pages with no index, and incomplete: at the time
  of writing `/routine/nomark` and `/routine/stem` are both 404, and the
  behaviour changes (§7 below) are largely absent, since there is no routine
  page to hang them on.
- **[rakudo/docs/language_versions.md](https://github.com/rakudo/rakudo/blob/main/docs/language_versions.md)**
  — the *policy* (how revisions work, why `PREVIEW` exists), not the contents.
- **The source of truth**, and what this page was built from:
  - `src/core.e/` in rakudo — `additions.rakumod` (new subs/terms/operators),
    `Fixups.rakumod` (augmented methods), `Formatter.rakumod`, `PseudoStash.rakumod`,
    `array_multislice.rakumod`, `hash_multislice.rakumod`, `hash_hyperslice.rakumod`,
    `Grammar.rakumod`;
  - every `language_revision >= 3` / `< 3` test in the grammars, actions and
    metamodel (3 = 6.e);
  - every `is revision-gated("6.e")` routine in `src/core.c/`;
  - roast's 32 `use v6.e.PREVIEW` test files (`*-6e.t`, `snip.t`, `format.t`,
    `sprintf-*.t`, `multislice-*.t`, `hyperslice.t`, …).

---

## 4. New syntax

### prefix `//` — "is defined"

```raku
say //42, " ", //Any;
```
| | |
|---|---|
| 6.d | `Null regex not allowed. Please use .comb if you wanted…` — `//` is an empty regex |
| 6.e | `True False` |

### term `nano`

Nanoseconds, as an `Int`, next to the existing `time` and `now`.

```raku
say nano ~~ Int;   # 6.d: compile error   6.e: True
```

### `q:o` / `q:format` — a `Format` literal

A compile-time-parsed `sprintf` template that is a callable object.

```raku
my $f := q:o/%5s/;      # or q:format/%5s/
say $f.^name;           # Format
say $f("foo");          # "  foo"
say (1,2,3).fmt(q:o/%3d/);   # "  1   2   3"
```

In **Rakudo this needs the RakuAST frontend as well as 6.e** — `q:o` is only in
`src/Raku/Grammar.nqp`, so `RAKUDO_RAKUAST=1 raku …` is required; without it you
get `Unrecognized adverb: :o` even under 6.e. The `Format` type itself is
available under plain 6.e (`Format.new("%5s")("hi")` → `"   hi"`). Raku++ has
both without an environment variable.

`.fmt(Format)` candidates were added to `Bag`, `BagHash`, `List`, `Map`, `Mix`,
`MixHash`, `Pair`, `Seq`, `Set`, `SetHash`.

### `RakuAST`

The `RakuAST::` package is available under 6.e without a pragma; under 6.d it
requires `use experimental :rakuast`.

```raku
say RakuAST::IntLiteral.new(42).DEPARSE;
```
| | |
|---|---|
| 6.d | `Use of RakuAST is experimental; please 'use experimental :rakuast;'` |
| 6.e | `42` |

### Multislices and hyperslices

`@a[0;1;2]` exists in 6.d, but 6.e adds the *slipped* form — a list of indices
spliced in with `||` — and the `{**}` hyperslice over nested associatives.

```raku
my @a = [[1,2],[3,4]],; my @i = 0,1,0;
say @a[||@i];
```
| | |
|---|---|
| 6.d | `([[1 2] [3 4]] (Any) [[1 2] [3 4]])` (three ordinary index lookups) |
| 6.e | `3` (one multi-dimensional lookup) |

```raku
my %h = A => { B => 1, C => 2 }, D => 3;
say %h{**}:k.sort.join(",");      # 6.d: (empty)   6.e: B,C,D
say %h{'A';'B'};                  # 6.d: (1)       6.e: 1   ← no longer a 1-element list
```

### `unit sub foo;`

The semicolon form of a routine declaration. 6.c/6.d allowed it only for `MAIN`;
6.e allows any sub but requires `unit` scope.

```raku
sub foo;          # 6.d: X::UnitScope::Invalid   6.e: X::UnitScope::MustHaveUnit
unit sub foo;     # 6.d: error                   6.e: fine
```

---

## 5. New terms, subs and operators

From `src/core.e/additions.rakumod` — all of these are compile errors under 6.d:

| Sub | Example | 6.e output |
|---|---|---|
| `rotor` | `rotor(2, 1..6)` | `((1 2) (3 4) (5 6))` |
| | `rotor(2, 1, 1..6)` | `((1 2) (3) (4 5) (6))` — cycle first, list last |
| `snip` | `snip(* < 3, 1,2,3,4)` | `((1 2) (3 4))` |
| `snitch` | `(1,2).snitch` | notes the value to `$*ERR`, returns it unchanged |
| `trans` | `trans("a" => "b", "banana")` | `bbnbnb` |
| `comb` w/ `Pair` | `comb(2 => 1, "abcdef")` | `(ab de)` — size => step, like `rotor` |
| `next`/`last` with a value | `(1,2,3).map({ $_ == 2 ?? next(42) !! $_ })` | `(1 42 3)` (6.d: `(1 3)`) |
| prefix `//` | `//$x` | `$x.defined` |
| term `nano` | `nano` | `Int` nanoseconds |

`infix:<~>` on two `Blob`s, and `infix:<div>` / `infix:<mod>` with native
`int`/`uint` candidates, are also (re)defined in `core.e`, but behave the same as
6.d on this Rakudo build.

---

## 6. New methods

From `src/core.e/Fixups.rakumod`. All of these are `No such method` under 6.d.

| Method | Example | 6.e |
|---|---|---|
| `Any.snip` | `(1,2,3,4,5).snip(* < 3)` | `((1 2) (3 4 5))` |
| `Any.snitch` | `my $x = (1,2).snitch` | notes, returns unchanged |
| `Any.skip(list)` | `(1..10).skip(2,3)` | `(1 2 6 7 8 9 10)` — alternates *produce N*, *skip N* |
| `Cool.nomark` / `Str.nomark` | `"élan vitál".nomark` | `elan vital` |
| `IO::Path.stem` | `"foo.tar.gz".IO.stem` | `foo`; `.stem(1)` → `foo.tar` |
| `Complex.sign` | `(3+4i).sign` | `0.6+0.8i` (6.d: throws — cannot convert to Real) |
| `Int.roll` / `Int.pick` | `6.pick(3)` | short for `(^6).pick(3)` |
| `Mu.Callable($name)` | `42.Callable("Str") ~~ Method` | `True`; `Failure` if not found |
| `Str.comb(Pair)` | `"abcdef".comb(2 => 1)` | `(ab de)` |
| `Str` `:smartcase` | `"Hello World".contains("world", :smartcase)` | `True` (6.d: `False`) |
| | on `contains`, `starts-with`, `ends-with`, `index`, `indices`, `rindex`, `substr-eq` | |
| `Date.DateTime(:timezone)` | `Date.new(2026,1,1).DateTime(:timezone(3600)).timezone` | `3600` (6.d: named arg ignored → `0`) |
| `Instant.DateTime(:timezone)` | same | `3600` (6.d: `0`) |
| `.fmt(Format)` | see §4 | on 10 container types |
| `Int.uniname` | unassigned codepoint | returns a `Failure` instead of `<unassigned>` (source-only: no codepoint in the current UCD tables showed the difference) |

`:smartcase` means: match case-insensitively *unless* the needle contains an
uppercase character.

---

## 7. Changed behaviour — the ones that bite

These are the items to read before turning 6.e on for existing code.

### `sqrt`/`log` of a negative number → `Complex`

```raku
say (-4).sqrt;              # 6.d: NaN          6.e: 0+2i
say (-4e0).sqrt;            # 6.d: NaN          6.e: 0+2i
say (-1e0).log;             # 6.d: NaN          6.e: 0+3.141592653589793i
```

### `Range.Bool` is emptiness, not "has endpoints"

```raku
say so (5..1);              # 6.d: True         6.e: False
say so ("b".."a");          # 6.d: True         6.e: False
```

### String ranges iterate by `.succ`

```raku
say ("az".."bc").join(",");
```
| | |
|---|---|
| 6.d | `az,ay,ax,…,ac,bz,by,…` — a per-position cross product, 52 elements |
| 6.e | `az,ba,bb,bc` |

### `sprintf` follows C on signs and flags

```raku
say sprintf("%#x", -256);        # 6.d: 0x-100     6.e: -0x100
say sprintf("[%+b][% b]", 5, 5); # 6.d: [+101][ 101]  6.e: [101][101]
say sprintf("[%#.0f]", 1);       # 6.d: [1]        6.e: [1.]
say sprintf("[%G]", NaN);        # 6.d: [NaN]      6.e: [NAN]
```
(`%g` keeps `NaN`; only `%G` upper-cases. roast covers this in eleven
`S32-str/sprintf-*.t` files, all `use v6.e.PREVIEW`.)

### A role's submethods are no longer composed into the class

```raku
role R { submethod s { 42 } }
class C does R { }
say C.new.s;
```
| | |
|---|---|
| 6.d | `42` |
| 6.e | `No such method 's' for invocant of type 'C'` |

`BUILD`/`TWEAK`/`DESTROY` from roles still run — 6.e adds them to the class's
BUILDPLAN and finalization list explicitly instead of relying on composition —
so object construction is unaffected; only *calling* a role's submethod as a
method on the class changes.

### A package no longer silently replaces an enclosing package of the same name

```raku
module A::B { class A::B { } }
say A::B::A::B.^name;
```
| | |
|---|---|
| 6.d | `Potential difficulties: Declaring class 'A::B' inside an enclosing module of the same name…`, then the inner class *replaces* the outer stash |
| 6.e | `A::B::A::B` — it nests, no warning |

### Shaped hashes default to `Mu`

```raku
my %h{Str}; say %h<nope>.WHAT.^name;   # 6.d: Any   6.e: Mu
```

### Placeholder `@_` is per-block

```raku
sub f(*@_) { my &c = { @_.elems }; say c(1,2,3) }; f(7,7,7,7);
```
| | |
|---|---|
| 6.d | `Too many positionals passed; expected 0 or 1 arguments but got 3` — the inner block's `@_` is the enclosing routine's |
| 6.e | `3` — each block gets its own implicit `*@_` |

### Pseudo-packages return `Failure`, and `LEXICAL::` is stricter

```raku
my $r = MY::<$nosuchvar>; say $r.^name;   # 6.d: Any   6.e: Failure
```
```raku
my $*dyn = 42; sub f { say LEXICAL::<$*dyn> }; f;
```
| | |
|---|---|
| 6.d | `42` |
| 6.e | `Cannot access '$*dyn' through LEXICAL, because it is not declared as lexical` |

Per the roast announcement, 6.e also specifies that `LEXICAL::` sees dynamics
from the caller chain, `SETTING::` sees symbols from *all* available `CORE`s, and
binding works on every pseudo-package.

### `Grammar.parse` fails instead of returning `Nil`

6.e gives grammars a new base class (`src/core.e/Grammar.rakumod`), so a failed
parse carries an `X::Syntax::Confused` with `pre`/`post` context:

```raku
grammar G { token TOP { \d+ } }; say G.parse("abc").^name;
```
| | |
|---|---|
| 6.d | `Any` |
| 6.e | `Failure` |

6.e also gives grammars a private default-parent HOW rather than binding the
name `Grammar` itself, which is what roast `S05-grammar/namespace-6e.t` covers
(`grammar Grammar { }`; this Rakudo accepts it under 6.d too).

### `splice` can insert an itemized array as one element

```raku
my @a = 1,2,3; @a.splice(1,1,$[8,9]); say @a.raku;
```
| | |
|---|---|
| 6.d | `[1, 8, 9, 3]` |
| 6.e | `[1, [8, 9], 3]` |

### `.pm` is no longer a module file extension

`CompUnit::Repository::FileSystem` looks for `.rakumod .pm6 .pm` in 6.c/6.d, and
only `.rakumod .pm6` in 6.e.

```raku
use OldMod;   # lib/OldMod.pm
```
| | |
|---|---|
| 6.d | loads, with `Saw 1 occurrence of deprecated code` |
| 6.e | `Could not find OldMod in: …` |

### `subset` records the language version

```raku
subset Even of Int where * %% 2; say Even.^ver;   # 6.d: 6.d   6.e: 6.e
```

---

## 8. New errors

Things that were silently accepted and now fail at compile time:

| Code | 6.d | 6.e |
|---|---|---|
| `/<\|f> abc/` | matches (unknown boundary is a no-op) | `Unrecognized regex boundary '<\|f>'. The known boundaries are '<\|w>' (word) and '<\|c>' (codepoint).` |
| `macro m() { … }` | works under `use experimental :macros` | `Experimental macros are no longer supported in Raku 6.e.` |
| `sub foo;` | `X::UnitScope::Invalid` (only `MAIN` allowed) | `X::UnitScope::MustHaveUnit` — needs `unit sub foo;` |

---

## 9. Deprecations

`is revision-gated("6.e")` candidates in `src/core.c/io_operators.rakumod` keep
the old forms working but emit a deprecation notice at exit, and add single-path
candidates:

```raku
say unlink("/tmp/no-such-file-abc123").raku;
```
| | |
|---|---|
| 6.d | `["/tmp/no-such-file-abc123"]` — list of paths that worked |
| 6.e | `Bool::True` — one path, one boolean |

The same applies to `rmdir`, `chown`, and `chmod` (which additionally gains
`chmod($path, :$mode)` and deprecates `chmod($mode, @paths)`). Multi-path use is
deprecated in favour of `@paths.grep(*.IO.unlink)`.

---

## 10. Declared in `core.e`, but not observably different (yet)

Worth knowing so you do not go looking for a change that is not there. On Rakudo
2026.07 these behave identically under 6.d and 6.e, either because the fix was
made unconditionally or because the `core.e` version is a re-implementation:

- `fail` with no arguments picking up the exception from `$!`
  (roast tests it in `S04-exceptions/fail-6e.t`, but 6.d does it too).
- `1 div 0` / `1 mod 0` returning a `Failure`.
- `Blob ~ Blob` concatenation.
- `.are` — often listed as 6.e, but it lives in `core.c` and works in 6.d.
- A role's `TWEAK` running on construction (same result, different mechanism).
- `Int.uniname` on the codepoints tested here (no unassigned codepoint in the
  current UCD tables produced a difference).

---

## 11. How Rakudo implements this

Useful when you want to check something yourself rather than trust a list:

- The revision is an integer: **1 = 6.c, 2 = 6.d, 3 = 6.e**, read as
  `nqp::getcomp('Raku').language_revision`.
- Each revision has a *nested setting*: `CORE.c` → `CORE.d` → `CORE.e`, built
  from `src/core.c/`, `src/core.d/`, `src/core.e/`. `core.e` consists almost
  entirely of `augment`s, so 6.e is 6.d plus a delta rather than a fork.
- Per-candidate gating uses the `is revision-gated("6.e")` trait, so a multi can
  have one body for 6.c and another for 6.e with the dispatcher choosing by the
  caller's revision.
- Syntax gating is inline in the grammars
  (`<?{ nqp::getcomp('Raku').language_revision >= 3 }>`).
- Types remember the revision they were declared under
  (`Metamodel::LanguageRevision`, `.^ver`), which is how a 6.d class composing a
  6.e role still gets the right submethod behaviour.

To find every gate in a rakudo checkout:

```bash
grep -rn "language_revision" src/ | grep -w 3
```

---

## 12. Raku++ status

Raku++ reports itself as implementing 6.d with 6.e features, and sets its
internal revision from the same pragma.

The list below is a snapshot. The same comparison, re-measured on every build
and scored per feature, is the support matrix at
**[raku.online/spec/6e](https://raku.online/spec/6e/)** — each entry there is one
snippet run three times (Rakudo 6.d, Rakudo 6.e, Raku++ 6.e) with all three
outputs shown, so a verdict can be checked rather than taken on trust.

Of the items above, these were verified as **already matching 6.e**:

`snip` (method and sub), `snitch`, `rotor` as a sub, `q:o`/`q:format`/`Format`
(without needing a RakuAST frontend), the `.fmt(Format)` candidates, all four
`sprintf` changes, `Complex.sign`, `(-4).sqrt`, the `.succ` string-range
semantics, role submethods not composed, nested same-name packages, `%h{**}`
hyperslices, `%h<A;B>` returning a scalar, and the single-path `unlink`.

And these are **gaps or divergences** as of this build:

| Item | Rakudo 6.e | Raku++ 6.e |
|---|---|---|
| prefix `//` | `True False` | parsed as an empty regex, prints an empty line |
| term `nano` | `Int` | `Undefined routine 'nano'` |
| `next($v)` / `last($v)` | value is used | value discarded |
| `trans` as a sub | `bbnbnb` | `Undefined routine 'trans'` |
| `Mu.Callable($name)` | `Method` | `No such method 'Callable'` |
| `Str.nomark` / `Cool.nomark` | `elan vital` | `No such method 'nomark'` |
| `IO::Path.stem` | `foo` | `No such method 'stem'` |
| `:smartcase` | `True` | `False` (adverb ignored) |
| `6.pick(3)` | 3 elements | 1 element |
| `Str.comb(2 => 1)` | `(ab de)` | `()` |
| `.skip(2,3)` | `(1 2 6 7 8 9 10)` | `(3 4 5 6 7 8 9 10)` |
| `@a[\|\|@i]` | `3` | `Unsupported prefix 'dimslip'` |
| `so (5..1)` | `False` | `True` |
| `(-1e0).log` | `0+3.14…i` | `NaN` |
| shaped-hash default | `Mu` | `Any` |
| `MY::<$missing>` | `Failure` | throws `Variable … is not declared` |
| `LEXICAL::<$*dyn>` | throws | returns `42` |
| `G.parse("abc")` | `Failure` | `Any` |
| `subset.^ver` | `6.e` | `v6.c` |
| `<\|f>` boundary | compile error | matches silently |
| `splice(1,1,$[8,9])` | `[1, [8, 9], 3]` | `[1, 8, 9, 3]` |
| `Date/Instant.DateTime(:timezone)` | honoured | ignored |
| `.pm` module extension | not found | still loaded |
| `use v6.e;` without `PREVIEW` | rejected | accepted |

---

## 13. Checking any of this yourself

Every line above came out of the same three-way run — 6.d, 6.e, and Raku++ 6.e —
which is two commands:

```bash
raku -e 'CODE'
```

```bash
raku -e 'use v6.e.PREVIEW; CODE'
```

For the `q:o`/`Format`/multislice syntax on Rakudo, add the RakuAST frontend:

```bash
RAKUDO_RAKUAST=1 raku -e 'use v6.e.PREVIEW; my $f := q:o/%5s/; say $f("foo")'
```
