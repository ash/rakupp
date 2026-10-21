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

### Numeric (claude/gallant-ellis-ffe145)

O15–O17, O33 and O34 of the ledger below, the same day, against the same base (cce315ef).
Each lane answers what the general path answers: the three regression files
run clean under Rakudo and on the base, and across ~2,300 Int, Rat and Num
rows (generated by Rakudo) and two probe files the new binary agrees with the
base byte for byte under the default tiers, `RAKUPP_NO_KERNELS=1`, `--cnp`
and `--jit`. Instructions retired per iteration of a 10^6 loop, median of five
interleaved runs, base → after; "no kernels" is `RAKUPP_NO_KERNELS=1`.

| # | What | Where | Evidence |
|---|---|---|---|
| O15 | A Rat beside a Num is read as its .Num, which is what the general path computes, in each fast lane: the loop kernel converts a Rat literal beside a Num once, when it compiles (`ratLitBesideNum`); the typed subtree reads a Rat leaf beside a Num as one (`typedNumRatArith`, and `typedCompare` for the six comparisons); `$x * 1.5`, two leaves the fast shape declines, is now a typed root; and applyArith's Num arm takes a plain Rat (`numRatPair`), which is the lane `--cnp` and `--exe` reach. An Int or a Rat on the other side still makes a Rat, and every operand's type is read on every evaluation | IntKernel.cpp `asNum`; InterpreterCore.cpp `typedArithSlow`, `typedCompare`, `typedTreeRoot`, applyArith | `$x = $x * 1.0000001 + 0.000000001`: 3,418 → 57 (the same line with `e0` literals: 58), 3,418 → 1,202 with no kernels (`e0`: 1,124), 6,410 → 57 under `--cnp`, 3,451 → 58 under `--jit=sync`. `$x = $x * 1.5` 2,209 → 42 (1,065 with no kernels); `while $x < 1000.5 { $x = $x + 0.001; $n++ }` 4,226 → 112; `$t = $i * 0.5` from an Int 4,687 → 2,025 with no kernels. t/regression/num-beside-rat-literal.raku |
| O16 | `** %% div mod gcd lcm min max` over two machine Ints are answered on applyArith's hot path beside `+` and `%`: `div` floors and `mod` takes the divisor's sign (`rtIntWordOp`), a zero divisor still fails or throws, -2**63 div -1 and -2**63 gcd 0 become big Ints, a power past int64 (`pow_ovf`) a big Int and a negative power a Rat. A tagged or enum operand keeps the general path, so `min`/`max` (a tie keeps the right, O6) answer as before. `$x = $a op $b` over plain Ints stores into the slot for these too (`fusedIntWordAssign`, out of line so the `+ - *` lane keeps its frame) | InterpreterCore.cpp applyArith, `fusedIntAssign`; Interpreter.h `rtIntWordOp` | `$s = $i op 7` with no kernels: `div` 2,745 → 1,144, `mod` 2,195 → 1,154, `**` 3,949 → 1,131, `gcd` 2,382 → 1,168, `min`/`max` 2,282/2,299 → 1,118/1,119, `%%` 2,133 → 1,296 (`+` 1,078 and `%` 1,140 unchanged). The loop and sub kernels take `gcd` and `lcm` too, bailing where the answer is a BigInt: with the kernels `gcd` 2,382 → 81 and `lcm` 2,403 → 89 (`--cnp` `gcd` 1,562 → 516). `$s = $i %% 7` into an Int variable still leaves the kernel, 2,134 → 1,297: an Int that becomes a Bool is the interpreter's (O34). t/regression/int-word-ops-hot-path.raku, 661 rows; kernel-gcd-lcm.raku |
| O17 | The small-Rat lane takes Int / Int (its gate was a Rat operand, so `1/3` went through BigInt divmod and gcd), and applyArith calls it before the general path for `+ - * /` over Rats and Ints (`smallRatLane`, Value.cpp, shared with the exact tower; its arithmetic is IntOps.h `ratOp128`, which typedRatArith uses too). A typed root of two literals (`1/3`) builds its Value once and shares it while no operator shadows it (`Binary::typedTree` 2), so `my $q = 1/3` stays on the declaration lane. Folding `1/3` at parse time was not done: a later `sub infix:</>` in the same scope, `.DEPARSE` and the sink warnings would all see the fold | InterpreterCore.cpp applyArith, the exact tower, `typedTreeValue`; Value.cpp; IntOps.h | With no kernels: `$r += 1/3` 7,836 → 3,562, `my $q = 1/3` in a loop 9,599 → 2,549, `$r += $q` 4,232 → 3,607. The kernels already ran these (759, 302, 577: unchanged). t/regression/int-over-int-small-rat.raku, 1,600 rows |
| O33 | The small-Rat lane's spill to Num (a denominator past 2**64) is the nearest double, ties to even, as Rat.Num and the BigInt arms make it: binary long division of the reduced __int128 parts to 55 bits, the remainder deciding the rounding. `(double)n / (double)d` rounded three times and put 1,436 of 6,174 probed spills an ulp off. Checked against Python's `float(Fraction(n, d))` on a million pairs, ~33,000 of them exact ties | Value.cpp `smallRatSpill` | `1/4611686018427387903 + 3/999999999999999989` 3.2168404344971007e-18 → 3.216840434497101e-18 (Rakudo's). A spill 7,680 → 8,327 instructions (through BigInt::ratioToDouble it was 22,258); the in-range lane unchanged. t/regression/small-rat-spill-rounds-once.raku, failing 4 checks on the base |
| O34 | A loop kernel's variable may hold a Bool: a slot of its own, 0 or 1, checked as a plain Bool at entry and written back as one; `True`, `False` and `!` are values in a loop kernel (a sub kernel's values stay Ints). A Bool is the Int it numifies to for arithmetic, the numeric comparisons and `+= -= *= /=`, "True"/"False" as text (`~`, `~=`, `.Str`, interpolation, `eq`), and the answer itself under `when`. `min`/`max` (which hand an operand back), `++`, `given` of a Bool, and a variable whose type would change (an Int that becomes a Bool needs a slot tagged at every read) keep the generic path | IntKernel.cpp (`plainBoolValue`, asStr, the operator gates, bindOuter) | With the kernels: the flag loop `$found = True if $i == 500_000` 1,010 → 47, `my $ok = $i %% 3; $n++ if $ok` 6,351 → 145, `$n = $n + ($i %% 3)` 2,494 → 81, `$b = !$b` 1,346 → 47, `$h = $i > 999; $n++ if $h` 2,656 → 126. t/regression/kernel-bool-variables.raku; 36 probe shapes print what Rakudo and the base print, under the default tiers, no kernels, `--cnp` and `--jit` |

