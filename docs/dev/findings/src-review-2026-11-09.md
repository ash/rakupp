# Code review of `src/` — 2026-11-09

Scope: the hand-written C++ and JS under `src/` (about 245,000 lines) at b7188025.
Generated tables (`unicode_*_gen.cpp`, `unicode_names.cpp`, `raku_docs_index_gen.cpp`,
`JsRuntimeSrc.cpp`, …) were skipped.

Method: eleven reviewers, one per area, each sampling its slice (no slice was read
in full — "not covered" lists are at the end). Evidence levels:

- **R** — reproduced against `build/rakupp` (and Rakudo 2026.09 where stated) before
  test runs were stopped.
- **C** — confirmed by reading the code; not run.
- **P** — plausible: mechanism read, consequence inferred.
- **✓** — I re-read the cited lines myself after the reviewer reported.

Line numbers are as of b7188025 and may be a few lines off.

## The five patterns behind most findings

1. **Raw `Value*` into a container held across code that can re-enter.**
   `lvalue()` returns a pointer into an `RVec<Value>` (reallocating) or a hash; the
   caller then evaluates a right-hand side, a block body or a callback, and writes
   through the pointer. Sites: A1, A2, A5, B1, B2, D2, D6, D7.
2. **`catch (...) {}` and `catch (RakuError&) {}` used as control flow.** Control
   flow is itself C++ exceptions (`ExitEx`, `ReturnEx`, `LastEx`, `NextEx`,
   `WorkerAbortEx`), and `ExitEx`'s constructor sets `g_exitUnwinding`
   (`Interpreter.h:1251`). There are 406 `catch (...)`, 158 with an empty body, 77
   of those directly after a call into user code; 5 are preceded by an `ExitEx`
   rethrow. Sites: A3, A4, A9, B9, K4.1.
3. **Copy-pasted blocks where a later fix reached only some copies.** Sites: A6,
   A7, B3, B6, B8, C6, C9, D2, D4, D5, E3, E8, F9, H4, H5, H6, I1, I4, I7.
4. **Arithmetic on user-supplied counts and indices** — unchecked `+ 1`, `* width`,
   `start + len`, no upper cap before an allocation or a loop, `-1`/`0.0`/`9e18`
   sentinels that collide with legal values. Sites: A8, C1, C3, C7, D1, D8, D9, E1,
   E2, E4, E5, G3, H4.
5. **Hand-maintained lists that must stay in step** — AST serializer fields, AST
   walkers, native-type names, word-operator names, type lattices. Sites: G1, K2.1,
   K3.1–K3.6.

---

## A. `InterpreterCore.cpp`

| # | Where | What | Ev. |
|---|---|---|---|
| A1 | 17512 / 17562 (also 17413 / 17427) | Compound assignment takes `lv = lvalue(target)`, then evaluates the RHS, which can grow the array; the store goes through the stale pointer. `my @a = 1,2; @a[0] += do { @a.push($_) for ^100000; 5 }; say @a[0]` prints 1 and the process traps at exit (Rakudo: 6). | R |
| A2 | 2652, 2629, 218–238; `InterpreterCalls.cpp:1988` | `given`/`with` on an array or hash element holds a raw pointer to the element for the whole body and writes it back on exit. | C |
| A3 | 21138, 21224, 21304, 21312, 21321 | `catch (...) {}` around regex code blocks swallows `exit`, `return`, `last`, `next` and `WorkerAbortEx`; a swallowed `ExitEx` leaves `g_exitUnwinding` set. | C ✓ |
| A4 | 17678 (and 17577) | `catch (RakuError&) {}` around a user `infix:<OP>` treats a `die` inside it as "no overload". `class A {}; sub infix:<+>(A $a, $b) { die "boom" }; my $x = A.new; $x += 1; say $x.raku` prints `1e0` (Rakudo dies "boom"). | R |
| A5 | 7843–7862, 10558–10586 | `const auto& wraps = …cold().wrappers` is held across `callCallable`; `.wrap`/`.unwrap` inside a wrapper mutates that vector. The sub and method paths are copies with the same hazard. | C |
| A6 | 1131–1141 vs 1440–1459 | `execPlainBlock` and `execBlockFull` disagree on whether a `return` passing through undoes `let`; which one runs depends on unrelated things (a `LEAVE` in the block). | C |
| A7 | 26739 vs 8887, 26786, `InterpreterCalls.cpp:1535` | "Is this infix name an assignment metaop" exists four times; the `hyper-with` copy excludes only `== <= >= != <=>`, the others also `=:= === =~= !=== …`. | C ✓ |
| A8 | 19692–19709 | `+<` has no upper bound on the shift count: `say 1 +< 9223372036854775807` grinds CPU and memory until killed (Rakudo errors at once). Same arm, 19696: `llabs(LLONG_MIN)` is UB; the gcd arm at 19794 already has the fix. | R ✓ |
| A9 | 17491 | The `$buf[$i] op= v` lane accepts `Blob`: `my $b = Blob.new(1,2); $b[0] += 1` mutates it (Rakudo: "Cannot modify an immutable Int"). | R |
| A10 | 1101–1111, 1198–1226 | A `LEAVE` that dies while the block is unwinding skips the hand-written epilogue (`let` restores, `tcx.cur = saved`). Candidate: `my $x = 1; try { { temp $x = 2; { LEAVE { die "L" }; die "B" } } }; say $x`. | C / P |
| A11 | 13060, 14231, 15477 | Three sites grow an array with a raw `resize(n + 1)` instead of `growToIndex` (no lock, no `is default`, a saturated index asks for 2^63 elements). | C |
| A12 | 11861, 12416 | `ix->adverb.clear()` mutates the shared AST node at run time; two threads on the same statement race on it. | C |
| A13 | 28284–28338 vs 28346–28380 | Multi-dim adverb subscript written twice; the per-tuple copy skips `shapedBoundsCheck` and `resolveKey`. | C |
| A14 | 26762, 26769, 26793 | Call-form `infix:<OP=>(…)` evaluates the first operand twice (once into `args`, once via `lvalue`). | C / P |
| A15 | 24633 | `[=] $a, $b, 42` stores with `*lvp = v`: no readonly, type or itemization checks. | P |
| A16 | 1520–1748 | `runLoopBody` restores `firstBody` by hand ~25 times; the NEXT-phaser block is pasted five times; the `letRestores` loop seven times although `runLetRestoresOf` exists. | C |
| A17 | 21114–21313 | "Bind captures, run regex code block, restore" copied three times with different restores; path choice by `code.find("make")` on source text. | C |
| A18 | 1886–1905 | `FramePool::release` resets `Env` field by field; a new field must be added here too (`programRoot` is already absent). | C |
| A19 | 19464–19475 | `div`/`mod` "inexact fallback" looks unreachable for the case its comment names; if reached it truncates instead of flooring. | C / P |

