# Source Map and Glossary

## Where to look for what

| If you are changing | Start in | Also read |
|---|---|---|
| tokenization, quoting, heredocs | `Lexer.cpp`, `Token.h` | Chapter 3 |
| statement or expression syntax | `Parser.cpp`, `Ast.h` | Chapters 4 and 6 |
| a user-declared operator | `Parser.cpp` `parseSub`, the `userInfix_` family | Chapter 5 |
| what a value *is* | `Value.h` | Chapter 7 |
| string performance | `Value.h` `CowStr`, `BuiltinsShared.h` | Chapters 8 and 23 |
| the number tower | `BigInt.cpp`, `IntOps.h`, `Value::rat` | Chapter 10 |
| scoping, assignment, binding | `Interpreter.cpp` `lvalue`, `evalAssign` | Chapter 11 |
| calls and signatures | `Interpreter.cpp` `callCallableRaw`, `bindParams` | Chapter 13 |
| `return`, `next`, `last`, `when` | the cooperative registers in `ExecContext` | Chapter 14 |
| a built-in routine | `Builtins.cpp` `registerBuiltins` | Chapter 15 |
| a built-in **method** | the four `methodCall` segments, **in order** | Chapters 2 and 15 |
| classes, roles, mixins | `Interpreter.cpp` `ClassDecl` handling, `Value.h` | Chapter 16 |
| laziness, `gather` | `LazySeqState`, `seqOp`, the gather stack | Chapter 17 |
| interpreter speed | `evalBinary`, `evalIndex`, the decided-once fields | Chapter 18 |
| regex syntax | `Regex.cpp` `parseAtom` | Chapter 19 |
| regex matching | `Regex.cpp` `matchNode` | Chapter 20 |
| grammars | `GrammarMatcher`, `Interpreter::grammarParse` | Chapter 21 |
| alternation ranking | `LtmNfa.cpp` | Chapter 22 |
| Unicode | `Unicode.cpp`, `tools/ucd/`, the generators | Chapter 23 |
| the CLI and compile drivers | `main.cpp` | Chapter 24 |
| the native compiler | `Codegen.cpp`, the `rt*` helpers in `Interpreter.h` | Chapters 25 to 27 |
| the parse cache | `AstSerial.cpp` | Chapter 28 |
| module loading | `Interpreter.cpp` `loadModule`, `Parser.cpp` `scanModuleOps` | Chapter 29 |
| `nqp::` ops | `Parser::makeNqpOp`, `Interpreter::evalNqpOp` | Chapter 30 |
| NativeCall | `Ffi.cpp`, `Interpreter::callNative` | Chapter 31 |
| the extension ABI | `rakupp_ext.h`, `ExtApi.cpp` | Chapter 32 |
| threads, the GIL, supplies | `Interpreter.h`'s concurrency section | Chapter 33 |
| lint, highlight, profile, REPL | `Lint.cpp`, `Highlight.cpp`, `Profiler.cpp`, `Repl.cpp` | Chapter 34 |

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

**GIL** — the global interpreter lock. Only its holder may touch interpreter
state; it is engaged lazily on first concurrent use.

**Grapheme** — what a reader calls a character. Raku's string indices are
grapheme indices; storage is UTF-8 bytes.

**Handle** — an opaque `RkValue` in the extension ABI. The mechanism by which an
extension never sees `Value`.

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
