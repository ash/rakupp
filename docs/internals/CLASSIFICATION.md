# Where Raku++ Sits in the Compiler Taxonomy

"What kind of compiler is this?" — LL or LR, one-pass or multi-pass, interpreter
or compiler — has no single answer here, because the three stages are classified
on three different axes. The front end is in the LL family, the regex engine is
its own separate machine, and the back end is an AST interpreter that grows a
transpiler when you ask for `--exe`.

This document places each stage in the standard taxonomy, and — more usefully —
says what each classification *costs*, since most of the front end's known
limitations follow directly from the box it sits in.

For the mechanisms themselves, see [PARSING.md](PARSING.md) (front end),
[RUNTIME.md](RUNTIME.md) (execution model) and
[ARCHITECTURE.md](ARCHITECTURE.md) (the four run modes). This file is only about
the classification.

## Contents

- [The one-line answer](#the-one-line-answer)
- [Stage 1: the lexer — separate, complete, no feedback](#stage-1-the-lexer--separate-complete-no-feedback)
- [Stage 2: the parser — recursive descent with a Pratt core](#stage-2-the-parser--recursive-descent-with-a-pratt-core)
- [The part with no textbook box: an extensible grammar](#the-part-with-no-textbook-box-an-extensible-grammar)
- [Stage 3: the regex engine, classified separately](#stage-3-the-regex-engine-classified-separately)
- [Stage 4: the back end — no IR](#stage-4-the-back-end--no-ir)
- [What it is not](#what-it-is-not)
- [Compared with other implementations](#compared-with-other-implementations)
- [What the classification costs](#what-the-classification-costs)
- [See also](#see-also)

## The one-line answer

> A hand-written, single-pass, syntax-directed **recursive-descent parser with a
> Pratt (precedence-climbing) expression core and a dynamically extensible
> operator table**, feeding an **AST tree-walking interpreter**, with an optional
> **source-to-source (C++) back end**.

Nothing in that sentence is generated: there is no grammar file, no parser
generator, no table, and no third-party dependency anywhere in the pipeline.

## Stage 1: the lexer — separate, complete, no feedback

[`Lexer::tokenize()`](../../src/Lexer.cpp) is a **character-level scanner** that
runs to completion and returns a flat `std::vector<Token>`; only then is a
`Parser` constructed over it:

```cpp
// src/Parser.cpp
Lexer lx(src);
Parser p(lx.tokenize());
```

So the two phases are **fully decoupled** — there is no parser-to-lexer feedback
loop, and nothing like C's "lexer hack", where the parser's symbol table decides
how the next token lexes. Every context-sensitive decision the lexer makes is
resolved from **its own one-token history**: a bare `/` is a regex in term
position and division in operator position, and `regexContext()` decides by
inspecting the *previous* token alone.

Two consequences follow from the lexer being closed over its input:

- **The operator vocabulary is static.** `lexOperator` matches against a
  hand-ordered table (first match in a manually longest-first ordering, not a
  true maximal-munch scanner). It tracks no user declarations — a novel operator
  falls through to a single-character `Tok::Op`, which is precisely how the
  parser gets a chance to see it at all.
- **Rule bodies are not Raku tokens.** `token`/`rule`/`regex NAME { … }` bodies
  are captured as one opaque `Tok::RegexLit` and re-lexed later by the regex
  engine. The regex sub-language never enters the main token stream.

## Stage 2: the parser — recursive descent with a Pratt core

[`Parser`](../../src/Parser.cpp) is the classic hybrid:

| Construct | Technique | Entry point |
|---|---|---|
| Statements, declarations, blocks | Recursive descent, dispatch on the leading token | `parseStatement()` |
| Expressions | Pratt / precedence climbing | `parseExpr(int minbp)` |
| Prefix / postfix operators | Interleaved around the primary term | `parsePrefix()`, `parsePostfix()` |

The split is not stylistic. Raku's operator table runs to some two dozen
precedence levels; pure recursive descent would need one mutually recursive
function per level. Precedence climbing
collapses all of them into a single loop driven by a left-binding-power table
(`BP_OR = 10` … `BP_PREFIX = 150`, spaced by 10 so user-defined levels can be
slotted between built-in ones).

**Where it lands in the LL/LR hierarchy:** LL family, top-down, but not LL(1) and
not any fixed LL(k). It is best described as **recursive descent with bounded
local backtracking** — the parser walks the token vector once, left to right, and
rewinds at exactly four places in 7,500 lines ([Parser.cpp:724](../../src/Parser.cpp:724),
[2692](../../src/Parser.cpp:2692), [6192](../../src/Parser.cpp:6192),
[6870](../../src/Parser.cpp:6870)), each a short speculative probe that restores
`pos_` on failure. There is no memoization, so it is not a packrat/PEG parser,
and no parse forest, so it is not GLR or Earley.

**Single forward pass, and nothing runs.** There is no pre-scan pass and no
compile-time execution: `BEGIN`, `constant`, `use` and `no` all become AST nodes
that the *interpreter* acts on afterwards. Named subs are hoisted — but by the
interpreter, not the parser, which is why subs need no forward declaration while
operators do.

## The part with no textbook box: an extensible grammar

The one parse-time side effect is that the **operator table is mutable while
parsing**. Declaring `sub postfix:<!>` registers a new operator, with its own
precedence and associativity from `is tighter`/`is looser`/`is equiv` traits, and
tokens after that point parse differently than tokens before it.

That places the front end outside the LL/LR classification altogether, because
both presuppose a grammar fixed before parsing begins. No static table can be
built for a grammar that is not fully known until the program has been read. The
nearest term in the literature is an **adaptive** (or extensible) grammar; in
practice, top-down parsing with a live operator table is the only tractable way
to implement one, which is why every language with user-defined operators —
Raku, Haskell, Prolog, Agda — has a top-down front end.

The extension mechanism is deliberately narrow. `use Foo` does not parse the
module at compile time: `Parser::scanModuleOps` *text-scans* the module source
for `sub`/`multi`/`proto` declarations of `infix:<…>` and friends and registers
those names, so the importing file can parse them. That is lexical bookkeeping,
not execution — see [MODULE-LOADING.md](MODULE-LOADING.md).

## Stage 3: the regex engine, classified separately

[`Regex.cpp`](../../src/Regex.cpp) is its own compiler and matcher, and belongs in
a different box from the Raku parser:

- **The matcher is backtracking recursive descent** over a compiled node tree —
  the Perl/PCRE lineage, not the Thompson/RE2 lineage. It has to be: Raku regexes
  carry captures, code blocks, assertions and recursive subrule calls, none of
  which a pure DFA can express.
- **A Thompson NFA appears in exactly one role.** [`LtmNfa.cpp`](../../src/LtmNfa.cpp)
  answers "how far can each alternation branch's *declarative prefix* reach?" in
  one linear, execution-free scan, to rank `|` branches for longest-token
  matching. It never matches on behalf of the engine; the real backtracking
  matcher then runs on the ranked branches. It is also not yet the default —
  the shipped ranker probes each branch for its greedy full-match end, and the
  NFA ranker is behind `RAKUPP_LTM=1` (see [REGEX-LTM.md](REGEX-LTM.md)).

## Stage 4: the back end — no IR

There is **no intermediate representation**: no bytecode, no three-address code,
no SSA, no control-flow graph, no register allocation. The AST is the sole
representation the whole way through, which is what makes this an *AST
interpreter* rather than a compiler in the Dragon-Book sense — for three of its
four modes:

| Mode | What it is, taxonomically | Where |
|---|---|---|
| default | **Tree-walking interpreter** | [`Interpreter.cpp`](../../src/Interpreter.cpp) |
| `--bundle` | Source embedded in the binary, parsed at startup | — |
| `--aot` | AST serialized into the binary; still tree-walked | [`AstEmit.cpp`](../../src/AstEmit.cpp), [`AstSerial.cpp`](../../src/AstSerial.cpp) |
| `--exe` | **Source-to-source compiler (transpiler)** to C++, handed to `cc` | [`Codegen.cpp`](../../src/Codegen.cpp) |

Only `--exe` is a compiler proper, and even there the optimizer is **AST-level
pattern matching at emit time** — direct-arity calls, inlined `int64` arithmetic,
guarded native-int expression lanes ([OPTIMIZATION.md](OPTIMIZATION.md)). Those
are local/peephole transformations by any standard classification. All classical
dataflow optimization — inlining, constant propagation, loop transforms,
scheduling, register allocation — is delegated to the host C++ compiler, which is
the point of emitting C++ rather than machine code.

## What it is not

Stated explicitly, since these are the usual guesses:

| Not | Why |
|---|---|
| LR, LALR, SLR, LR(1) | Bottom-up and table-driven; both are incompatible with an operator table that changes mid-parse |
| LL(1) | Several constructs need more than one token of lookahead (block vs. hash, typed scoped declarations) |
| PEG / packrat | No memoization, no ordered-choice formalism, no parser combinator layer |
| GLR / Earley | No parse forest, no ambiguity resolution after the fact — ambiguity is resolved as it is encountered |
| Parser-generator output | Nothing is generated; the lexer and parser are hand-written C++ |
| Bytecode VM | No instruction set, no dispatch loop over opcodes — the interpreter walks AST nodes |
| SSA / IR-based | No IR at all |
| JIT | Nothing is compiled at run time; `--exe` compiles ahead of time, via C++ |

## Compared with other implementations

| | Front end | Grammar fixed? | Back end |
|---|---|---|---|
| **Raku++** | Hand-written recursive descent + Pratt, single pass | No — live operator table | AST tree-walk; `--exe` transpiles to C++ |
| **Rakudo** | Self-hosted NQP grammars (top-down with LTM), compile-time execution | No — full slangs and macros | NQP → MoarVM bytecode, IR, JIT |
| **CPython** | PEG parser (generated), was LL(1) before 3.9 | Yes | AST → bytecode → VM |
| **GCC / Clang** | Hand-written recursive descent | Yes | GIMPLE / LLVM IR, SSA, full optimization pipeline |

Two things stand out in that table. Raku++ shares the *hand-written recursive
descent* row with the production C++ compilers — that is the mainstream choice
for languages with real-world syntax, not a shortcut. And it shares the *grammar
not fixed* row only with Rakudo, which is a property of the Raku language rather
than of either implementation.

## What the classification costs

Being single-pass, non-executing and top-down is not free, and most known front
end limitations are direct consequences rather than independent bugs:

- **Operators must be declared before use.** The only way the parser learns of an
  operator is by reaching its declaration; nothing pre-scans the file. (Subs are
  exempt because the *interpreter* hoists them.)
- **No macros and no slangs.** Both require user code to run during the parse and
  influence how later source is parsed. A parser that executes nothing cannot
  offer them.
- **Block-vs-hash stays a heuristic.** It follows Raku's documented rules, but a
  case that a backtracking full grammar would settle by trying both can still be
  decided wrongly here.
- **No whole-program analysis.** With no IR and no separate pass over the AST
  before execution, `--exe` optimizations must be expressible as local rewrites
  during emission.

The other side of the ledger: the pipeline is direct enough that one AST serves
all four run modes with no lowering step, a parse error can point straight at a
token, and there is exactly one implementation of the language semantics —
`librakupp_rt.a` — shared by the interpreter and every compiled binary.

## See also

- **[PARSING.md](PARSING.md)** — the front end in detail: lexer, Pratt core, user-defined operators, the honest limitations list.
- **[ARCHITECTURE.md](ARCHITECTURE.md)** — the four run modes and what happens to a program in each.
- **[RUNTIME.md](RUNTIME.md)** — the execution model the tree-walker implements.
- **[REGEX-LTM.md](REGEX-LTM.md)** — the two LTM rankers and the NFA.
- **[OPTIMIZATION.md](OPTIMIZATION.md)** — what the `--exe -O` passes actually do.
- **[METAPROGRAMMING.md](METAPROGRAMMING.md)** — how far in-program language mutation goes.