## B. `Interpreter.cpp`, `Interpreter.h`, `InterpreterBinding.cpp`

| # | Where | What | Ev. |
|---|---|---|---|
| B1 | `InterpreterBinding.cpp:9686` | `intRangeReduce`: unchecked `rFrom + 1` / `rTo - 1`, and `rTo == LLONG_MAX` means "endless". `[max] 1..9223372036854775807` gives Inf; `[max] 5..^(-9223372036854775808)` gives 9223372036854775807 (Rakudo -Inf). | R ✓ |
| B2 | `Interpreter.cpp:785` | `numifyStr` parses the radix of `:N<…>` with `atoi`: `+":4294967298<11>"` gives 3 (Rakudo: Failure). `+"1+2i/2"` gives `<0.5+1i>` (Rakudo fails). | R |
| B3 | `InterpreterBinding.cpp:5388`, 7391, 7415 | CArray bound check `(i + 1) * w > size` is signed and unchecked; `$carray[9223372036854775807]` passes it and reads or writes a wild address. Two identical copies. | C |
| B4 | `InterpreterBinding.cpp:1482` | `subsetMatches` caches verdicts in a `thread_local` map keyed by type-name strings, never invalidated; every exception path caches `false` for good. | C |
| B5 | `InterpreterBinding.cpp:6848–6881` | `rtIndexRef` (compiled-code write subscript) replaces any non-Hash/Array base with a fresh container; no `ASSIGN-KEY`/`AT-POS` arm, unlike `rtIndexGet`. | C / P |
| B6 | `InterpreterBinding.cpp:4131–4216` | `rtIndexAdverb` is a simplified copy of the interpreter's adverb arm: negative index wraps, no immutable-Map refusal, "exists" means "not Any". | C |
| B7 | `InterpreterBinding.cpp:5328–5345` | Compiled `(0..10**12)[3]` flattens the whole range; a lazy Seq is forced only to 1,000,000, beyond which an index silently returns the default. | C |
| B8 | `Interpreter.cpp:3979` vs 4037 | `maybeRegisterDestroy` walks one level of secondary parents, `runDestroyChain` the whole graph: a `DESTROY` on a grandparent via a secondary parent never runs. | C |
| B9 | `Interpreter.cpp:6916–7012` | `spawnPromise`'s parallel and GIL worker lambdas are near-copies; only the parallel one sets `forceRoutineFrame` and records `causeBt`. | C |
| B10 | `Interpreter.h:4317`, `Interpreter.cpp:7507` | `gilHeld_` is a plain bool written by main while abandoned workers read it. `declaredTypeOutranksRoutine` uses a sum of container sizes as a cache generation (erase + insert leaves it unchanged). | C |
| B11 | `InterpreterBinding.cpp:8035`, 8053, 2334–2360 | Manual save/restore of `tctx().cur` with only `catch (RakuError&)`; the named `is rw` copy-back does not switch to the caller's scope where the invocant copy does. | C |
| B12 | `Interpreter.cpp:4457` | `{*}` is recognised by the dynamic counter `protoDepth > 0`, not lexically. | C |

