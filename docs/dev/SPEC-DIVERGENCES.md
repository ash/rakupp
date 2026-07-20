# rakupp vs Rakudo divergences found while writing the spec site

Found on 20 July 2026 by cross-checking every runnable example on the
**[Raku++ Specification](https://spec.raku.online/)** (repo:
[ash/raku-spec](https://github.com/ash/raku-spec)) under `build/rakupp` and
Rakudo (v2026.06). The spec's generator (`build.raku --verify --oracle=raku`)
runs each `raku`/`output` example pair through **both** interpreters and fails
the build on any disagreement, so every divergence below was caught mechanically,
not by eye. Rakudo is treated as the authority; the spec's declared output
follows Rakudo, and each affected page is badged `divergent` or `partial`.

Coverage at the time of writing: **111 pages, 260 dual-verified examples**; the
18 items below are everything that disagreed. Each entry is self-contained:
reproduction, expected vs. actual, severity, and the spec page that documents it.

**Update (20 Jul 2026):** 15 of the 18 are now **fixed** in rakupp and marked
✅ below — #1 (`.round`), #3 (`Complex.exp`), #7 (`List.invert`), #8
(`Str.wordcase`), #9 (`split :skip-empty`), #10 (`Str.comb(Int)`), #11
(`Str.indent`), #13 (`.isa`), plus #2 (where), #4 (Capture), #6 (qq{}), #12 (parents/params), #16 (Map), #17/#18 (gists). Remaining: #5 (lexical regex shadowing), #14 (native array boxing), #15 (NFC/NFG).

**Legend:** 🐞 wrong result · 🕳️ missing method/feature · 🔤 semantic/type
difference · 💅 cosmetic (gist/stringification only)

---

## 🐞 Behavioural bugs (produce a wrong result)

### 1. ✅ FIXED — `.round` rounds negative halves the wrong way
Rakudo rounds halves toward **+∞**; rakupp rounds **away from zero**. Positive
halves agree.

```raku
say (-3.5).round;   # Rakudo -3   · rakupp -4
say (-2.5).round;   # Rakudo -2   · rakupp -3
say   3.5 .round;   # Rakudo  4   · rakupp  4   (agree)
```
Page: [builtins/rounding](https://spec.raku.online/builtins/rounding.html) · badged **divergent**.

### 2. ✅ FIXED — `where` constraints are not enforced
A parameter/`subset` `where` predicate is parsed and lets valid arguments
through, but a **failing** argument is not rejected — the body runs anyway.

```raku
sub pos(Int $n where * > 0) { $n }
say pos(-1);        # Rakudo: dispatch failure · rakupp: -1
```
Also affects `subset` types and value-based multi dispatch. Meaningful: `where`
can't be used for validation in rakupp.
Pages: [subs/constraints](https://spec.raku.online/subs/constraints.html),
[types/subsets](https://spec.raku.online/types/subsets.html) · badged **partial**.

### 3. ✅ FIXED — `Complex.exp` computes the wrong value
Real `sin`/`cos`/`exp` are fine; the complex exponential is not.

```raku
say (0 + 1i).exp;
# Rakudo: 0.5403023059000001+0.8414709848i   (= cos1 + sin1·i)
# rakupp: 1+1i
```
Niche — no spec page.

### 4. ✅ FIXED — Capture literal doesn't separate positional from named
`.list` leaks the named args and `.hash` leaks the positionals.

```raku
my $c = \(1, 2, :x(3));
say $c.list;   # Rakudo (1 2)              · rakupp (1 2 x => 3)
say $c.hash;   # Rakudo Map.new((x => 3))  · rakupp {1 => 2, x => 3}
```
No spec page.

### 5. A lexical regex named like a built-in subrule is shadowed by the built-in
`my regex ident {…}` (or `ws`, `alpha`, `digit`, …) is ignored when called as
`<ident>`; rakupp resolves to the built-in, Rakudo uses the lexical override.

```raku
my regex ident { <[a..z]>+ }
"hello123" ~~ /<ident>/;
say ~$<ident>;      # Rakudo hello · rakupp hello123 (built-in ident ran)
```
Page: [regexes/named-rules](https://spec.raku.online/regexes/named-rules.html).

### 6. ✅ FIXED — `qq{…}` brace-delimited interpolates a nested `{…}` block
With `{ }` as the *delimiter*, the inner block should be literal text.

```raku
say qq{one and one is {1 + 1}};
# Rakudo: one and one is {1 + 1}    (literal)
# rakupp: one and one is 2          (interpolated)
```
Workaround: use a non-brace delimiter (`qq[…]`) when you want `{}` interpolation.
Page: [literals/strings](https://spec.raku.online/literals/strings.html).

---

## 🕳️ Missing methods / features (error or no-op)

### 7. ✅ FIXED — `List.invert`
```raku
say (a => 1, b => 2).invert;
# Rakudo: (1 => a 2 => b)
# rakupp: No such method 'invert' for invocant of type 'List'
```
(`Pair.antipair` works.)

### 8. ✅ FIXED — `Str.wordcase`
```raku
say "hello world".wordcase;   # Rakudo: Hello World · rakupp: hello world
```
(`.tc`/`.tclc`/`.uc`/`.lc` work.)
Page: [methods/case](https://spec.raku.online/methods/case.html) · **partial**.

### 9. ✅ FIXED — `Str.split(:skip-empty)`
```raku
say "a1b2c3".split(/\d/, :skip-empty);
# Rakudo: (a b c)   · rakupp: (a b c )   (trailing empty kept)
```
Page: [methods/split](https://spec.raku.online/methods/split.html) · **partial**.

### 10. ✅ FIXED — `Str.comb(Int)`
```raku
say "hello".comb(2);          # Rakudo: (he ll o) · rakupp: (h e l l o)
```
(`.comb` with no arg or a regex works.)
Page: [methods/split](https://spec.raku.online/methods/split.html) · **partial**.

### 11. ✅ FIXED — `Str.indent`
```raku
say "hi".indent(4);           # Rakudo: "    hi" · rakupp: No such method 'indent'
```
Page: [methods/samecase](https://spec.raku.online/methods/samecase.html).

### 12. ✅ FIXED (parents, signature.params) — Metaobject protocol gaps
On [types/mop](https://spec.raku.online/types/mop.html) (**partial**):
- `Type.^parents` — not implemented (errors); Rakudo returns the parent list.
- `&sub.signature.params` — not implemented (errors). `&sub.arity`/`.count` **do** work.
- `.^methods` — returns user methods only; Rakudo also lists auto-generated ones
  (e.g. `POPULATE`).

`.^mro`, `.^attributes`, `.^name` all work.

---

## 🔤 Semantic / type differences

### 13. ✅ FIXED — `.isa(Role)` includes roles
rakupp's `.isa` answers "does it type-match" (roles included); Rakudo's `.isa`
is strict **class** inheritance.

```raku
say 5.isa(Numeric);   # Rakudo False (a role) · rakupp True
```
Use `~~` for role/type membership in both. Page: [types/mop](https://spec.raku.online/types/mop.html).

### 14. Native typed arrays are boxed
Native **scalars** agree (`my int $x` boxes to `Int` in both); native **arrays**
are the boxed `Array[int]` in rakupp, not the truly-native `array[int]`.

```raku
my int @a = 1, 2, 3;
say @a.WHAT;          # Rakudo (array[int]) · rakupp (Array[int])
```
Values identical. Page: [types/native](https://spec.raku.online/types/native.html) · **divergent**.

### 15. No NFC/NFG normalization
Grapheme counting is correct (`.chars` agrees), but combining sequences are not
normalised, so codepoint counts and composed-vs-decomposed identity differ.

```raku
my $s = "e" ~ "\x[301]";   # e + combining acute
say $s.chars;              # Rakudo 1 · rakupp 1  (agree — grapheme)
say $s.codes;              # Rakudo 1 · rakupp 2  (Rakudo precomposes to é)
```
Page: [methods/unicode](https://spec.raku.online/methods/unicode.html) · **divergent**.

### 16. ✅ FIXED — `Map` is a `Hash`
`Map.new(...)` yields a `Hash`, so there is no distinct immutable `Map` type
(and it isn't actually immutable). Lookups are identical.

```raku
say Map.new("a", 1).^name;   # Rakudo Map · rakupp Hash
```
Page: [methods/hash-ops](https://spec.raku.online/methods/hash-ops.html).

---

## 💅 Cosmetic (gist / stringification only — values are identical)

### 17. ✅ FIXED — Seq/List renders with `[…]` instead of `(…)`
The recurring one: a `Seq`/`List` gists with the **Array** brackets. Surfaces
in a bare `gather`, `constant @array`, `Pair.kv`, `.permutations` sublists, etc.
Likely **one underlying fix** (give `Seq`/`List` its own `.gist`).

```raku
say (gather { take 1; take 2 });   # Rakudo (1 2) · rakupp [1 2]
say (1, 2, 3).permutations[0];     # Rakudo (…)   · rakupp […]
```

### 18. ✅ FIXED — Assorted `.gist`/`.Str` differences
- `WhateverCode`/`Callable`: `say (* > 2)` → Rakudo `WhateverCode.new`, rakupp `sub { ... }`.
- `Version`: `say v1.2.3` → Rakudo `v1.2.3`, rakupp `1.2.3` (`.parts` agrees).

---

## Reproducing

Each snippet above runs headlessly under both interpreters:

```sh
printf '%s\n' 'say (-3.5).round;' > /tmp/x.raku
build/rakupp /tmp/x.raku      # rakupp
raku          /tmp/x.raku      # Rakudo v2026.06
```

Or open the linked spec page and press **Run** — the editor executes the same
`build/rakupp`-derived WebAssembly engine, and each page shows Rakudo's
authoritative output beneath the divergent ones.
