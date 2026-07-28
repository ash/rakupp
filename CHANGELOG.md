# Changelog

Release notes for tagged releases. Numbers are measured, not projected;
methodology for all Roast figures is in [docs/COUNTING.md](docs/COUNTING.md).

## Unreleased (on `main` since v1.2.6)

| | v1.2.6 | `main` |
|---|---:|---:|
| Documentation examples byte-identical to Rakudo | 936 | **944** |
| …of which Raku++ is the one that is wrong | 144 | **122** |
| Operator-matrix divergences | 72 | **30** |
| Roast assertions (all declared) | 196,381 | **196,395** |
| Roast files fully passing | 622 | **625** |
| Regression tests | 137 | **148** |

Both halves of <https://raku.online/spec/rules/divergences/> are measured here:
the per-type documentation sweep (1,451 examples) and the operator behaviour
matrix (833 expressions over 121 operators). Together the page's "Raku++ differs"
count went **202 → 152**.

Two caveats on reading those numbers. The conformance count has a **±5 flap
band** — the `Set`/`Bag`/`Mix`/`Map` examples move in both directions between
runs of identical code, because Rakudo randomizes hash iteration order per
process; two sweeps of the same tree gave 934 and 939. And six of the remaining
operator rows are `prefix:<~^>`, where *Rakudo* answers "not yet implemented" and
Raku++ works — counted as a divergence, but not a defect.

Roast itself flaps by a file: 624↔625 fully passing and 10↔11 timeouts across
runs, which moves the assertion total by about 20 either way. The figures above
are one coherent run, and the conservative of the three taken.

### Fixed