## C. `InterpreterCalls.cpp`, `InterpreterOperators.cpp`, `InterpreterModules.cpp`

| # | Where | What | Ev. |
|---|---|---|---|
| C1 | `InterpreterOperators.cpp:2390–2397` | `temp @a[0] = 9` captures the raw element pointer in the restore closure. The no-assignment arm just below (2404–2430) was fixed to restore by index; this arm was not. May be the "let/temp UAF" of the ASan baseline. | C ✓ |
| C2 | `InterpreterModules.cpp:1314` | `loadedModules_.insert(name)` happens before the load and nothing ever erases it (one insert, no erase in `src/`): after a failed `use`, the next `use` of the same name returns silently. | C ✓ |
| C3 | `InterpreterOperators.cpp:684`, 693 | Prefix `-` on an object negates `.Numeric` with `negInt(n.toInt())` or `-n.toNum()`: a big Int result is truncated, a Rat or Complex becomes a Num. The `+` arm returns the value intact. | C ✓ |
| C4 | `InterpreterModules.cpp:1236` vs 1964 | "Apply what `sub EXPORT` returned" exists twice; the repeat-`use` copy lacks the `&name => Any` skip and the type registration of the first-load copy. | C |
| C5 | `InterpreterCalls.cpp:1622–1631` | Atomic ops do raw `cur + 1`, `cur + n2` on `long long` (signed overflow at the int64 edge), not via `IntOps.h`. | C |
| C6 | `InterpreterCalls.cpp:3316` | `spliceStr`: `std::min(n, from + len)` overflows for a huge length (`substr-rw($s, 1, 9223372036854775807) = "x"`). | C |
| C7 | `InterpreterCalls.cpp:6081–6117` | Packed-store lane takes `PackedArr*` before evaluating a RHS that is only syntactically "pure" (a user `infix:<+>` or Proxy can run). | P |
| C8 | `InterpreterCalls.cpp:1601–1616` | `cas` code form holds `Value& lv` across the user block with no lock. | P |
| C9 | `InterpreterCalls.cpp:5331` | The multi-dispatch cache is published once and never cleared; after any symbol-generation change every earlier dispatcher falls back to full scoring for the rest of the run. | C |
| C10 | `InterpreterModules.cpp:1905`, 1916, 1925 | Seven-field manual restore written three times; `publish()` runs before the restore in the error arm. | C |
| C11 | `InterpreterOperators.cpp:1931–1950` | `exceptionFor` recovers the undeclared name by parsing the message text, then mutates shared `ClassInfo` without a lock. | C |

## D. Method dispatch (`MethodCall*.cpp`)

| # | Where | What | Ev. |
|---|---|---|---|
| D1 | `MethodCallPart3.cpp:6000` | `if (start + len > n)` overflows: `"abc".substr(1, 9223372036854775807)` returns an empty string. | R ✓ |
| D2 | `MethodCallPart3.cpp:1253–1318`, 5615–5689 | `.base(radix, digits)` and `.indent(n)` have no upper bound: `255.base(16, 99999999999)` and `"abc".indent(99999999999)` run until killed. `Buf.allocate` multiplications (`MethodCallPart1b.cpp:174`, 1166) are unchecked. | R |
| D3 | `MethodCallPart3.cpp:1615–1625` | `.log` uses `0.0` as "no base": `5.log(0)` gives ln 5 (Rakudo -0), `0.log(0)` gives -Inf (Rakudo NaN). | R ✓ |
| D4 | `MethodCallPart3.cpp:7217`, 7257 | `.words(-1)` / `.lines(-1)`: a negative limit lands on the "no limit" sentinel and returns everything (Rakudo `()`). `combLimit` already has a separate flag. | R |
| D5 | `MethodCallPart3.cpp:820–926` | Five `polymod` loops with different rules: the bigint one breaks silently on a zero divisor and saturates a bigint divisor; the lazy ones drop the remainder; a `-1` divisor loops forever. | C |
| D6 | `MethodCallTail.cpp:5486–5565` | `splice` reads the length, then calls user code for start and count, then uses the stale length. All indices are `long` (32 bits on MSVC; about 425 such sites in the slice). | P / C |
| D7 | `MethodCallTail.cpp:2857–2896` | `combinations(Range)` walks every negative k (`-9223372036854775808..2` spins); `mask` and `sel` are dead leftovers; `permutations`, `combinations`, `.roll(n)` are eager. | C |
| D8 | `MethodCallPart2.cpp:9219`, `MethodCallTail.cpp:4133` | Five pick/roll implementations disagree on a negative count: `(1,2,3).pick(-1)` gives `()`; `(1..5000).pick(-1)` returns all 5000. | R / C |
| D9 | `MethodCallTail.cpp:2083–2450` | Four hand-copied endless-range arms; three answer `.list` with a silent `take(10000)`, so `('a'..*).list[10000]` is Nil. | C |
| D10 | `MethodCallPart3.cpp:1569` | `.round(0)` silently substitutes scale 1 (Rakudo throws). Deliberate per the comment. | R |
| D11 | `MethodCallTail.cpp:292`, 3472–3508 | Rotor size/gap additions unchecked; `rotor(2 => -2)` is clamped to step 1. | R / P |
| D12 | `MethodName.h:75–81` | `MNameSet8` silently drops names longer than 8 characters; nothing breaks today. | C |

