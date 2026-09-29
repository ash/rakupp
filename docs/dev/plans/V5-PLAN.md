# Plan: v5.0.0 — 100% Roast, and the errors behind it

**Status, 2026-09-29:** v5.0.0 was tagged on number 1 — Roast at 1,423 of 1,424
files and 218,421 of 218,422 tests without skip/todo, on the list as it stands
at Roast `1f521d798`. Batches B0–B5 had not landed; they continue in v5.x
against the numbers below.

*Written 2026-09-26, before any code. The user chose the next two majors the
same day, from [V5-IDEAS.md](V5-IDEAS.md) and a survey taken for the purpose —
speed profiles, the compile path, value size and memory, coverage clusters,
four investigations run side by side; its data is in
[../findings/survey-2026-09-26/](../findings/survey-2026-09-26/). **v5 is about
errors; v6 is about speed** ([V6-PLAN.md](V6-PLAN.md)).*

*"Errors" means every measured way the engine gets an answer wrong: it refuses
code Rakudo runs, it crashes, it returns a wrong value without raising, or a
compiled backend fails to build or disagrees with the interpreter. Roast is
being taken to its ceiling by a campaign running in parallel. v5 is tagged when
that campaign has finished and the batches below have landed on top of it.*

---

## The numbers a stranger can re-measure

1. **Roast: all 1,434 files of `spectest.data` fully pass.**
   - Roast's own `spectest.data` is the list Rakudo's spectest runs, and the
     harness's default set since `5a08968d`. Every Roast figure in this plan is
     on that list. The 30 other `.t` files in the checkout (the `--all` set)
     are not part of the number.
   - The last full run was taken on 2026-09-26, just before `f78e5ee7`. Of the
     1,434 listed files it had **1,189** fully passing, 240 partial, 4 without
     TAP and 1 timing out, with 219,676 of 220,114 assertions that ran passing.
   - `f78e5ee7` took 12 more listed files to passing, which makes **1,201 of
     1,434**. The thirteenth file it names, `S14-roles/generic-subtyping.t`, is
     not on the list.
   - Rakudo 2026.08 passes 1,425 of the 1,434 through this harness
     ([rakudo-2026.08.list](../../status/roast-lists/rakudo-2026.08.list)).
     rakupp already passes all 9 that Rakudo fails:
     `S02-literals/format.t`, `version.t`, `S17-supply/batch.t`,
     `S24-testing/11-plan-skip-all.t`, `S29-os/system.t`, the gb18030, gb2312
     and shiftjis encode-decode files, and `S32-str/sprintf-b.t`. So every file
     on the list is reachable, and **100% means 1,434 of 1,434**.
   - This number belongs to the parallel campaign, and v5 is tagged on it.
2. **No module file that compiled under the last release fails to compile.**
   `-c` over every `lib/` file in the module battery, run with the last release
   and then with the candidate. Measured 2026-09-26, HEAD against v3.27.0: 25
   files in 12 distributions compile under v3.27.0 and fail at HEAD
   ([battery-compile-scan.tsv](../findings/survey-2026-09-26/battery-compile-scan.tsv)).
   The target is **0**.
3. **No crash.**
   - A `use` of every module in the battery ends without a signal.
   - The nine-line `evalCall` repro dies 0 times in 1,000 runs. It segfaults
     about half the time today.
   - Every program `--cpp` accepts also builds.
4. **Distributions passing, of the 1,791 Rakudo passes** — from a full sweep at
   the tag. The board is from v4.0.0 (1,006 on 2026-09-17), and it is already
   stale: of 60 queue distributions re-run on 2026-09-26, 7 now pass. The
   target is not a count. It is the clusters in B4 landing, and **no
   distribution that passed at v4.0.0 failing at v5.0.0**.
5. **The JavaScript gate agrees with the interpreter.** `t/js/run.raku`
   requires 0 disagreeing programs. It reported **102** on 2026-09-26 (179 in
   core and agreeing, 452 refused, of 733).

---

## What the survey found

### Regressions: strictness that refuses code Rakudo accepts

Parse-time checks added while chasing Roast files now refuse ecosystem code.
Each row below was reproduced as a one-liner on 2026-09-26. Rakudo 2026.08
accepts every snippet that rakupp refuses.

