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
reproducible across two runs (nondeterminism).

**STATUS after the fix round (2026-07-13): 116 → 12 deduped mismatches**
(148 → 14 raw), of which 5 are harness artifacts and 6 are prompt-EOF
nuances — one real feature gap remains (the Iterator role).
Fixed across two commits (round 1 `3324752`, round 2 follows this file).
Every batch passed the zero-regression gate: full Roast run with no
pass-list drops, benchmarks equal-or-faster.

## Fixed (30 of the 34 findings)

Containers & binding: `:=` scalar aliasing (Proxy over the source Env slot),
`is default(...)` + `.VAR` container records (`.name`/`.default`/`.^name` =
Scalar, gists as the value), Nil assignment resets to the container default,
typed scalars enforce assignment type checks (X::TypeCheck::Assignment),
List immutability (element assignment dies), `$(@a)` itemization in
iteration/flat.

List/Seq semantics (post-GLR): comma lists no longer flatten members —
`my @a = 1, ^3, [^3]` is 3 elements, `for (3,3,3), (3,4,5) -> ($a,$b,$c)`
destructures rows; the single-iterable one-arg rule preserved (`my @a = @b`,
`[<a b>».Str]`); `<a b c>` is a List, `.words/.split/.comb/(1...5)` are Seqs;
`f (5, 6)` passes one List; `1..*` open ranges are lazy-infinite;
`lazy`/`.is-lazy`; `[Z]` is an n-ary zip (transpose); Z/X tuples are Lists.

Gists: Hash `{a => 1}`, `Map.new((...))`, Set/Bag/Mix, Junction eigenstates
`any(1, 2, 3)`, Attribute `(Str $!name)`, default object
`Class.new(attr => ...)`, `.enums` Map, WhateverCode type name, Range Str is
its elements (`put 1..5` → `1 2 3 4 5`) while the gist stays `1..5`.

Numerics: Rat/Num modulo (`10.3 % 3` = 1.3), `+'3.14'` → Rat,
`Int('1.23E4')` = 12300, Unicode Nl/No numerals as literals (⓷ ❷ ⒌ ㊄ ½ …,
the whole UCD), Mix keeps fractional weights (`$m<flour>` = 2.5,
`.total` = 3.25).

Parser/lexer: `q:c` / `Q:s` / `qq:!s` quoting adverbs (feature-mask
interpolation), negated ops (`!eq`, `!%%`), `<a b>>>.uc` hyper re-glue,
keyword pair keys (`role => 'admin'`), embedded mid-line comments
`` #`( … ) `` / `` #`[ … ] ``, `no strict`, chained-comparison currying
(`60 < * < 70`), syntactic Whatever-curry (`(* * 2).^name` names the
WhateverCode; `.map(*.^name)` still curries).

Runtime: `spurt :append`, custom `trait_mod:<is>` dispatched at the
declaration's textual position, `prompt` EOF → Nil, `$!` is Nil (Nil absorbs
method calls), bare `ord` throws X::Obsolete, hash-slice adverbs
(`%h<a c>:kv`), Pair-by-key indexing, multi-key sort (`{ -.value, .key }`),
`sink`, `done` stops an eager `whenever`, `%*ENV` reaches child shells, MOP
names (Perl6::Metamodel::ClassHOW, `HOW.name($x)`, `.WHO` Stash),
`.^mro.map(*.^name)`, split limits (incl. `*`) + char-split edge strings,
head/tail sub forms take the count first, Range `.antipairs` / Int pair
keys, `:s` sigspace requires `<!ww>`, `$0` in-flight backreferences
(`(.) $0*` run-length = a3b4c2), inline `{ make … }` in tokens,
Date.is-leap-year, DateTime.new positional time args,
`.later/.earlier(:months)` with month-end clamping, `:!exists:delete` dies.

## Remaining (12 snippets, 5 root causes)

1. `prompt`-family EOF nuances: interpolating a Nil `$name` diverges in
   warnings/rc; `.say for $begin .. $end` over Nil endpoints (Rakudo: cannot
   iterate an Any range). Harness-EOF-specific.
2. Allomorphs page: `my Int $i = $input` now throws like Rakudo, but the
   success path needs IntStr allomorphs from `prompt`/`val()` — not
   implemented.
3. Iterator-role page (`class Countdown does Iterator`) — user classes as
   Iterators driving `for` (pull-one protocol). Deferred (MOP-deep).
4. Phaser END ordering on a compile-error exit (cumulative-page artifact;
   both engines rc 1).
5. Same-scope `my $x` redeclaration: Rakudo compile error, we are lenient
   (cumulative-page artifact, known leniency).
6. `factorial(@*ARGS[0].Int)` with no args — infinite recursion under Rakudo
   too (timeout); excluded.
7. `say 'a\b\c\\'` — verified identical by hand; the harness's Rakudo run
   was the artifact. Excluded.
8. `.VAR.default` on a cumulative page that REDECLARES `my $language`
   several times in one scope — redeclaration semantics differ (Rakudo
   warns and keeps one container; we re-declare). Standalone blocks pass.

## Known Roast residue from the post-GLR change

S32 multislice-6e adverb-slices (`%hash{$a;$b;$c<>}:kv:delete` families)
partially regressed (hash file 395→210 of 549, array file −32):
assignability-through-a-call (`assignable-ok(%hash{$a;$b;$c}, …)`) and
dynamic `:$delete` slice adverbs need the 6.e slice machinery reconciled
with non-flattening semilists. Tracked as campaign residue; the round's net
Roast effect is positive (see the round-2 commit message for gate numbers).
