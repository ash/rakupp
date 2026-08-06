# Pre-2.0 independent review — 2026-08-06

A fresh-eyes review of the whole hand-written `src/` surface (~57k lines,
generated Unicode tables excluded), run as nine parallel review passes:
Interpreter.cpp in three slices, Builtins.cpp, Parser+Lexer, the MethodCall
trio, Regex, Codegen+AST/serializer, and the support files. Every finding was
triaged into five gated batches; every batch passed `t/run.raku` and the
relevant wider gates before the next batch started. Rollback: branch
`backup/pre-v2-review` at b9f87d9, per-batch patches in `rc-work/review-2.0/`,
pre-review binary `build-arm64/rakupp-prereview`.

## Baselines (pre-review, at b9f87d9)

- Roast: 194,980 / 589 fully-passing (the post-subtest-reckoning honest line)
- t/run.raku 310/310 · battery 50 PASS · corpus 1,592/1,812 MATCH
- perf-guard OK (loopsum at +4.8% of the v1.5.1 baseline — no headroom)

## What the review found, in one paragraph

The codebase is unusually clean for its growth rate: no stray debug output
(everything is env-gated), no `#if 0`, no stale TODOs, and nearly every
Rakudo-quirk carries a comment naming the module or test that forced it. The
dominant defect class — by a wide margin — is **copy-then-diverge**: the same
rule implemented in two to four places where one copy later learned something
the others didn't. Every confirmed behavior bug below is an instance of that
class. The dead code that existed was almost all *shadowed dispatch arms* (an
arm re-implemented later in the chain after an earlier arm already claimed the
method name), which is the split-file version of the same disease.

## Batch 1 — dead code (~170 lines deleted, zero behavior)

Shadowed dead arms: `pairup`×3→1, `unival`/`univals` (both a dead direct
registration in Builtins and a dead drifted arm in Part3), `tree`×2→1,
inline `roots`, Array `AT-POS`/`EXISTS-POS`/`ASSIGN-POS` in Tail (Part3 owns
them), Hash `antipairs` branch, `caps` tokens in the Match keys arm,
flavored-`path` arm, dead second `$?LINE` arm and uncalled `stmts()` in
Codegen. Plus: duplicated role-resolution lookup pair, duplicated `pod`
assignment, unused locals/params (`copyOutRw`'s `methodCtx`, `haveAny`,
`wasWhatever`, lexer `save`/`afterKw`), an undeclared-but-unused
`argsToPositional`, a stale `RAKUPP_ALWAYS_INLINE` copy, dup includes,
identical if/else bodies, and the unreachable `<` atom tail in Regex.cpp
(clang -Wunreachable-code confirmed).

## Batch 2 — parser/lexer/dispatch divergences (each oracle-verified)

- `»²` superscript: the check compared against a double-encoded "Â»" literal.
- `∞ < 5`: angleTermContext lacked regexContext's ∞ exclusion.
- `$(42 with 5)`: applyExprModifiers lacked the with/without topicalizer.
- `else -> $y is copy`: the else-binder never skipped traits.
- `:16<FFFF_FFFF_FFFF_FFFF>` returned −1: radix colonpairs now recompute
  exactly on overflow, like `0x…` literals (this alone un-blocked
  S02-types/version-stress.t: +1,996 assertions).
- `qq:!closure♥…♥` / `qq:!closure«…»`: three drifted quote-adverb ladders
  (the «» path ignored feature adverbs entirely) → one `quoteFeatAdverbs`.
- named-param and slurpy `where` constraints boolified instead of
  smartmatching (`multi g(:$x where Int)` was unresolvable) — the third and
  fourth copies of a rule the positional arm already fixed → both mirrored.
- `else -> $x` re-evaluated the last condition (side effects ran twice).
- `"1٢".Int` threw: the non-ASCII guard tested two exact bytes, not the range.

## Batch 3 — interpreter correctness + micro-perf

