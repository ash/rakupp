# docs/dev/findings/ — what we ran, and what it found

The record of things measured against something outside this repo: Rakudo as the
oracle, the Roast suite, the module ecosystem, other engines, another language's
runtime. Where [`plans/`](../plans/) says what we intend and
[`experiments/`](../experiments/) says what we tried and sometimes reverted, this
directory says **what we pointed the engine at, and what came back**.

Every entry below names the date or version it is anchored to, because most of
these are snapshots and a snapshot with no date is a claim with no evidence. A
few are living logs, appended to as work continues; those are marked.

The annotated selection in the [dev index](../README.md) covers the divergence
logs and the reviews. This page is the complete list.

## Divergence logs — a corpus run under both engines

- **[CORPUS-DIFF.md](CORPUS-DIFF.md)** *(living; rounds 1–4, 2026-07-18 →
  2026-07-22)* — byte-for-byte differential of the raku-corpus programs against
  committed Rakudo v2026.06 references, round by round: 1,443 of 1,789 exact
  matches at the first run, 1,532 of 1,812 (84.5%) by round four, with grammar
  and `Match` residue the largest remaining cluster. Data in
  [`corpus-diff/`](corpus-diff/).
- **[PWC-DIVERGENCES.md](PWC-DIVERGENCES.md)** *(living; swept 2026-07-14, round
  two 2026-08-20)* — 10,428 Perl Weekly Challenge solutions under both engines:
  byte-identical output went 2,663 → 4,056 of ~6,800 comparable programs over
  fifteen batches, and six authors hold 51% of the 2,707 remaining mismatches.
  Data in [`pwc/`](pwc/).
- **[COURSE-DIVERGENCES.md](COURSE-DIVERGENCES.md)** *(living; 2026-07-20 on
  rakupp 0.9.0, first swept 2026-07-13)* — 1,778 course.raku.org snippets, 1,698
  byte-identical (95.5%); the real divergences cluster in reactive/async
  (`await` on a Supply, `done` inside `react`, interval timers) and `rule`
  sigspace around `%`. Data in [`course-diff/`](course-diff/).
- **[SPEC-DIVERGENCES.md](SPEC-DIVERGENCES.md)** *(2026-07-20, Rakudo v2026.06)*
  — the 18 disagreements found by dual-verifying 260 examples across 111
  raku.online specification pages; 17 fixed, leaving native typed arrays boxed as
  `Array[int]`.
- **[ROSETTACODE.md](ROSETTACODE.md)** — 333 RosettaCode Raku tasks under both
  engines: of the 197 comparable ones, 34 match byte-for-byte and 41% run to
  completion, parse errors the largest blocker. Eleven fixes landed.
- **[BUGS.md](BUGS.md)** *(living; from 2026-07-17, appended through 2026-08-09)*
  — article-series snippets cross-checked against Rakudo. Quantified-capture
  arity, Blob range slices and eager junction collapse fixed; non-Latin hash
  names, ideograph numerals shadowing identifiers, `+'+'` returning `0e0` and
  `$?FILE` in modules still open.
- **[BUGS-JS-SHOWCASE.md](BUGS-JS-SHOWCASE.md)** *(2026-07-19)* — fourteen
  divergences from writing the JavaScript/TypeScript interpreter in
  `showcase/js`: the four grammar/lexer bugs and both `CATCH` bugs are fixed;
  five runtime and three `--exe`-only divergences remain open.
- **[BUGS-SQLITE-SHOWCASE.md](BUGS-SQLITE-SHOWCASE.md)** *(2026-08-18)* — ten
  divergences from the NativeCall SQLite client in `showcase/sqlite` — `LEAVE`
  firing at its declaration in methods, `.throw` on any object, `Pointer`
  numification, NULL `char*` — all fixed and every workaround retired.
- **[TRIAGE.md](TRIAGE.md)** *(living; 2026-07-11, 07-12, 08-20, 08-31, 09-01, 09-07)* —
  behavioural gaps hit while writing real programs *outside* the harness: array
  assignment aliasing, `next` inside `.map`, `return` in `CATCH`, native `--exe`
  multi-method dispatch, role-body lexicals shared across composers, what the
  `END` phaser fix reaches and what it leaves to `--target=js`, each with a
  minimal repro.

