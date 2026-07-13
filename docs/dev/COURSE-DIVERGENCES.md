# Raku-course examples: rakupp vs Rakudo divergences

Source: [The Complete Course of the Raku Programming
Language](https://course.raku.org/) —
[source on GitHub](https://github.com/ash/raku-course).

Method: every fenced code block (```raku and untagged) from the English course
pages (`essentials advanced oop paradigms regexes addendum about-this-course
exercises` — 1,318 pages) plus the exercise `.raku` files was run under both
engines (stdin closed, 6 s timeout, sandbox cwd) — **3,068 comparisons**.
Blocks that do not run under Rakudo (theory fragments, output samples,
incomplete code) were ignored, as were snippets whose Rakudo output is not
reproducible across two runs (nondeterminism). Raw data:
`course-cmp/mismatches.json` in the session scratchpad (148 raw entries,
116 after folding cumulative-page duplicates).

**Findings only — nothing fixed yet.** Grouped by root cause, roughly by
severity × how many course pages each affects.

## A. Containers & binding

1. **Scalar binding does not alias** — the biggest one.
   `my $y := $x; $x = 20; say $y` → Rakudo `20`, rakupp `10` (bind copies).
   Pages: advanced/ordered-containers/binding, bind-a-scalar exercise (×3).

2. **`is default(...)` trait unimplemented** (and `.VAR.default`).
   `my $port is default(8080); say $port` → `8080` vs `(Any)`;
   `$x.VAR.default` dies. Pages: scalar-containers/default, traits-pragmas,
   introspection (×7).

3. **`.VAR` returns the value, not a Scalar container.**
   `$x.VAR.^name` → `Scalar` vs `Int`; `.VAR.name` → `$lang` vs `Str`. (×5)

4. **Lists are not immutable**: `my $odd = (1,3,5); $odd[3] = 8` must die
   (`X::Assignment::RO`-ish); rakupp assigns. (lists page)

5. **`$(@a)` itemization ignored in iteration/flat**:
   `$count++ for $(@a)` → 1 vs 3; `flat($(@a), @b)` → `([1 2 3] 4 5)` vs
   fully flat. (×3)

## B. List/Seq typing & gists (the GLR family — already a campaign item)

6. **Word lists / results type as Array, not List/Seq**: `<a b c>.WHAT` →
   `(List)` vs `(Array)`; `.words/.split/.comb` gist `[..]` instead of
   `(..)`; `(1,2,3).raku` → `[1, 2, 3]`; `(1...5).WHAT` → `(Seq)` vs
   `(Array)`; `X`/`Z` results show inner `[..]`; `f (5, 6)` passes a List
   (rakupp: Int). (×10 pages)

7. **`1..*` lazy pipelines produce ()**: `(1..*).head(3)` → `()`;
   `(1..*).map(* * 7).head(5)` → `()`. `lazy 1..3` reports not lazy. (×5)
   (`… ... *` sequences work; it's the open RANGE that breaks.)

## C. Associative gists & slices

8. **Hash/Set/Map gist inconsistencies**: `say %data` (from a pairs list)
   prints tab-lines instead of `{alpha => 1, …}`; `say $set` → tab-lines
   instead of `Set(2 3)`; `Colour.enums` prints raw lines instead of
   `Map.new(…)`; `Dog.^attributes` gists a hash dump instead of
   `(Str $!name)`. (×4)

9. **Hash slice with adverb**: `%h<a c>:kv` → `()` (expected `(a 1 c 3)`).

10. **Indexing a Pair by its key**: `('employee name' => 'x'){'employee name'}`
    → `(Any)` (expected the value).

## D. Junction gist

11. `say 1|2|3` → `any` — eigenstates missing (`any(1, 2, 3)`).
    `$j + 10` → `any` instead of `any(11, 12, 13)`. (×4 — `.raku` was fixed
    in batch 7; the **gist** path still bare.)

## E. Numerics

12. **Rat modulo**: `10.3 % 3` → `1` vs `1.3`.
13. **`+'3.14'` numifies to Num, not Rat.**
14. **`Int('1.23E4')`** → `1` vs `12300` (scientific-notation string → Int).
15. **Unicode numeric literals** (`⓷ ❷ ⒌ ㊄`) don't parse. (quiz page + file)

## F. Quoting adverbs

16. **`q:c/…{…}…/` (closures), `Q:s{…}` (scalars-only), `qq:!s{…}`
    (disable scalars)** — none of the q/Q adverbs work; `qq:!s` dies. (×6)

## G. Regexes & grammars

17. **`:s` sigspace requires whitespace**: `'foobar' ~~ /:s foo bar/` must be
    False; rakupp matches.
18. **`$0*` backreference (+ m:g match objects)**: run-length encoding gives
    `a1a1a1…` instead of `a3b4c2` — backreference `$0*` inside the pattern
    matches empty, and per-match `.chars` is wrong. (×2)
19. **`{ make … }` inline code block in a token**: `.made` → Nil (works via
    action classes, fails inline). (×2)

## H. Traits, pragmas, MOP

20. **Custom `trait_mod:<is>`** definitions are accepted but never invoked
    (`sub foo() is traced` doesn't call the trait handler). (×3)
21. **`no strict`** doesn't enable undeclared variables — still dies.
22. **MOP naming/introspection**: `.HOW.^name` →
    `Metamodel::ClassHOW` vs Rakudo's `Perl6::Metamodel::ClassHOW`;
    `.HOW.name($x)` returns the HOW's name, not the type's; `Int.WHO` dies
    (no Stash). (×3)
23. **Default object gist**: `say Dog.new` → `Dog<obj>` vs `Dog.new`.

## I. Signatures & calls

24. **`*%options` slurpy-named breaks inside interpolated method calls**
    (`"{%options.elems}"` dies). (signatures page)
25. **`.grep: 60 < * < 70`** — chained comparison does not curry into a
    WhateverCode (matches everything). (colon-calls page)
26. **Negated word/misc operators**: `'a' !eq 'b'` and `5 !%% 2` fail to
    parse/run (`!eq` prints operand). (metaoperators page)
27. **`<raku perl>>>.uc`** — hyper `>>.` directly on a word list dies. (×1)
28. **`[Z] @matrix`** (reduce-zip for transpose) → `([1 6])` instead of
    `((1 4) (2 5) (3 6))`. (×2)

## J. Sorting

29. **List-returning sort keys**: `.sort({ -.value, .key })` ignores the
    secondary key (ties come out in hash order). (word-frequency ×2)

## K. IO / processes / async

30. **`spurt $f, $s, :append` overwrites** instead of appending. (×3)
31. **`%*ENV` mutations don't reach child processes**: set `%*ENV<NOTES>`,
    then `shell 'wc -l < "$NOTES"'` — empty result. (read-env exercise)
32. **`done` inside `whenever` doesn't stop a from-list Supply**: collects
    all 5 items instead of stopping at 3. (react quiz)

## L. prompt/EOF & allomorphs (several pages, one root)

33. `prompt` at EOF returns `(Any)` instead of `Nil`, and its result isn't an
    allomorph: `my Int $n = prompt …` then `say $n` → Rakudo prints `(Int)`
    type object (or dies for a non-numeric), rakupp gives `(Any)`;
    `my Int $i = $input` (Str input) must throw X::TypeCheck — rakupp binds.
    Also `say prompt` → `Nil` vs `(Any)`. (×9 pages — inflated by the
    stdin-closed harness, but the Nil-vs-Any and type-check differences are
    real.)

34. **`say $!.message` with no error**: Rakudo prints `Nil` (warning);
    rakupp exits 1 silently. (try page)

## Artifacts noted but excluded (not engine bugs to chase)

- `factorial(@*ARGS[0].Int)` with no args — infinite recursion under Rakudo
  (timeout) vs clean death in rakupp; environment-dependent either way.
- Same-scope `my $value` redeclaration in concatenated page blocks: Rakudo
  dies at compile (X::Redeclaration), rakupp allows — cumulative-page
  artifact, though the redeclaration check itself is a known leniency.
- `say 'a\b\c\\'` block: Rakudo produced empty output in the harness run —
  needs a manual recheck before trusting either way.
- `sink (…)` page: outputs match except a trailing-newline/exit-code nit.