## E. Builtins and data libraries

| # | Where | What | Ev. |
|---|---|---|---|
| E1 | `Builtins.cpp:5521–5700` | `to-json` has no depth cap and no cycle check: `my @a; @a[0]=@a; to-json(@a)` runs until killed. | R |
| E2 | `Builtins.cpp:3659–3692` | `sprintf("%d", 1e30)` prints 9223372036854775807 (Rakudo: the exact value); `sprintf("%d", "abc")` prints 0 (Rakudo dies). | R |
| E3 | `Builtins.cpp:3693`, `BuiltinsRegister2.cpp:815`, `MethodCallTail.cpp:2673` | `%c` and `chrs` encode any integer with no range or surrogate check, giving invalid UTF-8 (`sprintf("%c", -1)`). `chr` validates. | R / C |
| E4 | `Builtins.cpp:5741` | `to-json(:spacing(-1))` asks for a `SIZE_MAX` string (`std::length_error`, not a Raku error). | C |
| E5 | `BuiltinsNqp.cpp:899`, 1558 | `nqp::div_i` / `mod_i` guard zero but not `LLONG_MIN / -1` (SIGFPE on x86-64); shifts by ≥ 64 are UB; add/sub/mul are raw signed. | C |
| E6 | `Builtins.cpp:1002–1024` | The older stdin-feeding spawn loop reacts only to `POLLOUT`; a child that exits without reading a > 64 KB input leaves it spinning on Linux. The live-proc loop at 1114 handles `POLLERR | POLLHUP`. | C / P |
| E7 | `BuiltinsSupply.cpp:1359`, 1425 | Signal self-pipe is not `CLOEXEC` and is blocking; the handler does not save `errno`; closing a `signal(SIGINT)` tap leaves the sigaction, so Ctrl-C stays swallowed. | C |
| E8 | `Builtins.cpp:3612–3640` | `sprintf("%5%")` ignores the width; `%0$s` accepted; `%*d` with width -2^63 negates with overflow. | R / C |
| E9 | `BuiltinsRegister.cpp:408` | `rakupp-repo-unlock($fd)` closes any fd given, including 1 and 2. | C |
| E10 | `BigInt.cpp:69–85` | `fromString` silently drops non-digits (`"12a3"` → 123); `divmod` returns 0,0 on a zero divisor ("caller guards"). | C |

Read as sound: `IntOps.h` (including INT64_MIN cases), BigInt sign/zero normalisation
and division, Zlib inflate bounds, Digest padding, the JSON::Fast reader, CSV.

## F. Regex and grammar engine

| # | Where | What | Ev. |
|---|---|---|---|
| F1 | `Regex.cpp:4855`, 5748, 5880 vs 5707 | Three of seven copies of the `<( … )>` trim test `capFrom` where `capTo` is meant. With `<(` and no `)>`, the node handed to the action method has `to = -1`; the returned Match is right. | C ✓ |
| F2 | `Regex.cpp:3752`, 3556, 4848 | `Node::metaCache` stores a pointer into a per-parse `GrammarMatcher`; regexes from `<{ code }>` live in `dynRxCache_` for the interpreter's life, so the second parse dereferences a destroyed matcher. Candidate: `grammar G { token TOP { <{ '<foo>' }> }; token foo { a } }; G.parse("a"); G.parse("a")`. | C |
| F3 | `Regex.cpp:3649`, 4892, 4967, 5022 | The 8,000,000-step budget is shared across all start positions of one search and exhaustion is reported as no-match: a plain scan of a few-million-character subject can answer Nil although a match exists. | C |
| F4 | `Regex.cpp:5783–5967` | In the grammar path the budget is per rule frame and `StepLimitExceeded` is never caught: left recursion is unguarded, and a long legitimate rule throws a non-Raku exception past `matchSubMeta`'s scope pops. | C |
| F5 | `Regex.cpp:1498–1509`, `ParserRegexCheck.cpp:569` | `**` bounds parsed twice, differently, with unchecked overflow: `/a ** 2..99999999999999999999999/` matches (Rakudo throws); `/a ** -1/` is `a ** 0` then a literal `-1`; `**^0` becomes `*` by reading. | R / C |
| F6 | `Regex.cpp:4674`, 4688 vs 4629 | Zero-width iterations: the ratchet loop counts them toward the minimum, the backtracking loops fail. `"a" ~~ /[a?] ** 3/` is expected to fail in a `regex` and succeed in a `token`. | C |
| F7 | `Regex.cpp:4364`, 4446 | `Node::ltmNfa` is built under a mutex but used after unlock; another thread can reset it mid-`rank`. `lookMin/lookMax`, `metaCache`, `listCapsFrozen_` are unsynchronised. | C / P |
| F8 | `Regex.cpp:4651` | The repetition lambda that recurses once per iteration carries a 4 KB `inlineStops[512]`; a few hundred thousand iterations of `[ab]+` in a plain `regex` should overflow the stack before the step budget trips. | P |
| F9 | `Regex.cpp:115` | The constructor's `catch (...) { ok_ = false; }` turns internal errors (including `std::out_of_range` from unguarded `stol` at 383, 590, 820, 2786) into "never matches". | C |
| F10 | `Regex.cpp:4274` vs 4305 | `$<name>` backreferences ignore `:i`; `$0` backreferences honour it. | C |
| F11 | `LtmNfa.h:26`, 79; `Regex.h:313`, 602 | Comments that no longer describe the code ("oracle harness only", "no allocation after warm-up", "RAKUPP_LTM=1"). | C |