## Ecosystem campaigns

- **[ECOSWEEP-2026-08.md](ECOSWEEP-2026-08.md)** *(living; 2026-08-23 →
  2026-08-30)* — every REA distribution put through `rakupp test` across six
  sittings: 624 of 2,524 green at the first pass, 746 of 2,526 after the fix
  campaigns, with 18 reproducible regressions and 383 distributions still
  dependency-blocked. Data in [`ecosweep/`](ecosweep/).
- **[rakuast/README.md](rakuast/README.md)** *(2026-09-11, Rakudo 2026.08)* — the
  RakuAST tree-oracle baseline over raku-corpus: 1,858 of 1,870 programs
  tree, 162,992 nodes, 213 classes; the 12 that do not, and the harness traps.
- **[FRESH100-2026-08-20.md](FRESH100-2026-08-20.md)** *(2026-08-20)* — the 100
  newest REA distributions under `rakupp test`: 16 pass, 49 are blocked by a
  dependency before their own suite runs, five parse gaps proved ours against
  Rakudo — and our own installer is the top blocker. Data in
  [`fresh100/`](fresh100/).

## Roast and the specification

- **[ROAST-GAPS.md](ROAST-GAPS.md)** *(living; from 2026-07-11)* — every failing
  Roast file bucketed into six blocker classes (parser clusters, missing types
  and runtime subsystems, Unicode data, semantic bugs, S17 timing), with a
  suggested attack order.
- **[CONFORMANCE.md](CONFORMANCE.md)** *(historical, not maintained)* — all ten
  `FEATURES.md` sections walked against docs.raku.org, fixing every safely
  fixable divergence — `.raku` round-trips, `^^`, `~~ Callable`,
  `when`/`proceed`/`succeed`, `CATCH` in closures — and lifting Roast
  fully-passing from 224 to 249 with no regressions.

## Knowledge sheets — Rakudo's sources read at the release tag

