# Source Map

The vocabulary the book uses — the general compiler terms and this project's
own — is in Appendix D.

## Where to look for what

| If you are changing | Start in | Also read |
|---|---|---|
| tokenization, quoting, heredocs | `Lexer.cpp`, `Token.h` | Chapter 4 |
| statement or expression syntax | `Parser.cpp`, `Ast.h` | Chapters 5 and 7 |
| a user-declared operator | `Parser.cpp` `parseSub`, the `userInfix_` family | Chapter 6 |
| what a value *is* | `Value.h` | Chapter 8 |
| string performance | `Value.h` `CowStr`, `BuiltinsShared.h` | Chapters 9 and 24 |
| where a value's out-of-line blocks live | `Ref.h`, `BodyPool.h`, `SlabPool.h` | Chapter 41, VALUE32-PLAN.md |
| the number tower | `BigInt.cpp`, `IntOps.h`, `Value::rat` | Chapter 11 |
| scoping, assignment, binding | `InterpreterCore.cpp` `lvalue`, `evalAssign` | Chapter 12 |
| cyclic garbage | `Cycle.cpp` | Chapter 12 |
| how lists and argument lists are stored | `ValueVec.h` `RVec`, `Value.h`'s `ValueList` | Chapter 12 |
| calls and signatures | `InterpreterCore.cpp` `callCallableRaw`, `bindParams` | Chapter 14 |
| `return`, `next`, `last`, `when` | the cooperative registers in `ExecContext` | Chapter 15 |
| a built-in routine | `BuiltinsRegister*.cpp` `registerBuiltins` | Chapter 16 |
| a built-in **method** | the six `methodCall` segments, **in order** | Chapters 2 and 16 |
| classes, roles, mixins | `InterpreterModules.cpp` `ClassDecl` handling, `Value.h` | Chapter 17 |
| laziness, `gather` | `LazySeqState`, `seqOp`, the gather stack, `Coro.cpp` | Chapter 18 |
| interpreter speed | `evalBinary`, `evalIndex`, the decided-once fields | Chapter 19 |
| integer, loop and hyper kernels | `IntKernel.cpp`; the hyper kernels in `InterpreterRegex.cpp` | Chapter 41, KERNEL-PLAN.md |
| packed native arrays | `Value.h` `PackedArr`, `Value.cpp` `selfPack` | PACKED-ARRAY-PLAN.md |
| regex syntax | `Regex.cpp` `parseAtom` | Chapter 20 |
| regex matching | `Regex.cpp` `matchNode` | Chapter 21 |
| grammars | `GrammarMatcher`, `Interpreter::grammarParse` | Chapter 22 |
| alternation ranking | `LtmNfa.cpp` | Chapter 23 |
| Unicode | `Unicode.cpp`, `tools/ucd/`, the generators | Chapter 24 |
| the CLI and compile drivers | `main.cpp` | Chapter 25 |
| the native compiler | `Codegen.cpp`, the `rt*` helpers in `Interpreter.h` | Chapters 26 to 28 |
| what a binary keeps | `SlimScan.cpp`, `ucd_seam.h`, `src/stubs/` | Chapter 29 |
| tier-up: counting, eligibility, entry | `Jit.cpp` — the candidate walk, `Site`, `runIfReady` | Chapter 44 |
| copy-and-patch: stencils and the patcher | `src/cnp/`, `Cnp.cpp`, `tools/cnp-extract.cpp` | Chapter 44 |
| the parse cache | `AstSerial.cpp` | Chapter 30 |
| the JavaScript back end | `src/codegen/Js.cpp`, `JsRuntimeSrc.cpp`, `src/js-rt/` | Chapter 31 |
| the browser build | `rakujs/rakupp_web.cpp`, `rakujs/build.sh`, `raku.js` | Chapter 32 |
| module loading | `InterpreterModules.cpp` `loadModule`, `Parser.cpp` `scanModuleOps` | Chapter 33 |
| the installer and the store | `tools/install.raku`, `Builtins.cpp` `.install` | Chapter 34 |
| `nqp::` ops | `Parser::makeNqpOp`, `Interpreter::evalNqpOp` | Chapter 35 |
| NativeCall | `Ffi.cpp`, `Interpreter::callNative` | Chapter 36 |
| the extension ABI | `rakupp_ext.h`, `ExtApi.cpp` | Chapter 37 |
| embedding — Raku inside a host | `rakupp.h`, `EmbedApi.cpp` | Chapter 37 |
| threads, the GIL, supplies | `Interpreter.h`'s concurrency section | Chapter 38 |
| the race detector | `RaceDetector.cpp` | RACE-DETECTOR-PLAN.md |
| the sandbox | `Sandbox.cpp`, `SandboxOs.cpp` | SANDBOX-PLAN.md |
| lint, highlight, profile, REPL | `Lint.cpp`, `Highlight.cpp`, `Profiler.cpp`, `Repl.cpp` | Chapter 39 |
| the undeclared-variable gate | `DeclCheck.cpp`, `isSpecialVar` in `Interpreter.cpp` | Chapter 39 |
| the will-never-work call gate | `CallCheck.cpp` | Chapter 39 |
| the formatter | `Fmt.cpp` | Chapter 39, FMT-PLAN.md |
| the language server | `Lsp.cpp`, `LspIndex.cpp`, `editors/` | Chapter 39 |
| documentation links | `DocLinks.cpp`, `raku_docs_index_gen.cpp` | Chapter 39 |
| the RakuAST view | `RakuAstView.cpp`, `RakuAstClasses.cpp`, `RakuAstDeparse.cpp` | RAKUAST-PLAN.md |
| the MCP server | `McpServer.cpp` | Chapter 39 |
| the Jupyter kernel | `JupyterKernel.cpp`, `Digest.cpp`, `JsonLite.h` | Chapter 39 |
| the Arduino back end | `src/codegen/Arduino.cpp`, `ArduinoSim.h` | ARDUINO-PLAN.md |