## G. Front end (lexer, parser, AST)

| # | Where | What | Ev. |
|---|---|---|---|
| G1 | `AstSerial.cpp:254–577` | About 35 AST fields the runtime reads are never serialized (Param: `shapeDims`, `codeSig`, `pastDoubleSemi`, …; UseStmt: `isRequire`, `isImport`; AttrDecl: `readonly`, `notBuilt`; SubDecl: `assocRight`, `testAssertion`; …). A unit loaded from the precomp cache differs from a fresh parse. The v20–v62 history in `AstSerial.h` is this hole being patched one field at a time. | C |
| G2 | `Parser.cpp:12790`, 4412, 5206; `Pod.cpp:477` | Unguarded `std::stoll`/`stoi` on user digits: `sub f(@a[99999999999999999999]) {}`. 12790 also reads the token's spelling, so `@a[0x10]` is shape 0 and `@a[1_000]` is 1. | C ✓ |
| G3 | `Parser.cpp:6570`, 6833, 10857 | Unchecked literal arithmetic: the fraction of `:16<1.0000000000000000>` wraps the divisor to 0; `<0xFFFFFFFFFFFFFFFFFFFF>` wraps. Radix-to-integer exists four times; two copies handle overflow. | C |
| G4 | `Pod.cpp:164–179` | `fmtClose` is exponential on unclosed nested formatting codes (`A<B<C<D<…`, 40–60 deep). | C |
| G5 | `Parser.cpp:7188`, 12737, 12796 | Backtracking catches `ParseError` and resets only `pos_`; `parseBlock`'s nine hand-managed scope stacks and `routineDepth_` stay one level deep. 7188 also turns any syntax error inside `:( … )` into an empty signature. | C / P |
| G6 | `Lexer.cpp:857–990` | `scanUserOps` finds operator declarations by `find("infix:<")` over raw text: a `sub prefix:</>` quoted in pod, a heredoc or a string turns off `/…/` literals for the file; any `#` earlier on the line hides a real declaration. `scanDeclaredSubNames` got the heredoc fix (f929935a); this did not. | C |
| G7 | `Lexer.cpp:1364` vs 1418 | The `#`«…»` embedded comment is implemented twice; only the main-path copy handles repeated openers. | C |
| G8 | `Parser.cpp:10917`, 10965 | `"\x[110000]"`, `"\x[D800]"`, `"\x[-5]"` put invalid UTF-8 into a Str (two identical `emitCp` lambdas, no range check). | C |
| G9 | `ParserWordPairs.cpp:90`, `Parser.cpp:14157` | The sub-parser for `"{…}"` copies a hand-picked dozen fields (`newlineSeq_` is missing); `isExpr ? parseEmbeddedExpr(inner) : parseEmbeddedExpr(inner)` has identical arms. | C |
| G10 | — | Accepted malformed input: `say 1e` (rc 0), `say :` gives ` => True`, `say 1 R` fails only at run time. Not compared with Rakudo. | R |
| G11 | `AstDump.cpp:118`, 179 | No cases for `SymbolicRef`, `AllomorphLit`, `NqpOp`, `NamedRegexDecl`, `SubsetDecl`. | C |

About 50 truncated inputs (unterminated strings, heredocs, comments, pod, regexes)
all gave clean errors; no crash or hang.

## H. Back ends (`--exe`, JS, Arduino, JIT)

| # | Where | What | Ev. |
|---|---|---|---|
| H1 | `Codegen.cpp:192`, `codegen/Js.cpp:71` | `\x` and `$x` mangle to one identifier: `my \x = 1; my $x = 2; say x, $x` fails to compile under `--exe` ("redefinition of 'v_sx'") and dies at load under JS. | R |
| H2 | `codegen/Js.cpp:47` | NUL is written `\0`, so a following digit makes a legacy octal escape: `"a\x[0]1b".ords` is `(97 1 98)` under JS. | R ✓ |
| H3 | `js-rt/10-core.js:689` | `say 1e15` prints `1e+15` under JS; the C++ boundary is `1e16` (`Value.cpp:889`). | R ✓ |
| H4 | `Codegen.cpp:753`, `Cnp.cpp:618` | Prefix `+^` is `~x.toInt()` in `--exe` and the copy-and-patch cold path; the interpreter was fixed to be exact for big Ints (`InterpreterCore.cpp:25970`). | C |
| H5 | `codegen/Js.cpp:864` | A big Rat literal is emitted from the raw digit string: `0.1234567890123456789` becomes `01234567890123456789n`, a JS SyntaxError. | C |
| H6 | `Jit.cpp:796–806` | Outside verbose mode the compile command ends in `; rm -f`, so its status is always 0: the `rc != 0` branch is dead and a failed compile surfaces only as "dlopen failed". | C ✓ |
| H7 | `cnp/CnpEmit.cpp:130–159` | `MAP_JIT` is tried only if the plain `mmap` fails; a failed `mprotect(RX)` on Apple is taken as "the MAP_JIT route" and returns true. Matters only under a hardened runtime. | C / P |
| H8 | `Codegen.cpp:2536`, 6513, 8517 | NumLit emission has no Inf/NaN guard (JS and Arduino have one); 8517 uses an `ostringstream` although 2521 says never to. | C / P |
| H9 | `Codegen.cpp:1314` vs `codegen/Js.cpp:630` | `hasWhatever` differs between the two compilers: `* => True`, `**`, `$x ~~ *`. | C |
| H10 | `cnp/stencils.c:147–225` | Uses `__builtin_*_overflow` directly. Defensible (clang-only, build-time, never linked) but nothing in the file says so. | C |
| H11 | `js-rt/10-core.js:610` | `Number.isInteger(a) ? Math.abs(a) : Math.abs(a)`. | C |

The fast integer paths (IntKernel, stencils, patcher range checks) read correct.

## I. Runtime core, host interfaces, tools

| # | Where | What | Ev. |
|---|---|---|---|
| I1 | `Which.cpp:247` | Set/Bag/Mix key by rendering, not `.WHICH`: `set(1,"1").elems` is 1 (Rakudo 2); `(1, 1.0, 1e0).Set.elems` is 1 (Rakudo 3). Documented as known in the comment at 232. | R |
| I2 | `Which.cpp:157`, 214 | A Pair holding an object has WHICH `Pair|<address>`, the address-reuse problem `bodySerial` fixed for Array/Hash/Object: `(^5).map({ (a => [$_]).WHICH }).unique.elems` is 1 (Rakudo 5). Two same-named lexical classes share a WHICH. | R |
| I3 | `Ref.h:59`, 236, 282 | Copy-assign is `o.retain(); release(); p_ = o.p_;` — reads `o` after `release()` may have destroyed it (`node = node->next` shape). Silent with the slab pool; a UAF read with pools off. | C ✓ |
| I4 | `McpServer.cpp:285` | MCP protects stdout only by swapping `cout`/`cerr` streambufs; a child from `run`/`shell` or NativeCall code writes fd 1 directly into the JSON-RPC stream. | C / P |
| I5 | `JupyterKernel.cpp:494–513` | ZMTP long frames take an unchecked 8-byte size before any HMAC check: unbounded buffering, and `off + size` overflows. | C ✓ |
| I6 | `Lsp.cpp:153–283` | A second JSON parser, diverged from `JsonLite.h`: skips 4/5 characters on `t`/`f`/`n` without checking the word (174–176 ✓), no low-surrogate range check (209), all numbers as double, `static_cast<int64_t>` of any double. | C ✓ |
| I7 | `Lsp.cpp:447–460` | `didOpen`/`didChange` run diagnostics outside any try; a `Content-Length` over 64 MB ends the server with exit 0 and no reply. MCP's `LineReader` has no line cap. | C |
| I8 | `McpServer.cpp:178–216` | Watchdog: `disarm()` does not notify, so a new call arriving at the old deadline can be killed by it. Microsecond window. | C |
| I9 | `ValueHash.h:334–353` | Erased entries are never compacted: a long-lived hash used as a queue grows without bound. Hashing is unseeded. | C |
| I10 | `ExtApi.cpp:162–210` | `rk_at_pos`, `rk_arg` etc. return raw pointers into container storage; the hash-walk memo is invalidated only by `rk_set`. | C |
| I11 | `IStr.h:117–119` | `chunks()[i >> kChunkBits]` is indexed before the `>= kMaxChunks` test. | C ✓ |
| I12 | `Cycle.cpp:725–741` | `cycleNoteRelease` reads the plain global `g_interp` on any thread while `cycleDetach` rewrites it under a different mutex. | C / P |
| I13 | `Profiler.cpp:63–85`, 125 | Aggregates by `Callable*` address (reused by the pool); its JSON escaper handles only `"` and `\`. | C / P |
| I14 | `main.cpp:3419`, 3424, 3580, 3624 | `-I`, `-M`, `-o`, `-F` as the last argument are silently ignored. No out-of-bounds `argv` access found. | C |