- **[semantics/](semantics/README.md)** *(started 2026-09-17, Rakudo tag
  2026.08)* — behaviour that neither docs.raku.org nor Roast states, extracted
  from Rakudo's setting one type at a time as executable probes with the
  oracle's output recorded verbatim, for a later session to implement without
  the source open. The rule: features and semantics, never internals. Sheets so far:
  [Supply.md](semantics/Supply.md) — 69 items, 38 neither fully documented
  nor fully Roast-asserted, three Rakudo bugs flagged so that nobody copies
  them; IMPLEMENTED 2026-09-18 (rakupp differs on 7, four of them on purpose;
  Roast S17-supply went 27 → 55 of 58 files); [Nil-Any.md](semantics/Nil-Any.md) —
  Nil and Any as a one-element list, 50 items, 16 undeclared, rakupp
  differs on 43; [Str.md](semantics/Str.md) — Str, Stringy,
  the string half of Cool and the allomorphs, 66 items, 12 undeclared,
  rakupp differs on 60, ten Rakudo bugs flagged (three of them hangs);
  [List-Array.md](semantics/List-Array.md) — List, Array, Seq and Slip, 36
  items, 4 undeclared, rakupp differs on 29; [Hash-Map-Pair.md](semantics/Hash-Map-Pair.md)
  — Hash, Map and Pair, 20 items, 2 undeclared, rakupp differs on 16;
  [IO.md](semantics/IO.md) — IO::Path, IO::Handle, IO::Spec::Unix,
  IO::Special, IO::Pipe and IO::CatHandle, 26 items, 7 undeclared, rakupp
  differs on 24, one quirk (slurp throws where the docs say fail);
  [Range.md](semantics/Range.md) — Range and its operators, 33 items, 12
  undeclared, rakupp differs on 29, two Rakudo bugs flagged (`reverse` of a
  non-integral range, `first(:end, :kv)`); [Int-Num-Rat.md](semantics/Int-Num-Rat.md)
  — Int, Num, Rat, FatRat and the Real/Numeric roles, 28 items, 3 undeclared,
  rakupp differs on 26, six Rakudo bugs flagged (`7 mod 2.5`, a negative base
  to a negative power, `[lcm] ()`, `1 +< -64`, `narrow` losing the value,
  `polymod` with a non-Int divisor);
  [Sequence.md](semantics/Sequence.md) — the `...` operator, 29 items, 13
  undeclared, rakupp differs on 28, two Rakudo bugs flagged (a `none`
  endpoint taken for infinity, chained `^...^` keeping its end);
  [Promise.md](semantics/Promise.md) — Promise, await, start, sleep, Lock,
  Lock::Async, Lock::Soft and Semaphore, 36 items, 12 undeclared, rakupp
  differs on 36, four Rakudo bugs flagged (a dying `:synchronous` then, a Nil
  in a Channel hanging `await`, `sleep-timer NaN`, 32-bit Semaphore permits);
  [Date-Time.md](semantics/Date-Time.md) — Instant, Duration, Date and
  DateTime, 33 items, 4 undeclared, rakupp differs on 31, four Rakudo bugs
  flagged (Date ranges compared by Str, a date-only DateTime string throwing
  X::AdHoc, `.Str` rounding to second 60, `cmp` on DateTimes by Str);
  [Proc-Async.md](semantics/Proc-Async.md) — Proc, Proc::Async, `run` and
  `shell`, 37 items, 9 undeclared, rakupp differs on 36, two Rakudo bugs
  flagged (a pre-spawn `.exitcode` of 1 that freezes the status, `IO::Pipe.read`
  ignoring its count);
  [Code-Introspection.md](semantics/Code-Introspection.md) — Code, Block,
  Routine, Signature, Parameter, Attribute and WhateverCode, 37 items, 24
  undeclared, rakupp differs on 37, eight Rakudo bugs flagged (`.multi`
  answering 0, method signatures printing a double colon, `.prec` failing its
  own return check, `.raku` spellings that do not re-parse, hand-built
  Signatures that cannot bind, `Parameter.new` name handling, a null `.file`
  on a WhateverCode, ForeignCode gist);
  [Exception-Backtrace.md](semantics/Exception-Backtrace.md) — Exception,
  Backtrace, Failure and the control-exception protocol, 36 items, 15
  undeclared, rakupp differs on 35, three Rakudo bugs flagged (a rethrown
  CX::Return replacing the value, a bare `succeed` yielding an engine null,
  LEAVE running before NEXT in a loop);
  [Set-Bag-Mix.md](semantics/Set-Bag-Mix.md) — Set, Bag, Mix, their hash
  forms and the set operators, 44 items, 17 undeclared, rakupp differs on 44,
  four Rakudo bugs flagged (`.Bag` truncating the source Mix, an uninitialised
  `is Set` variable, set operators hanging on a Junction, a Hash intersection
  ignoring false values); [Blob-Buf.md](semantics/Blob-Buf.md) — Blob, Buf,
  the typed buffers and the encodings, 37 items, 21 undeclared, rakupp differs
  on 37, nine Rakudo bugs flagged (`.bytes` of native-Int blobs, `allocate`
  with an empty pattern hanging, `splice` past the end, signed `~&` and `~|`,
  two endless recursions, `read-bits` and `write-ubits` off by bits);
  [Grammar-Match.md](semantics/Grammar-Match.md) — Grammar, Match and
  Regex as objects, 41 items, 23 undeclared, rakupp differs on 39, five Rakudo
  bugs flagged (`.actions` answering NQPMu, a type-object topic warning instead
  of False, `:nth` with `:x`, `:x(Nil)`, `.substr` with a regex);
  [Compiler-Side.md](semantics/Compiler-Side.md) — the precedence table, the
  quote constructs and sink context, 34 items, 8 undeclared, rakupp differs on
  32, one Rakudo bug flagged (a brace-delimited `qx` running a nested brace
  group as its own command).

## Reviews — before and after a release

- **[REVIEW-1.0.md](REVIEW-1.0.md)** *(2026-07-12, updated 07-13)* — eight finder
  angles over the uncommitted native-codegen batch plus six module reviews before
  tagging v1.0: eleven codegen bugs, two kill-proof zombie mechanisms, silent
  wrong answers, and the subtest Pair-form no-op that had inflated the 418-file
  Roast baseline.