`X=>`/`[=>]` keep non-Str pair keys (mirrored from `Z=>`); `gcd`/`lcm` exact
past long long; `s/x/{…}/` restores the caller's `$_` (erase-not-Any, and on
throw); `pendingRwSlots_` nulled on unwind; exception-JSON escapes C0;
`jsonEncode` big Ints + 17-digit doubles; `allocate(-1)` throws Rakudo's
message instead of "Internal error: basic_string" (exit 0!); sub `sum` and
`first` delegate to their exact/matcher-aware methods (first-family roast
files +~40); `isnt`/`is-deeply` honor desc/directive extraction; `minmax ()`
is `Inf..-Inf` (both duplicated infix sites and the method); `valueEq` compares
Ints exactly (2**53 ≠ 2**53+1); `toInt` lost its ~7 µs exception path
(strtoll, stoll-parity); `--doc` works on a precomp cache hit; `--exe`
fallbacks keep `-I` libPaths; Ffi dlopen no longer leaks rejected candidates;
`_BitScan*64` gated to 64-bit MSVC; `%h.push` accumulation de-quadratified
(the promised Hash gate didn't exist); `emit`'s per-call getenv hoisted;
`dlopenLib`/`envLookup` dedup.

Reversed during verification: the general Supply `.tap` arm "dropping" the
named `:emit` matches Rakudo (a positional `&emit` can't be passed by name) —
the two arms that *accept* `:emit` are the divergent ones. Left as-is.

## Batch 4 — codegen + AST cache (compiler-only; six confirmed interp-vs-exe)

Serializer: `VarExpr::processScoped` and `WhileStmt::params` were never in
the visit list (the one hole the one-visitor design can't self-check) —
added, `kAstSerialVersion` 2→3; corrupt-cache counts now throw the typed
error instead of `length_error` escaping to abort.
Codegen: `f('a' => 1)` stays positional (`quotedKey`); `^...`/`^...^`,
do-loops collecting values, `for @a <-> $x`, and pointy-signature `while` now
fall back (they compiled wrong or died at runtime); compiled methods, lexical
subs and anonymous `sub {}` closures get the ReturnEx boundary (a compiled
`(sub { fail "x" })()` aborted with an uncaught exception); `with`/`without`
use `rtIsDefined` (a Failure counted as defined); bigint literals no longer
truncate in the Range fast path; the `canSignal` sniff sees closures invoked
through builtins; multis with `where`/`:D` constraints fall back instead of
dispatching by declaration order; `--aot` output compiles on Windows and
calls setConsoleUtf8. Gates: run-optbench 4-way identity OK, all repros MATCH.

## Batch 5 — regex engine (each oracle-verified)

Quoted literals and `<word list>` entries are single Lit nodes, so the
fold-aware `:i` matcher sees whole spans (`m:i/'WEISS'/` now matches "Weiß");
`<blank>` is horizontal whitespace (flag "b"), not `\s`; grammar-path `<ws>`
gained the `<!ww>` gate (`rule TOP { foo bar }` no longer matches "foobar");
grammar-path `<ident>` is a real multi-char identifier; `nodeWidth` stopped
understating multibyte-capable classes (`<?after \w>` works after "é");
`matchAt` wires hooks + startPos and named-regex bodies count toward the
hook gates (`my regex r { {…} \d }` runs its block); `litPrefix` rolls back
on failed alternation branches; `\c[NAME]..\c[NAME2]` class ranges work; the
write-only `RxMatch::subs`/`MState::subs` maps are gone (one less map copy
per alternation probe).

## Final gates (all green, measured on build-arm64/rakupp-review)

| gate | pre-review | post-review |
|---|---|---|
| Roast | 194,980 · 589 fully-pass | **197,060 · 593 fully-pass** (+2,080; 14 files up, 2 un-blocked from no-TAP, **zero down-movers**) |
| t/run.raku | 310/310 | 310/310 |
| module battery | 50 PASS · 2 DIFF · 6 ENV · 1 NOTESTS | identical (same two known DIFFs) |
| corpus | 1,592 / 1,812 MATCH | **1,594 / 1,812** (two DIFF→MATCH, zero MATCH→DIFF) |
| perf-guard | OK (loopsum +4.8% standing debt) | OK — fib 740→727 ms, others equal; loopsum debt unchanged |
| run-optbench | OK | OK (4-way output identity) |

The +2,080 Roast jump is dominated by S02-types/version-stress.t (+1,996,
un-blocked by the radix-overflow fix) plus the first-family, gcd/lcm,
S04 `with`, and regex-fix files.

## Deferred (documented, deliberately not attempted pre-2.0)

- **The invokeMethod/callCallableRaw twin**: the method path still lacks
  LEAVE/ENTER phasers, `let` restores, and `--> Type` enforcement, and the
  multi-dispatcher/wrapper/placeholder logic is duplicated per path. A
  dedicated convergence campaign; the code comments already know.
- **Z/X three ways** (applyArith, applyBinOp, evalBinary — plus the
  listCtx-vs-flatten hyper-assign drift) and the match/subst Match-builder
  twins (subst's lacks listCaps/hashNames/child recursion).
- Worker-spawn boilerplate ×6 in Builtins (async core; ~120 lines).
- The three term-position classifiers and three delimiter scanners in the
  lexer (documented deliberate differences; needs its own gated batch).
- Regex: the ~7 copies of the builtin-class name/flag tables; the capture
  record/rollback dance ×5; proto-LTM measure-pass saveState; lookbehind
  seeding in-flight captures (`(.) <?before $0>`).
- Codegen bindParams hardening (typed/where params accept anything compiled;
  blanket unsupported() would kill --exe usefulness — needs real checks);
  BEGIN/INIT/FIRST phaser scheduling in non-mainline blocks; NK::VarDecl
  removal; emitBody()/when-chain dedup.
- dist-id sha1 uses a no-op `"\0"` C-literal separator (collision-adjacent
  only; fixing invalidates installed CURI layouts — do at a major bump with a
  reinstall note).
- BigInt::divmod single-limb fast path (perf; Rat normalization hot path).
- Multi-dispatch per-call allocations (visited set, RedispatchCtx sameArgs
  copies) — the known Value-churn cost center, needs measurement first.
- `parse-base` returning bare `Failure` type objects; `.to-posix` on any
  numeric; negative-index wrap inconsistencies in evalIndex slices; IOSpec
  `extension` on dotted dirs; Win32 `tmpdir`; `sleep-till` stub; isa-ok's
  private drifted ancestry table; Buf sole-Blob-arg rule in the sized `add`.
