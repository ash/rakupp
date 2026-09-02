# Source Map and Glossary

## Where to look for what

| If you are changing | Start in | Also read |
|---|---|---|
| tokenization, quoting, heredocs | `Lexer.cpp`, `Token.h` | Chapter 4 |
| statement or expression syntax | `Parser.cpp`, `Ast.h` | Chapters 5 and 7 |
| a user-declared operator | `Parser.cpp` `parseSub`, the `userInfix_` family | Chapter 6 |
| what a value *is* | `Value.h` | Chapter 8 |
| string performance | `Value.h` `CowStr`, `BuiltinsShared.h` | Chapters 9 and 24 |
| the number tower | `BigInt.cpp`, `IntOps.h`, `Value::rat` | Chapter 11 |
| scoping, assignment, binding | `Interpreter.cpp` `lvalue`, `evalAssign` | Chapter 12 |
| how lists and argument lists are stored | `ValueVec.h` `RVec`, `Value.h`'s `ValueList` | Chapter 12 |
| calls and signatures | `Interpreter.cpp` `callCallableRaw`, `bindParams` | Chapter 14 |
| `return`, `next`, `last`, `when` | the cooperative registers in `ExecContext` | Chapter 15 |
| a built-in routine | `Builtins.cpp` `registerBuiltins` | Chapter 16 |
| a built-in **method** | the four `methodCall` segments, **in order** | Chapters 2 and 16 |
| classes, roles, mixins | `Interpreter.cpp` `ClassDecl` handling, `Value.h` | Chapter 17 |
| laziness, `gather` | `LazySeqState`, `seqOp`, the gather stack | Chapter 18 |
| interpreter speed | `evalBinary`, `evalIndex`, the decided-once fields | Chapter 19 |
| regex syntax | `Regex.cpp` `parseAtom` | Chapter 20 |
| regex matching | `Regex.cpp` `matchNode` | Chapter 21 |
| grammars | `GrammarMatcher`, `Interpreter::grammarParse` | Chapter 22 |
| alternation ranking | `LtmNfa.cpp` | Chapter 23 |
| Unicode | `Unicode.cpp`, `tools/ucd/`, the generators | Chapter 24 |
| the CLI and compile drivers | `main.cpp` | Chapter 25 |
| the native compiler | `Codegen.cpp`, the `rt*` helpers in `Interpreter.h` | Chapters 26 to 28 |
| what a binary keeps | `SlimScan.cpp`, `ucd_seam.h`, `src/stubs/` | Chapter 29 |
| the parse cache | `AstSerial.cpp` | Chapter 30 |
| the browser build | `rakujs/rakupp_web.cpp`, `rakujs/build.sh`, `raku.js` | Chapter 31 |
| module loading | `Interpreter.cpp` `loadModule`, `Parser.cpp` `scanModuleOps` | Chapter 32 |
| the installer and the store | `tools/install.raku`, `Builtins.cpp` `.install` | Chapter 33 |
| `nqp::` ops | `Parser::makeNqpOp`, `Interpreter::evalNqpOp` | Chapter 34 |
| NativeCall | `Ffi.cpp`, `Interpreter::callNative` | Chapter 35 |
| the extension ABI | `rakupp_ext.h`, `ExtApi.cpp` | Chapter 36 |
| embedding — Raku inside a host | `rakupp.h`, `EmbedApi.cpp` | Chapter 36 |
| threads, the GIL, supplies | `Interpreter.h`'s concurrency section | Chapter 37 |
| lint, highlight, profile, REPL | `Lint.cpp`, `Highlight.cpp`, `Profiler.cpp`, `Repl.cpp` | Chapter 38 |
| the undeclared-variable gate | `DeclCheck.cpp`, `isSpecialVar` in `Interpreter.cpp` | Chapter 38 |
| the MCP server | `McpServer.cpp` | Chapter 38 |

## Rules that are easy to break by accident

Collected here because each has cost real debugging time.

**The method-dispatch chain is order-sensitive.** The four segments are ordered
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
The near-miss is already in the struct: libstdc++'s short `std::string` points
at its own inline buffer, which is why the path is chosen by a run-time probe.
Run `tools/reloc-probe.cpp` after touching the struct.

**Add an early exit, never restructure the general path underneath it.** The
first node specialisation cost the control 5.7% by doing the latter.

