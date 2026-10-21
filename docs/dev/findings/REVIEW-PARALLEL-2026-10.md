# Review of the parallel sessions — 2026-10-21

Between 2026-10-14 and 2026-10-20 the interpreter took about a hundred `src/`
commits, many written in worktrees running side by side and merged into main
the same day: the V6 P1 batch, the Callable split, scopes on the slab, `try`
and `do` in place, the float report, the operator filter, the Order path,
element binding, the `tctx()` change, issue #185. The pipeline is green. This
review asked what green does not show: two sessions building the same
mechanism, one session's invariant broken by another's change, a lane one
change missed, and the corners that cost speed in numeric and text code. It is
a review, not a speed round; what was in reach was fixed, and the rest is the
ledger at the end.

Method: read the commits of each parallel cluster against each other, profile
the BinaryHeap comment's shape, and send three read-only lanes over the
2026-10-14/16 cluster (lane B), the numeric paths (lane C) and the text paths
(lane D). Lane C's differential, ~1,100 expressions × 12 evaluation shapes
(var-var, var-literal, typed tree, reduction, loop kernel, sub kernel, `op=`,
…) under the interpreter, the kernels and `RAKUPP_NO_KERNELS=1`, found the
many Int/Num/Rat fast paths of the interpreter and the kernels agreeing on
every plain operand: there the parallel numeric work duplicated code but did
not diverge (the compiled tiers did, on natives and Nil: O6). Every finding
below was reproduced at HEAD against Rakudo 2026.09
(`~/.rakubrew/versions/moar-2026.09`; bare `raku` on this box is Raku++), and
every fix that changes an answer carries a t/regression file that runs clean
under Rakudo and fails on the old binary.

Start: 1ac4abdb (5.4.1-160). Gates at the start: t/run.raku 1709/1709; Roast
1424 files fully passing, 220,845 of 220,846 declared assertions.

## The verdict, in one paragraph

The parallel work did not collide in the merges: no conflict was resolved
wrongly, and the `tctx()` change left no direct use of the old thread-local
behind. It collided in the design. Twice two sessions built the same thing
(two filters over the same operator hash on one afternoon; two copies of the
Num lane two weeks apart). Twice a session changed an invariant and missed one
of its users: closures sharing their code also shared an integer kernel bound
to the first closure's scope, and "every routine has its own `$_`" skipped the
plain-sub lane that takes over from a sub's second call. Once a change of
units (`:c` counting characters) left two callers speaking the old one. The
BinaryHeap gap was mostly not any of those, but a third kind of alias the
element-binding round did not reach.

## Fixed

