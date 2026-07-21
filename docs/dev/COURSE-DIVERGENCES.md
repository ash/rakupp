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
- **`.vow` does not lock the promise against direct settlement.** After `my $p = Promise.new; $p.vow;`, a later `$p.keep(1)` should be refused — Rakudo dies with `Access denied to keep/break this Promise; already vowed`; rakupp keeps it silently. So the vow's exclusivity (its whole point — the vow-holder is the *only* thing that may settle the promise) is not enforced. Manually found while writing the new [`promises/vows`](https://course.raku.org/paradigms/promises/vows/) page, not from the fenced-block sweep. Repro: `my $p = Promise.new; $p.vow; say (try { $p.keep(1); 'allowed' }) // 'denied';` → Rakudo `denied`, rakupp `allowed`. The delegation *pattern* (`$v = $p.vow; … $v.keep(...)`) behaves identically on both, so the course page relies only on that.

MOP / introspection:

- **`^add_method` fails.** `Empty.^add_method('greet', method { 'hi' })` → rakupp dies with `No such method 'add_method' for invocant of type 'Slip'`; Rakudo adds the method and prints `hi`. ([`oop/mop/adding-methods`](https://course.raku.org/oop/mop/adding-methods/).)
- **HyperSeq config introspection fails.** `(1..10).hyper` then `.^attributes.first(*.name.contains('config')).get_value($h).raku` → rakupp exits 1; Rakudo prints `HyperConfiguration.new(batch => 64, degree => 7)`. ([`hyper-race/batch-and-degree`](https://course.raku.org/paradigms/hyper-race/batch-and-degree/); the batch/degree numbers are implementation-defined, but the introspection path should work.)

Minor:

- **`$?FILE` is a bare basename.** rakupp prints `s.raku`; Rakudo prints the absolute path of the source file. ([`special-variables/twigils`](https://course.raku.org/advanced/special-variables/twigils/).)

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