| check | one-line repro | rakupp at HEAD | distributions |
|---|---|---|---|
| "Redeclaration of return type" (Parser.cpp ~10909) | `sub f(&cb (Int --> Int)) returns Int { … }` | refuses: the callback's `-->` is taken as `f`'s | LibCurl, Cairo, GLib (10 files), Inline::Perl5 |
| `#` inside a regex word list (Parser.cpp ~12444) | `"a # b" ~~ / (< # ^ >) /` | "Couldn't find terminator" | Template::Mustache |
| `:` inside a negated class (Parser.cpp ~12353) | `/ <-lower -[:]>+ /` | "Backtrack control ':' …" | Inline::Perl5 |
| "Quantifier quantifies nothing" (Parser.cpp ~12340) | `/^ $<w>=.*? +%% ["```" \n? $<c>=.*? "```" \n?] $/` | refuses | Base64::Native, PDF::Lite, CSS::Properties (a shared README test) |

Also in the scan, and not yet reduced to a snippet:

- `$!text` inside a `sub FETCH` nested in a method: "used where no 'self' is
  available". PDF::Content, and with it the PDF family.
- JSON::Class (4 files):
  - `has Str:D $.x is mooish(…)`: "requires an initializer". Rakudo refuses
    this shape in a plain class, so what needs checking is what the real
    trait does.
  - "Redeclaration of symbol '::Mu'".
- A stubbed `role Message { ... }`: "No appropriate parametric role variant".
  IRC::Client, and six IRC::Client plugins behind it. Rakudo refuses the naive
  reduction too, so this also needs the real module.
- Two slangs no longer activate: BDD::Behave and Qwiratry.

### Crashes

- **`use Text::Markdown` exits 139** (SIGSEGV) at HEAD. The survey placed it in
  `collectMentionedS`.
- **A data race in `evalCall` segfaults the process.** Four `start` blocks
  calling one sub, 200 rounds; about half the runs die
  ([EVALCALL-RACE-2026-09-17.md](../findings/EVALCALL-RACE-2026-09-17.md)). It
  is still open. The Rat literal's mutable `cacheN`/`cacheD` is the other known
  race class.
- **`--exe` fails to build a program with an `Inf` or `NaN` literal.**
  - Codegen.cpp ~1079 (and the unboxed path at ~3301) prints a Num with
    `%.17g`, which writes `inf` or `nan` into the generated C++.
  - main.cpp ~1302 falls back to bundling only when the code generator itself
    refuses a construct. When the C++ compiler fails, the build fails.
  - This hits 10 of the 1,273 programs `--cpp` accepts; `infinity.t` and
    `categorize-list.t` are two.
- **A stale runtime archive silently corrupts `--exe` binaries.**
  - `build-arm64/librakupp_rt.a` was dated 2026-09-20 while `build-arm64/rakupp`
    was dated 2026-09-26.
  - Every binary built from that pair either died with SIGKILL at exit
    (`_os_unfair_lock_corruption_abort` in a `std::mutex` destructor, reached
    from the generated `static Interpreter rt`) or failed to link.
  - `findRuntime` (main.cpp ~826) pairs whatever archive it finds with the live
    headers.
  - A layout-hash symbol, defined by the archive and referenced by the
    generated code, turns this into a link error.

### Silent wrong answers, verified at HEAD

- **An imported user-defined operator answers wrongly without raising.**
  - An imported `infix:<ip==>` returns 1 for `1 ip== 1`.
  - An imported postfix parses as "Confused".
  - `postcircumfix:<{|| }>` is not supported at all.
  - Operators from a used module are found by a text scan (Parser.cpp ~703).
  - Affects Net::IP::Parse and Slippy::Semilist, plus 5 language-slang
    distributions (French, German, Chinese, Japanese, ClassicalChinese) not yet
    verified.
- **`split(:v)` loses the named captures** of its separator matches
  (MethodCallPart3.cpp ~5178). Template6.
- **`@.x` and `%!x` share one storage slot** (`ObjectData.attrs`).
  Email::Simple, Prettier::Table.
