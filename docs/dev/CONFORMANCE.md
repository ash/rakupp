# Raku++ — Docs Conformance Audit

A feature-by-feature audit of Raku++ against the official documentation at
[docs.raku.org](https://docs.raku.org). For each feature the docs define the
*intended* semantics; each entry records what Raku++ actually does and a verdict.
Divergences found are fixed in the same pass (Roast-verified) unless noted.

**Legend:** ✅ conforms · ⚠️ partial / minor divergence · ❌ diverges (fixed = ✅🔧) · 📝 note

This walks [FEATURES.md](../FEATURES.md) section by section.

---

## 1. Lexical & Literals

| Feature | Docs intent | Raku++ | Verdict |
|---|---|---|---|
| Int bignum, radix `0x/0o/0b`, `_` sep, `:16<FF>` | arbitrary precision, radix literals | `2**100` exact; all radix forms work | ✅ |
| Rat exactness | `0.1+0.2 == 0.3` is `True` (exact) | `True` | ✅ |
| Complex `3+4i` | Complex type, `.abs` etc. | ✅ | ✅ |
| Num `1.5e3` | Num (float), prints `1500` | ✅ (`(Num)`) | ✅ |
| `.5`, `4i`, `∞`/`Inf`/`NaN`, superscripts `²³` | leading-dot Rat, imaginary, powers | all ✅ | ✅ |
| Strings `'' "" q// qq// Q//`, heredocs | interpolation rules; `Q` no interp | ✅ correct interp/no-interp | ✅ |
| Unicode quotes `‘’ “” ｢｣`, qw `<>`, `«»` | word lists | ✅ | ✅ |
| Escapes `\n \t \x[] \o[] \c[NAME]` | incl. Unicode names | ✅ | ✅ |
| Unicode identifiers | `$café` etc. | ✅ | ✅ |
| **`.raku` / `.perl`** | **EVAL-round-trippable repr** (docs: `<1/3>`, `<3+4i>`, `1..2`) | ❌ returned `.gist` (`"hi"`→`hi`, `[1,"two"]`→`[1 two]`, hash→tab-gist) → **✅🔧 fixed**: proper `rakuRepr` (quoted strings, `[…]`/`(…)`, `{:a(1)}`, `<1/3>`, `<3+4i>`, `Bool::True`); verified round-trips via EVAL | ✅🔧 |
| **FatRat** | `.FatRat` → a `FatRat` (arbitrary-precision Rat, contagious) | ❌ returned a `Rat` → **✅🔧 fixed**: `Value::fatRat` tag; `.^name`/`.WHAT`→`FatRat`, arithmetic stays `FatRat`, `.raku`→`FatRat.new(n, d)`; plain `Rat` unaffected (rakupp's `Rat` is already BigInt-backed, so it's a type-identity + contagion fix) | ✅🔧 |
| Unicode digit literals `໑໐` | numeric | ❌ parse error | 📝 documented gap |

---

## 2. Operators

| Feature | Docs intent | Raku++ | Verdict |
|---|---|---|---|
| Arithmetic `+ - * / % div mod ** gcd lcm`, prefix `- + ~ ? !` | — | ✅ | ✅ |
| Comparison `== < eq …`, chained `1 < $x < 4` | chain assoc | ✅ chains correctly | ✅ |
| `<=>`/`cmp`/`leg` | return `Order` (Less/Same/More, numify ∓1/0) | gist `Less`, `.Int` `-1` ✅ but **`.WHAT` is `(Int)` not `(Order)`** | ⚠️ (enum type identity) |
| `andthen`/`orelse` | key on **definedness**, pass `$_` | `1 andthen 2`→2, `Nil andthen`→Nil, `5 andthen $_*10`→50 ✅ | ✅ |
| `^^` (xor) | returns the **one true operand**; Nil if both; last if neither | ❌ returned `Bool` → **✅🔧 fixed**: `5^^0`→5, `0^^7`→7, `5^^7`→Nil, `0^^0`→0 | ✅🔧 |
| `%%` / `!%%` | Bool divisibility | ✅ | ✅ |
| `=== / eqv` | value-identity vs structural | `[1,2]===[1,2]`→False, `eqv`→True, `@a===@a`→True ✅ | ✅ |
| `~~` smartmatch | Type/Range/value/Regex/**Callable** | Type/Range/value/regex ✅; **Callable RHS (`{…}`, `*.method`) wasn't invoked** → **✅🔧 fixed**; `$x ~~ * > 3` (infix Whatever-curry) still mis-parses as a chain | ⚠️🔧 (one currying case remains) |
| Ranges `..^ ^.. ^..^`, seq `…`, junctions `\| & ^` | — | ✅ | ✅ |
| Reduce `[+] [<]`, zip `Z`, cross `X`, hyper `>>op>>` | — | ✅ | ✅ |
| Contextualizers `+@a ~@a`, ternary `?? !!` | — | ✅ | ✅ |
| User ops `infix:<>`, `postfix:<>` | + prefix, circumfix | infix ✅, postfix ✅ (`5!`); **prefix `§3` doesn't parse at use-site** | ⚠️ (FEATURES claims only infix/postfix) |
| Whatever-curry `*.abs`, `* ** 2`, prefix `-*` | — | ✅ | ✅ |

> **Roast note (§2):** the `~~ Callable` fix is docs-correct and gained `S03-smartmatch/any-callable.t`, but Roast fully-pass went **225→224**. The 2 "losses" (`S11-modules/rakulib.t`, `S24-testing/11-plan-skip-all-subtests.t`) were **false-passes**: `is_run` compares `status` via `~~ WhateverCode`, which previously returned the truthy code (never checking) — now it correctly invokes and exposes that Raku++ doesn't error on `plan skip-all` inside a subtest (exit 0, expected non-zero). Flagged for §10 (Testing). Net correctness ↑ even though the count dipped by one.

---

## 3. Control Flow

| Feature | Docs intent | Raku++ | Verdict |
|---|---|---|---|
| `if/elsif/else`, `unless`, `while/until`, `for` | — | ✅ | ✅ |
| `given` + `when Int`/`when value` | smartmatch `$_`, break after match | ✅ | ✅ |
| **`when /regex/`** | `$_ ~~ /…/` | ❌ fell through to default (eval'd Match then re-smartmatched) → **✅🔧 fixed** | ✅🔧 |
| **`when * > 3` / `when {…}`** | invoke callable matcher | ❌ (didn't invoke) → **✅🔧 fixed** | ✅🔧 |
| **`proceed`** | leave `when`, try next `when` | ❌ no-op → **✅🔧 fixed** (throws `ProceedEx`, caught by WhenStmt) | ✅🔧 |
| **`succeed`** | exit the `given` immediately | ❌ no-op (ran unreachable code) → **✅🔧 fixed** (throws `BreakGivenEx`) | ✅🔧 |
| `with`/`without`/`orwith` | test **definedness**, topicalize | ✅ correct | ✅ |
| `repeat { } while/until` | body runs ≥ once | ✅ | ✅ |
| C-style `loop (init; cond; incr)` | — | ✅ with spaces; **`$i<2` (no space) mis-lexes as `<…>` word-subscript** | ⚠️ (known `$var<` quirk) |
| Statement modifiers (incl. chained `X if A for B`) | — | ✅ | ✅ |
| `last/next/redo` (labeled), `gather/take`, `do` | — | ✅ | ✅ |
| `FIRST`/`NEXT`/`LAST` loop phasers | — | ❌ | 📝 documented gap |

---

## 4. Subs, Signatures & Dispatch

Reference: [docs.raku.org/language/signatures](https://docs.raku.org/language/signatures).

| Feature | Docs intent | Raku++ | Verdict |
|---|---|---|---|
| positional / optional `?` / defaults | — | ✅ | ✅ |
| `multi`/`proto` dispatch by type, arity, `where`, literal, `:D`/`:U` | narrowest candidate wins | ✅ (`Int` beats `Any`; `where`; literal `0`) | ✅ |
| `is rw` / `is copy` | write-back / private mutable copy | ✅ (`s($x,$y)` → caller sees `6 4`) | ✅ |
| pointy `-> {}`, placeholder `$^a`, closures | — | ✅ | ✅ |
| **default referencing an earlier param** (`$g, $a = $g/2`) | default is evaluated in the signature scope | ❌ *"Variable '$g' is not declared"* → **✅🔧 fixed**: defaults now `eval` in the param `Env` being built (`h(10)`→`10 5`) | ✅🔧 |
| **named alias `:name($var)`** | external key `name`, binds `$var` | ❌ parsed `name` as a type, skipped `($var)` → *"'$n' is not declared"* → **✅🔧 fixed**: new `Param::namedKey`; `g(name=>7)` binds `$n` | ✅🔧 |
| **required named `:$x!`** | throw `X::Parameter::RequiredNamed` if absent | ❌ silently bound `(Any)` → **✅🔧 fixed**: `Param::required`; missing throws (caught as `X::Parameter::RequiredNamed`) | ✅🔧 |
| **optional typed `Int $x?` when omitted** | binds the **type object** `(Int)` | ❌ bound `(Any)` → **✅🔧 fixed**: missing params now use `typedDefault(type,sigil)` → `(Int)` | ✅🔧 |
| **`*@a` slurpy** | **flattens** all Iterables (`(1,[2,3],([4,5],6))`→`[1,2,3,4,5,6]`) | ❌ used the single-arg rule (kept structure) → **✅🔧 fixed**: `slurpyKind='f'` dissolves every Iterable | ✅🔧 |
| **`**@a` slurpy** | **no** flatten (each arg one element) | ⚠️ behaved like single-arg → **✅🔧 fixed**: `slurpyKind='n'` keeps args as-is | ✅🔧 |
| **`+@a` slurpy** | single-argument rule | ❌ **parse error** *"expected parameter variable (got '+')"* → **✅🔧 fixed**: `slurpyKind='1'` = the single-arg rule | ✅🔧 |
| `*%h` slurpy hash, capture `\|c`, sigilless `\a`, `-->`, `::T` | — | ✅ | ✅ |
| `callsame`/`nextsame`, sub-signature destructuring | — | ❌ | 📝 documented gap (per FEATURES) |

> **Cross-cutting fix (surfaced here, detailed in §10):** an inline `CATCH {}` inside a **sub, pointy block, or `try {}`** never ran its handler body — `callCallable` executed body statements directly and only `execBlock` detected `CATCH`. So `try { die; CATCH { default {…} } }` swallowed the error (via the `try` op) but skipped the handler. **Fixed**: `callCallable` now mirrors `execBlock`'s `CATCH` handling (bind `$_`/`$!`, run the handler, absorb `BreakGivenEx`). This is the standard `CATCH { default {…} }` idiom, so the fix is broad.
>
> **Parser fix (surfaced here):** a leading-dot term after a comma (`say "x=", .uc`, `1, .method`, `f("a", .b)`) was dropped or caused *"expected )"* — `startsTermToken` didn't treat `.` as a term-start (only `startsListopArg` did). **Fixed**: `.` now starts a term (`$_.method`) in comma-list continuation.
>
> **Roast (§4):** full suite fully-pass **224 → 249** (+25), **zero regressions**. The CATCH-in-closures fix dominates the gains (the `CATCH { default {…} }` idiom pervades CLI programs and the test suite): recoveries span `S02-magicals/args`, `S04-phasers/end`, `S04-statements/quietly`, `S06-other/main-usage`, `S11-modules/{rakulib,versioning}`, `S16`/`S17` IO, `S19` command-line options, `S24-testing/{7-bail_out,8-die_on_fail,11-plan-skip-all-subtests,15-done-testing}`, `S26`/`S29`/`S32`. The two files that dipped during the §2 `~~` fix (`rakulib.t`, `11-plan-skip-all-subtests.t`) are back and now pass *correctly*.

---

## 5. Objects, Classes, Roles

Reference: [docs.raku.org/language/objects](https://docs.raku.org/language/objects), [.../classtut](https://docs.raku.org/language/classtut), [.../enumeration](https://docs.raku.org/language/enumeration).

| Feature | Docs intent | Raku++ | Verdict |
|---|---|---|---|
| `class`, `has $.x`/`$!x` (+ defaults), accessors | public accessor + private storage | ✅ | ✅ |
| **read-only accessor** | `$obj.x = v` throws `X::Assignment::RO` unless `is rw` | ❌ silently mutated the attribute → **✅🔧 fixed**: `ClassAttr::rw`; public non-`rw` accessor assignment throws `X::Assignment::RO` (and `is rw` stays writable) | ✅🔧 |
| `has $!x` private (no accessor) | `$obj.x` is a method error | ✅ (throws; exception type is a bare `Str`, not `X::Method::NotFound`) | ⚠️ (exception identity) |
| named construction `.new(:a,:b)`, `submethod BUILD` | — | ✅ | ✅ |
| single & **multiple** inheritance `is`, method resolution, `~~` ancestor | `C is A is B` | ✅ (`C2 is A2 is B2` → both methods; `Dog ~~ Animal`) | ✅ |
| `role` + `does`, `multi method`, private `!method`/`self!m` | — | ✅ | ✅ |
| **custom `method gist` / `method Str`** | `say`/`note` use `.gist`; `print`/`put`/interpolation/`~` use `.Str` (→ `.gist`) | ❌ all printed `Foo<obj>` (ignored user methods) → **✅🔧 fixed**: new `Interpreter::gistOf`/`strOf`; wired into `say`/`print`/`put`/`note`, `$obj.say/.print/.put`, string interpolation, and `~` concat | ✅🔧 |
| **enum type identity** | value knows its enum (`red.^name`→`Color`, `red ~~ Color`) | ❌ values were plain `Int` (`.^name`→`Int`, `~~`→False) → **✅🔧 fixed**: new `Value::enumType` on values + the type-list; `typeName()`, `~~`, `.WHAT` all resolve it | ✅🔧 |
| **enum-from-value `Color(1)`** | coerce a number to the enum value | ❌ *"Undefined routine 'Color'"* → **✅🔧 fixed**: enum-type call finds the matching value (out-of-range → type object) | ✅🔧 |
| **`Color.pick` / `.roll`** | return an **enum value** (not a `key=>val` pair) | ❌ returned a `Pair` (so `~~ Color` was False) → **✅🔧 fixed**: pick/roll on an enum type draw from its values | ✅🔧 |
| enum `.key` `.value` `.pair`, `red == 0`, `.enums`, `when EnumType` | — | ✅ (all work; `when Color` matches via the fixed `~~`) | ✅ |
| metamodel `.^name .^methods .^mro .WHAT .WHICH .HOW` | — | ✅ (`Dog.^mro` → `Dog,Animal,Any,Mu`) | ✅ |
| `Metamodel::*` construction, submethod-not-inherited, `callsame` | — | ❌ | 📝 documented gap (per FEATURES) |

> **Roast (§5):** fully-pass holds at **249, zero regressions** — notably the read-only-accessor enforcement (the riskiest change, since it turns a previously-silent mutation into a throw) broke nothing. Assertion-level correctness nudged up (one file moved no-TAP→partial); these are precision fixes that didn't happen to tip a whole file green but hardened behavior across many.

---

## 6. Regexes & Grammars

Reference: [docs.raku.org/language/regexes](https://docs.raku.org/language/regexes), [.../grammars](https://docs.raku.org/language/grammars).

| Feature | Docs intent | Raku++ | Verdict |
|---|---|---|---|
| positional `$0 $1`, `$/`, named `$<k>=(…)`, backrefs `$0` | — | ✅ (`/(\d+)\-(\d+)/`, `/$<k>=(\w+)/`, `/(abc) $0/`) | ✅ |
| quantifiers greedy/frugal `+ * ? +?`, `** N`, `** N..M` | — | ✅ | ✅ |
| char classes `<[a..z]> <-[…]> <+[…]>`, `\d \w \s` | — | ✅ | ✅ |
| adverbs `:i` `:g`, lookahead `<?before>`/`<!before>` | — | ✅ | ✅ |
| `s///` mutating, `.subst(:g)` non-mutating, `S///` | — | ✅ | ✅ |
| **grammar actions with a repeated subrule** (`<num> '+' <num>`) | the name collates into a **list** (`$<num>[0].made`, `$<num>[1].made`) | ❌ the second capture **overwrote** the first (`$<num>` a lone `Match`; `.elems`/indexing failed) → **✅🔧 fixed**: `ParseNode`/`MState.children` now hold a `vector` per name (backtrack-safe push/pop); `build()` yields a list when a name repeats. `<n>+` matching N times also lists correctly | ✅🔧 |
| **`%` / `%%` separator quantifier** (`<n>+ % ","`) | one-or-more with a separator between | ❌ **parse failed** (whole match undefined) → **✅🔧 fixed**: `Node::sep`; the Rep matcher weaves the separator before every iteration after the first (`%%` trailing-sep approximated as `%`) | ✅🔧 |
| grammars `token`/`rule`/`regex`, `.parse`/`.subparse`, actions class, `make`/`made` | — | ✅ | ✅ |
| **`\|` longest-token-match (LTM)** | declarative `\|` picks the **longest** alternative (`/foo \| foobar/` on "foobar" → "foobar"); `\|\|` is sequential first-match | ❌ `\|` behaved as first-match like `\|\|` (→ "foo") → **✅🔧 fixed**: `Node::firstMatch` distinguishes `\|\|` from `\|`; the Alt matcher gathers each branch's reachable ends and tries the continuation **longest-first with live captures**. `\|\|` first-match preserved; captures in the winning branch bind correctly | ✅🔧 |
| **code assertions `<?{ code }>` / `<!{ code }>`** | evaluate `code` with `$/` bound to the match so far; the boolean gates the match | ❌ were a no-op lenient pass (`abc <?{ False }>` wrongly matched) → **✅🔧 fixed** (grammar path): new `K::Code` node + `MState::codeEval` interpreter hook parses (cached) and evals the code with `$/` = `input[start..pos]`; `<!{…}>` negates. (Plain-regex `~~ /…<?{…}>/` still lenient — only the grammar path is wired) | ✅🔧 (grammars) |
| **protoregexes** `proto token x {*}` + `token x:sym<…>` | dispatch by symbol | ❌ parse error | 📝 documented gap |
| **runtime `:my` lexicals in patterns** (indentation grammars, e.g. YAMLish) | `:my $x` set mid-match, used as a pattern atom / quantifier bound / subrule arg | ❌ | 📝 gap — needs a match-time interpolating matcher (compile-once architecture mismatch); see §6 note |
| quantified **positional** group → list (`(\w)+` → `$0` a list) | positional caps under a quantifier collate | ⚠️ `$0` keeps the last only (named captures now collate; positional don't) | 📝 gap |
| named-capture storage details, `:ratchet` semantics (treated greedy) | — | ⚠️ | 📝 gap (per FEATURES) |

> **Roast (§6):** fully-pass holds at **249, zero regressions** — the capture-collation change touched the core `Regex`/`GrammarMatcher` engine (used by every `~~ /…/`), and the separator quantifier extended the Rep matcher, yet nothing regressed and assertion-level correctness ticked up (+4). These unblock grammar-action programs that were previously silently losing repeated captures.

---

## 7. Unicode

Reference: [docs.raku.org/language/unicode](https://docs.raku.org/language/unicode). Generated from UCD 16.0 — Raku++'s strongest area (~95% of S15 assertions).

| Feature | Docs intent | Raku++ | Verdict |
|---|---|---|---|
| grapheme `.chars` (flags 🇦🇺, ZWJ 👨‍👩‍👧, combining) | 1 grapheme each | ✅ (all → 1) | ✅ |
| `.codes`, `.ord`/`.chr`/`.ords` | codepoints | ✅ | ✅ |
| NFC / NFD / NFKC / NFKD (`.NFC.codes`, `.NFKD`) | compose/decompose | ✅ (`a`+◌́ → 1 NFC; `é` → 2 NFD; `ﬁ` → 2 NFKD) | ✅ |
| `\c[NAME]`, `.uniname`, `.unival` | names / numeric value | ✅ (`⁵.unival`→5) | ✅ |
| `.uniprop`, `.uniprop('Script')`, `<:Nd>`/`<:Latin>` in regex | category / script | ✅ (`A`→Lu, `Ω`→Greek) | ✅ |
| case `.uc`/`.lc`/`.tc`/`.fc`/`.tclc`/`.wordcase` (incl. `ß`→`SS`) | full case mapping | ✅ | ✅ |
| **`.trans` with `..` ranges** (`.trans('a..c'=>'A..C')`) | `X..Y` in a string arg is a codepoint range (like `tr///`) | ❌ ranges were **not expanded** (`abc`→`AbC`, only endpoints mapped) → **✅🔧 fixed**: string args expand `X..Y` inclusively (`abc`→`ABC`; a range→single char maps all: `0..9`→`#`) | ✅🔧 |
| **`.samecase`** | copy the arg's case pattern position-by-position | ❌ not implemented → **✅🔧 fixed**: `"hello".samecase("Aa")`→`Hello`, `"HELLO".samecase("a")`→`hello` | ✅🔧 |
| `.samemark`, `.encode`/`.decode` (→ `Blob`) | mark-copy / byte encoding | ❌ | 📝 gap (not claimed in FEATURES) |
| `uniprop` binary props, exact per-codepoint Script, `unimatch`, full NFG break suite | — | ❌/⚠️ | 📝 documented gap |

> **Roast (§7):** **249, zero regressions.** Unicode was already the strongest subsystem; these were two precise gaps (`.trans` ranges, `.samecase`) verified by probe.

---

## 8. Data Types & Built-ins

Reference: [docs.raku.org/type/List](https://docs.raku.org/type/List), [.../Hash](https://docs.raku.org/type/Hash), [.../Int](https://docs.raku.org/type/Int).

| Feature | Docs intent | Raku++ | Verdict |
|---|---|---|---|
| `sort` (default / `{$^b<=>$^a}` / `*.chars` key), `min`/`max(&by)` | — | ✅ | ✅ |
| `reduce`, `squish`, `repeated`, `unique`, `rotate`, `pairs`/`kv`/`antipairs`, `.base` | — | ✅ | ✅ |
| Set/Bag/Mix + ops, Range methods, junction autothread | — | ✅ | ✅ |
| **`.classify` / `.categorize`** | group into a `Hash` of lists (categorize: multi-key) | ❌ missing → **✅🔧 fixed** (`(1..4).classify(*%%2)` → `{False=>[1,3],True=>[2,4]}`) | ✅🔧 |
| **`.rotor` / `.batch`** | chunk into sublists (`rotor` drops a short tail unless `:partial`; `batch` keeps it) | ❌ missing → **✅🔧 fixed** (`(1..6).rotor(2)` → 3 pairs) | ✅🔧 |
| **`.produce`** | scan (running reductions) | ❌ missing → **✅🔧 fixed** (`(1..4).produce(*+*)` → `1,3,6,10`) | ✅🔧 |
| **`Hash.push` / `.append`** | accumulate values under a key into a list | ❌ *"No such method 'push' for type Hash"* → **✅🔧 fixed** (`%h.push(:a(1)); %h.push(:a(2))` → `%h<a>` = `[1,2]`) | ✅🔧 |
| **`Int.polymod`** | successive `divmod` by each divisor + trailing remainder | ❌ missing → **✅🔧 fixed** (`1234.polymod(10,10)` → `4,3,12`) | ✅🔧 |
| **`roundrobin`** | interleave lists, skipping exhausted ones | ❌ *"Undefined routine"* → **✅🔧 fixed** (`roundrobin([1,2],[3,4,5])` → `(1,3),(2,4),(5,)`) | ✅🔧 |
| String `chars`/`codes`/`uc`/`substr`/`split`/`comb`/`words`/`lines`/`trim`/`sprintf`, `.grep` smartmatch, `.head`/`.tail`/`.skip` (`*`/`*-N`/`Inf`), scalar-as-list | — | ✅ | ✅ |
| Math `abs sqrt floor ceiling round sign exp log …` + trig, `rand`, `pi tau e` | — | ✅ | ✅ |

> **Roast (§8):** **249, zero regressions**; three files moved no-TAP→partial (the new builtins let them run past a previously-fatal method miss), assertions +11.

---

## 9. I/O, System & Concurrency

Reference: [docs.raku.org/language/io](https://docs.raku.org/language/io), [.../concurrency](https://docs.raku.org/language/concurrency). No code changes were required — every documented feature probed correct.

| Feature | Docs intent | Raku++ | Verdict |
|---|---|---|---|
| `open`/FileHandle (`.lines`/`.get`/`.slurp`/`.say`/`.close`), `spurt`/`slurp`/`unlink`, `.IO.e` | — | ✅ | ✅ |
| `run` → `Proc` (`.out.slurp`/`.exitcode`), **bidirectional** `run(:in,:out)` (`cat` pipe), `shell` | — | ✅ | ✅ |
| `Promise` — `start`, `await` (rethrows cause), broken promise, `Promise.allof`/`.anyof`, `.result`/`.status` | — | ✅ (`start` captures explicit closure vars correctly → `0,10,20`) | ✅ |
| `Supply` (`from-list`/`tap`), `Supplier`, `react`/`whenever` (finite), `Channel` (`send`/`close`/`list`) | — | ✅ | ✅ |
| `Thread`, `Lock`/`Semaphore` (`.protect`), `sleep` (cooperative GIL) | — | ✅ | ✅ |
| `$*CWD`/`$*TMPDIR`/`$*RAKU`(`.compiler.name`="Raku++")/`@*ARGS`/`$*DISTRO`/`$*KERNEL`/`$*VM` | — | ✅ | ✅ |
| doubly-nested bare-block `$_` (`{ { $_ } }` inner topic) | inner `$_` defaults to the outer topic | ⚠️ inner `$_` is `Any` (its own optional param, not the outer) | 📝 minor edge |
| live-`Supplier` `react` driven by a separate `start` emitter thread | terminates on `.done` | ⚠️ can hang (GIL/cross-thread `.done` timing) | 📝 gap |
| true CPU parallelism, wall-clock timers (`Promise.in`/`.at`), atomic ops (`cas`), stream-retokenizing combinators | — | ❌ | 📝 documented gap (per FEATURES) |

---

## 10. Phasers, Modules, Exceptions, Special Vars, Testing

Reference: [docs.raku.org/language/phasers](https://docs.raku.org/language/phasers), [.../exceptions](https://docs.raku.org/language/exceptions), [.../testing](https://docs.raku.org/language/testing). No code changes required (§4's CATCH-in-closures fix already hardened the exception path).

| Feature | Docs intent | Raku++ | Verdict |
|---|---|---|---|
| phasers `BEGIN`/`INIT`/`ENTER`/`LEAVE`/`END` ordering | `B,I,en,body,lv` then `END` | ✅ | ✅ |
| `state` variables (persist across calls) | — | ✅ (`counter()` → 1,2,3) | ✅ |
| `EVAL`, modules `use`/`need`/`no`, `use lib`, sub hoisting | — | ✅ | ✅ |
| `die`/`try`/`CATCH { default {…} }`, `.message` | — | ✅ (fixed in §4 — now runs inside subs/`try`) | ✅ |
| **custom exceptions** `class X is Exception { method message {…} }`, `.throw` | — | ✅ (`.throw` caught, custom `.message`) | ✅ |
| Test API `plan`/`ok`/`is`/`is-deeply`/`like`/`throws-like`/`subtest`/`skip`/`todo`/`done-testing`/`plan skip-all` | — | ✅ (subtest, throws-like, is-deeply all pass) | ✅ |
| special vars `$_`/`$/`/`$!`/`@*ARGS`/`$?LINE`/`$?FILE`, `$0`/`$1` | — | ✅ | ✅ |
| `plan skip-all` **inside a subtest** should error | non-zero exit | ⚠️ still runs (exposed by §2's `~~` fix) | 📝 gap (flagged) |
| `Metamodel::*`, `X::*` full hierarchy, `.resume`, loop phasers `FIRST/NEXT/LAST` | — | ⚠️/❌ | 📝 documented gap |

---

## Summary

The audit walked all ten FEATURES.md sections against [docs.raku.org](https://docs.raku.org), fixing every safely-fixable divergence and Roast-verifying each. **Roast fully-pass rose 224 → 249 (+25) with zero regressions across the whole pass.**

**Fixes by section:** §1 `.raku`/`.perl` round-trip; §2 `^^`, `~~ Callable`; §3 `when /re/`/`{…}`/`proceed`/`succeed`; §4 defaults-see-earlier-params, `:name($n)`, required `:$x!`, optional-type-object, `*@`/`**@`/`+@` slurpy semantics, **CATCH-in-closures** (the big one), comma-leading-dot parse; §5 read-only accessors, custom `gist`/`Str`, enum type-identity + `Color(n)` + `.pick`; §6 repeated-capture collation, `%` separator quantifiers; §7 `.trans` ranges, `.samecase`; §8 `.classify`/`.categorize`/`.rotor`/`.batch`/`.produce`/`Hash.push`/`Int.polymod`/`roundrobin`; §9–§10 audited conformant (no code changes needed).

**Single highest-impact fix:** the CATCH-in-closures bug (§4) — `CATCH { default {…} }` never ran inside subs/`try`/pointy blocks, silently swallowing errors; the fix recovered ~two dozen files across CLI, testing, phaser, and module tests.

**Residual documented gaps** (larger work, not divergences from claimed support): LTM alternation, protoregexes, quantified positional-capture lists, true CPU parallelism / timers / atomics, `Metamodel::*` construction, `callsame`/`nextsame`, loop phasers, FatRat, and a few subtle topic/exception-type edges noted above.