| # | What | Where | Kind | Evidence |
|---|---|---|---|---|
| 1 | `@a.BIND-POS($i, $var)` / `.BIND-KEY` stored an env-slot alias: the variable's frame kept alive, the name found again through a FETCH call on every read. It now binds the variable's cell (`:=`'s), or a readonly parameter's value in a cell of its own | InterpreterRegex.cpp `containerOfExpr` | perf, divergence | BinaryHeap-shaped heap, 2^16 push+pop: 47.7G → 36.5G instructions, peak RSS 200 → 100 MB (the table below). After `$v := $w` the element read `$w`; Rakudo, and now we, keep `$v`'s old container |
| 2 | Two filters for "has some scope bound `&infix:<op>`": `g_lexShadowMask` (e9841fec) confirms its hits against the spellings armed; `g_boundInfixMask` (b35718ba, the same afternoon) did not, so `tops` armed `>`'s bit again. One `ArmedSpellings` list now serves both | Interpreter.cpp, Interpreter.h | duplicate | the two bit filters share `lexShadowSlot`; t/regression/lex-shadow-filter-exact.raku and order-enum-term-cache.raku green |
| 3 | A sub with a signature wrote the caller's `$_` from its second call on: ef35899d gave routines their own `$_` in bindRoutineParams, and callPlainSub, the lane a plain sub moves to after its first call, never defined one. Also: an `EVAL` in the body, and `$_` in a parameter's default, read the caller's | InterpreterCore.cpp `callPlainSub`, `bindRoutineParams`; Interpreter.cpp `mayUseTopic` | contradiction | `sub p($v) { $_ = $v }; p(5); p(7); say $_` printed 7 (Rakudo: the caller's own); `s///` in such a sub rewrote the caller's topic; 8 new checks in t/regression/routine-has-its-own-topic.raku |
| 4 | Closures of one body share an integer kernel (6d67f0cd moved it into CodeInfo), but the kernel checked its callees in the compiling closure's scope, and Value.h still said a copy decides afresh. A second closure whose `my sub g` was wrapped ran the first one's `g`. The root's calls now resolve from the entering closure's scope, as a loop kernel's do — which also stops the check reaching into a freed closure's scope | IntKernel.cpp `callsBound`, `tryIntKernel` | broken invariant | `$bg.wrap(-> $n {1000}); $b(5)`: 12, Rakudo 2000; t/regression/kernel-closure-callees.raku. Closures over a shared top-level sub keep the kernel (same instructions as before) |
| 5 | `numFastArith` (10-01) and `rtNumPair`/`rtNumArith` (48223469) were the same double arm; binaryFastHandler ran one and applyArith the other | InterpreterCore.cpp | duplicate | −25 lines; the lane-B differential of 2,080 Num×Num/Int cases agrees across paths |
| 6 | `makeCellProxy` built a cell proxy with its own FETCH/STORE closures beside the shared-cell proxy bb3cc0af made cheap | InterpreterCalls.cpp | duplicate | now one line over `makeSharedCellProxy` |
| 7 | `.trans` with a Regex key passed a codepoint count to `:c`, which since 8edfb4c7 counts characters, and read `.from`/`.to` back as codepoints; a bare `m:c` took `$/`'s byte offset for a character count | MethodCallPart3.cpp, InterpreterCore.cpp | contradiction | `"line1\r\nline2 x".trans(/x/ => "X")` was `…line2Xx`; `$_ = "ééaa"; m/a/; m:c/a/` found nothing; t/regression/char-positions-trans-continue.raku |
| 8 | `graphemeToByte` walked every cluster from the front on each call, so a `m:c($pos)` tokenizer was quadratic with a large constant | InterpreterRegex.cpp (moved from Builtins.cpp, at its size ceiling) | perf | 5,000 ASCII tokens: 29.9G → 0.48G instructions. Non-ASCII text 25.3G → 20.2G: still quadratic, see O7 |
| 9 | `.contains`/`.comb`/`.split` with a regex compiled the pattern on every call, past the per-thread cache (`.split` with several regex delimiters did it per position) | MethodCallPart3.cpp `CallRegex` | perf | `.split(/\s+/)` × 100k: 4.4G → 2.3G instructions. A `<?{…}>` pattern keeps a fresh compile carrying its hooks (checked in the same regression file) |
| 10 | A sigilless read did a hash probe of the global scope before the check that is false for every program hiding nothing | InterpreterCore.cpp | perf | order of two tests |
| 11 | `do { }` walked its body for placeholders on every evaluation | InterpreterCore.cpp | perf | 1,330 instructions per evaluation on an 8-statement body (lane B) |
| 12 | `1 +< $n` / `1 +> $n` with $n = -2**63: the two arms called each other until the stack was gone (SIGBUS) | InterpreterCore.cpp | crash | Rakudo answers 1; so do we (in kernel-closure-callees.raku) |
| 13 | `div`, `mod`, `gcd`, `lcm`, `+&` `+|` `+^` over a Num of 2**63 or more took it through a saturating toInt() or an undefined cast | InterpreterCore.cpp `bigNumAsInt` | silent wrong answer | `1e20 div 7` was 1317624576693539401, `1e20 +& 255` 255, `1e20 gcd 10` 1; t/regression/num-past-int64-integer-ops.raku |
| 14 | The run-time helpers `--exe` and `--jit` share (rtDiv, rtMod, rtModOp, rtDivides) divided -2**63 by -1 in C++, undefined behaviour (SIGFPE on x86-64, read from the code; lane C saw a wrong `div` under `--jit` on arm64, which did not reproduce here); a divisor of -1 now takes applyArith's guarded path, as the kernels' already did | Interpreter.h | UB | the same regression file, in a loop, under the interpreter, `--jit` and `--cnp` |
| 15 | `Order.pick` built the three values by hand; two misplaced comments | MethodCallTail.cpp, Interpreter.h | cleanup | — |

## Gates at the end

Against a clean build of 1ac4abdb (`git archive`, so no peer edit leaks in):

- **t/run.raku**: 1713/1713 (the five regression files above are new or
  extended).
- **Roast**: the same 1424 files fully passing as the baseline, the per-file
  lists identical, 220,845 of 220,846 declared assertions on both. The one
  short file is `S17-procasync/stress.t` timing out at 23 of 24 under load, the
  same in every run of either binary.
