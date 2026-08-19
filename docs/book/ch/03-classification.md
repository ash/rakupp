# Where It Sits in the Taxonomy

There is a question every compiler gets asked before any other: *what kind of
compiler is it?* LL or LR, one-pass or multi-pass, interpreter or compiler,
bytecode or native. The answer for Raku++ is that the question decomposes —
the front end, the regex engine and the back end are classified on three
different axes, and only when they are taken apart does the taxonomy say
anything useful.

It is worth doing before the mechanisms, because most of the front end's
limitations are not independent defects. They are what the box costs. A reader
who knows the classification can predict the limitation list in Chapter 6
without being told it.

The one-line answer:

> A hand-written, single-pass, syntax-directed **recursive-descent parser with
> a Pratt (precedence-climbing) expression core and a dynamically extensible
> operator table**, feeding an **AST tree-walking interpreter**, with an
> optional **source-to-source back end** that emits C++.

Nothing in that sentence is generated. There is no grammar file, no parser
generator, no state table, and no dependency — which is the same constraint
Chapter 1 opened with, seen from the compiler-theory side.

## The lexer: separate, complete, no feedback

`Lexer::tokenize()` is a character-level scanner that runs to completion and
returns a flat `std::vector<Token>`. Only then is a parser built over it:

```cpp
// src/Parser.cpp
Lexer lx(src);
Parser p(lx.tokenize());
```

The two phases are therefore **fully decoupled**. That is worth stating
explicitly because the opposite arrangement is so common: C compilers famously
need the parser's symbol table to decide whether an identifier is a type name,
and the resulting parser-to-lexer feedback loop is known in the trade as the
lexer hack. Raku++ has nothing of the sort. The lexer never asks the parser
anything, because by the time the parser exists the lexer has finished.

Raku's context-sensitivity is real, so the work has to happen somewhere; here it
is resolved from the lexer's **own one-token history**. A bare `/` opens a regex
in term position and divides in operator position, and `regexContext()` decides
by inspecting the previous token alone — after most operators a term is
expected, so a regex follows; after a value, a `)`, or a postfix `++`, division
does. Chapter 4 gives the rule and the keyword set that patches the identifier
case.

Two consequences follow from a lexer that is closed over its input before
parsing begins, and both matter later:

**The operator vocabulary is static.** `lexOperator` matches against a
hand-ordered table, first match wins, entries arranged longest-first by hand
rather than by a maximal-munch scanner. It tracks no user declarations. An
operator spelling it does not know falls through to a single-character token —
which is precisely how a user-declared operator reaches the parser at all.

**Rule bodies are not Raku tokens.** A `token`, `rule` or `regex` body is
captured whole as one opaque token and re-lexed later by the regex engine
(Chapter 20). The regex sub-language never enters the main token stream, which
is why Part V can be read as a separate compiler.

## The parser: recursive descent with a Pratt core

The parser is the classic hybrid:

| Construct | Technique |
|---|---|
| Statements, declarations, blocks | Recursive descent on the leading token |
| Expressions | Pratt, i.e. precedence climbing |
| Prefix and postfix operators | Interleaved around the primary term |

The split is not a matter of taste. Raku's operator table runs to some two
dozen precedence levels, and pure recursive descent spends one mutually
recursive function per level to express them — the textbook
`expr → term → factor` ladder, with twenty-odd rungs. Precedence climbing
collapses the ladder into a single loop driven by sixteen named binding powers,
which is why `parseExpr` fits on a page (Chapter 5).

Placing it in the LL/LR hierarchy: it is **top-down, LL family, and neither
LL(1) nor any fixed LL(k)**. The honest description is *recursive descent with
bounded local backtracking*. The parser walks the token vector once, left to
right, and rewinds at exactly four places in seven and a half thousand lines,
each a short speculative probe that restores the position on failure. Because it
memoizes nothing, it is not a packrat or PEG parser; because it builds no parse
forest, it is not GLR or Earley. Ambiguity is resolved where it is met, not
afterwards.

The pass structure is equally plain: there is no pre-scan, and **no user code
runs during the parse**. `BEGIN`, `constant`, `use` and `no` all become AST
nodes that the interpreter acts on later. Named subs are hoisted — but by the
interpreter, which is the reason a sub needs no forward declaration while an
operator does.

## The part with no textbook box

One thing does happen at parse time: the **operator table is mutated while
parsing**. Declaring `sub postfix:<!>` registers a new operator with its own
precedence and associativity, taken from `is tighter`, `is looser` and
`is equiv`, and tokens after that point parse differently from tokens before it.

That single fact puts the front end outside the LL/LR classification entirely.
Both presuppose a grammar fixed before parsing starts; no static table can be
built for a grammar that is not fully known until the program has been read. The
nearest term in the literature is an **adaptive**, or extensible, grammar.

In practice, top-down parsing with a live operator table is the only tractable
way to implement one, which is why every language that lets users declare
operators — Raku, Haskell, Prolog, Agda — has a top-down front end. The choice
of recursive descent here was not made in spite of Raku's grammar. It was made
because of it.

The extension mechanism is deliberately narrow, and Chapter 6 is about exactly
how narrow. `use Foo` does not parse the module at compile time: the parser
*text-scans* the module source for declarations of `infix:<…>` and its
relatives, and registers those names so the importing file can parse them. That
is lexical bookkeeping, not execution — the distinction Chapter 32 returns to.

## The regex engine, classified separately