- **`+$obj` ignores `handles <Numeric>`** (Interpreter.cpp ~42354). BitEnum.
- **`0xfe -^ 0xff - 1` gives `253e0`, not −2.** Data::MessagePack.
- **`my int @b = @a` stores `Any`** where `@a` has a hole, instead of 0.
  Three native-array Roast files.
- **`.lazy` returns a List.** 3 distributions.
- **A user-defined `method can` is overridden** by the builtin.
- **Issue #90:** nested exported `CStruct` classes coerce wrongly.
- **rakupp accepts code Rakudo refuses.** `class A { method a { 1 } method b {
  2 } }` on one line runs here; Rakudo says "Strange text after block". Being
  stricter is exactly how the regressions above happened, so this goes through
  the compile scan like every other new check.
- **The known wrong answers carried over from the candidate list**, recorded in
  July and August in [BUGS.md](../findings/BUGS.md) and
  [TRIAGE.md](../findings/TRIAGE.md), to re-verify before planning:
  - `.append($h<k>)` spreads
  - `[ %( ) ]` does not flatten
  - `return` outlives its routine
  - a lazy `.map` returned from `when` is reified
  - a `LEAVE` phaser's own exception is swallowed
  - `$?FILE` inside a module names the main program
  - non-Latin hash variable names cannot be assigned
  - ideograph numerals shadow identifiers
  - the regex scan limit of about 8M characters answers "no match"
  - regex code blocks re-run on every backtrack; one fix was attempted and
    backed out
  - `:exhaustive`

### The ecosystem queue

Rakudo passes 774 distributions that rakupp fails: the v4.0.0 board joined with
the 2026-09-16 Rakudo baseline
([ecosystem-queue.tsv](../findings/survey-2026-09-26/ecosystem-queue.tsv); 720
fail their own tests, 37 fail through a dependency, 15 time out). Where the
first errors cluster:

| cluster | distributions | examples |
|---|---:|---|
| the regressions above | 12 now, plus dependents | LibCurl, JSON::Class, IRC::Client, Base64::Native, the PDF family, Cairo, GLib, Inline::Perl5 |
| missing MOP methods | ~18 | `add_fallback` ×3, `set_name` on a Regex ×2, `enum_value_list` ×2, `private_method_table`, `get_attribute_for_usage`, `set_body_block` (Tinky) |
| a grammar `token` does not satisfy a role's `method {...}` stub (Interpreter.cpp ~13756: `hasImpl` looks only at `c->methods`) | 4 | CSS::Module, CSS::Stylesheet, CSS::TagSet, Font::Resources |
| the ValueClass family: `$.bless: x => 3, }` does not parse, and `my value-class` is not supported | 4 | |
| qualified names not resolved relative to the current package | 5–7 | `Level::trace` inside `module LogP6` (×3), `our grammar` in an attribute default (URI::Template), Terminal::MultiProgress |
| one root each | 3–5 per family | Testo (and IO::Dir, Directory, Distribution::Extension::Updater, Benchy); List::MoreUtils EXPORT tags ×4; DSL::Entity::* `substr` −1 ×3; "Too many levels of recursion" (Version::Repology, PURL, VERS; Grammar::PrettyErrors, Protobuf) |
| small one-offs | 1–4 each | `Match.Hash`, `$*SCHEDULER.max_threads`, `[ EXPR for LIST; ]`, `(next LABEL)` inside an expression, Gzz::Text::Utils ("Missing block" at EOF) |
| slangs | 13 | parse errors from `use Slang::*` ([SLANG-PLAN.md](SLANG-PLAN.md)) |
| custom HOW hooks: only `compose` runs | a few | OO::Actors counts wrongly; B4 lists the rest of this family |

"A wrong value, nothing raised" is 365 of the queue, and it still does not
cluster. Twelve were traced on 2026-09-26 and every root cause was different.
The only recurring themes are internal lookups that skip `handles` delegation,
and container copy semantics.

**The Cro cluster is not a lever.** Rakudo itself fails 20 of the 21 Cro
distributions on this machine, so only Cro::H is in the reachable queue. The
"168 distributions behind Cro" in V5-IDEAS was counted against all 2,529, not
against the 1,791.

### Roast: the parallel campaign's map