- **t/jit/run.raku**: 1129 agreeing. Three files differ under `--jit` and one
  check reports 16 of 21 tier-up cases entering a kernel. Both happen on the
  base tree too: the three print timings or memory figures inside their `ok`
  lines (lookbehind-lexical-token-window, closed-write-handle-registry,
  lex-shadow-filter-exact), so no two runs match byte for byte, `--jit` or
  not. The JIT gate cannot pass with them as they are.
- **perf-guard**, interleaved, two runs each, best of three per kernel: every
  one of the 24 kernels within noise of the base; none slower.
- **Source budget**: OK. graphemeToByte moved from Builtins.cpp (at its
  ceiling) to InterpreterRegex.cpp, beside the `:c`/`:pos` code that calls it.

Headline numbers, the final binary against the base tree:

| | base | after | Rakudo |
|---|---|---|---|
| BinaryHeap-shaped heap, 2^16 push + pop | 47.7G instr, 3.7 s, 200 MB | 36.5G, 2.7 s, 100 MB | 0.79 s, 199 MB |
| `m:c($pos)` tokenizer, 5,000 ASCII tokens | 29.9G, 1.48 s | 0.48G, 0.06 s | 0.26 s |
| the same over non-ASCII text, 3,000 tokens | 25.3G, 1.25 s | 20.2G, 0.94 s | 0.17 s |
| `.split(/\s+/)` on a 9-word line, 100k times | 4.4G, 0.33 s | 2.3G, 0.14 s | 1.29 s |

## Fixed after the review

O2–O4 of the ledger below, the same day, against the same base (cce315ef),
O24 against the O2–O4 commit (1918e400), O26, found fixing O24, against
O24's (dc76aa01), O28, found fixing O26, against O26's (432a3398), O27
against O28's (8174a3c3), O29, found fixing O27, against O27's
(62b8949f), and O30, found fixing O29, against O29's (6ba94541); each
regression file runs clean under Rakudo and fails on its base.