- Reported: [#8](https://github.com/ash/rakupp/issues/8) — two `--exe` codegen
  bugs, both about writing through a subscript. A closure mutating a captured
  container **by key** (`%bag{$k}++`, `@r[$i] += 1`) was not recorded as
  mutating it, so the variable was captured `const` and the generated C++ would
  not compile; and `.push` on a not-yet-existing element (`@ready[2].push(10)`)
  mutated a temporary and vanished, which is why the reporter's program looped
  forever once it did compile. The interpreter was always right; only the
  compiler diverged.

- **`@(…)` did not keep its Array.** `@(%h<k>).raku` gave `(1, 2)` where Rakudo
  gives `[1, 2]` — the contextualiser converted to a List instead of only
  stripping the itemisation. Found while writing the containers FAQ page against
  a live binary.

- Reported: [#9](https://github.com/ash/rakupp/issues/9) — an object hash
  (`my %h{Int}`) lost its key type. `%h{+$k} = 66` came back as the Str `"33"`,
  and `.raku` rendered a plain `{"33" => 66}`. The store is a
  `map<std::string, Value>`, so a key is a lookup *string* and its real value has
  to come from somewhere: a Set/Bag/Mix parks it in the count's `pairKey`, an
  object hash can rebuild it from the declared key type. Both now go through one
  `hashEntryKey`, which is what `.keys`, `.pairs`, `.kv`, `.antipairs`,
  `.invert`, `.sort` and iteration all ask. `.raku` renders the declaration that
  rebuilds it (`(my Any %{Int} = 33 => 66)`) rather than a Str-keyed literal that
  would not round-trip, and `.^name` reports `Hash[Any,Int]` — on the name only,
  since `typeName()` is what dispatch and error messages key on.

  Still open, and the reason this is narrowed rather than general: a key type
  that cannot be rebuilt from a string — a class, or bare `Any`/`Mu`, where
  Rakudo distinguishes `%h{3}` from `%h<3>` and Raku++ cannot — stays a `Str`.
  That is the pre-existing "Hash keys are plain strings" limit, unchanged.

- **A declared type did not survive compilation.** Fixing #9 in the interpreter
  left `--exe` still printing `{"33" => 66}`, and the cause was broader than
  object hashes: codegen emitted a bare `Value::array()`/`makeHash()`/`any()`
  for *every* declaration and dropped the declared type on the floor. So
  `my Int $x` was `(Any)` compiled and `(Int)` interpreted, `my Int @a` was an
  Array of `Mu`, and `my %h{Int}` lost its key type entirely. Both sides now go
  through the interpreter's own `typedDefault` via a small `rtTypedDefault`
  shim, rather than the compiler keeping its own idea of what a declaration
  means. Top-level declarations become C++ globals, so the type had to be
  carried there too.

- **An enum value numified to itself, tag and all.** `b.Numeric` and `+b`
  rendered as `b`, while `.Int` and `.value` — which build a fresh Int —
  correctly gave `1`: one value answering three ways. Both coercion paths now
  return a plain Int.

### Conformance: closing divergences

Measured against the two data sets behind the divergences page. The operator
matrix collapsed hardest, because two of these are parse- and lex-level and so
cascade across every operator they touch.

- **`Nil` is a TERM, not a routine** (~15 operator rows). The parser fell through
  to its general identifier path, so anything that could begin a listop argument
  turned into a call: `Nil ~ 1` parsed as **`Nil(~1)`** and died with "No such
  method 'Nil' for invocant of type 'Str'"; `Nil ff 1` as `Nil(ff 1)`. `max`,
  `min`, `X`, `Z`, `but`, `^`, `minmax`, `notandthen` and `?^` were all the same
  bug. This is also what took Roast from 624 to 625 files.
- **A non-numeric string operand is an error, not a silent 0** (9 rows).
  `"a" +& "b"` answered `0`: the bitwise, repeat and approx-equal operators each
  reached for `toInt()`/`toNum()` directly, while ordinary arithmetic had long
  gone through `numifyStrOrThrow`. One `strictNum` helper now serves `+&` `+|`
  `+^` `+<` `+>` `x` `xx` `≅` and the two prefix forms.
- **The flip-flop family** (8 rows). The state machine was implemented but three
  of its eight spellings were unreachable: `^ff`, `ff^` and `^ff^` lexed as three
  tokens, so the carets read as prefix/postfix on the operands. It also answered
  `Any` while off where Rakudo answers `Nil`. All six variants now match Rakudo
  element for element.
- **`Nil` is a set element** (6 rows). `Nil (|) 1` dropped it — while a `Nil`
  *inside a list* had always been kept, which was the tell that the scalar case
  was simply excluded.
- **An anonymous mixin role is named `<anon|N>`** (5 rows): `1 but 2` is an
  `Int+{<anon|1>}`, not the bare `Int+{}`.
- **`0..^N` renders as `^N`** (3 rows), for `.gist` and `.raku` alike — Int-zero
  only, so `0.0..^5`, `0..^5e0` and `0..^0.5` keep the long form.
- **`+<` after a term is the shift** (2 rows). `1 +< "2"` lexed as `+` followed by
  a word list and swallowed the rest of the line. The term test is deliberately
  narrow: a bare identifier may be a LISTOP, and treating `is-deeply ~<2>, '2'`
  as a shift cost all 119 assertions of `S02-literals/allomorphic.t` before the
  rule was tightened to literals, variables, closers and the five identifiers
  that can never be a listop.
- **Methods the documentation exercises that did not exist**: `Proc::Async.command`,
  `Format.directives`, `Buf.splice`, `Blob.unpack`, and `.files` on a
  `CompUnit::Repository`. `Buf.splice` is the interesting one — it mutates in
  place, and a Buf's bytes are a plain `std::string` rather than a shared_ptr the
  way an Array's elements are, so unlike `Array.splice` it cannot mutate through
  a copy. `methodCall` takes its invocant BY VALUE, so it lives beside `bufBitOp`
  as a `Value&`-taking member.
- **An `IO::Handle` gists as an `IO::Handle`** and Strs as its path — it had no
  rendering of its own and dumped `buffer`/`mode`/`path` as a hash, the same root
  cause as the Proc dump in [#10](https://github.com/ash/rakupp/issues/10).
- **`IO::Path::Parts` subscripts positionally**, in declaration order:
  `$parts[0]` is the volume Pair. (Only the subscript — Rakudo treats it as ONE
  item for `.list`, `.pairs` and `for`, which spreading it broke in the other
  direction, so that was tried and reverted.)
- **`Complex.round($scale)`** honours the scale, per component. Each component
  goes through the scalar path rather than repeating the arithmetic — doing it
  again in doubles gave `-3.9000000000000004` for the imaginary part.

### Semantic-duplication audit — batches B7–B11

One rule implemented in more than one place, so that fixing a bug in one copy
leaves the other wrong. Each batch is gated on the full Roast run.

- **B7 — value identity has one home (`whichOf`).** `.WHICH` identified an
  object by its address; the quanthash key identified it by its rendering, so
  two distinct objects collapsed into one Set element (`set($x, $y).elems` was
  1). `===` and `eqv` also lacked Range and parametric-type arms. Fixing the
  Range case exposed a bug it had been masking: `Rat.Range` carried saturated
  int endpoints where they should be ±Inf.
- **B8 — a type object stringifies empty.** `Int.Str` was already `""` while
  `~Int`, `"{Int}"`, `put Int` and `.join` gave `(Int)`. This was tried once
  before and backed out because quanthash keys were built from `toStr`; B7's
  `baggyKeyStr` was the missing prerequisite. +37 assertions, 622 → 624 files.
- **B9 — an exception's payload is its type object, everywhere.** Seventeen
  throw sites stored a Str naming the class, a Str that was never a class name
  (`"op"`, `"Not callable"`), or nil — so `$!.message` died with "No such
  method". Two of them were in the code generator, meaning a multi with no
  matching candidate could not be caught by class under `--exe` at all.
- **B10 — one class and one sentence for an immutable-container write.**
  `$s<1> = 5` threw `X::Assignment::RO`, `$s<1>:delete` threw `X::Immutable`;
  same rule, so a `CATCH` caught one and missed the other. Only the
  subscript-write sites moved — Rakudo keeps `X::Immutable` for a different rule
  that Roast asserts.
- **B11 — divide-by-zero is one Failure with one wording.** Three verbatim copies
  of the zero check each returned a bare Failure *type object*, which carries no
  exception and never detonates, and the one message built hard-coded `infix:<%%>`
  whatever operator was used. That exposed a second half: `try` cleared `$!`
  unless something was *thrown*, so a block that returns a Failure left `$!`
  unset.

### Documentation

- **[docs/faq/](docs/faq/)** — a new section, six pages: running external
  commands, containers and itemisation, compiling, performance, debugging, and
  where Raku++ and Rakudo differ. Every runnable snippet is executed on both
  engines and must produce identical output; where the two genuinely diverge the
  page says so rather than documenting whichever is convenient.
- Spec and tour links repointed from `ash/raku-spec` / `ash/raku-tour` to
  `ash/raku.online` (`sites/spec`, `sites/tour`), across six files.
- All measured figures refreshed against a single clean run — including several
  that had gone stale independently of the headline: ROAST.md claimed "≈39% of
  files" and "about a sixth produces no TAP" (it is a tenth), the no-TAP
  denominator note said 101 files (88), and the per-synopsis table was a release
  old. README now states that its figures are measured on `main`, since `main`
  is ahead of the v1.2.6 tag.

### Known, not fixed

- `:(…) ~~ :($ where …)` is True here, False in Rakudo — Rakudo fails a
  signature smartmatch whenever the target carries *any* `where` constraint,
  since it cannot statically verify one.
- A `rotor` **sub** is registered that Rakudo has no routine for
  (`rotor(1..6, 2)` gives `((2))` here, "Undeclared routine" there).
- A missing semicolon between statements is accepted where Rakudo rejects it;
  `die` prints no backtrace. Both documented in the FAQ as gaps.

## v1.2.6 (2026-07-28) — Proc rendering

A point release for one user-visible bug found just after v1.2.5 shipped.

### Fixed

- `say shell("ls")` printed the listing **twice**: once as the child ran, once
  inside the Proc's own gist. A Proc had no gist of its own, so it fell through
  to the generic hash rendering, which prints every slot — including the
  captured `out-str`. It renders in Rakudo's shape now, byte-identical apart
  from the pid:
  `Proc.new(in => IO::Pipe, out => IO::Pipe, err => IO::Pipe, os-error => Str,
  exitcode => 0, signal => 0, pid => 58353, command => ("ls",))`.
  The argv is rendered .raku-style so a Str argument keeps its quotes and a
  one-element command keeps its trailing comma; the child's pid is plumbed out
  of the spawn on both platforms rather than reported as Any.

  The same missing gist explains the tab-separated key/value dump in
  [#10](https://github.com/ash/rakupp/issues/10) — that was never a Windows
  artefact, it is what a Proc looked like everywhere.

Roast and documentation-conformance numbers are unchanged from v1.2.5
(196,381 assertions, 622 files, 936 examples).

## v1.2.5 (2026-07-28) — correctness, and one rule in one place

A maintenance release. Two threads: closing documentation divergences, and a
systematic audit for **semantic duplication** — one rule implemented in more than
one place, so that fixing a bug in one copy leaves the other wrong.

| | v1.2.0 | v1.2.5 |
|---|---|---|
| Documentation examples byte-identical to Rakudo | 835 | **936** |
| Roast assertions (all declared) | 196,052 | **196,381** |
| Roast files fully passing | 611 | **622** |
| Regression tests | 113 | **137** |

### Fixed

Reported: [#7](https://github.com/ash/rakupp/issues/7) `.min`/`.max`/`.minmax`
took `&by` as the first ARGUMENT rather than the first CODE argument, so an
adverb in front of the block silently dropped it and the extremum was taken over
the raw elements. [#10](https://github.com/ash/rakupp/issues/10) `run`/`shell`
failed on Windows with exitcode -1 and no output — four causes: `shell` ran a
hardcoded `/bin/sh`, the spawn passed a possibly-invalid stdin handle to
`STARTF_USESTDHANDLES`, a failed spawn reported nothing at all, and every
argument was quoted unconditionally (which breaks a `cmd.exe` switch).

Silent wrong answers, none of which Roast caught:

- `my $x = 5; say "$x-1"` printed **4**. A `-` continues an identifier only
  before a letter or `_`; the string-interpolation scanners accepted a digit, so
  the arithmetic result was interpolated.
- `fail` under `--exe` **aborted the binary** with an uncaught ReturnEx; fixing
  it exposed a second copy of the same rule, codegen inlining "undefined"
  without the Failure case.
- `.Array` did not decontainerize: `$v.Array.elems` said 3 while
  `my @a = $v.Array` bound 1. Found by running showcase/perl against real perl,
  where three of six examples produced wrong output.
- `.raku` silently **dropped every inherited attribute**, so an object could not
  round-trip through EVAL. `.gist` and `.raku` of a hookless object are one
  renderer now.
- `('fig'..'banana')` built 1,000,000 elements and peaked at 952 MB; the
  emptiness guard compared lengths before values.
- `for 'a'..'c' { .say }` printed `0` under `--exe`.
- `∞/∞` gave Inf — the lexer read the `/` as opening a regex.

Also: exact Mix weights, `Nil` subscripts, `Junction.defined`, allomorph
identity in sets and `===`/`eqv`, `.perl` aliased once rather than in sixteen
places, Uni and ISO-8601 rendering moved into the value model, `@.`/`%.`
attributes coercing to their sigil's container, and the MSVC build (an
`__attribute__` MSVC does not know had left windows-x64 red).

### Not done, deliberately

Rakudo iterates an equal-length multi-char Str range as a per-position cross
product; we still use a succ chain. Quanthash identity keying needs Hash keys to
carry their key object. `42[2]` should throw X::OutOfRange — correct in
isolation, but it turns a soft failure into a file-killing throw until the
`@p[0]` list-assignment bug behind it is fixed. Each reason is recorded at the
site, not only here.

## v1.2.0 (2026-07-26) — documentation conformance

A release measuring one thing: how much of the *official Raku
documentation* Raku++ now reproduces exactly. Every runnable example in the docs
is executed on both engines and classified three ways (see
[CONFORMANCE.md](https://github.com/ash/raku.online/blob/main/sites/spec/CONFORMANCE.md)); the number to
watch is `ok` — documentation, Rakudo and Raku++ all agreeing.

| Verdict | before | now |
|---|---:|---:|
| `ok` — all three agree | 596 | **835** |
| `rakupp-differs` — Raku++ is wrong | 471 | **237** |
| `all-differ` — needs a human | 176 | 164 |
| `doc-drift` — the docs are stale | 104 | 112 |
| `rakudo-differs` — Rakudo is the odd one out | 16 | 15 |

Roast moved with it: **194,904 → 196,052** assertions and **598 → 611** files
fully passing, with no removals from the fully-passing list.

Sixteen batches, each gated on both suites and each with a regression test that
passes under Rakudo too. The larger pieces:

- **The custom Iterator protocol.** A class with its own `.iterator` decides what
  iterating it means, for both `for` forms and for an Iterable held in a `$`. The
  blocker was not the protocol but binding: `my @a := SubclassOfArray.new` coerced
  the object to a plain Array and threw the class away, so a user `.iterator` was
  unreachable however it was written.
- **Itemization.** A `$` container itemizes the list or hash it holds, which
  fixed `list()`, `.item`, `my @b = $t` and the `$(…)` render marker together.
- **Definiteness types as first-class.** `Foo:D` in term position was parsed and
  discarded, so the constraint existed only inside a signature; it now rides on
  the type value, and `.^name`, smartmatch and `.^base_type` all follow.
- **Runtime class creation** via `Metamodel::ClassHOW.new_type`, and the HOW
  spellings of the MOP operations.
- `.match`'s occurrence adverbs (`:continue`, `:pos`, `:x`, `:nth`, `:1st`…),
  `.split`'s separator adverbs, `.lines`/`.words` arguments, a fuller `substr`,
  `:ignoremark` on the Str search routines.
- `DateTime.new(date => …)`, which had been dropping the date entirely, plus an
  exact clock: fractional seconds parse as a Rat, so `.day-fraction` and the
  Julian dates stay rational.
- `.round($scale)` in exact arithmetic; non-numeric string coercions answering a
  Failure; `exit` ending the process from any thread.

Two behaviours were deliberately NOT copied. Rakudo's `first(…, :end, :kv)`
reports an index counted from the end where `:end, :k` reports the true one; the
consistent answer is implemented instead, leaving one documented example
permanently in `rakupp-differs`. And treating every subscript target as a
list-assignment — which is what Rakudo parses — costs 210 emitted Roast tests,
so the computed-key case (`%h{%other.keys} = …`) is parked rather than bought at
that price.

## v1.1.0 (2026-07-24) — 100% Unicode (S15)

Every S15 (Unicode / strings / NFG) assertion now passes: **91,752 / 91,752**,
80 of 82 files fully passing. The one non-passing file, `S15-nfg/concat-stable.t`,
is a *performance* timeout (its O(n²) concat loop meets an O(n) `Array.shift`),
not a correctness gap — every assertion in it passes when it finishes. The full
UCD case tables also lifted string-heavy tests suite-wide: the run went from
576 → 598 files fully passing (194,506 → 194,901 assertions), no regressions.

- **Full Unicode case mapping.** `uc`/`lc`/`tc`/`fc` are driven by complete UCD
  tables — simple mappings from `UnicodeData.txt`, full (1:N) mappings from
  `SpecialCasing.txt`, and case folding from `CaseFolding.txt` (generated by
  `tools/gen_unicode_case.raku`). The old hand-rolled ranges missed whole blocks
  (Latin Extended Additional, …), so `Ḍ.lc` used to be a no-op. Case change is
  now **NFG-aware**: it maps each grapheme's base, keeps the combining marks, and
  places them correctly when the base expands (`"ﬀ̣".uc` → `F̣F`).
- **Grapheme-level (NFG) regex.** `.`, character classes, and `<:Prop>` assertions
  match a whole grapheme cluster; an enumerated class matches a multi-codepoint
  grapheme only when its member is that exact grapheme (`\c[A, COMBINING…]`),
  never a bare base codepoint (`S15-nfg/regex.t`).
- **Complete `uniprop`.** Age, Block (proper names), Line_Break, Word_Break,
  Sentence_Break, Grapheme_Cluster_Break, East_Asian_Width, Hangul_Syllable_Type,
  Joining_Type/Group, Decomposition_Type, Numeric_Type, the case-mapping and
  Bidi_Mirroring_Glyph properties, plus Emoji/Full_Composition_Exclusion/
  Changes_When_NFKC_Casefolded/Bidi_Mirrored as strict binary properties
  (unknown names are now `False`, not a lenient match). `uniprop` on a type
  object throws `X::Multi::NoMatch`.
- **`Buf.decode('utf-16' | 'utf-32')`** decodes fixed-width code units (with
  surrogate pairing and BOM detection) into NFG strings; `Uni.new(…)` gains
  `.raku`/`.gist` and the `Uni(97)` coercion form.

### Ecosystem & concurrency — a live Cro server, byte-for-byte with Rakudo

- **A real Cro HTTP server runs**, verified byte-identical to Rakudo end-to-end
  (route + `Cro::HTTP::Server` over real sockets/threads). This drove a cluster
  of general fixes: on-demand `supply {…}`/`whenever`/`tap` wiring with
  done-propagation and CLOSE/QUIT/LAST phasers; `IO::Socket::Async`
  (listen/read/write on GIL-parked worker threads); method-frame `state` vars;
  EVAL'd anonymous `regex {…}` as first-class closures that run their code
  blocks and assertions; proto-token action dispatch; and coercion-param
  multi-dispatch.
- **`signal(SIGINT, …)`** — OS signals delivered as a Supply, so the standard
  `react { whenever signal(SIGINT) { $server.stop; done } }` shutdown works
  (self-pipe dispatcher; the Signal enum members resolve to their OS numbers).
- **`.lines` strips `\r\n`** (not just `\n`), matching Rakudo — an HTTP
  response's `.lines[0]` no longer keeps a stray `\r` (`S15`-adjacent
  `S32-str/lines.t` 9 → 13).
- **Typed blobs** (`blob16/32/64` little-endian words) — element-wise `.elems`/
  index/list/`for`/`Z`/coerce, `.Int` = element count, plus radix digit-lists
  `:256[…]`, colon-arg list precedence, `( expr; )` grouping, and native
  `uint32 @a.push` wraparound. Together these make **pure-Raku `Digest::SHA1`
  byte-identical** (`sha1("abc")` = `a9993e36…`).
- **Ecosystem module fixes** (batch 11) unblocked HTTP::UserAgent, Text::Utils,
  and others: if/elsif binding traits (`-> $x is copy`), alternative regex
  delimiters, indented POD, enum trait args, `!=:=`, Blob-is-not-Stringy
  dispatch, `Capture.new`/`Signature.ACCEPTS`, `Mu.return`, and a streaming
  `Encoding::Registry` decoder. `$*RAKU.compiler.version` now reports Raku++'s
  own version, not a faked Rakudo date. Tier-2 module battery: **37 / 50**
  genuinely byte-identical (both engines run and agree).

### Packaging

- **OpenBSD is now a packaged release target** (`rakupp-openbsd-x86_64.tar.gz`).
  OpenBSD (amd64, base clang/libc++) had been a build+smoke portability gate
  since PR #3, but its binary was never packaged or attached to a Release; the
  release job now installs, dist-layout `--exe`-smokes, tars, and attaches it
  alongside the macOS/Linux/Windows assets.

### Earlier post-1.0 fixes, all Roast-gated:

- **Hyper compound assignment**: `@a <<+=>> n` applies the base op elementwise
  and mutates in place (all spellings; advent2009-day06.t now fully passes).
- **Undeclared-attribute errors print the `===SORRY!===` compile banner** with
  `file:line` (the exception carries filename/line, X::Comp style).
- **Default `.new` binds declared public attributes only** — stray named args
  no longer enter the attribute store; plain `.name` is no longer universal
  (a user instance without one dies X::Method::NotFound); attributive
  `:$!attr` / `:$.attr` parameters (BUILD/TWEAK style) are actually
  implemented, including `:$!x = default` initialization.
- **New lint rule `new-arg-matches-no-attribute`**: warns when a literal named
  argument to a locally-declared class's `.new` matches no public attribute
  (the default constructor silently ignores it). Zero false positives across
  the 1,900-file corpus; three true catches.
- **Corpus round-2 batch**: glued `-ne'…'`/`-npe'…'` one-liner flags; typed
  scalars reject undefined values (`my Int $i = $undef` dies); big-part
  Rat→Num converts with a single correct rounding; bare `$` is a true
  anonymous state variable (`say ++$` numbers lines); substitution
  replacements decode qq escapes (`s/$/\n/`).
- **Match numification follows the Str ladder** (`+$0` of digits is Int).
- Corpus differential: **1,532 / 1,812 exact matches (84.5%)** on the
  reorganized corpus (rounds 2–4 in
  [docs/dev/CORPUS-DIFF.md](docs/dev/CORPUS-DIFF.md)).

## v1.0.0 — 2026-07-22

Everything since v0.9.1 (2026-07-20), 65 commits — the "90% campaign": many
small, fully-gated legs, each run against the complete Roast suite with zero
fully-passing-file regressions.

### Headline

- **1.0**: the campaign target set for this release — 90% of all declared
  Roast tests — is reached. **194,496 / 216,066 declared assertions pass
  (90.0%)**, up from 189,102 / 214,384 (88.2%); **583 / 1,462 files fully
  pass** (was 558). 97.4% of tests that actually run pass. (The declared
  denominator grew because files that previously died before announcing a
  plan now declare their real, often larger, plans.)
- **A regression-test suite is born**: [t/regression/](t/regression/) — one
  self-contained case per bug we introduced and had to fix (21 cases,
  auto-discovered by `t/run.raku`; the suite is at 79 checks).

### Language & runtime

- **Quanthashes grew up** (`Set`/`Bag`/`Mix` + `*Hash` variants): `.of`/`.keyof`
  report the real value/key types; `Bag[Int]`-style parameterization is
  enforced end-to-end (declaration, `Set[Str].new`, assignment — bad keys
  throw `X::TypeCheck::Binding`); the immutable three reject re-initialization,
  autovivification, and element assignment; `^Inf .Bag` throws a typed
  `X::Cannot::Lazy` with `.what`; non-numeric/NaN/Inf/complex weights throw
  typed conversion errors; `%h<a>--` on a `BagHash` removes the key at zero;
  `.new` follows the single-arg rule (a quanthash argument is ONE element, a
  plain Hash iterates, bare named args are swallowed); the coercers flatten one
  level through bare Lists only, keeping `».Bag` nodal.
- **`index`/`rindex`**: splatted multi-needle form (`.index("a", "o", :i)`) and
  needle lists, with `:i`/`:ignorecase`; out-of-range start positions return a
  typed `X::OutOfRange` **Failure** (so `fails-like` semantics hold).
- **`sort`**: `:k` (sorted indices) and `:by(&cmp)` on both sub and method
  forms; `NaN` orders last and is `eqv`-identical to itself; a declared
  0-arity comparator is rejected.
- **`RUN-MAIN` / CLI**: full command-line parsing — repeated options become
  arrays, values are `val()`-allomorphed, `--` ends options, `--/name`
  negates, `%*SUB-MAIN-OPTS<named-anywhere>` honoured.
- **Declarator pod**: leading `#|` and trailing `#=` comments attach to subs,
  classes, and parameters and surface through `.WHY`.
- **Arity enforcement** for direct calls to subs with declared signatures
  (too many positionals now die, with the lenient carve-outs Rakudo has).
- **Typed exceptions throughout**: unknown operator categories
  (`X::Syntax::Extension::Category`), blank `:sym<>` (`…::Null`), pod
  `=begin` without an identifier, `else if`/`elsif` misspellings, malformed
  loop specs, stubbed packages (`X::Package::Stubbed`), method-not-found with
  `.method`/`.typename`, undeclared return types, and more.
- **Str ranges**: `'a'..'z'` is a real Range (codepoint-stepped endpoints,
  containment, `min`/`max`, `.pick`/`.roll`, reversibility).
- **Sequence operator**: generator + literal endpoint is properly lazy
  (`1, 2, * + 1 … 10` stops on exact match; runaway-capped).
- **Unicode quoting**: the curly-quote family (`‘’ “” „ ‚`), CJK corner
  brackets with nesting, and arbitrary paired-punctuation delimiters.
- **Numification follows the Str ladder everywhere it should**: `+"9"`,
  `+$0` (Match captures), and prefix `-` yield `Int`/`Rat`/`Num` like Rakudo,
  and allomorphs answer `.isa` on both faces.
- Also: `once` blocks, `.VAR.dynamic`, `pairup`, `samemark`, `roots`,
  `trusts` declarations, for-loop sub-signatures on the multi-element path,
  regex bodies containing `{ … }` code blocks, and `--highlight` support for
  multi-line embedded comments (`` #`( … ) ``).

### Tooling, benchmarks, ecosystem

- **Benchmarks re-measured at release** (per the runbook): kernels and the
  `-O` suite within noise of v0.9.1's snapshot (sieve 50.5× with lanes;
  `mandel` 0.13 s vs Rakudo 0.47 s). The YAMLish grammar workload drifted
  ~8% slower over the campaign week — bisected to gradual accretion across
  parse/regex hot paths, no single culprit; recorded in
  [docs/BENCHMARKS.md](docs/BENCHMARKS.md) as a post-1.0 item.
- **Suite infra**: the test-server `stop-server` used `pkill -f` with the
  full script path — a regex in which `raku++` is invalid, so no server ever
  died; 54 zombies had accumulated and one answered a later run's INCR with a
  stale count. Servers are now killed by basename.
- REFERENCE.md appendices regenerated from source (188 subs, 571 methods);
  spec.raku.online's conformance map generator now parses its counting block
  from the results file instead of hard-coded literals.

## v0.9.1 — 2026-07-20

Everything since v0.9.0 (2026-07-19), 80 commits. Every change is gated on the
full Roast suite with no fully-passing file regressions.

### Headline

- **Roast: 558 / 1,462 files fully pass** (was 533). Passing assertions grew from
  187,749 to **189,102** — **88.2%** of the 214,384 declared tests, and 97.4% of
  the tests that actually ran.
- **New `--lint` mode**: a static analyzer that parses a program and reports
  likely mistakes without running it — unused variables, unused lexical
  routines, redeclarations, unreachable code, self-assignment, constant
  `if`/`unless` conditions, and numeric comparison of a string literal (all
  warnings), plus unused parameters and redundant trailing `return` (notes).
  Exits 1 on any warning, so it drops into CI or a pre-commit hook. The rules
  are deliberately conservative — interpolation and regex pattern text count as
  uses, and `EVAL`/symbolic references stand the "unused" rules down — to keep
  false positives near zero on Raku's dynamic constructs. Rule reference in
  [docs/LINT.md](docs/LINT.md); one-rule-per-file demos in
  [examples/lint/](examples/lint/).

### Language & runtime

- **Shaped multidimensional arrays**: `my @a[2;3]` / `Array.new(:shape)` —
  declaration, fill, `.shape`, and multi-dim `AT`/`EXISTS`/`ASSIGN-POS`. Iteration
  (`keys`/`values`/`kv`/`pairs`/`flat`) yields leaves with index tuples;
  list ops (`join`/`map`/`sort`/`pick`/…) delegate to the leaves; `.gist`/`.raku`/
  `.clone` are structured; assignment is structurally validated (nested must match
  dims, shape-mismatch and flat-list throw); fixed-dim mutators/`reverse`/`rotate`
  throw. Closes `decl`/`assign`/`methods`/`multi_dimensional_array`.
- **Fractional numeric ranges**: `-1.5..1.5`, `1.1..^3.1` keep their real
  endpoints and step by 1 across `list`/`for`/`min`/`max`/bounds/`gist`.
- **Regex**: conjunction operators `&` / `&&` (all terms match at one position,
  span the last); numbered capture aliases `$N=(…)`; named array `@<name>=(…)`
  and hash `%<name>=(…)` capture aliases; the `:exhaustive`/`:ex` modifier.
- **Version** comparison: Unicode-letter alpha parts, numeric-before-alpha
  ordering, trailing-alpha-before-release, insignificant trailing zeros, and
  underscores preserved in `<>` word-quote spellings — `version.t` fully passes.
- **Negative-index semantics** now match Rakudo: an out-of-range negative
  subscript returns a `Failure` (`X::OutOfRange`), not a Python-style wraparound
  (`@a[*-1]` is how you index from the end); `:exists` on it is `False`; indexing
  an unhandled `Failure` propagates it.
- `classify`/`categorize` gain Hash and Array classifiers and `:into(%h)`
  appending; `round()` the sub delegates to the method (honouring a scale arg and
  NaN/Inf); Set-from-pairs uses value truthiness while Bag/Mix keep numeric
  weight; `.tree` (nested view / depth-limit / per-level closures) and
  `.^parameterize` (`Set.^parameterize(Str)` is `Set[Str]`).
- Numeric-literal underscores must sit between two digits (`1__0`, `100_`,
  `1_000_____000` are now rejected, in mantissa and exponent alike). String→number
  coercion learns `:N<>` radix, the `0d` prefix, and Complex / unicode-minus forms.

### Spec faithfulness

Fixes from building **spec.raku.online** and diffing against Rakudo — each a
behaviour where Raku++ had diverged: NFC/NFG string normalization; a lexical
regex shadowing a same-named built-in subrule; `.isa` as strict class inheritance
(roles excluded); round-half-up, `wordcase`, `comb(Int)`, `split :skip-empty`,
`indent`, `List.invert`; `qq{}` brace-delimited interpolation; `where`
enforcement and Capture/Map/Seq gists.

### Performance

- **`~=` string building is O(n) again in every mode.** The NFC-normalization
  work in v0.9.0 had made in-place append re-normalize the whole accumulator on
  each `~=`, turning `strcat` O(n²) (~360 ms). Appending pure-ASCII now skips the
  re-normalize; non-ASCII appends still normalize across the join. `strcat` is
  back to ~12 ms interpreted (15× Rakudo) and correctness is unchanged.

### Ecosystem & docs

- New [docs/ECOSYSTEM.md](docs/ECOSYSTEM.md): the projects built on this
  interpreter (Raku.js, raku.online, spec.raku.online, raku-corpus, the Homebrew
  tap), how they connect, and the release runbook for rebuilding the wasm and
  redeploying the sites after a version bump.
- REFERENCE.md inventory refreshed to 183 subroutines / 562 methods; FEATURES and
  the benchmark tables brought current.

## v0.9.0 — 2026-07-19

Everything since v0.7.1 (2026-07-16), 147 commits. Every change is gated on the
full Roast suite with no fully-passing file regressions.

### Headline

- **Roast: 533 / 1,462 files fully pass** (was 501). Passing assertions grew from
  171,817 to **187,749** — **87.5%** of the 214,569 declared tests, and 96.9% of
  the tests that actually ran.
- **`--exe` native binaries now have interpreter-parity recursion depth on every
  platform**: the generated `main()` runs the whole program on the same 1 GiB
  big-stack thread the interpreter uses (macOS/Windows also carry link-time stack
  flags). Deep recursion that the interpreter handles no longer crashes a native
  build.
- **Windows `--exe` works out of the box** (GitHub issue #1 closed): the generated
  `main()` no longer collides with the CRT `__argv` macro, MSVC builds default to
  the static CRT so native links don't fail `LNK2038`, a compiler is found on
  `PATH`, and `vcvars` is bootstrapped when `cl` isn't in the shell.

### Language & runtime

- Reduction metaops thunk their operands: `[&&]`/`[||]`/`[//]`/`[andthen]`/… and
  their `[\op]` scans short-circuit without evaluating later operands.
- 6.e array/hash multislices with star/list adverbs (`@a[*;0;*]:delete`,
  `%h{*;"b";"c"}`), and `@a[*-1, *-2]` list-slices now resolve `*`/`*-1` against
  the length.
- Set/Bag/Mix family: `for`-loops over `.values`/`.kv`/`.pairs` of a
  `SetHash`/`BagHash`/`MixHash` alias the weights (a weight of 0, or negative for
  MixHash, removes the element); `.ACCEPTS`/`.STORE`/`.Capture`, the coercer
  calls, and `class MySet is Set` subclassing.
- `Pair.value` is a writable container; typed-container multi dispatch
  (`multi f(Int @a)`); a slurpy multi candidate is now correctly the least-narrow
  tiebreaker.
- Regex `m:nth(N)`/ordinal/`:nth(*)`/list-and-`:global` counted adverbs; `Buf`
  `subbuf` Callable/`*` forms, `Buf.new(Range)`, `.allocate` fills; compile-time
  "Useless use … in sink context" warnings on the mainline.
- Containers: `@a = …` / `%h = …` refill the existing container in place, so
  bindings, captures, and closures track the reassignment.
- Correctness fixes from the pre-release review: `.Int` on a string/match wider
  than int64 is now exact (was 0); a brace character in a string inside an
  embedded regex code block parses correctly.

### Native compile (`--exe`) & the browser

- Caught builtin errors answer `.message` inside a native `CATCH` (was a bare type
  payload); `exceptionFor` synthesizes real exception objects for `X::`-named
  payloads. Block-final `if`/`given` is a pointy block's value; `Less`/`Same`/
  `More` and `PromiseStatus` resolve to real enum values under native name-term
  lookup.
- New [docs/MEMORY.md](docs/MEMORY.md): reserved-vs-resident memory and the
  measured recursion depths per mode (interpreter / `--exe` / WebAssembly).
- New [docs/COMPILERS.md](docs/COMPILERS.md): which compiler and architecture to
  use — arm64 vs. x86_64/Rosetta on macOS, Clang vs. GCC (with a measured
  ~1.3–2× gap on this codebase), MSVC vs. MinGW on Windows — both for building
  Raku++ and for the compiler `--exe` invokes.
- New showcases on the WebAssembly playground: a JavaScript/TypeScript
  interpreter, a Scheme, and a Forth, each written in Raku.

### Concurrency

- `react`/`whenever` no longer hangs when an eager `start { $s.emit(…); $s.done }`
  runs before the react taps the supply — the Supplier records its done state so a
  late tap closes immediately.
- A `.then` registered on a `start`/Promise before its worker settles now fires
  (was silently dropped).
- `CurrentThreadScheduler.cue` rejects `:every`, as in Rakudo.

### Robustness (pre-release review)

An independent multi-reviewer pass over the sources fixed eight default-build
defects: the regex greedy quantifier no longer overflows the stack on long runs
(`/\d+/` over millions of chars), `substr-eq` with only an adverb no longer reads
out of bounds, plus the correctness and concurrency items listed above.

### Known limitations

- **`RAKUPP_PARALLEL=1` (the opt-in GIL-free mode) is experimental and not
  production-safe.** Under it, `Channel`, the shared Rat-literal cache, and
  worker-side `class`/`EVAL` are not fully synchronized and can race. The default
  cooperative-GIL build (what ships and what every example uses) is unaffected.
- **Native (`--exe`) recursion is uncatchable if it overflows**: a compiled
  program that recurses past its stack dies with a signal rather than a catchable
  `X::Recursion` (the interpreter throws). See docs/MEMORY.md.
- A native `given`/`when`/`CATCH` with a bare `my` declaration *between* clauses
  fails to compile (a `goto` past an initializer) instead of falling back to
  bundling; wrap the declaration in its own block.
- The parse-only entry points (`--cpp`/`--ast`) can overflow the stack on
  pathologically deep bracket nesting; ordinary execution is shielded.

## v0.7.1 — 2026-07-16

Everything since v0.5.1 (2026-07-13), ~100 commits. (A 0.7.0 tag was cut but
never published — its Windows build was broken — and is folded into this
release.)

### Headline

- **Roast: 501 / 1,462 files fully pass** (was 419) — the 500-files milestone.
  Passing assertions grew from 157,293 to **171,817**; the declared-test
  denominator also grew (191,546 → 213,617) because parse fixes keep
  surfacing plans that previously died unannounced, so per-test percentage
  moves less than the absolute counts (~80% declared).
- Ten zero-regression campaign batches (R1–R3, NM1–NM6), each gated on the
  full suite with no pass-list drops and equal-or-faster benchmarks.

### Language & runtime

- **`$*SCHEDULER.cue`** is implemented: `:at`/`:in`/`:every`/`:times`/`:stop`/
  `:catch`, `Cancellation` (`.cancel`/`.cancelled`), `.loads`,
  `.uncaught_handler`, `CurrentThreadScheduler`; NaN/±Inf delay semantics and
  argument-combination errors match the spec. Cued jobs run on worker threads
  with a drift-free deadline clock.
- **`subtest 'desc' => { … }` (the Pair form) now executes its body** — it
  used to pass vacuously. Landed together with seven batches of the
  pre-existing bugs it exposed (Rat 0-denominator cluster, `categorize-list`/
  `classify-list`, `.toggle`, non-flattening `:=`, strict `fails-like`,
  `substr-eq`, Capture semantics, …).
- `return` is `Routine`-only (a bare block's `return` returns from the
  enclosing routine); cooperative `return` works inside a method's loops.
- `&?BLOCK` / `&?ROUTINE` resolve lazily from the frame (`&?ROUTINE` outside
  a routine is a parse error, as in Rakudo).
- `X but VALUE` mixins compose a constant method named by the value's type.
- Weighted `pick`/`roll` on Bag/Mix draw without materializing pools;
  `roll(*)` is an infinite lazy sequence.
- DateTime: exact (non-float) seconds, leap-second table, fixed-offset
  timezones, single-numeric POSIX constructor.
- Junctions: `all`/`none` autothread outside `any`/`one`; whatever-curry wins
  over junction autothreading; the standard matcher-method exemptions
  (`grep`, `first`, `classify`, `comb`, `subst`, …).
- MAIN: Rakudo-compatible dispatch strictness, generated `$*USAGE` (including
  `#=` declarator-pod option descriptions), `sub USAGE` takes over the
  failure path, CLI arguments bind as allomorphs.
- Implicit `$a`/`$b` in paramless blocks removed (post-GLR semantics sweeps:
  element itemization, `Z`-comma, stacked zip/cross metaops, one-level
  operands, min/max flattening, rotor pairs, rw loop params, …).
- New builtins and methods across the campaign — inventories now stand at
  **179 subroutines / 505 methods** ([docs/REFERENCE.md](docs/REFERENCE.md)):
  `Lock::Async`, minimal `IO::CatHandle`, `FileHandle.encoding`,
  `List.lazy`, `cross(:with)`, the hyperbolic-trig family
  (`sech`/`cosech`/`cotanh` + inverses), `%%` by zero throwing
  `X::Numeric::DivideByZero`, Set↔Bag↔Mix coercions, `TYPE ~~ TYPE` role
  smartmatch, and more.
- `SIGPIPE` is ignored process-wide: TCP servers survive client disconnects.
- EVAL-only statement strictness ("two terms in a row") with typed
  `X::Syntax::Confused` parse errors.

### Parser

- A `}` at end of line terminates the statement (Rakudo's rule) — previously
  `x => {…}` followed by an `if`/`else` chain could silently re-parse as a
  statement modifier.
- The tight-paren reduce call `[+](…)` takes only its parens — it used to
  swallow the rest of the enclosing comma list, inflating some test files'
  emitted-test counts for years.
- Variable subscript adverbs (`%h{$k}:exists`-family with variable keys),
  adverbed zen slices, dative method syntax (`name $obj: args`), `INIT` as an
  expression, contextualizer circumfixes, comma-list shapes.

### Native codegen (`--exe`)

- `s///`, `$0` captures, and post-GLR slips compile natively (the pastebin
  showcase no longer needs `--bundle`).

### Raku.js — new subproject

- The unmodified C++ interpreter compiled to WebAssembly, with a browser
  playground: worker-based execution with live streaming output and a Stop
  button, syntax-highlighting editor, theme switcher, 24 bundled examples.
  Live at [raku.online](https://raku.online/).
- First performance measurements (experimental; [rakujs/README.md](rakujs/README.md)):
  1.3–6.8× slower than the native interpreter on a clean host, dominated by
  the `-fexceptions` call trampolines; Node vs Bun comparison included.

### Real-world output parity

- **Perl Weekly Challenge corpus** (10,428 community solutions run under
  both engines): byte-identical stdout+status went **2,663 → 4,056** across
  15 fix batches ([docs/dev/PWC-DIVERGENCES.md](docs/dev/PWC-DIVERGENCES.md)).
- **Raku course**: the generator reproduces the full 1,483-page course
  byte-for-byte identically to Rakudo after two rounds of divergence fixes.

### Showcases & tests

- Seven new showcase programs, each with a README: **lisp** (a Scheme on a
  Raku grammar), **pastebin** (HTTP on raw sockets), **markdown** (grammar →
  HTML), **chat** (concurrent TCP), **forth** (a stack machine), **kvstore**
  (a key-value protocol), **rakus** (a static HTTP file server).
- New `t/` regression suite (47 checks: golden example outputs + showcase
  behaviour), wired into CI on the POSIX platforms.

### Performance

- Cold start is **~2 ms** (best of 200 spawns; previously documented ~12 ms).
- Full benchmark refresh against Rakudo v2026.06
  ([docs/BENCHMARKS.md](docs/BENCHMARKS.md)): the interpreter is ahead on 8
  of 9 kernels (fib remains Rakudo's, 1.7×), `--exe` ahead on all 9.
- A perf regression found and reversed mid-campaign: eager `&?BLOCK`/
  `&?ROUTINE` frame bindings cost +40% on call-heavy code; the lazy
  resolution above restored the baseline.

### Platforms & CI

- MSVC: `clock_gettime(CLOCK_REALTIME)` replaced with `std::chrono` (this is
  what broke the unpublished 0.7.0 tag's Windows build); srand seeding
  widened to 64-bit (was UB on LLP64 and wasm32).
- Windows suite: portable process cleanup in `t/run.raku`.

## v0.5.1 — 2026-07-13 and earlier

Pre-changelog releases: v0.5.1, v0.5.0, v0.1.0. History is in git and the
docs as they stood at each tag.