Part V is a second compiler inside the first, and it belongs in a different box.

**The matcher is a backtracking recursive-descent walker** over a compiled node
tree — the Perl and PCRE lineage, not the Thompson and RE2 lineage. It has to
be. Raku regexes carry captures, embedded code blocks, assertions and recursive
subrule calls, none of which a pure DFA can express, and a grammar is a class
whose methods are regexes. Chapter 21 is the matcher; Chapter 22 is what a
grammar adds on top of it.

**A Thompson NFA appears in exactly one role.** Longest-token matching has to
answer "how far can each alternation branch's declarative prefix reach?", and
the NFA answers it in one linear, execution-free scan of the input. It never
matches on behalf of the engine — the real backtracking matcher then runs on the
branches it ranked. It is also not yet the default: the shipped ranker probes
each branch for its greedy full-match end, and the NFA ranker is behind an
environment variable, for reasons Chapter 23 sets out along with the phase plan
for flipping it.

So the engine is a backtracking matcher that borrows one automaton for one
question. Describing it as "an NFA engine" would be wrong in both directions.

## The back end: there is no IR

Here the classification is unusually short, because a whole layer is missing.
There is **no intermediate representation**: no bytecode, no three-address code,
no SSA, no control-flow graph, no register allocation, no lowering pass. The AST
is the sole representation the whole way through. That is what makes this an
*AST interpreter* rather than a compiler in the Dragon-Book sense, for three of
its four modes:

| Mode | What it is, taxonomically |
|---|---|
| default | Tree-walking interpreter |
| `--bundle` | Source in the binary, parsed at startup |
| `--aot` | AST rebuilt at startup, still tree-walked |
| `--exe` | Source-to-source compiler — a transpiler to C++ |

Only `--exe` is a compiler proper, and even there the optimizer is **AST-level
pattern matching at emit time**: direct-arity calls, inlined `int64` arithmetic,
guarded native-int expression lanes. Those are local, peephole-class
transformations by any standard classification (Chapter 27). Everything
classical — inlining, constant propagation, loop transforms, instruction
scheduling, register allocation — is delegated to the host C++ compiler, which
is the whole point of emitting C++ instead of machine code. Chapter 26 is about
what that delegation buys and what it costs.

The interpreter's own speed work sits in the same place on the map. Node
specialisation (Chapter 19) caches a decision on the syntactic shape of a node;
it is a fast path on a tree walk, not a compilation step, and no amount of it
turns the tree walk into something with an instruction stream.

## What it is not

Stated plainly, since these are the usual guesses:

| Not | Why |
|---|---|
| LR, LALR, SLR | Bottom-up and table-driven; both are incompatible with an operator table that changes mid-parse |
| LL(1) | Several constructs need more than one token of lookahead |
| PEG or packrat | No memoization, no ordered-choice formalism |
| GLR or Earley | No parse forest; ambiguity is settled where it is met |
| Generated | The lexer and parser are hand-written C++ |
| Bytecode VM | No instruction set and no opcode dispatch loop |
| SSA-based | No IR of any kind |
| JIT | Nothing is compiled at run time; `--exe` compiles ahead of time |

## Compared with three others

| | Front end | Grammar fixed? | Back end |
|---|---|---|---|
| **Raku++** | Recursive descent + Pratt, hand-written | No — live operator table | AST tree-walk; `--exe` emits C++ |
| **Rakudo** | Self-hosted NQP grammars, code runs at parse time | No — slangs and macros | Bytecode, IR, JIT on MoarVM |
| **CPython** | Generated PEG parser | Yes | AST → bytecode → VM |
| **Clang** | Recursive descent, hand-written | Yes | LLVM IR, SSA, full pipeline |

Two rows of that table are worth a sentence each.

Raku++ shares its *front end* row with a production C++ compiler. Hand-written
recursive descent is not a shortcut taken by small projects; it is what
industrial compilers for languages with real syntax actually use, because
generated parsers are hard to give good errors and impossible to give
context-sensitivity.

And it shares its *grammar not fixed* column with Rakudo alone — which is a
property of the Raku language rather than a choice either implementation made.
Any Raku implementation has to solve it. The two solved it differently, and the
difference explains most of what Chapter 6 says is missing here: Rakudo runs the
program's own code during the parse, and Raku++ does not.

## Honest limitations

Nearly every front-end limitation in this book is downstream of the
classification above, and worth reading as a consequence rather than a bug list:

- **Operators must be declared before use.** The only way the parser learns of
  one is by reaching its declaration, and nothing pre-scans the file. Subs are
  exempt only because the interpreter hoists them.
- **No macros and no slangs.** Both require user code to run during the parse and
  change how later source is parsed. A parser that executes nothing cannot offer
  them, and no amount of work short of restructuring the pass would.
- **Block versus hash stays a heuristic.** It follows Raku's documented rules,
  but a case a backtracking full grammar would settle by trying both can still be
  decided wrongly here.
- **No whole-program analysis.** With no IR and no separate pass over the tree
  before execution, an `--exe` optimization has to be expressible as a local
  rewrite during emission.

The other side of the ledger is the reason the design holds. One AST serves all
four run modes with no lowering step between them, so a language feature is
implemented once (Chapter 25). A parse error can point straight at a token,
because there is no table-driven state to translate back into source. And there
is exactly one implementation of the semantics — `librakupp_rt.a` — shared by the
interpreter and by every binary it compiles.
