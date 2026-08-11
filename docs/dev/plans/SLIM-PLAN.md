# Plan: slim binaries — `--exe` ships only what the program needs

*Written 2026-08-09, before any code. The **v3.14** campaign — see
[VERSIONS.md](VERSIONS.md#v3140--only-what-the-program-needs-planned-2026-08-09).
Starts after v3.0.1 ships.*

Goal: a compiled Raku program stops carrying the parts of Raku it cannot
reach. Today every `--exe` binary is the same size whatever the program does,
because the runtime is one indivisible archive and everything in it is
genuinely reachable from `Interpreter::Interpreter()`.

**The number a stranger can re-measure:** `say "Hello"` compiled with
`--exe --slim` is **≤ 5.5 MB**, down from 9.83 MB, while every program in
`t/`, `examples/` and the module battery produces byte-identical
stdout/stderr/exit status built slim and built full.

Measurements in this file: 2026-08-09, `build-arm64/rakupp` at v3.0.1,
Apple clang 17.0.0, macOS 15 (Darwin 24.6.0), arm64.

---

## Where we are — measured

### The size is a constant

| program | `--exe` bytes |
|---|---|
| `say "Hello"` | 9,830,680 |
| `examples/quine.raku` | 9,830,760 |
| `examples/json.raku` | 9,905,640 |

24 examples, 0.8% spread end to end. The program contributes nothing; the
runtime is the binary.

### Where the bytes are

Attribution from a linker map (`-Wl,-map`), aggregating symbol sizes per
archive member; 7.70 MB attributed, the rest is symbol table and headers.

| component | KB | % |
|---|---|---|
| **Unicode data + code (11 objects)** | **3,280** | **41.6** |
| ↳ `unicode_names` (UCD names + numeric values) | 1,747 | 22.2 |
| ↳ `unicode_coll_gen` (DUCET collation) | 742 | 9.4 |
| ↳ props / props2 / norm / case / gen | 651 | 8.2 |
| ↳ scripts / bidi / blocks / gb | 98 | 1.2 |
| ↳ `Unicode.cpp` (the code over those tables) | 39 | 0.5 |
| `Interpreter.cpp` | 1,639 | 20.8 |
| `Builtins.cpp` | 1,130 | 14.3 |
| `MethodCallPart2/3/Tail` | 830 | 10.5 |
| `Parser` + `Lexer` | 545 | 6.9 |
| `Regex` | 144 | 1.8 |
| `AstSerial` | 68 | 0.9 |
| **`IOSpec` — all of I/O** | **46** | **0.6** |
| everything else | ~130 | 1.6 |

Two conclusions worth stating before any design:

- **Unicode is the campaign.** 41.6% of the binary, and most of it is two
  tables.
- **Dropping I/O is not worth doing.** The whole of `IOSpec.cpp` is 46 KB,
  0.6%. A program that reads nothing saves half a percent. This idea is
  explicitly abandoned, not deferred.

### The toolchain cannot do this for us

| variant | bytes | vs base |
|---|---|---|
| current `--exe` | 9,830,680 | — |
| `strip -x` | 8,287,128 | −15.7% |
| `-Wl,-dead_strip` + strip | 8,084,296 | −17.8% |
| `-flto` + dead_strip + strip | 8,178,488 | −16.8%, **link 21 s vs 2 s** |

Dead-stripping buys 200 KB over plain stripping and LTO is *worse* than
`-O2` while making every `--exe` an order of magnitude slower to compile.
That is not a toolchain failing — the reachability is real:

- [`Builtins.cpp:6090`](../../../src/Builtins.cpp) `registerBuiltins()` installs
  205 lambdas into `builtins_` from the constructor. Every one is
  address-taken, so every one is live.
- Method dispatch is `if (m == "…")` chains inside a handful of very large
  functions — indivisible at link granularity.
- `Unicode.cpp` names `ucd::NAMES`, `ucd::COLLCE`, `ucd::BIDI` &c. directly,
  and the builtin table reaches `Unicode.cpp`.
- The generated `main` always constructs an `Interpreter`.

So the cut has to be made by us, in the source, on purpose.

### What is cuttable — verified by building it

Removing archive members and supplying stub definitions, then linking and
**running** the result:

| variant | bytes | gz | vs base |
|---|---|---|---|
| `-dead_strip` + strip | 8,084,296 | 3,024,465 | −17.8% |
| − names, collation, scripts, bidi, blocks | 5,425,768 | 2,424,603 | −44.8% |
| − also Parser + Lexer | 4,854,712 | 2,142,527 | **−50.6%** |

Both pruned binaries print `Hello` correctly.

Two findings that shape the design:

1. **The parser has a four-symbol surface.** Deleting `Parser.cpp.o` and
   `Lexer.cpp.o` leaves exactly four undefined symbols — `Lexer::Lexer`,
   `Lexer::tokenize`, `Parser::Parser`, `Parser::parseProgram`. Those are the
   `EVAL` entry points and nothing else. 545 KB behind one small door.
2. **`\c[NAME]` is already resolved at compile time.** `say "\c[SNOWMAN]"`
   transpiles to `Value::str("☃")` — the literal form needs no name table at
   run time. Only the runtime forms (`.uniname`, `uniparse`, `unival`) do.

---

## The design

### One rule: cut data, not code paths

Phase one cuts **data tables** and **the parser**. It does *not* try to
work out which of the 205 builtins or which method branches a program uses.

This is the decision the whole plan rests on, so it is worth being explicit
about why. If we pruned builtins, correctness would depend on a reachability
analysis over every dynamic dispatch in the language — and being wrong would
mean an unresolved symbol at link time or, worse, a differently-behaving
binary. Because we only prune the *data behind* a builtin, every builtin
lambda stays exactly where it is, and the entire failure surface collapses to
**one function**: the table accessor. If the analysis is wrong, that
accessor throws a named exception with a message telling the user which flag
to rebuild with. There is no third outcome.

Per-builtin code pruning is a **non-goal** for 3.14 (see below).

### The seam

Today `Unicode.cpp` reaches the tables by name:

```cpp
size_t lo = 0, hi = ucd::NAMES_N;          // Unicode.cpp:683
for (size_t i = 0; i < ucd::NAMES_N; i++)  // Unicode.cpp:696
    m[ucd::NAMES[i].cp] = ucd::NAMES[i].name;
```

Replace each direct reference with an accessor **defined in the same
translation unit as the data**:

```cpp
// unicode_names.cpp (generated) — the real one
const NameEnt* ucd::namesTable(size_t* n) { *n = NAMES_N; return NAMES; }

// unicode_names_stub.cpp — the absent one
const NameEnt* ucd::namesTable(size_t*) {
    featureMissing("unicode-names", "uniname/uniparse/unival");  // throws
}
```

Link one or the other. No weak symbols (they do not carry to MSVC), no
`#ifdef`-built runtime variants, no per-program rebuild of the runtime.

**Accessors return a pointer and a count, once.** Callers hoist into a local
before any loop, so the inner loops still index raw memory. This matters:
`ucaElements()` indexes `COLLCE` per collation element, and a function call
per element would be a measured regression. The perf gate below exists for
exactly this.

### The archives

Four features × real/stub is four pairs, not sixteen variants:

```
lib/librakupp_rt.a            everything except the optional data + the parser
lib/librakupp_ucd_names.a     ⎫
lib/librakupp_ucd_coll.a      ⎬ the real tables
lib/librakupp_ucd_props.a     ⎭
lib/librakupp_parse.a         Lexer + Parser
lib/librakupp_stubs.a         all four stubs, each in its own object
```

`--exe` links `rt.a`, then for each feature either its real archive or lets
the stub satisfy the link from `stubs.a` (last on the line, so a real archive
always wins). Total shipped bytes are unchanged but for the stubs; nothing is
duplicated.

Windows carries the same split as `.lib` files, and `findRuntime()`
([`main.cpp:305`](../../../src/main.cpp)) learns to locate the set rather
than a single archive — with a clear error naming the missing member, in the
style it already uses.

### The features

Short list on purpose. A knob nobody can reason about is worse than no knob.

| feature | contents | saves | what needs it |
|---|---|---|---|
| `unicode-names` | `NAMES`, `NUMV` | 1.75 MB | `uniname`, `uninames`, `uniparse`, `unival`, `univals`, and the `.uniname`/`.uniparse`/`.unival` methods |
| `unicode-collation` | DUCET `COLLCE`/`COLLSING`/`COLLCONTR` | 0.74 MB | `unicmp`, the `coll` operator, `.collate`, `$*COLLATION` |
| `unicode-props` | `SCRIPTS`, `BLOCKS`, `BIDI` | 0.08 MB | `uniprop` family with a non-literal property name; regex `<:Script<…>>`, `<:Block<…>>`, bidi classes |
| `eval` | `Lexer`, `Parser` | 0.55 MB | `EVAL`, `EVALFILE`, `require`, a regex built from a runtime string |

**Not cuttable, deliberately:** `unicode_props_gen`, `unicode_props2_gen`,
`unicode_norm_gen`, `unicode_case_gen`, `unicode_gb_gen` (~651 KB together).
General category, binary properties, normalization, case mapping and grapheme
breaking are reached by ordinary string operations — `.uc`, `.chars`, any
regex, any comparison. Cutting them would mean analysing every string
expression in the program, which is the analysis this plan is built to avoid.

`unicode-props` at 80 KB barely pays for itself, but it costs nothing extra
once the seam exists and it keeps the story uniform. It can be dropped from
the flag surface if the scan turns out to be fiddly.

### One constraint from outside this plan: librakupp pins `eval`

*(Added 2026-08-11, after ABI-PLAN's A0–A2 shipped in v3.1.0 — this plan and
that one were written a day apart and predicted the collision; it is now
real.)*

`librakupp` exports `rk_eval` and `rk_run`, and those ARE the parser: an
embedder's whole reason to load the library is to hand it source text at run
time. So the shared library is **always built with the real `eval` feature**,
never the stub — a `librakupp` that answered `rk_eval` with
`X::Feature::NotBuilt` would be a library whose one job is the thing it
refuses to do.

What this costs is almost nothing, and the numbers above already say so: the
parser is 545 KB of a 51-point cut — roughly six points. The Unicode tables,
which are the campaign, are genuinely optional for an embedder and stay
cuttable in a slim `librakupp` build if that variant is ever wanted; `eval`
alone is pinned.

Concretely, in the machinery below: the `rakupp_shared` target always links
`librakupp_parse.a` (P2's split), the scan never applies to the shared
library, and `--slim`'s surface is a property of **`--exe` output only** —
which the gates already assume ("the interpreter never slims"). The same
sentence extends to the embedding artifact: *the interpreter never slims, and
`librakupp` is the interpreter.*

---

## Not cutting something that is needed

The user-facing risk of this whole campaign is one sentence: *a binary that
builds fine, runs fine on the developer's input, and dies six months later on
a code path the scan did not see.* Six defences, in the order they take
effect.

### 1. Prove unused, do not guess unused

`--slim=auto` removes a feature only when the scan **proves** no site in the
program — or in any module riding along in the embedded graph — can reach it.
Anything the scan cannot decide keeps the feature. The default answer to
uncertainty is always "keep".

The scan runs where the module graph is already collected, in
`compileNative()` ([`main.cpp:459`](../../../src/main.cpp)), over the same
`Program` and the same `collectModuleGraph()` results codegen uses — so a
`use`d module's AST is scanned exactly like the mainline.

### 2. Force-full triggers

Any of these means the program can run code the scan never saw, so
`--slim=auto` keeps **everything** and says so on stderr:

1. `EVAL`, `EVALFILE`, or `require`.
2. Symbolic lookup: `&::($name)`, `::($name)`, `.^lookup`, or an indirect
   method call `."$name"()` whose name is not a literal.
3. A regex constructed from a runtime string.
4. A `use`d module that could not be embedded as an AST. This one matters:
   today an unembeddable module is skipped **silently**
   ([MODULES-PLAN.md](MODULES-PLAN.md) names this as a v4 defect), which
   would let a binary reach disk-loaded code the scan never inspected.
   3.14 must at minimum *detect* the skip and fall back to full; fixing the
   silence is v4's job.
5. `--exe` fell back to AOT bundling on a codegen error — the bundled
   interpreter can parse and run anything. `--slim` is ignored, loudly.

### 3. A wrong cut is loud, never silent

The stub accessors do not return empty tables. Empty tables are the dangerous
design: lookups quietly return "no such character" and the program computes a
wrong answer. Instead every stub throws:

```
X::Feature::NotBuilt: uniname needs the Unicode name table, which this
binary was built without (--slim removed "unicode-names", 1.7 MB).
Rebuild with:  rakupp --exe prog.raku --slim=+unicode-names
```

The exception is a normal Raku exception object — catchable, with
`.feature` and `.rebuild-with` accessors — so a program can degrade on
purpose if it wants to.

### 4. The binary knows what it is

`--exe` embeds a manifest string (feature set, `--slim` mode, rakupp
version). `rakupp --exe-info ./prog` prints it. Deployed binaries stop being
mysteries, and a bug report can start with the manifest instead of a guess.

### 5. A differential gate, not an opinion

The gate that actually holds the line, in the project's usual style: for
**every** program in `t/`, `examples/`, and the module battery, build twice —
`--slim=auto` and full — run both, and require byte-identical stdout, stderr
and exit status. A cut that changes any observable behaviour fails the
release. This is the number the campaign is judged on as much as the size.

### 6. A negative suite

The mirror image, and the one that proves defence 3 works: for each feature,
a program that definitely uses it, built with that feature forced out. Each
must produce the exact `X::Feature::NotBuilt` message — not a crash, not a
wrong answer, not an empty result. `t/slim/` holds both suites.

### And a seventh, for releases

`--slim=verify` builds both binaries, runs the program's own test command
against each, and refuses to emit the slim one unless they agree. Slower by
construction; meant for a release pipeline, not a dev loop.

---

## The command line — one key

The first draft of this plan had six flags (`--slim`, `--keep`, `--cut`,
`--features`, `--why-keep`, `--strip`/`--no-strip`). That is sprawl for a
single concern, on a command line the v3.0.0 campaign
([CLI-PLAN.md](CLI-PLAN.md)) had just finished tidying. Everything lives
under **one key** instead:

```
--slim[=SPEC]
```

`SPEC` is a comma-separated list. Each element is a **level**, a
**±feature**, or a **directive** — the `-fsanitize=a,b` and
`-march=native+crypto` idiom, which is where a reader's hands already are.

### Levels — a ladder, at most one

| level | what it does |
|---|---|
| `none` | nothing at all: no dead-strip, symbols kept. For debugging a compiled binary. |
| `safe` | **the default with no flag.** `-dead_strip` + strip symbols. No Raku feature removed, no analysis run. Free, behaviour-preserving. |
| `auto` | **what bare `--slim` means.** `safe`, plus every feature the scan *proves* unreachable. Any force-full trigger keeps everything. Sound. |
| `max` | `auto`, but ignoring the force-full triggers. Unsound by design; a wrong cut throws at run time. |

Any `--slim=…` that names no level means `auto`. So `--slim=+eval` is
"automatic pruning, but keep EVAL", and the rarer "free wins plus one
deliberate cut" spells itself out as `--slim=safe,-unicode-names`.

### ±features

`+name` keeps, `-name` cuts, whatever the level concluded. Names are the four
features in the table above plus `symbols` (the symbol table — a thing you can
keep, like any other), the group `unicode` for the three Unicode features, and
`all`.

`+` always wins over the level, so `--slim=max,+unicode` is well defined:
smallest possible, Unicode intact.

### Directives

| directive | |
|---|---|
| `help` | print the grammar, the feature table and current sizes. The key documents itself. |
| `list` | for *this* program: keep/cut per feature, with the reason and the bytes. Does not compile. |
| `why:FEAT` | every site — file, line, construct — that forced FEAT to be kept. |
| `verify` | build slim and full, run both, emit only if they agree (defence 7). |

### Conflicts are errors, never last-wins

Two levels, `+x` with `-x`, or an unknown name: each is an error naming the
valid alternatives. On a flag whose whole job is to decide what a binary can
do, silent precedence is the wrong default — the user must be told which of
two contradictory things they asked for.

### Not under this key

```
rakupp --exe-info ./prog    Print a produced binary's embedded manifest.
```

`--exe-info` inspects a finished binary rather than steering a compile, so it
is its own mode. Keeping that boundary is what makes "one key" an honest
claim rather than a filing trick.

Worked shapes:

```bash
rakupp --exe prog.raku                              # level safe: the free 16%, nothing removed
rakupp --exe prog.raku --slim                       # the button: sound automatic pruning
rakupp --exe prog.raku --slim=+eval                 # auto, but keep EVAL working
rakupp --exe prog.raku --slim=max,+unicode          # smallest, Unicode intact
rakupp --exe prog.raku --slim=safe,-unicode-names   # one deliberate cut, no scan
rakupp --exe prog.raku --slim=list                  # what would happen, and why
rakupp --exe prog.raku --slim=why:unicode-collation # who is pulling it in
rakupp --exe prog.raku --slim=none                  # symbols and all, for debugging
rakupp --slim=help                                  # the grammar and the feature table
```

## The way not to think about it

Two answers, and the second is the real one.

**Today: `--slim`, with nothing after it.** One word, no feature names, no
decisions — the sound level, where the compiler cuts only what it can prove
and keeps everything else. A user who never reads the rest of this file gets
45% off and cannot be wrong. Everything above is for the minority who need to
override a specific answer.

**Eventually: nothing at all.** `auto` is sound by construction — that is the
whole point of defences 1 and 2 — so the honest end state is that it becomes
the default and the flag is only ever typed to *stop* it. This project has
done this twice already in one release: `RAKUPP_PARALLEL` and `RAKUPP_LTM`
both shipped opt-in, earned the flip on measured gates, and kept an escape
hatch for one release ([VERSIONS.md](VERSIONS.md), v3.0.0). The same shape
applies here, with `--slim=safe` as the escape — and the same discipline:
**do not flip on a hunch.** The flip is its own phase with its own gate (P6),
and it needs the differential suite green across the whole corpus over
several releases, not one clean run.

So: `--slim` is the button now, and the plan is to make the button
unnecessary.

No environment variable while it is opt-in. `RAKUPP_*` in this project means
"escape hatch for something that is on by default"; if and when P6 flips the
default, `RAKUPP_SLIM=0` becomes the correct spelling of the escape and gets
added then — not before.

---

## The phases

### P0 — the free 16%: level `safe` becomes the default

Add `-Wl,-dead_strip` (`-Wl,--gc-sections` with `-ffunction-sections
-fdata-sections` on ELF; `/OPT:REF` for MSVC) and symbol stripping to
`compileCmd()` ([`main.cpp:216`](../../../src/main.cpp)). This is level
`safe`, and it is on with no flag: it removes no Raku feature and runs no
analysis, so there is nothing for a user to weigh. The escapes are
`--slim=none` and `--slim=+symbols`.

The one real cost is that a segfault in a shipped binary yields a less
useful crash report. Mitigated by the manifest (P3): `--exe-info` names the
rakupp version and settings, so a reporter can reproduce with `--slim=none`.
Worth stating in [guide/CLI.md](../../guide/CLI.md).

Only the level-parsing skeleton of `--slim` is needed here, not the feature
machinery — so this phase is independent of everything below and can ship in
a 3.0.x if wanted.
**Gate:** all suites byte-identical, 9.83 MB → 8.08 MB on hello.

### P1 — the seam

Convert every direct `ucd::` table reference in `Unicode.cpp` into an
accessor defined beside the data (~15 sites). Hoist pointer+count out of
loops. No stubs, no flags, no behaviour change yet.
**Gate:** byte-identical everything; `perf-guard --check` flat — specifically
the collation and normalization paths, which are the ones with per-element
table access.

### P2 — split the archives

CMake object libraries for the four feature groups, install rules for the new
paths, `findRuntime()` locating the set. Everything still links the real
archives, so sizes do not move.
**Gate:** clean build and working `--exe` on macOS, Linux, Windows (MSVC and
MinGW), OpenBSD; release tarball layout updated in
[RELEASING.md](../RELEASING.md).

### P3 — stubs, the exception, and explicit `-feature`

`X::Feature::NotBuilt`, the stub archive, the embedded manifest and
`--exe-info`. The `±feature` half of the SPEC grammar lands here — explicit,
no analysis — so the whole mechanism is testable before the scan exists.
**Gate:** the negative suite (`t/slim/`); `--slim=-all` hello ≤ 5.0 MB and
runs; every conflict case (two levels, `+x` with `-x`, unknown name) errors
with the valid alternatives.

> **Outcome (2026-08-11).** `--slim=-all` hello: **4,856,664 bytes** against
> the ≤ 5.0 MB gate — the four features were carrying 3.2 MB. The parser's
> out-of-line surface re-measured at exactly the four symbols §P1 predicted,
> so `stub_eval.cpp` is four throwing definitions, and the same surface holds
> for the AOT pipeline (`--aot --slim=-all` links and runs). `when
> X::Feature::NotBuilt` catches a cut feature in compiled code; uncaught, the
> message names the feature and the rebuild flag. Grammar semantics as
> planned, plus one precedence rule: a named feature beats the `all` group
> (`-all,+eval` = cut three), same sign twice is idempotent, both signs is a
> conflict. `--bundle --slim=-eval` is refused outright — a bundled binary
> parses its embedded source, so the cut contradicts the mode. The manifest
> needed one real fight: clang at `-O2` elides an unescaped volatile local,
> initializer and all, so the keep-alive initializer instead passes the
> pointer OUT (to `rakuppKeepManifest` in rt) — an escaped address survives
> every linker we drive. Negative suite: `t/slim/run.raku`, 30 checks, wired
> into release.yml beside the embed smoke (POSIX legs).

### P4 — the scan, and the levels above `safe`

The AST feature scan over program plus module graph, the force-full triggers,
levels `auto` and `max`, and `+feature` overriding a level's decision.
**Gate:** the differential suite over `t/`, `examples/` and the battery;
`--slim` hello ≤ 5.5 MB.

> **Outcome (2026-08-11).** Bare `--slim` hello: **4,856,872 bytes** against
> the ≤ 5.5 MB gate, all four features cut by proof. The scan
> (src/SlimScan.cpp) walks program + embedded module graph; regex patterns
> are scanned textually (each rule measured against slim binaries: `\c[…]` →
> names, `<:Prop>` → props exactly when `uniPropNeedsCutTables()` says so,
> every embedded-code construct → eval), and a literal `{…}` block's source
> is extracted, parsed with the real Parser and walked — degradation is
> always to a trigger, never a guess, including "an AST node the walker does
> not model". All five triggers implemented, including the unembedded-module
> detection this plan called a prerequisite. The differential harness is
> `tools/slim-diff.raku` (per-run process-group-killed timeouts; programs
> that disagree with THEMSELVES are classified nondeterministic, not
> judged); final run **239/270 byte-identical, 0 different**. Its first full
> run caught a real wrong cut — ordinary numification reaches NUMV via
> non-ASCII digit transliteration — settled per this plan's own never-cut
> criterion: decimal digits moved to a never-cut decade-starts table
> (`uniDigitValue()`), which the cross-check against NUMV then revealed had
> been missing twelve newer-script decades in the Lexer's private copy all
> along (and Ol Onal's zero is at U+1E5F1, misaligned to `…0` — the reason
> it stays a table, not a formula). Also fixed en route: `uniMatchesProp`
> touched the SCRIPTS seam before the category checks (so `-unicode-props`
> broke `<:Lu>`); native codegen silently mis-ran regexes that touch program
> variables (now refused into the bundling fallback — the engine env-bridge
> is future codegen work; grammar/named-regex match-context blocks stay
> native and verified); and X::Feature::NotBuilt could be swallowed by eight
> lenient catch sites — it is now its own C++ type, `FeatureNotBuilt`,
> rethrown exactly where leniency must not apply. The battery leg of the
> differential runs at the release gate (RELEASING.md gate 4b).

### P5 — introspection and documentation

The directives — `help`, `list`, `why:`, `verify` —
[guide/CLI.md](../../guide/CLI.md), README, and a size row in the release
notes.
**Gate:** every level, feature and directive has a golden in `t/run.raku`,
in the style the CLI campaign established.

> **Outcome (2026-08-11).** All four directives live, riding the compile
> modes: `help` stands alone and prints the grammar with the REAL archive
> sizes beside the running rakupp; `list` and `why:FEAT` run the parse, the
> module graph and the scan — the same `slimDecide()` the compile path uses,
> so what `list` prints is what a compile does BY CONSTRUCTION — and stop
> without compiling; `verify` builds slim + a full reference (identical
> strip, no cuts), runs both, and emits only on byte-agreement of
> stdout/stderr/exit — measured refusing an explicit wrong cut
> (`safe,-unicode-names,verify` on a uniname program: exit 6, nothing
> emitted). The scan grew evidence: every use-site is recorded with what/
> where/line (walkers maintain the nearest enclosing node line), which is
> what `list`'s reason column and `why:`'s site table print. Grammar: one
> directive per SPEC, `help` alone, `why:` validates its feature name.
> Gate: t/run.raku grew 406 → 433 checks — `help` names every token; `list`
> and `why:` fragment-checked on a fixture (archive sizes vary per platform,
> so the checks pin structure and reasons, not bytes); every LEVEL compiles
> fibonacci, lands in the manifest and matches the byte-golden; `-all`
> names all four features in the manifest and still matches; `verify`
> agree-path golden'd there, refuse-path in `t/slim/run.raku` (now 48
> checks). CLI.md gets the directive table, README the size row.

### P6 — flip `auto` to the default

Not part of the 3.14 tag. `auto` is sound by construction, so the end state
is that it needs no flag and `--slim=safe` is the escape — the shape
`RAKUPP_PARALLEL` and `RAKUPP_LTM` both took in v3.0.0, escape hatch kept one
release. `RAKUPP_SLIM=0` gets added at the flip, not before.

**Gate, deliberately harder than the others:** the differential suite green
across the whole corpus on **several consecutive releases**, not one clean
run, plus at least one real user-reported binary built with `--slim` in the
field. The failure mode this guards against — a program that works for months
and then dies on a cold path — is exactly the one a single green run cannot
see.

### P7 — stretch: `--aot`

The AOT path reconstructs a known AST, so the same scan applies; it keeps a
full interpreter, so only the data features are cuttable, not `eval`.
Attempt only if P0–P5 hold their gates.

---

## Gates (all must hold)

1. **Differential:** `t/`, `examples/`, module battery — slim and full
   byte-identical in stdout, stderr and exit status.
2. **Negative:** every feature forced out with `-feature` throws the exact
   `X::Feature::NotBuilt` message; no crashes, no wrong answers.
3. **Size:** `say "Hello"` under `--slim` ≤ 5.5 MB; under `--slim=-all`
   ≤ 5.0 MB.
4. **Performance:** `perf-guard --check` green. The seam is the risk; the
   collation and normalization benchmarks are the ones that must not move.
5. **Roast:** unchanged. The interpreter never slims — only `--exe` output
   does.
6. **Portability:** the archive split builds and links on every platform CI
   covers, MSVC and MinGW included.

---

## Risks, named

- **The accessor seam costs speed.** Per-element indirection in
  `ucaElements()` or the normalization tables would be a real regression.
  Mitigated by the pointer+count-once shape, caught by gate 4. If a hot path
  cannot be made free, that table stays non-optional — 80 KB is not worth a
  slowdown.
- **The scan is wrong in a way the corpus does not cover.** This is the
  campaign's central risk and why defences 2, 3 and 6 exist. The honest
  position: `auto` is sound only up to the trigger list, so the trigger list
  is reviewed as carefully as the code.
- **Silently-skipped modules** ([MODULES-PLAN.md](MODULES-PLAN.md)) would
  make the scan's inputs incomplete. Detect-and-fall-back is a P4
  prerequisite.
- **Windows archive plumbing** is where multi-archive layouts historically
  break (github issue #1 was a single archive). P2 exists as its own phase
  for that reason.
- **A SPEC grammar is still a grammar.** One key beats six flags, but
  `--slim=max,+unicode,-eval` is a small language, and small languages grow.
  Held in check three ways: the feature list stays at four, conflicts are
  errors rather than precedence rules, and `--slim=help` is part of the key
  itself so the grammar can never drift from its documentation. If a seventh
  feature name ever looks necessary, that is the signal to re-read the
  non-goals rather than extend the grammar.
- **The ABI pins the parser.** `rk_eval`/`rk_run` in `librakupp` are the
  parser, so the shared library always links the real `eval` archive (see the
  constraint above). The risk is a future phase forgetting this and wiring the
  scan into `rakupp_shared` — the embed-smoke gate in CI would catch it, since
  its C host calls `rk_eval` on every run.
- **`safe` on by default changes shipped artifacts.** Stripping is invisible
  to Raku-level behaviour but not to a C++ crash report. The manifest plus
  `--slim=none` is the answer; if bug reports get harder to act on in
  practice, `safe` drops back to opt-in — it is worth 16%, not worth
  blind spots.

---

## Non-goals

- **Per-builtin or per-method code pruning.** The remaining 3.6 MB
  (`Interpreter` + `Builtins` + `MethodCall*`) would need registration split
  into linkable units and the `if (m == "…")` dispatch chains rebuilt as
  tables. Upper bound perhaps another 1.5 MB, floor around 3–3.5 MB, at a
  cost of weeks and a much harder correctness story. Revisit only if a real
  user need appears.
- **Dropping I/O.** 46 KB. Measured, dismissed.
- **LTO.** Measured worse than `-O2` and 10× the link time.
- **Compressed or self-extracting binaries.** gzip already halves the
  artifact for transport (2.1 MB for the slim build); a self-extracting
  wrapper trades startup time for disk and is a different campaign.
- **Slimming the `rakupp` binary itself.** It is an interpreter; it needs all
  of Raku by definition.

---

## Reproducing the measurements

```bash
RAKUPP_KEEPGEN=1 build-arm64/rakupp --exe hello.raku -o hello
c++ -std=c++17 -O2 -w -pthread -I src hello.rakupp.gen.cpp \
    build-arm64/librakupp_rt.a -o hello -Wl,-map,hello.map
```

Then aggregate the map's `# Symbols:` section by object file to get the
attribution table; `-Wl,-dead_strip` and `strip -x` for the toolchain rows;
`ar d` on a copy of the archive plus hand-written stub definitions for the
cuttable rows.