### Text (claude/hopeful-lamport-607e9e)

O2–O4 of the ledger below, the same day, against the same base (cce315ef). On top of them (1918e400), two branches, each with the items its fixes turned up:
the getc/socket branch (claude/heuristic-edison-7269cb) O23 and then O35;
the stdin branch (claude/recursing-jackson-2f5546) O24, then O26, O28, O27, O29, O30, O31 and O32, each against the one before.
Each regression file runs clean under Rakudo and fails on its base.

| # | What | Where | Evidence |
|---|---|---|---|
| O2 | Text decoded from a file, `$*IN`, `-n`/`-p`, `$*ARGFILES`, a pipe or Proc::Async is NFC, as Rakudo's decoders and our own `Buf.decode` make it; utf8-c8 keeps its bytes. One helper, `nfcDecodeInPlace`: a vectorized pass for the highest byte answers ASCII and Latin text (nothing below 0xCC composes), and the rest takes the quick check, where a BMP bitmap now answers a codepoint the NFC table does not hold instead of a binary search (every non-ASCII `~` join asks the same check) | NfcJoin.cpp, Unicode.cpp; decodeTextEnc and the stdin and pipe readers (MethodCallPart3.cpp, BuiltinsRegister.cpp); Proc::Async (Builtins.cpp, MethodCallPart2.cpp) | t/regression/decoded-text-is-nfc.raku, 31 checks failing on the base. 35 MB of ASCII through `.IO.lines`, `.slurp`, `$fh.lines`, `$*IN.lines` and `-n`: instructions within noise of the base (−1.4% to +0.1%). 28 MB of Cyrillic and CJK: `.IO.lines` 2.19G → 2.56G instructions, `.slurp` 7.93G → 8.31G, `$*IN.lines` +1.2% (the quick check without the bitmap had made `.IO.lines` 3.90G) |
| O3 | `.split('')` splits graphemes; `<space>` and `<cntrl>` (every built-in rule's ASCII lane) take a CR LF whole, as `\s` does; sprintf's `%s` width and precision count graphemes; `Match.raku` prints character offsets; `.indices` and `.contains(/rx/)` take a character position, found in the folded text under `:i`/`:m`. `charToByte` had no caller left and is gone (O21) | MethodCallPart3.cpp, Regex.cpp, Builtins.cpp, MethodCallTail.cpp | t/regression/grapheme-units-split-rules-sprintf.raku, 36 checks failing on the base |
| O4 | `uc`, `lc`, `tc`, `chars` and `ord` as subs: a plain Str or Int keeps the short path and anything else is the method, so a Junction autothreads, an object answers with its own, and `chars($buf)` refuses as `.chars` does. `--exe -O`'s fastB table calls the same functions | BuiltinsSupply.cpp, BuiltinsRegister2.cpp | t/regression/case-subs-dispatch-as-methods.raku, 13 checks failing on the base; the same file under `--exe -O`, `--jit` and `--cnp` |
| O23 | A handle's `.getc` and `.readchars` hand out graphemes, and a CR LF reads as `"\n"` (utf8-c8 keeps its codepoints apart, as Rakudo's does): the cursor holds a cluster to an element, a CR LF as it came, so an `.encoding` switched midway still counts the bytes behind it. `$*IN.getc` waits for the character after the one it hands out, as Rakudo's does (it may be a mark that belongs to it), and puts back what it read ahead for the next reader; after a control character it does not wait, so a line typed at a terminal is answered at its newline. Socket text is NFC: IO::Socket::Async's text chunks (each ends on a cluster boundary, so each normalizes alone) and IO::Socket::INET's `.get`, `.lines` and `.recv`. A Blob refuses the methods Rakudo's Str has and its Blob does not, `Buf.new(1).uc`, `.lc`, `.flip`, `.ord`, `.substr`, `.trim`, `.contains`, `.lines` and the rest (X::Method::NotFound), so `uc($buf)` does too. `.indices("", $pos)` past the end is `()`; `.contains(/rx/, $pos)` at or past the end is False; `.split(Str, $limit)` is a List where there is nothing to split (a limit of 1, an empty string) | MethodCallPart3.cpp (handleReadChars, charCursor, stdinGrapheme; refuseStrOnlyOnBlob, called from methodCall in Builtins.cpp), BuiltinsSupply.cpp, MethodCallPart2.cpp, MethodCallTail.cpp | t/regression/handle-getc-readchars-graphemes.raku, socket-text-is-nfc.raku and blob-not-str-and-position-edges.raku: 20, 5 and 42 checks failing on the base; the three under `--jit` and `--cnp` too. perf-guard's 24 kernels by instructions retired within −0.09%..+0.18% of the base, but for strscan's mode (O36) |
| O35 | A binary handle refuses text reads, as Rakudo's does: `.get`, `.getc`, `.readchars`, `.lines`, `.words`, `.comb` and `.split` on `open(:bin)`, or after `.encoding(Nil)` / `.encoding("bin")`, die with X::IO::BinaryMode (`.trying` names the method); `.slurp` and `.read` answer bytes, and `.encoding("utf8")` makes the handle text again. `$*IN` keeps the setting, so `lines()` and `words()` refuse too. `.readchars(-1)` dies. IO::Socket::INET's text `.recv($n)` takes $n whole characters from the buffer `.get` reads into, else after one more read, else all it has (Rakudo's consume-exactly-chars, then consume-all-chars); `.read` and `.recv(:bin)` take what that buffer holds first, then the socket. The one place this parts from Rakudo: there a byte read asking for more than the buffer holds reads the socket past it, so `.get` then `.recv(:bin)` loses or reorders what `.get` read ahead; here it comes first. `.index`, `.rindex`, `.indices`, `.contains` and `.substr-eq` fail with Rakudo's X::OutOfRange: `what` "Position in calling 'index'", `comment` Any, "… Is: -1, should be in 0..3" (`.substr-eq`'s held a bare type with no message) | MethodCallPart3.cpp (the FileHandle arm, handleReadChars, index/rindex, substr-eq), BuiltinsRegister.cpp (unreadableStdin), BuiltinsSupply.cpp (inetSocketRead, out of MethodCallPart2.cpp's Socket arm), MethodCallTail.cpp (outOfRangePos) | t/regression/binary-handle-refuses-text-reads.raku, socket-recv-counts-characters.raku and string-position-out-of-range-failure.raku: 13, 5 and 12 checks failing on the base; the three under `--jit` and `--cnp` too. perf-guard's 24 kernels by instructions within −0.29%..+0.10% of the base (strscan −0.74%: its lower mode, O36) |
| O24 | Standard input is read a block at a time. std::cin keeps no buffer of its own (it is synced with C stdio, and libc++ ignores `sync_with_stdio(false)`), so every `std::getline(std::cin, …)` and `ss << std::cin.rdbuf()` was a virtual call down to `getc` per character — over a thousand instructions a byte; a bare C++ getline loop over the file was 37.3G of the 40G. `stdinGetLine` and `stdinReadAll` read the same `stdin` FILE with getline(3) and fread, so `.read`, `.getc` and stdinReadAhead still see one stream, a line still arrives while the writer holds the pipe open, and std::cin's eof/fail bits come out as std::getline leaves them. std::getline's sentry flushed `$*OUT` before every line; the flush now comes before a read that may wait (stdio holds no whole line), so a raised `out-buffer` still shows its answer before blocking. An embedder's fed text (`rk_set_input`) is std::cin's own string buffer and keeps the old reader | MethodCallPart3.cpp (`$*IN.get`, `.lines` streamed and eager, `.slurp`, `nl-in`), BuiltinsRegister.cpp (`lines()` under `-n`/`-p`, `prompt`, `words()`), InterpreterBinding.cpp (`$*ARGFILES` `-`) | t/regression/stdin-reads-a-block-at-a-time.raku: 15 checks, the base fails its speed check (6× `.IO.lines`; now 0.6×). Instructions, 32 MB / 400k ASCII lines, base → now: `$*IN.lines` 40.0G → 2.90G, `lines()` 40.0G → 2.90G, `-n` 44.1G → 7.0G, a `$*IN.get` loop 45.5G → 8.5G, `$*IN.slurp` 57.4G → 0.19G; `.IO.lines` 4.6G. 27 MB / 300k lines of Latin, Greek, Cyrillic and CJK: `$*IN.lines` 43.4G → 11.9G, `-n` 46.5G → 15.0G, `.get` loop 47.6G → 16.1G, `.slurp` 56.4G → 7.9G; `.IO.lines` 13.4G. Rakudo reads the ASCII file from stdin in 3.5G. Like for like, stdin is now cheaper than a file: `-n` against `for $f.IO.lines -> $_ is copy { our $n += .chars }` 7.0G vs 8.2G, `$*IN.get` against `$fh.get` 8.4G vs 9.3G; what `-n` still pays over `lines()` is O26 and `is copy` (~1.9k a line) |
| O26 | A plain `our $x` (no type, default, trait, shape or export) names the package's one container: a cell under the published name in GLOBAL, with the declaring scope's `$x` bound to it, as Rakudo binds an `our` lexical to its stash entry, so re-running the declaration only binds the name again. Each run used to make the variable a fresh closure Proxy onto the scope that ran it and publish that view again (`noteSymbolMutation`, which also flushed the multi-dispatch cache), and every later read was a FETCH call. The views also lost the variable: in a file, `for ^3 { our $n += 1 }` and a bare `our $n; $n++` counted to 1, a value set before the loop or through `$GLOBAL::` was dropped, `$P::q` missed a package loop's writes, a `.map` block and routines made in a loop each saw their own, `temp` restored to Any, and `$x := 5` rebound the package's variable where Rakudo rebinds the name only. The assignment publish (`evalAssign`) and the module-load republish leave a cell-bound name alone | InterpreterCalls.cpp (`ourCell`, `ourBoundToCell`), InterpreterCore.cpp (the `our` declare arms of `evalVarExpr` and `lvalue`, the `evalAssign` publish), InterpreterModules.cpp (republish), Interpreter.h | t/regression/our-is-one-container.raku: 31 checks, 20 failing on the base. Instructions, dc76aa01 → now: `for ^400_000 { our $n += 1 }` 3.35G → 1.69G, `{ our $n; $n += 1 }` 4.01G → 1.86G, a bare `{ our $n }` 1.70G → 0.98G (a bare `{ my $n }` is 0.99G), `{ our $n = 1 }` 3.47G → 2.07G, `sub f { our $n += 1 }` × 400k 4.92G → 2.95G; `-ne 'our $n += .chars; END say $n'` over 400k lines 7.09G → 5.54G. The rest is O27. Roast list and totals identical; perf-guard −0.5%…+0.2% in instructions |
| O28 | An `our`'s package is the one around it LEXICALLY: a package body's scope records its prefix (`EnvExtras::pkgPrefix`, set where class, package/module and generic-role bodies are made), and `ourPkgPrefix` finds the nearest one the routine closed over, so a method's or a package sub's `our $k` is its package's `$k` wherever the call comes from. A typed, defaulted or trait-carrying `our` runs the declaration's own path, which keeps the type, default and traits in the declaring scope as for `my`, and then binds the name to the package's cell (ourCell with the initial value) instead of defining a fresh slot; a name that cannot share a cell keeps the old view (`ourViewFallback`). And an `our` variable anywhere a role is the nearest type, its methods included, is X::Declaration::OurScopeInRole at parse time, as the role body's own statements already were | InterpreterCalls.cpp (`ourShareable`, `ourPlain`, `ourPkgPrefix`, `ourViewFallback`), InterpreterCore.cpp (the declaration exits of `evalVarExpr` and `lvalue`), InterpreterModules.cpp and Interpreter.cpp (the recorded prefixes), Parser.cpp (the role check), Interpreter.h | t/regression/our-belongs-to-its-package.raku: 22 checks, 14 failing on the base — `class C { our $k = 5; method k { our $k } }` (Any) → 5, `our sub f { our $x }` in a package, a class counting instances in `new`, a nested class, `for ^3 { our Str $t ~= "x" }` `x` → `xxx`, `our $d is default(5)` re-run 2 → 7, a re-run `our Int $x` accepting a Str. Instructions: `for ^400_000 { our Int $n += 1 }` 4.79G → 2.75G; a method's `our $n += 1` × 400k 5.18G → 5.39G, now building `$C::n`'s name where it read GLOBAL's `$n`; the plain loops of O26 unchanged. Roast list and totals identical; perf-guard −0.6%…+0.2% in instructions |
| O27 | `=`, `+=`, `-=`, `*=` and `~=` on a plain `$` name with no pad slot take the simple-assign lane when the name is bound to a shared cell — an `our`, its declaration included (the `-n` idiom's `our $n += .chars`), or a `:=` alias — and the scope that owns it records nothing about it (no type, default, constraint, coercion, constant, bound value or rw link): `cellLaneSlot` gives the proof a pad slot's layout gives, and the lane's own checks on the value (cold block, native width, readonly, Proxy, an Object operand, a user infix) are unchanged. Classes 11–15 of the lane verdict; a name that turns out not to be cell-bound takes the full path as before. An `our` never became a pad slot instead: the IntKernel's declarations and the module republish (which reads the name map) would both have to learn about it | InterpreterCore.cpp (evalAssign's lane), InterpreterCalls.cpp (`cellLaneSlot`), Interpreter.h | t/regression/assign-lane-cell-bound-names.raku: 35 checks of what the long path does that the lane must still do, and a kernels-off speed check (our `+=` within 1.8× of my) the base fails at 3.1×. Instructions, kernels off, 8174a3c3 → now: `our $n = 0; for ^400_000 { $n += 1 }` 1.15G → 0.52G (`my` 0.42G), the same through `my $a := $b` 1.13G → 0.50G; kernels on: `for ^400_000 { our $n += 1 }` 1.69G → 0.93G, `sub f { our $n += 1 }` × 400k 2.94G → 2.18G; `-ne 'our $n += .chars; END say $n'` over 400k lines 5.53G → 4.74G (the same loop over a `my` 3.86G). Roast list and totals identical; perf-guard −0.1%…+0.7% in instructions, within one binary's run-to-run spread |
| O29 | Every `our` a unit declares gets its package's cell under the published name BEFORE any of the unit runs — the main program, a loaded module and an EVAL'd string alike — as Rakudo installs the stash entry at compile time (so `GLOBAL::<$x>:exists` is now True before a nested `our $x` has run, as in Rakudo). ShareSweep, which already walks every node of a unit before it runs, collects the declarations with the package prefix each body runs under (`ours`, `pkg`), and `preinstallOurCells` makes the cells; the FIRST declaration of a name decides, so one whose value only its own run can make (`is default`, a container trait, a shape, a parameterized type, a coercion) leaves the name to that run. A unit package's file scope now records its prefix as package bodies do, so the install and `ourPkgPrefix` agree in a `unit module`. The parser flags a unit with an `our` in it (`Program::mayHaveOur`), and any other skips the walk | Interpreter.cpp (ShareSweep, `preinstallOurCells`), InterpreterModules.cpp (the three call sites; a `unit package`'s prefix), InterpreterCalls.cpp (`ourPkgPrefix`), Parser.cpp/.h and Ast.h (`mayHaveOur`), Interpreter.h | t/regression/our-on-worker-threads.raku: five crashing programs run six times each — the base dies in 50–100% of their runs — plus the installation checks and a unit module's `our`. The nine threaded programs of the investigation: 0 crashes in 50 runs each, against 2–50 of 50 on 62b8949f. Instructions: `say 1`, `use Test` and a Roast file unchanged; 2000 EVALs of a string with an `our` +4.5% (the walk); perf-guard −0.04%…+0.3% (`loadtime`, a big unit with no `our`, +0.16%). Roast list and totals identical to 62b8949f's (S17-procasync/stress.t times out at 23/24 or passes with the box's load, on both binaries; two single-assertion flakes in two of five runs, S07-hyperrace/basics.t and S02-types/sethash.t, did not recur in 31 reruns each) |
| O30 | Loading a module no longer publishes the variables of its top-level scope into GLOBAL under their bare names unless the module exports them: its `my`s are its own, and its `our`s go by their package's name (`$UM::v`) or, in a module file with no package of its own, already live in GLOBAL as the package's cell. Before, after `use UM` (`unit module UM; our $v = 1; sub bump is export { our $v; $v++ }`), the program's own `our $v` was UM's — 1, then 2 after `bump()` (Rakudo: Any both times) — and `::('$secret')` found a module's private `my`. The exported-variable names are collected beside the exported subs' (`exportedVars`), and `$_`, `$/` and twigil names are published as before | InterpreterModules.cpp (loadModuleImpl's publish) | t/regression/module-variables-stay-the-modules.raku: 3 checks over two modules, failing on 6ba94541 and on 1918e400. Roast list and totals identical to 6ba94541's; perf-guard −0.7%…+0.4% in instructions; `use Test` startup unchanged |
| O31 | `GLOBAL::<$x>:exists` (and `GLOBAL::{$k}:exists`) answers from GLOBAL's own table — an `our` variable or routine, a lax variable, a qualified write — instead of evaluating the name, which called an undeclared `@x`/`%x` (an empty container), an undeclared `&x`, the built-in `&say` and a top-level `my @lex` all present. `OUR::<$x>:exists` (the package the code is in, lexically) and `Foo::<$x>:exists` (a named package's table) take the adverb at all: it went to the enclosing call, so `say OUR::<$z>:exists` printed `(Any)exists => True`. Reads keep their own path. The second half of the item, a package-less module's `our` shared with the program, is KEPT as it is, on the evidence: under Rakudo the module disagrees with ITSELF — with `our $config = 3` and `sub self-view is export { $GLOBAL::config }`, `self-view()` is (Any) while the module's `$config` is 3, and after the module writes `$GLOBAL::config = 8` its own `$config` is still 3 — so the program-side (Any) is the two-container split of its per-unit GLOBAL merge, which nothing can usefully rely on, where rakupp's one container is what `our $x` (an alias of `$?PACKAGE::x`) says. Left over: O32 | InterpreterBinding.cpp (`pseudoStashCall` EXISTS-KEY), BuiltinsRegister2.cpp (`__sym-exists` OUR/PKG), Parser.cpp/.h (`OUR::` in the adverb list, `pkgExistsAdverb`) | t/regression/stash-exists-answers.raku: 16 checks (cff43a59 cannot parse it). Roast list and totals identical to cff43a59's (S07-hyperrace/basics.t, a concurrency stress test with no `our` or stash in it, lost one assertion in one of two runs; 0 failures in 20 runs alone and 30 runs six at a time, on this binary and on 62b8949f); perf-guard −0.6%…+0.3% in instructions |
| O32 | A named routine declared without `our` — `sub f`, `my sub f`, a plain `multi` — records that it is lexical (`CodeInfo::lexicalSub`, set where the SubDecl becomes a routine and on a multi group's dispatcher; an `our proto`/`our multi` clears it), and GLOBAL's table (`globalTableSlot`) does not count it as the package's symbol when it sits under its own name: `GLOBAL::<&f>:exists` is False for it, True for an `our sub`, an `our &x = …` or an alias. `GLOBAL::{EXPR}` reads that table as a Stash's key (`SymbolicRef::globalStash`, serialized; AST cache v47): a sigilled symbol that is not there reads Any — a missing one, a top-level `my`, a lexical routine, an import — not the X::NoSuchSymbol Failure or the lexical `::(…)` finds; a sigilless key resolves as before, the key evaluated once. Assigning to `GLOBAL::{'$x'}` makes the slot (it died X::Undeclared) or writes the `our` already there | Value.h (`lexicalSub`), InterpreterModules.cpp (execDeclStmt's SubDecl), InterpreterBinding.cpp (`globalTableSlot`, EXISTS-KEY), InterpreterCore.cpp (the SymbolicRef read and lvalue), Ast.h, AstSerial.cpp/.h, Parser.cpp, Interpreter.h | t/regression/stash-reads-and-lexical-routines.raku: 17 checks, 77741ce6 dies on its assignment. Roast list and totals identical to 77741ce6's; perf-guard −0.8%…+0.05% in instructions |

## Open: the ledger

*Numbering.* The branches above were written side by side and numbered new
items independently, so O26, O27 and O29 each named two or three things. At
the merge (2026-10-21) the stdin branch kept its O26–O32, which its code
comments cite, and the others moved: the numeric branch's O26 (the Rat spill)
is O33 and its O29 (Bool kernel variables) O34; the getc branch's O26 (a
binary handle refusing text reads) is O35 and its open O27 (strscan's two
instruction modes) O36. New items start at O37.

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
  until the stack is gone), and `×=` ignores it. Fixed 2026-10-21: the node
  carries the name it was written with
  ([unicode-operator-names-apart.raku](../../../t/regression/unicode-operator-names-apart.raku)).
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

### Speed

- **O25. Fix 3's own `$_` is defined for any sub with an nqp op in it**:
  mayUseTopic's `default: return true` covers NqpOp, so since cce315ef a
  plain sub whose body holds `nqp::…` defines `$_` (Env::define, a hash
  emplace) on every call: `sub f($t) { nqp::chars($t) }`, 200k calls, 996M →
  1,100M instructions (+520 a call). perf-guard's `strpass` and `subcall`,
  both such subs, are +1.3% and +0.9% in instructions against 1ac4abdb,
  which the timings above could not see. Look into the op's arguments, as
  the Call arm does (the default is Interpreter.cpp ~1801).
- **O36. perf-guard's strscan is bimodal in instructions**: the kernel
  (`.substr($i, 1).ord` over a 200k ASCII string) retires ≈2,018.6M
  instructions in most runs and ≈2,031.5M in some, on one and the same
  binary, about 67 more an iteration (twice the iterations, twice the
  difference). Which mode a run lands in looks like a matter of layout
  rather than of the code a run executes: on the base 1 run in 40 lands
  high; with a function nothing calls added to MethodCallPart3.cpp, 5 in
  40; with O23's change to methodCallPart3, 19 in 25, and with the rest of
  O23 but the base's MethodCallPart3.cpp, 1 in 12; and O35, which changes
  methodCallPart3 again and nothing on strscan's path either, took it back
  to 2 in 20 (O23's build: 18 in 20 in the same sample). Cycles and wall
  time show no difference. Something on the `.substr`/`.ord` dispatch path costs
  by address, and a ±0.5% instruction gate will keep tripping over it.
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
  the boxed and packed `.sum` folds are two loops with one rule; three 128-bit
  Rat reducers (IntOps.h `ratOp128`, which the small-Rat lane and
  typedRatArith share since O17, the kernel's xratio, cnp's rk_gcd), xratio
  calling `__builtin_ctzll` directly instead of IntOps.h's (inside
  RAKUPP_HAS_INT128, so MSVC still builds); rtPow re-implements `pow_ovf`.