## Rules that are easy to break by accident

Collected here because each has cost real debugging time.

**The method-dispatch chain is order-sensitive.** The six segments are ordered
slices of one function; later arms deliberately catch what earlier ones decline.
Moving an arm for readability is a behaviour change.

**A decided-once field may hold a fact about the syntax, never a value that can
change.** The literal cache is the one exception, and a literal is a constant by
definition.

**Never store an `FnRef`.** It borrows the caller's lambda; storing it dangles.

**A pointer is only a valid map key when its target's lifetime is at least the
map's.** AST nodes are never freed, so a flip-flop state map keyed on one is
fine. Regex nodes are freed and their addresses recycled, which is why a cache
keyed on one produced automata built for other patterns.

**Intern a closed vocabulary, never an open one.** The intern table is
append-only, so a field that can hold arbitrary runtime data would leak an entry
per distinct value.

**Do not add a non-`const` `operator[]`, `begin()` or `data()` to `CowStr`.**
That is exactly the interface that made copy-on-write non-conforming for
`std::string`.

**Nothing in a `Value` may point at itself.** `ValueList` relocates its buffer
with a `memcpy`, which is sound only while every member survives having its
bytes moved without the source being destroyed. A field with an interior
pointer, a self-registering handle or an intrusive list node breaks it — and
breaks it silently, by producing wrong data rather than by failing to compile.
The near-miss was in the struct until 2026-11-07: `CowStr` held a
`std::string`, and libstdc++'s short one points at its own inline buffer, which
is why the path is chosen by a run-time probe. A `Value` holds no `std::string`
now and the probe is still there. Run `tools/reloc-probe.cpp` after touching
the struct.

**Every block a `Value` refers to comes from `BodyPool`.** Where handles are
on, the payload slot, the cold block and a long string's body are 32-bit
offsets into one reserved region, so a block made with plain `new` has no
handle. Make payloads with `makePayload`, which also holds each one to the
pool's 4,096-byte limit with a `static_assert`.

**Add an early exit, never restructure the general path underneath it.** The
first node specialisation cost the control 5.7% by doing the latter.

**No helper a stencil calls may throw.** A copy-and-patch code buffer carries
no unwind tables, so an exception crossing one reaches `std::terminate`. Every
`rk_cnp_*` helper wraps its body in a catch-all, stashes the exception in the
frame and returns a status instead. Widening what a kernel may call means
answering this for the new helper first (Chapter 44).

**A memo is only sound if the thing memoised is a function of the key.** A
grammar rule that reads a dynamic variable is not a function of (rule, position),
which is what the `dynDep` flag records.

**`gilPark`'s window must touch no interpreter state.** Only thread-local
buffers and syscalls.

**Never join a thread while holding the lock the joinee might want.**

**A derived-data mechanism must degrade to recomputation, never to a guess.**
Every failure mode in the caching and emitting paths ends in "parse it again".