| # | What | Where | Evidence |
|---|---|---|---|
| O2 | Text decoded from a file, `$*IN`, `-n`/`-p`, `$*ARGFILES`, a pipe or Proc::Async is NFC, as Rakudo's decoders and our own `Buf.decode` make it; utf8-c8 keeps its bytes. One helper, `nfcDecodeInPlace`: a vectorized pass for the highest byte answers ASCII and Latin text (nothing below 0xCC composes), and the rest takes the quick check, where a BMP bitmap now answers a codepoint the NFC table does not hold instead of a binary search (every non-ASCII `~` join asks the same check) | NfcJoin.cpp, Unicode.cpp; decodeTextEnc and the stdin and pipe readers (MethodCallPart3.cpp, BuiltinsRegister.cpp); Proc::Async (Builtins.cpp, MethodCallPart2.cpp) | t/regression/decoded-text-is-nfc.raku, 31 checks failing on the base. 35 MB of ASCII through `.IO.lines`, `.slurp`, `$fh.lines`, `$*IN.lines` and `-n`: instructions within noise of the base (−1.4% to +0.1%). 28 MB of Cyrillic and CJK: `.IO.lines` 2.19G → 2.56G instructions, `.slurp` 7.93G → 8.31G, `$*IN.lines` +1.2% (the quick check without the bitmap had made `.IO.lines` 3.90G) |
| O3 | `.split('')` splits graphemes; `<space>` and `<cntrl>` (every built-in rule's ASCII lane) take a CR LF whole, as `\s` does; sprintf's `%s` width and precision count graphemes; `Match.raku` prints character offsets; `.indices` and `.contains(/rx/)` take a character position, found in the folded text under `:i`/`:m`. `charToByte` had no caller left and is gone (O21) | MethodCallPart3.cpp, Regex.cpp, Builtins.cpp, MethodCallTail.cpp | t/regression/grapheme-units-split-rules-sprintf.raku, 36 checks failing on the base |
| O4 | `uc`, `lc`, `tc`, `chars` and `ord` as subs: a plain Str or Int keeps the short path and anything else is the method, so a Junction autothreads, an object answers with its own, and `chars($buf)` refuses as `.chars` does. `--exe -O`'s fastB table calls the same functions | BuiltinsSupply.cpp, BuiltinsRegister2.cpp | t/regression/case-subs-dispatch-as-methods.raku, 13 checks failing on the base; the same file under `--exe -O`, `--jit` and `--cnp` |
| O24 | Standard input is read a block at a time. std::cin keeps no buffer of its own (it is synced with C stdio, and libc++ ignores `sync_with_stdio(false)`), so every `std::getline(std::cin, …)` and `ss << std::cin.rdbuf()` was a virtual call down to `getc` per character — over a thousand instructions a byte; a bare C++ getline loop over the file was 37.3G of the 40G. `stdinGetLine` and `stdinReadAll` read the same `stdin` FILE with getline(3) and fread, so `.read`, `.getc` and stdinReadAhead still see one stream, a line still arrives while the writer holds the pipe open, and std::cin's eof/fail bits come out as std::getline leaves them. std::getline's sentry flushed `$*OUT` before every line; the flush now comes before a read that may wait (stdio holds no whole line), so a raised `out-buffer` still shows its answer before blocking. An embedder's fed text (`rk_set_input`) is std::cin's own string buffer and keeps the old reader | MethodCallPart3.cpp (`$*IN.get`, `.lines` streamed and eager, `.slurp`, `nl-in`), BuiltinsRegister.cpp (`lines()` under `-n`/`-p`, `prompt`, `words()`), InterpreterBinding.cpp (`$*ARGFILES` `-`) | t/regression/stdin-reads-a-block-at-a-time.raku: 15 checks, the base fails its speed check (6× `.IO.lines`; now 0.6×). Instructions, 32 MB / 400k ASCII lines, base → now: `$*IN.lines` 40.0G → 2.90G, `lines()` 40.0G → 2.90G, `-n` 44.1G → 7.0G, a `$*IN.get` loop 45.5G → 8.5G, `$*IN.slurp` 57.4G → 0.19G; `.IO.lines` 4.6G. 27 MB / 300k lines of Latin, Greek, Cyrillic and CJK: `$*IN.lines` 43.4G → 11.9G, `-n` 46.5G → 15.0G, `.get` loop 47.6G → 16.1G, `.slurp` 56.4G → 7.9G; `.IO.lines` 13.4G. Rakudo reads the ASCII file from stdin in 3.5G. Like for like, stdin is now cheaper than a file: `-n` against `for $f.IO.lines -> $_ is copy { our $n += .chars }` 7.0G vs 8.2G, `$*IN.get` against `$fh.get` 8.4G vs 9.3G; what `-n` still pays over `lines()` is O26 and `is copy` (~1.9k a line) |
| O26 | A plain `our $x` (no type, default, trait, shape or export) names the package's one container: a cell under the published name in GLOBAL, with the declaring scope's `$x` bound to it, as Rakudo binds an `our` lexical to its stash entry, so re-running the declaration only binds the name again. Each run used to make the variable a fresh closure Proxy onto the scope that ran it and publish that view again (`noteSymbolMutation`, which also flushed the multi-dispatch cache), and every later read was a FETCH call. The views also lost the variable: in a file, `for ^3 { our $n += 1 }` and a bare `our $n; $n++` counted to 1, a value set before the loop or through `$GLOBAL::` was dropped, `$P::q` missed a package loop's writes, a `.map` block and routines made in a loop each saw their own, `temp` restored to Any, and `$x := 5` rebound the package's variable where Rakudo rebinds the name only. The assignment publish (`evalAssign`) and the module-load republish leave a cell-bound name alone | InterpreterCalls.cpp (`ourCell`, `ourBoundToCell`), InterpreterCore.cpp (the `our` declare arms of `evalVarExpr` and `lvalue`, the `evalAssign` publish), InterpreterModules.cpp (republish), Interpreter.h | t/regression/our-is-one-container.raku: 31 checks, 20 failing on the base. Instructions, dc76aa01 → now: `for ^400_000 { our $n += 1 }` 3.35G → 1.69G, `{ our $n; $n += 1 }` 4.01G → 1.86G, a bare `{ our $n }` 1.70G → 0.98G (a bare `{ my $n }` is 0.99G), `{ our $n = 1 }` 3.47G → 2.07G, `sub f { our $n += 1 }` × 400k 4.92G → 2.95G; `-ne 'our $n += .chars; END say $n'` over 400k lines 7.09G → 5.54G. The rest is O27. Roast list and totals identical; perf-guard −0.5%…+0.2% in instructions |
| O28 | An `our`'s package is the one around it LEXICALLY: a package body's scope records its prefix (`EnvExtras::pkgPrefix`, set where class, package/module and generic-role bodies are made), and `ourPkgPrefix` finds the nearest one the routine closed over, so a method's or a package sub's `our $k` is its package's `$k` wherever the call comes from. A typed, defaulted or trait-carrying `our` runs the declaration's own path, which keeps the type, default and traits in the declaring scope as for `my`, and then binds the name to the package's cell (ourCell with the initial value) instead of defining a fresh slot; a name that cannot share a cell keeps the old view (`ourViewFallback`). And an `our` variable anywhere a role is the nearest type, its methods included, is X::Declaration::OurScopeInRole at parse time, as the role body's own statements already were | InterpreterCalls.cpp (`ourShareable`, `ourPlain`, `ourPkgPrefix`, `ourViewFallback`), InterpreterCore.cpp (the declaration exits of `evalVarExpr` and `lvalue`), InterpreterModules.cpp and Interpreter.cpp (the recorded prefixes), Parser.cpp (the role check), Interpreter.h | t/regression/our-belongs-to-its-package.raku: 22 checks, 14 failing on the base — `class C { our $k = 5; method k { our $k } }` (Any) → 5, `our sub f { our $x }` in a package, a class counting instances in `new`, a nested class, `for ^3 { our Str $t ~= "x" }` `x` → `xxx`, `our $d is default(5)` re-run 2 → 7, a re-run `our Int $x` accepting a Str. Instructions: `for ^400_000 { our Int $n += 1 }` 4.79G → 2.75G; a method's `our $n += 1` × 400k 5.18G → 5.39G, now building `$C::n`'s name where it read GLOBAL's `$n`; the plain loops of O26 unchanged. Roast list and totals identical; perf-guard −0.6%…+0.2% in instructions |
| O27 | `=`, `+=`, `-=`, `*=` and `~=` on a plain `$` name with no pad slot take the simple-assign lane when the name is bound to a shared cell — an `our`, its declaration included (the `-n` idiom's `our $n += .chars`), or a `:=` alias — and the scope that owns it records nothing about it (no type, default, constraint, coercion, constant, bound value or rw link): `cellLaneSlot` gives the proof a pad slot's layout gives, and the lane's own checks on the value (cold block, native width, readonly, Proxy, an Object operand, a user infix) are unchanged. Classes 11–15 of the lane verdict; a name that turns out not to be cell-bound takes the full path as before. An `our` never became a pad slot instead: the IntKernel's declarations and the module republish (which reads the name map) would both have to learn about it | InterpreterCore.cpp (evalAssign's lane), InterpreterCalls.cpp (`cellLaneSlot`), Interpreter.h | t/regression/assign-lane-cell-bound-names.raku: 35 checks of what the long path does that the lane must still do, and a kernels-off speed check (our `+=` within 1.8× of my) the base fails at 3.1×. Instructions, kernels off, 8174a3c3 → now: `our $n = 0; for ^400_000 { $n += 1 }` 1.15G → 0.52G (`my` 0.42G), the same through `my $a := $b` 1.13G → 0.50G; kernels on: `for ^400_000 { our $n += 1 }` 1.69G → 0.93G, `sub f { our $n += 1 }` × 400k 2.94G → 2.18G; `-ne 'our $n += .chars; END say $n'` over 400k lines 5.53G → 4.74G (the same loop over a `my` 3.86G). Roast list and totals identical; perf-guard −0.1%…+0.7% in instructions, within one binary's run-to-run spread |
| O29 | Every `our` a unit declares gets its package's cell under the published name BEFORE any of the unit runs — the main program, a loaded module and an EVAL'd string alike — as Rakudo installs the stash entry at compile time (so `GLOBAL::<$x>:exists` is now True before a nested `our $x` has run, as in Rakudo). ShareSweep, which already walks every node of a unit before it runs, collects the declarations with the package prefix each body runs under (`ours`, `pkg`), and `preinstallOurCells` makes the cells; the FIRST declaration of a name decides, so one whose value only its own run can make (`is default`, a container trait, a shape, a parameterized type, a coercion) leaves the name to that run. A unit package's file scope now records its prefix as package bodies do, so the install and `ourPkgPrefix` agree in a `unit module`. The parser flags a unit with an `our` in it (`Program::mayHaveOur`), and any other skips the walk | Interpreter.cpp (ShareSweep, `preinstallOurCells`), InterpreterModules.cpp (the three call sites; a `unit package`'s prefix), InterpreterCalls.cpp (`ourPkgPrefix`), Parser.cpp/.h and Ast.h (`mayHaveOur`), Interpreter.h | t/regression/our-on-worker-threads.raku: five crashing programs run six times each — the base dies in 50–100% of their runs — plus the installation checks and a unit module's `our`. The nine threaded programs of the investigation: 0 crashes in 50 runs each, against 2–50 of 50 on 62b8949f. Instructions: `say 1`, `use Test` and a Roast file unchanged; 2000 EVALs of a string with an `our` +4.5% (the walk); perf-guard −0.04%…+0.3% (`loadtime`, a big unit with no `our`, +0.16%). Roast list and totals identical to 62b8949f's (S17-procasync/stress.t times out at 23/24 or passes with the box's load, on both binaries; two single-assertion flakes in two of five runs, S07-hyperrace/basics.t and S02-types/sethash.t, did not recur in 31 reruns each) |
| O30 | Loading a module no longer publishes the variables of its top-level scope into GLOBAL under their bare names unless the module exports them: its `my`s are its own, and its `our`s go by their package's name (`$UM::v`) or, in a module file with no package of its own, already live in GLOBAL as the package's cell. Before, after `use UM` (`unit module UM; our $v = 1; sub bump is export { our $v; $v++ }`), the program's own `our $v` was UM's — 1, then 2 after `bump()` (Rakudo: Any both times) — and `::('$secret')` found a module's private `my`. The exported-variable names are collected beside the exported subs' (`exportedVars`), and `$_`, `$/` and twigil names are published as before | InterpreterModules.cpp (loadModuleImpl's publish) | t/regression/module-variables-stay-the-modules.raku: 3 checks over two modules, failing on 6ba94541 and on 1918e400. Roast list and totals identical to 6ba94541's; perf-guard −0.7%…+0.4% in instructions; `use Test` startup unchanged |

## Open: the ledger

Ranked by what a user sees: a silent wrong answer, then a crash, a divergence,
then speed. Each names the lane that found it and reproduced it at HEAD
against Rakudo 2026.09; "this review" items were found and checked by the
coordinator.

### Silent wrong answers

- **O1. `×` and `*` are one operator to the evaluator** (this review). The
  lexer rewrites `×` `÷` `−` `≤` `≥` `≠` to their ASCII twins, so
  `multi sub infix:<×>(V, V)` is never consulted (`V.new × V.new` answers the
  built-in product; `$v ×= …` answers 0), a lexical `my &infix:<×>` captures
  every `*` in its scope (`6 * 7` calls it, and `$a * $b` inside it recurses
  until the stack is gone), and `×=` ignores it. Full entry, with the plan:
  [TODO-FOLLOW-UPS.md](../plans/TODO-FOLLOW-UPS.md), "Operators and types".
- **O5. `.trans` tie-breaking** (lane D): Rakudo lets the LATER of two string
  keys of one length win (`'a..z'=>'A..Z','l'=>'X'` is HEXXO), the first inside
  one pair, and the earlier of a string and a regex; ours is first-wins
  throughout.
- **O6. Numeric divergences** (lanes B and C):
  - the compiled tiers store Nil into an untyped `$`: `my $r = 5; for ^3 { $r
    = NaN <=> 1e0 }; say $r.^name` is Nil under `--cnp` and `--jit` (Any
    interpreted and in Rakudo); the cnp scalar write-back (Cnp.cpp ~1731)
    lacks the Nil→Any reset its element store has;
  - natives: `--cnp` skips nativeMixedSignNe (`uint8 7 != int -2` is True);
    `uint` 2**63-1 + 1 wraps under `--cnp` but not interpreted; native `+<`
    is missing from nativeIntArith, so `my int $a = 2**63-1; $a +< 1` does
    not wrap while `$a +<= 1` does;
  - `min`/`max` keep the right operand on a tie (InterpreterCore.cpp ~19510):
    `(5 max <5>).^name` is IntStr, `True max 1` Int, `-0e0 min 0e0` 0e0, and
    the NaN rules differ; Rakudo's Int and Num candidates, and our own
    `.min`/`.max` fold, keep the left;
  - `(2**54).Num` prints `18014398509481984`, Rakudo `1.8014398509481984e+16`:
    `%.17g` turns fixed in [1e16, 1e17) (Value.cpp ~817);
  - `1 +< 100_000_000` runs past a minute (Rakudo 0.17 s) and `1 +< 3037000500`
    never returns where Rakudo dies at once: `lb * BigInt(2).pow(sh)` instead
    of a limb shift;
  - `0 ** -1` is a Failure (Rakudo `<1/0>`); `-0e0 ** 3` is `-0` (Rakudo 0);
    `my num @z = -0e0, -0e0; @z.sum` is 0 (Rakudo -0); a Duration becomes a
    Rat through `[+]` and `.sum`; `(do { $x }).VAR` is not the Scalar.

- **O23. Left over from O2–O4** (found while fixing them; each checked
  against Rakudo 2026.09 unless it says otherwise):
  - a file handle's `.getc` and `.readchars` hand out codepoints: over
    `"q\x[301]xy"`, `.getc` is `"q"` and `.readchars(2)` starts with the
    stray mark (Rakudo `"q\x[301]"`, then `"xy"`);
  - socket text is not NFC: IO::Socket::Async's Supply emits the bytes it
    holds as they are for UTF-8 (BuiltinsSupply.cpp ~2554, read from the code,
    not probed), and so, likely, does IO::Socket::INET's text `.recv`/`.get`;
  - `Buf.new(1).uc` answers `"\x[1]"` where Rakudo dies, so `uc($buf)` now
    does too (the sub is the method);
  - `"abc".indices("", 5)` is `(3)` (Rakudo `()`), `"abc".contains(/$/, 3)`
    True (Rakudo False), `"abc".split('', 1)` a Seq (Rakudo a List).

- **O31. GLOBAL's stash answers that Rakudo does not** (found fixing O30;
  the same on 1918e400): `GLOBAL::<@x>:exists` and `GLOBAL::<%x>:exists` are
  True for any name, declared or not (`$x` is right); and a module FILE with
  no package of its own shares its `our $config` with the program — after
  `use NoPkg` the program reads `$GLOBAL::config` as the module set it and
  writes reach the module — where Rakudo installs the symbol (a program's
  own `our $config` is a redeclaration) but reads (Any) through it and
  leaves the module's variable alone. `GLOBAL::<&gsub>:exists` for that
  module's `our sub gsub` is True here, False in Rakudo.

### Speed

- **O25. Fix 3's own `$_` is defined for any sub with an nqp op in it**:
  mayUseTopic's `default: return true` covers NqpOp, so since cce315ef a
  plain sub whose body holds `nqp::…` defines `$_` (Env::define, a hash
  emplace) on every call: `sub f($t) { nqp::chars($t) }`, 200k calls, 996M →
  1,100M instructions (+520 a call). perf-guard's `strpass` and `subcall`,
  both such subs, are +1.3% and +0.9% in instructions against 1ac4abdb,
  which the timings above could not see. Look into the op's arguments, as
  the Call arm does (the default is Interpreter.cpp ~1801).
- **O7. `m:c`/`:pos` over non-ASCII text is still quadratic** (fix 8 made the
  ASCII case linear in practice). graphemeToByte gets a `std::string`; the
  string's own body has the grapheme index (`StrBody::gIndex`). Pass the CowStr
  through. t/scaling's `str-match-continue` TODO: ×50 for ×8 (it timed out
  before).
- **O8. Appending throws the string's cached facts away** (lane D):
  `CowStr::appendText` invalidates `allAscii`, `crFree`, `gIndex` and
  `cpIndex`, so `$s ~= "x"; $s.substr(*-1)` rescans the whole string each turn
  (×3.2 per doubling), and with non-ASCII text the grapheme table is rebuilt
  (20k iterations 5.4 s, Rakudo 0.18); `nfcPrependCow` does not carry the
  count `nfcAppendCow` does. Keep the flags when the appended bytes are ASCII
  and CR-free.
- **O9. The loop kernel copies a growing string every iteration** (lane D):
  `for ^200000 { my $k = $s; $s ~= "abcde" }` 3.09 s with kernels, 0.07 s
  without (ssetFn, IntKernel.cpp ~1948). perf-guard's `sharedappend` passes
  only because the kernel declines that shape for another reason.
- **O10. Bodies run in place by `try`/`do`/`quietly` look outer lexicals up
  by name** (lane B): resolvePads never enters those operands, so `$t += 1` on
  an outer `$t` inside `do { }` is 2,667 instructions against 522 outside it.
  Give such a body an inline-block layout in its owner's pass.
- **O11. `my $c = @a[$i]` leaves the declaration lane** (this review): about
  3,500 instructions more than `my $c = 5` per iteration (varWhereCheck by
  name, Env::define, the sigilless-bind probe in evalAssignInner).
- **O12. Attribute reads** (this review): `$!x` costs ~500 instructions more
  than `$x`, `@!a[1]` ~1,230 more than `@a[1]`; the attribute arm of
  evalVarExpr tests `repr` with three outlined string compares on every read.
- **O13. Text hot paths against Perl** (lane D, 200k-line inputs; Raku++ ÷
  Perl): `tr` 94×, `write` 19×, `regex-cap` 14×, `sprintf` 13.6×, `uni-uc`
  10.7×, `lines` 10×. In reach: `.trans` re-expanding `a..z` per call and
  comparing every key at every byte (a 256-entry table when every key is one
  ASCII character); `%f` searching for the shortest digits with up to 18
  `snprintf`/`strtod` pairs (one `std::to_chars`); `.chars` on non-ASCII
  allocating three vectors to count (the single-pass counter exists);
  `.comb()`/`.flip` with no ASCII lane; `.` in a regex always walking a whole
  cluster (TEXT-PLAN T3.1); `<:Lu>` resolving its category by string per
  character. `lines` and `write` are TEXT-PLAN T1.
- **O14. The `* cmp * == More` curry** calls its inner curry as a full call
  (TODO-FOLLOW-UPS, "Performance"); BinaryHeap's comparator is that shape.
- **O15. Num code written with decimal literals falls off every fast tier**
  (lane C). `$x = $x * 1.0000001 + 0.000000001` is ~3,400 instructions an
  iteration with kernels on, against 58 for the same line with `e0`
  literals: a decimal literal is a Rat, and the kernel (IntKernel.cpp ~829),
  the typed tree and the fast shape (`plainScalarLit`) all refuse a Rat beside
  a Num. Read such a literal as its cached `.Num` there, which is what the
  general lane computes. The largest numeric win the review saw.
- **O16. Int `div mod %% ** min max gcd` miss applyArith's hot path** (lane C):
  `+` 540 instructions, `%` 603, `mod` 1,650, `div` 2,203, `**` 3,247. Arms
  with the -1 and zero guards, and `pow_ovf` for `**`.
- **O17. `1/3` skips the small-Rat lane** (lane C): its gate is `anyRat &&
  …`, so Int / Int goes through BigInt divmod, gcd and a malloc (`$r += 1/3`
  7,838 instructions, `$r += $q` 3,790); and `my $q = 1/3` leaves the
  declaration lane for good. Open the gate to `/`, then fold IntLit/IntLit
  into a Rat literal.
- **O18. A `for` loop's variable is never in the pad** (lane C;
  Interpreter.cpp ~5509): every `$i` read is an Env::find, ~13% of a loop's
  samples. And an empty `for ^N -> $i {}` costs 443 instructions an
  iteration: a full Value move for the topic, `isDefined` on every exit for
  `let`, a `shared_ptr<Env>` by value.
- **O19. The fast shape is evaluated twice on a decline** (lane C):
  evalBinary's fast-shape block is a copy of binaryFastHandler, which on a
  decline calls evalBinary, which tests the shadow filter and looks both
  operands up again before evaluating them a third time.

### Structure

- **O20. Four near-identical AST whitelist walks** (lane B):
  `exprMayReadAtArgs`, `exprMayUseState<InPlace>`, `mayUseTopic`,
  `collectPHStmt`, each with its own default for a node it does not know
  (fix 3 was one of them answering "no topic" where another said "yes"). One
  template with several questions, about −150 lines.
- **O21. Two character-offset conversions and two grapheme tables for the
  same strings** (lane D): `graphemeToByte`, `prefixGraphemes`,
  `subjectGraphemeOffset`'s per-thread table and `StrBody::gIndex`; and four
  "is it ASCII" checks the regex engine reads none of. (The third conversion,
  `charToByte`, counted codepoints; it went with O3.)
- **O22. Minor duplicates** (lanes B and C): `callable_for_phaser` repeats
  `stateFrameOf`'s once-only setup (safe while every phaser block forces a
  state frame); `kCoreEnums` (Builtins.cpp) repeats `coreEnumMembers`' list;
  the boxed and packed `.sum` folds are two loops with one rule; four 128-bit
  Rat reducers (the small-Rat lane, typedRatArith, the kernel's xratio, cnp's
  rk_gcd), three calling `__builtin_ctzll` directly instead of IntOps.h's
  (inside RAKUPP_HAS_INT128, so MSVC still builds); rtPow re-implements
  `pow_ovf`.