- **[REVIEW-2.0.md](REVIEW-2.0.md)** *(2026-08-06)* — nine parallel fresh-eyes
  passes over the ~57k-line hand-written `src/`, landing five gated batches.
  Copy-then-diverge was the dominant defect class; Roast rose from 194,980/589
  files to 197,060/593 with zero down-movers.
- **[REVIEW-3.5.md](REVIEW-3.5.md)** *(2026-08-19)* — a profile-first pass: an
  ASCII `uc` fast path cut a dispatch benchmark 28%, and the clone detector
  surfaced sigilless-name shadowing, `m:g` answering `Nil` instead of an empty
  `List`, and issue #22's junction autothreading.
- **[REVIEW-3.7.md](REVIEW-3.7.md)** *(2026-08-27, at v3.7.0+37)* — the red
  perf-guard turned out to be a stale baseline rather than a regression; the
  clone sweep found fifteen drifted clusters, worst of them compiled `[op]`
  reduce disagreeing with the interpreter, fixed in three same-day batches.
- **[TOOLS-3.23.md](TOOLS-3.23.md)** *(2026-08-29, opening v3.23.0)* — every
  gate, benchmark and measuring tool this project runs, reviewed before the
  re-baseline: twenty-five defects fixed, including binaries picked by bare PATH
  lookup and hand-written lists that nothing compares against, and gate 7 shown
  to have no red path at all.

## Release-gate post-mortems

- **[OPEN-3.21.md](OPEN-3.21.md)** *(v3.21.0 sitting, 2026-08-29)* — seven
  findings: `flat` over a bound array shipped wrong RIPEMD digests, plus six gate
  defects — corrupted status lines, no archived baseline, a 1,253-process fork
  bomb, an unexplained perf-baseline move.
- **[GATES-3.22.md](GATES-3.22.md)** *(v3.22.0)* — each of those fixed and
  measured: a bound `@` parameter keeps a `List`, the Roast harness stops
  splicing child output into its status lines, and the moved perf baseline
  survives four more eliminations uncaused.

## Performance investigations

- **[STRING-SCAN-QUADRATICS.md](STRING-SCAN-QUADRATICS.md)** *(2026-08-09, at
  v3.0.1)* — seven per-character string operations rescanned the whole string on
  every call, making `JSON::Fast`'s parse quadratic in both the ASCII and
  non-ASCII lanes; caching the verdicts on the immutable `StrBody` left only the
  AST-walk gap to Rakudo.
- **[HASHFILL-AND-BIGINT.md](HASHFILL-AND-BIGINT.md)** *(2026-08-21 and
  2026-09-01)* — the two times an outside comparison said we were slower and did
  not say where: `hashfill` against perl, `bigint` against mutsu. What each
  turned out to be, why the competitor's own answer was the wrong answer both
  times, and the thing they had in common — a large share of both gaps was
  copying rather than computing.

## One-off bug reports

- **[BUG-SPAWNCAPTURE-HANG.md](BUG-SPAWNCAPTURE-HANG.md)** *(2026-08-18)* — two
  rakugrid generator hangs where the parent parked in a syscall inside
  `spawnCapture` after the child had exited and its zombie went unreaped;
  `SIGCONT`/`SIGCHLD` freed it, which points at a lost wakeup. Still
  unreproduced from a small script.

## Sub-series

- **[engines/](engines/README.md)** — nine implementations of dynamic languages
  read against this codebase, one findings doc each: Perl 5, PHP 7, CPython,
  Ruby, MoarVM, Rakudo, Lua/LuaJIT, JavaScriptCore and V8's front end. Each pairs
  an engine's primary sources with the measured state of our own structures and
  ranks what transfers. It has [its own index](engines/README.md), which also
  records which findings were already ours before the corresponding engine was
  read.

## Data directories

Raw output for the logs above, kept so a claim can be re-checked rather than
taken on trust: [`corpus-diff/`](corpus-diff/), [`course-diff/`](course-diff/),
[`ecosweep/`](ecosweep/), [`fresh100/`](fresh100/), [`pwc/`](pwc/).