On the list, 245 files did not fully pass in the last full run. `f78e5ee7`
passed 12 of them, which leaves **233**:

- `S17-supply/supplier-preserving.t`, which emits no TAP and is not clustered
- the 232 in [roast-clusters.tsv](../findings/survey-2026-09-26/roast-clusters.tsv),
  every one on the list and every one passed by Rakudo

Of the 232, 95 are one failing assertion from passing and 86 are two. A file
counts under "flip" when fixing that cluster alone makes it pass:

| cluster | flip (touched) | examples | mechanism |
|---|---:|---|---|
| scalar containers and `is rw` aliasing | 25 (33) | `S03-operators/identity.t`, `S03-binding/arrays.t`, `S04-statements/for.t`, `S32-hash/kv.t` | There is no container type (Value.h ~480). Aliasing is copied back per construct (Interpreter.cpp ~15050, `scalarListAlias` ~28240). |
| missing compile-time checks | 20 (30) | quoting, ternary, `S06-advanced/stub.t`, `S14-roles/basic.t` | more than 20 separate checks. **Every one goes through the compile scan** (B0). |
| metamodel: role groups and the MOP | 14 | `S14-roles/typecheck.t`, `S12-methods/qualified.t`, mro-6c/6e | parametric role groups are not first-class |
| laziness | 12 (14) | `S02-types/lazy-lists.t`, `S04-statements/gather.t`, `S32-list/seq.t` | `map`/`grep`/`gather` over a finite source run eagerly, and `gather` re-runs its block instead of suspending (Interpreter.cpp ~43581) |
| lexical import and scope | 11 | `S11-modules/import.t`, `lexical.t`, `require.t` | imports leak out of their block |
| the CompUnit and Distribution API | 8 | `cur-candidates.t`, `curli-install.t`, `precompiled.t` | missing classes and methods |
| phasers, multi dispatch, coercion | 7, 8, 6 | `will.t`, `proto.t`, `coercion-methods.t` | one issue per file |
| pseudo-packages | 5 | pseudo-6c/6d (`OUR::` binding, `CALLER::UNIT`, `::("SETTING")` in EVAL) | |
| holes in arrays | 5 (7) | `native-int.t`, `native-num.t`, `native-str.t`, `S32-array/delete.t`, `S32-list/reverse.t` | a hole copied into a native array reads as `Any` (see the wrong answers above); `.List`/`.Slip` do not fill holes with `Nil`; `reverse` returns no containers for holes |
| `catch.t` times out | 1 | 500k `die`/CATCH iterations | a throw costs ~70 µs here and ~4.5 µs on Rakudo. It is a speed item (V6 P2), and moves here if the campaign needs it. |

---

## Order

**B0 can start now.** It touches only `tools/` and `t/`, and not `src/`,
where the Roast campaign is working. **B1 onward starts after the campaign's
last commit.** Both efforts would otherwise be editing Parser.cpp and
Interpreter.cpp in one tree, and the first four regressions came from exactly
that code.

### B0 — instruments first

- **`tools/battery-scan.raku`.** Runs `-c` on every battery `lib/` file with a
  reference binary and a candidate binary, and lists the files that compile
  under the reference and fail under the candidate.
  - The reference is the last release's downloaded asset, not a build of the
    tag.
  - It takes about 5 minutes on one core, runs sequentially, and has a
    per-file timeout.
- **`tools/battery-use-smoke.raku`.** Runs `use` on each module of each battery
  distribution in a fresh process, and records signal exits and hangs.
- **`t/race/evalcall.raku`.** The nine-line repro, run N times, reporting the
  number of deaths. It stays opt-in until B2 brings it to 0.
- **A `--cpp` build check.** Every program `--cpp` accepts in `examples/`,
  `t/` and the fully-passing Roast files is compiled with `-fsyntax-only`. It
  is sequential, and a release gate rather than a per-batch one.

### B1 — the regressions

The four verified checks, then each unreduced item, reduced against the real
module. For each one:

- Probe Rakudo on the exact snippet.
- Restore acceptance without losing the Roast file the check was added for.
  `git show` of the introducing commit names it.
- Add a regression test under `t/`.

Done when the battery scan against the last release reports 0.