Read as sound: the sandbox (deny-all wrap, `ioFsPath` backstop, Seatbelt/Landlock),
`RVec` self-aliasing, CowStr invariants, LSP position clamping, Jupyter HMAC.

## K. Cross-file duplication and codebase-wide patterns

### K1. Helpers defined several times

- **JSON**: three parsers (`JsonLite.h:137`, `Lsp.cpp:157`, `BuiltinsJson.cpp:34`) and
  eight string escapers. They differ on lone surrogates, raw control characters,
  `+`-prefixed numbers, and literal checking (I6).
- **UTF-8**: about 20 hand-inlined encoders (none validates > U+10FFFF or
  surrogates — the root of E3, G8, F-`\x[…]`) and about 22 decoders that differ on
  stray continuation bytes, lead bytes F8–FF and truncated sequences.
- **Identifier predicates**: 12+ local definitions with four alphabets (with or
  without `-`, `'`, ≥ 0x80); the canonical ones are `rakuIdentStart`/`rakuIdentCont`.
- **`pureSubscript`** ×2 with different rules (`InterpreterCalls.cpp:1772`,
  `InterpreterCore.cpp:13091`); **`sameBoundValue`** ×2 with different comparisons.
- Small copies: `trim` ×6, file slurp ×4 (one reports failure), `isFile` ×2
  (`!S_ISDIR` vs `S_ISREG`), `isBlobKind` ×3, `shq` ×2, the `mkdir -p` loop ×2
  (only one has the Windows-drive test).

