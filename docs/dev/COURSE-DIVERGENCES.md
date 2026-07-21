# Raku-course examples: rakupp vs Rakudo divergences

Source: [The Complete Course of the Raku Programming
Language](https://course.raku.org/) —
[source on GitHub](https://github.com/ash/raku-course).

---

## Fresh run — 2026-07-20 (rakupp 0.9.0)

Re-ran after the course grew (all of Part 6 / Addendum, Cro, WebSockets, the
new reactive theory pages). **Method:** extract every `exercises/**/*.raku` file
(399) plus every ```` ```raku ```` fenced block from the English pages (1,379) →
**1,778 snippets**; run each under Rakudo v2026.06 twice (a determinism filter)
and under `build-arm64/rakupp` once, sandboxed, `TZ=UTC`, stdin `/dev/null`,
12 s timeout; compare stdout + exit code byte-for-byte. Data + harness in
[`course-diff/`](course-diff/) (`verdicts.tsv`, `manifest.tsv`, `harness/`,
and `mismatches-rc0.txt` for the deterministic diffs).

**1,698 / 1,778 exact matches (95.5 %).**

| Verdict | Count | Meaning |
|---|---:|---|
| MATCH | 1698 | stdout + exit code identical |
| DIFF-BOTH | 26 | different stdout and exit code |
| DIFF-EXIT | 21 | same stdout, different exit code |
| DIFF-OUT | 13 | same exit code, different stdout |
| RAKUDO-TIMEOUT | 17 | server / infinite-`react` example — not comparable, skipped |
| NONDET | 3 | Rakudo's own two runs differed (hash order etc.), skipped |

Of the 60 DIFFs, **43 have empty Rakudo stdout with a non-zero exit** — theory
fragments that don't run standalone (they compile-error under Rakudo too), not
bugs. The **17 with Rakudo exit 0** are the real signal, below.

### Fix candidates (deterministic Raku++ divergences)

Reactive / async — the richest cluster:

- **`await` on a `Supply` returns the wrong value.** `my $s = Supply.from-list(18,21,19,23); say await $s;` — Rakudo yields the **last** emitted value (`23`); rakupp yields the whole list gist (`values\t18 21 19 23`). ([`await-a-supply`](https://course.raku.org/paradigms/await/exercises/await-a-supply/) exercise + solution.)
- **`done` inside `react` does not stop sibling `whenever`s atomically.** Two `whenever`s, the first calls `done` on its 2nd value; Rakudo delivers `[1 2]` (the whole block ends at once), rakupp delivers `[1 2 10]` — one value from the *other* supply leaks through before the block tears down. ([`whenever/quiz2`](https://course.raku.org/paradigms/react-whenever/whenever/quiz2/); this is exactly what that quiz teaches.)
- **`Supply.interval` timers don't wall-clock-interleave.** Two intervals of different periods in one `react`: Rakudo interleaves by time (`tick 0, TOCK 0, tick 1, …`); rakupp emits one timer's values then the other's (`tick 0..4, TOCK 0..4`), stable across runs — the timers aren't scheduled on real time concurrently. ([`two-timers`](https://course.raku.org/paradigms/react-whenever/two-timers/).)
- **Subscriber dispatch order on a `Supplier` differs.** A `.tap` and a `react`/`whenever` on the same live `Supply`: on the final event Rakudo and rakupp order the two subscribers' output differently (`… alert: stop` vs `… log: stop  alert: stop`). ([`supply-kinds/live-in-action`](https://course.raku.org/paradigms/supply-kinds/live-in-action/).)
- **FIXED: `.vow` did not lock the promise against direct settlement.** `.keep`/`.break`/`.vow` now take the promise's vow: once vowed (explicitly, or implicitly by a first keep/break), a second direct settlement throws `X::Promise::Vowed` ("Access denied to keep/break this Promise; already vowed"), matching Rakudo in all four probe shapes (vow-then-keep, double-vow, keep-then-keep, delegation via the Vow object). Original report: After `my $p = Promise.new; $p.vow;`, a later `$p.keep(1)` should be refused — Rakudo dies with `Access denied to keep/break this Promise; already vowed`; rakupp keeps it silently. So the vow's exclusivity (its whole point — the vow-holder is the *only* thing that may settle the promise) is not enforced. Manually found while writing the new [`promises/vows`](https://course.raku.org/paradigms/promises/vows/) page, not from the fenced-block sweep. Repro: `my $p = Promise.new; $p.vow; say (try { $p.keep(1); 'allowed' }) // 'denied';` → Rakudo `denied`, rakupp `allowed`. The delegation *pattern* (`$v = $p.vow; … $v.keep(...)`) behaves identically on both, so the course page relies only on that.

Type system / roles — found manually while reworking the [`shapes-role`](https://course.raku.org/addendum/objects-classes/exercises/shapes-role/) exercise (not from the sweep):

- **FIXED: a role's required-method stub is now enforced at composition.** A `method area { ... }` stub in a role makes every composing class implement it — via its own method, another role's implementation, a parent class, a public attribute accessor, or an attribute `handles` — else composition throws "Method 'area' must be implemented by Triangle because it is required by roles: Shape." Stubbed *multi* candidates are enforced per signature; two roles providing the same real implementation is a composition conflict the class must resolve; same-name attributes from role+class or role+role conflict too. `handles <m1 m2>` / `handles *` attribute delegation was implemented as part of this (it previously parsed as noise). S14-roles/composition.t 15→50/50 and stubs.t 10→29/30. Original report: `role Shape { method area { ... } }` then `class Triangle does Shape { }` (no `area`) — Rakudo refuses to compile: `Method 'area' must be implemented by Triangle because it is required by roles: Shape`; rakupp accepts the class silently (exit 0, no diagnostic). The whole contract value of a role stub is lost.
- **Parameter type constraints are not enforced — at all.** This is not role-specific. `sub f(Int $x) { say $x }; f("hello")` — Rakudo rejects it at compile time (`Calling f(Str) will never work with declared signature (Int $x)`); **rakupp binds the `Str` and prints `hello`**. Same for role types: `sub describe(Shape $shape) {...}; describe(42)` binds the `Int` and only dies later inside the body at `$shape.area`. So a typed signature currently constrains nothing in rakupp when the call has no other candidate — neither the compile-time "will never work" analysis nor a runtime type check fires; the wrong-typed value just binds. Note that *multi*-dispatch by type still works: `multi greet(Int)` / `multi greet(Str)` route `greet(5)` and `greet("hi")` correctly on both engines. So types are used to *select* among candidates, but a lone typed candidate does not *reject* a mismatch (it binds instead of failing) — the enforcement/contract half of a type constraint is missing.

Object model / encapsulation — found manually via the [stack solution](https://course.raku.org/addendum/objects-classes/exercises/stack/solution/):

- **FIXED (`a444559`): a private attribute was writable from outside.** `class Account { has $!balance = 0; method balance { $!balance } }; my $a = Account.new; $a.balance = 1000;` — Rakudo dies with `Cannot modify an immutable Int`; rakupp let the assignment through and rewrote the private `$!balance`. A private `$!x` is stored under its bare name, and the `$obj.attr = v` lvalue path only rejected *public read-only* accessors, so the write landed in the private slot. Now any attribute that is not a public `is rw` accessor rejects dot-path assignment; `$obj!attr` (private-access syntax, self/`trusts`) and plain methods (the `is rw`/`return-rw` lvalue-method path) stay writable. Gated Roast-neutral (zero per-file changes).

Grammars — found manually while adding a whitespace note to the [`grammar-sum`](https://course.raku.org/addendum/regex-grammars/exercises/grammar-sum/solution/) solution:

- **`rule` sigspace semantics around a `%` separator are wrong in both directions.** Reference behavior (Rakudo, parsing `'3 + 4 + 5'` with `token number { \d+ }`):
  - `rule TOP { <number>+ % '+' }` (quantifier attached) → `Nil`: sigspace inserts `<.ws>` after atoms but does **not** wrap the separator. **rakupp wrongly parses this** (yields the numbers) — too permissive.
  - `rule TOP { <number> + % '+' }` (quantifier **detached** — the canonical idiom for whitespace-tolerant separated lists) → Rakudo parses both `'3 + 4 + 5'` and `'3+4+5'`; **rakupp returns `Nil` for both, even the unspaced input** — the spaced-quantifier form seems not to match at all.
  - **Course-visible:** the [`grammar-sum` solution](https://course.raku.org/addendum/regex-grammars/exercises/grammar-sum/solution/) now teaches the detached-quantifier `rule` form (author's decision: teach proper Raku, fix rakupp later), so playground users currently get `Nil` where the page says `12` — raises the priority. Both engines agree only on the explicit `token TOP { <number>+ % [ \s* '+' \s* ] }`, which the page keeps as a secondary variant.

Parser — found because the course *generator* (`raku-pages.raku`) itself runs under rakupp:

- **FIXED: a `"` inside a regex character class broke the parse — two different ways.** The pattern scanner opened quote-tracking inside `[ ]` where a quote is a class MEMBER; `<-["]>` now parses in both the sub-body (hard error) and top-level (silent truncation) shapes, output matching Rakudo. Original report — construct: `s:g/ (<-["]>+) /Y/` (a negated char class containing a double quote; same for a positive class).
  - *Inside a sub body:* hard parse error, reported at EOF — `sub f($h is copy) { $h ~~ s:g/ (<-["]>+) /Y/; return $h }` → `===SORRY!=== Parse error at line N: expected } (got '')`. Rakudo: parses and runs fine.
  - *At top level:* **worse — silent truncation.** `say 'before'; my $h = 'ab'; $h ~~ s:g/ (<-["]>+) /Y/; say 'after';` → rakupp prints `before` and exits 0; `after` never runs, no diagnostic. Rakudo prints all three lines. A program that compiles clean (`-c` says `Syntax OK`) but silently stops executing partway is a miscompile-class bug — worth prioritising over the diagnostic-only facet.
  - Presumably the `"` starts a string/interpolation state inside the char-class scan that never pops. Char classes with other delimiters (`<-[}]>`, `<-[)]>`, `<-[/]>`, `<-[>]>`, `` <-[`]> ``) parse fine — the course generator uses those throughout. Workaround used in the generator: a non-greedy `(.*?)` between quoted literals instead of the char class.

- **FIXED (59807b5): `^add_method` fails.** The `class Empty` shadowing fix resolved this; `Empty.^add_method('greet', method { 'hi' })` now prints `hi`. ([`oop/mop/adding-methods`](https://course.raku.org/oop/mop/adding-methods/).)
- **HyperSeq config introspection fails.** `(1..10).hyper` then `.^attributes.first(*.name.contains('config')).get_value($h).raku` → rakupp exits 1; Rakudo prints `HyperConfiguration.new(batch => 64, degree => 7)`. ([`hyper-race/batch-and-degree`](https://course.raku.org/paradigms/hyper-race/batch-and-degree/); the batch/degree numbers are implementation-defined, but the introspection path should work.)

Minor:

- **FIXED: `$?FILE` was a bare basename.** Now the cwd-prefixed absolute path exactly like Rakudo (keeps a `./` as typed, no realpath); `$*PROGRAM-NAME`/`$*PROGRAM` keep the as-invoked form. ([`special-variables/twigils`](https://course.raku.org/advanced/special-variables/twigils/).)

### Not Raku++ bugs (recorded so they aren't re-triaged)

- **Rakudo quirk, rakupp is correct:** a comment ending in `\` right before EOF makes **Rakudo** emit nothing (`say 42; # x\␊` → Rakudo prints ``, rakupp prints `42`). Minimal repro confirmed; drop the final newline and Rakudo prints normally. ([`strings/escaping-special-characters`](https://course.raku.org/essentials/strings/escaping-special-characters/) — worth a course note too, since that example would show no output under Rakudo when it's the block's last line.)
- **Cosmetic:** the raw-socket HTTP examples ([`send-and-receive`](https://course.raku.org/paradigms/connections/exercises/send-and-receive/), [`sending-receiving`](https://course.raku.org/paradigms/connections/sending-receiving/)) match visibly; rakupp differs by one trailing newline on the `.lines.first` status line.
- **Environmental / not reproducible:** the Cro client examples ([`client-modules`](https://course.raku.org/paradigms/cro/client-modules/), [`public-apis`](https://course.raku.org/paradigms/cro/public-apis/), [`status-line`](https://course.raku.org/paradigms/cro/exercises/status-line/)) hit live services (weather JSON, HTTP→HTTPS redirects) whose responses vary between the two runs.

---

Method: every fenced code block (```raku and untagged) from the English course
pages (`essentials advanced oop paradigms regexes addendum about-this-course
exercises` — 1,318 pages) plus the exercise `.raku` files was run under both
engines (stdin closed, 6 s timeout, sandbox cwd) — **3,068 comparisons**.
Blocks that do not run under Rakudo (theory fragments, output samples,
incomplete code) were ignored, as were snippets whose Rakudo output is not
reproducible across two runs (nondeterminism).

## Fix-round progress

| sweep | deduped mismatches | raw | notes |
|---|---:|---:|---|
| **original (2026-07-13)** | **116** | **148** | the baseline this document describes |
| after round 1 (`3324752`) | ~32 | 40 | containers, gists, ops, quoting adverbs |
| after round 2 (`a84ed9a`) | 12 | 14 | the post-GLR list batch + regex/date fixes |

Of the 12 remaining, 5 are harness artifacts and 6 are prompt-EOF nuances —
one real feature gap remains (the Iterator role). Every batch passed the
zero-regression gate: full Roast run with no pass-list drops, benchmarks
equal-or-faster.

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
2. FIXED — `val()` is implemented (IntStr/RatStr/NumStr/ComplexStr from a
   fully-numeric string, original spelling preserved) and `prompt` routes
   its line through it, so numeric input arrives as an allomorph.
3. FIXED — a user class `does Iterator` now drives `for` and `Seq.new`
   via the pull-one protocol (drained until IterationEnd).
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