### B2 — crashes

- Text::Markdown.
- The `evalCall` race, done as a project rather than a patch: find the shared
  name-lookup state, and extend the TSan ratchet to cover the repro.
- The Rat literal cache race.
- `--exe` with `Inf`/`NaN` (spell them as `std::numeric_limits<double>`), and
  fall back to bundling when the C++ compiler fails.
- The runtime layout-hash symbol.

Done when the use smoke shows 0 signals, the race repro 0 deaths in 1,000 runs,
and the `--cpp` build check 0 failures.

### B3 — silent wrong answers

Everything under "Silent wrong answers" above.

- Re-verify the carried-over list first, and fix the entries that still
  reproduce.
- Exported user operators come first. A silent wrong value is the worst class,
  and 3 to 8 distributions depend on them.
- Regex code blocks under backtracking need side effects deferred to the
  accepted path. Read why the first attempt was backed out before starting.
- New strictness, such as "Strange text after block", lands only with a clean
  battery scan.
- Two divergences an adopter's course teaches (Raku Koans,
  [live/ADOPTIONS.md](../../../live/ADOPTIONS.md)). Rakudo refuses a call that
  can never bind when it compiles the file (`Calling f(Int) will never work
  with declared signature (Str $x)`, and the same for a missing required
  named); rakupp raises that error only when the call runs, so
  `dies-ok { f(42) }` passes here and never compiles there. And under a label
  named `OUTER`, Rakudo reads `next OUTER` as the pseudo-package and dies;
  rakupp takes the label. The first is new strictness, so the battery scan
  applies.

### B4 — the ecosystem clusters

Take the table's rows from the top:

- The small MOP methods and the role-stub rule. The stub rule is one
  condition.
- The ValueClass parse, and package-relative qualified lookup.
- The one-root families and the one-offs.
- Custom HOW hooks: dispatch `new_type`, `add_attribute` and `add_method` to
  a module-supplied metaclass.
- `Attribute` MOP objects with `get_value` and `set_value`.
- Conformance of parameterized roles (`R2[Int] ~~ R2[Cool]`).
- Red's compile-time `my` (issue #77).

Re-run each affected distribution's tests as its cluster lands.

### B5 — compiled backends that never disagree

- **JavaScript gate: 102 disagreeing to 0.**
  - The duplicate-`let` declarations and ReferenceErrors, which kill whole
    programs: 13.
  - A run-time "No such method" exits with a distinct code. It then counts as
    out of core rather than as a disagreement (23), and `--fallback=wasm` can
    catch it.
  - The remainder, case by case.
- **The `--slim` size gate.** It measured 10,242,504 bytes for a `--slim`
  hello from `build/` on 2026-09-26, over its pin. Confirm with `t/slim/run.raku`
  on both slices, as [SLIM-PLAN.md](SLIM-PLAN.md) and the gate's comment block
  prescribe. Re-pin it if the growth is reachable runtime code, and carve it
  out if it is not.

### B6 — measure and tag

- A full Roast run, alone on the machine.
- A full ecosystem sweep. It is multi-core and needs asking first; follow the
  sweep runbook.
- `perf-guard --check`, the battery scan, the use smoke and the `--cpp` build
  check.
- Then CHANGELOG, VERSIONS.md, MILESTONES.md, and every document that carries
  the Roast figures.

---

## Gates, every batch

- **Zero Roast regressions:** the sorted `[PASS]` list compared with `comm`.
  Once the campaign has finished, 100% stays 100%.
- **The local suite**, `t/run.raku`.
- **`perf-guard --check`.**
- **The battery compile scan:** 0 new failures against the last release. From
  B0 on, this gate applies to every change that adds a check.
- **The use smoke**, from B2 on.

## Decided nearby

- **Speed is v6**, including the cost of a thrown exception, unless `catch.t`
  needs it for the Roast number.
- **The rest of V5-IDEAS stays unscheduled.** That is platforms and hosts, the
  capability sandbox, modules and the supply chain, and developer experience;
  each remains in [V5-IDEAS.md](V5-IDEAS.md) with its evidence.
- **An enum's trailing `does role` stays parsed and dropped.** Rakudo does not
  compose it either.