### K2. Duplicated blocks

- Exact copy-paste is modest: 65 runs of ≥ 8 identical lines. Largest:
  `CallCheck.cpp:954–987` = `DeclCheck.cpp:355–391` (the AST expression walker; the
  surroundings differ on `require`).
- **K2.1** — about 70 hand-written recursive AST walkers (106 `case NK::Binary`
  sites in 24 files) and no generic child visitor in `Ast.h`. Of 19 composite node
  kinds only 4 walkers name all of them; several whole-tree scans stop silently at
  kinds they omit (`Interpreter.cpp:1597`, 1785; `InterpreterOperators.cpp:834`,
  913; `Codegen.cpp:3972`, 5344).

### K3. Literal lists that already disagree

1. **Native type names** — 36 places although `isNativeTypeName` exists.
   `atomicint` is missing from 12 of them; `Value.cpp:1644` (the `.^name` list)
   lacks `byte` and `atomicint`.
2. **Word infix operators** — 8+ lists. `xor`/`notandthen` are missing from the Z/X
   metaop list (`Parser.cpp:616`); the hyper list (`Lexer.cpp:4210`) lacks `eqv`,
   `minmax`, `and`, `or`, `but`, `does`.
3. **Type lattices** — `Builtins.cpp:188` (runtime) vs `CallCheck.cpp:35`
   (compile-time): 6 shared types differ (Uni is not Stringy at run time; Blob/Buf
   are Cool only in CallCheck). Four more "known type" tables: union 96, common 41.
4. **Set/Bag/Mix and Blob families** — `"SetHash"` appears 95 times in 22 files;
   `utf8/utf16/utf32` are missing from 5 of 7 Blob lists.
5. **Method-name tables** of 117–133 entries in three places, 110 in common.
6. **Declarator keywords** — 9 lists; `monitor`, `knowhow`, `macro`, `only`, `proto`
   each missing somewhere.

### K4. Hazard counts

| Pattern | Count | Note |
|---|---|---|
| `__builtin_*_overflow` | 3 | all in `IntOps.h` — rule holds (plus `cnp/stencils.c`, H10) |
| `strcpy` / `strcat` / `sprintf` / `gets` | 0 | |
| `#if 0`, real `TODO`/`FIXME` | 0 | |
| `catch (...)` | 406 | 158 empty; 77 right after user code; 42 of those in `BuiltinsSupply.cpp` |
| `std::sto*` | 43 | about 15 unguarded on unbounded digit strings (G2, F9, `pack`, `m:Nx`) |
| `atoi` / `atol` / `atoll` | 23 | wrap silently (B2) |
| `9000000000000000000LL` "endless" sentinel | 76 | a finite range that large is treated as infinite |
| non-const `static` locals | 408 | about 56 mutable; unsynchronised: `g_reprInArrayElem` (`Builtins.cpp:1997`), temp-file `ctr` (`BuiltinsRegister3.cpp:779`), `punSerial`, `concSerial` |
| prototypes declared inside `.cpp` files | 104 | `isPragmaName` in 5 places, `noteAttrInit` and `coerceElemValue` with no header |