**A memo is only sound if the thing memoised is a function of the key.** A
grammar rule that reads a dynamic variable is not a function of (rule, position),
which is what the `dynDep` flag records.

**`gilPark`'s window must touch no interpreter state.** Only thread-local
buffers and syscalls.

**Never join a thread while holding the lock the joinee might want.**

**A derived-data mechanism must degrade to recomputation, never to a guess.**
Every failure mode in the caching and emitting paths ends in "parse it again".

## Glossary

**AOT** — the `--aot` mode: parse at build time, emit C++ that rebuilds the AST,
then interpret it at run time.

**Allomorph** — a value that is simultaneously a number and its own string, such
as `IntStr`. Represented as a numeric tag with the text in `s`.

**Autothreading** — distributing an operation over a junction's eigenstates and
recombining the results.

**Bundle** — the `--bundle` mode: embed the source bytes in a standalone binary
that parses and interprets them at run time.

**Byteset** — a 256-bit bitmap cached on a regex character-class node, answering
"does this byte match?" without re-deriving the class.

**Cooperative control flow** — implementing `return`, `next`, `last` and `when`
with a flag and a frame counter instead of a C++ exception, when no callable
boundary was crossed.

**Declarative prefix** — the leading part of a regex alternative made of
literals, character classes and quantifiers, up to the first procedural
construct. What longest-token matching ranks by.

**Decided-once field** — a mutable field on an AST node holding a fact about the
syntax, computed on first evaluation and never recomputed.

**Eigenstate** — one of the values inside a junction.

**Fat struct** — the `Value` design: one struct with a type tag and a field for
every kind of payload, several of which may be live at once.

**Seam** — an accessor that is the only way to reach a piece of data, so the
object defining it can be swapped for a stub at link time. What makes a
Unicode table cuttable.

**Trigger** (in `--slim`) — a construct meaning the program can run code the
scan never saw, so every feature is kept. An unmodelled AST node is one.

**Emscripten** — the toolchain that compiles the runtime to WebAssembly.
Its `-fexceptions` mode routes C++ throws through JavaScript, which is what
bounds recursion depth in the browser.

**GIL** — the global interpreter lock. Only its holder may touch interpreter
state; it is engaged lazily on first concurrent use.

**Grapheme** — what a reader calls a character. Raku's string indices are
grapheme indices; storage is UTF-8 bytes.

**Handle** — an opaque `RkValue` in the extension ABI. The mechanism by which an
extension never sees `Value`.

**Trivially relocatable** — a type whose bytes may be moved to a new address
without the source being destroyed, with the result equivalent to
move-constructing and then destroying. `Value` is; libstdc++'s short
`std::string` is not. It is what lets `RVec` grow by `memcpy`.

**Free list** — a chain of released blocks of one size, kept for reuse instead
of being returned to the allocator. `RVec` keeps one per capacity for the
small argument-list sizes; the frame pool is the same idea for `Env`.

**Model gap** — a construct the longest-token automaton builder could not model,
as opposed to one that genuinely ends the declarative prefix. A gap forces a
fallback to the probe ranker.

**NFG** — Raku's normalization-form-grapheme storage model. Strings are
normalised to NFC on the way in.

**Packrat memo** — the cache of a ratcheting grammar rule's match at a position,
sound because such a rule does not backtrack.

**Publish** — the step at the end of module loading that copies the module's
environment into the global one.

**Ratchet** — the property of `token` and `rule` that their quantifiers are
possessive and their matches commit.

**Sigspace** — the `:s` adverb and the `rule` declarator, under which whitespace
in a pattern means "match optional whitespace here". Implemented by wrapping
atoms with a `<ws>` subrule at compile time.

**Sink context** — a statement whose value is discarded, signalled down so an
assignment need not materialise its result.

**Slip** — `|@a`, a list that splices into the surrounding list or argument list.

**Specificity** — the score `scoreCandidate` assigns a multi-dispatch candidate.

**Superinstruction** — a fused node kind representing a common pattern. The
approach node specialisation deliberately did *not* take.

**Transparent** (of a regex construct, in ranking) — treated as an epsilon
transition by the longest-token automaton, because the commit engine will
enforce it for real.

**Twigil** — the second sigil character: `*` dynamic, `!`/`.` attribute, `^`
placeholder, `?` compile-time.

**Wrapper stack** — the `&routine.wrap({…})` layers on a `Callable`, run
outermost first, each able to `callsame` into the next.