- **K4.1** — a `die` or `exit` inside a Supply tap is silently dropped at the 42
  empty catch-alls in `BuiltinsSupply.cpp`, and a swallowed `exit` leaves
  `g_exitUnwinding` set so no later `LEAVE` runs. (C; not run.)
- `ValueVec.h:243`, 298, 557, 596 relocate `Value` with `memcpy`, guarded by a
  run-time probe that covers `std::string` only.
- `main.cpp:769`, 786 pass the compiler path to `popen` unquoted; `Jit.cpp:1351`
  quotes it.

Header hygiene is clean: every header guarded, no `using namespace`, no non-inline
definitions.

---

## Not covered

No slice was read in full. Explicitly unread or only grepped:

- `InterpreterCore.cpp`: `execForStmt`, `bindParams`/`typeCheckBindImpl`,
  `callCallableRaw`, most of `lvalue`, `evalBinary`, most of `evalIndex`,
  `evalMethodCallExpr`.
- `InterpreterModules.cpp`: `evalString`, roles, the class-declaration executor,
  `run()`. Precomp cache read/write (`Interpreter.cpp:8586–8830`).
- `MethodCallPart2.cpp` almost entirely; `MethodCallEntry.cpp`; Hash/Set/Bag arms.
- `Parser.cpp` beyond about 15%; `DeclCheck`, `CallCheck`, `Lint`, `SlimScan`,
  `CompileTime`, `AstEmit`.
- `Builtins.cpp` 5900–10100; most of `BuiltinsSupply.cpp`'s locking; `IOSpec`,
  `NfcJoin`, `Unicode.cpp`.
- `Codegen.cpp` beyond helpers and literals; the Cnp `Lower` pass; `js-rt` beyond
  `10-core.js`.
- `RaceDetector`, `CustomHow`, `Repl`, `DocLinks`, `RakuAst*`; NativeCall marshalling.
- Deep-nesting behaviour of the parser, and every finding marked C or P, was not run.

## Repro results (run 2026-11-09, one at a time, 10 s / 1 GB cap)

All nine reproduce on `build/rakupp`; Rakudo 2026.09 output alongside. These
supersede the C / P marks for A2, A10, C1, C2, C3, F1, F2 and G2 in the tables above.

| # | Program | rakupp | Rakudo |
|---|---|---|---|
| A1 | `my @a = 1,2; @a[0] += do { @a.push($_) for ^100000; 5 }; say @a[0]` | `1`, then rc 133 | `6` |
| A2 | `my @a = 1,2,3; given @a[0] { @a.push(7) for ^10000; $_ = 42 }; say @a[0]` | `1`, then rc 139 (segfault) | `42` |
| C1 | `my @a = 1,2,3; { temp @a[0] = 9; @a.push($_) for ^10000 }; say @a[0]` | `9`, then rc 139 (segfault) | `1` |
| C2 | `try EVAL q[use No::Such]; EVAL q[use No::Such; say "alive"]` | `alive` | dies "Could not find No::Such" |
| C3 | `class A { method Numeric { 2**100 } }; say -A.new` | `-9223372036854775807` | `-1267650600228229401496703205376` |
| F2 | `grammar G { token TOP { <{ q[<foo>] }> }; token foo { a } }; G.parse("a"); say G.parse("a")` | no output, rc 139 (segfault) | `｢a｣` |
| F1 | `grammar G { token TOP { <x> c }; token x { a <( b } }; class X { method x($/) { say "x sees [", ~$/, "] ", $/.from, "..", $/.to } }; G.parse("abc", :actions(X))` | `x sees [bc] 1..0` then `x sees [b] 1..2` | `x sees [b] 1..2` once |
| A10 | `my $x = 1; try { { temp $x = 2; { LEAVE { die "L" }; die "B" } } }; say $x` | `2` | `1` |
| G2 | `sub f(@a[99999999999999999999]) {}; say "ok"` | "Internal error: stoll: out of range", rc 3 | `ok` |

Notes:

- F1 needs a subrule: with `<(` directly in `TOP` the action sees the right span.
  In the subrule form the action method also runs **twice** in rakupp (first with
  the wrong span, then the right one); the `)>`-only form `token x { a )> b }`
  likewise prints `x sees [ab] 0..2` before `x sees [a] 0..1`.
- G2 ends in the top-level "Internal error" handler with rc 3, not an abort.

Do not run unbounded: `1 +< 9223372036854775807` (A8), `.base`/`.indent` with huge
counts (D2), `to-json` of a cyclic array (E1).
