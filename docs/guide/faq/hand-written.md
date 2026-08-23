# Hand-written vs. written by hand

The [README](../../../README.md) calls the front end "a hand-written lexer,
parser, and tree-walking evaluator." This is also a project built with an AI
coding assistant. A reader put the two together and asked whether
"hand-written" is honest.

Fair question. The answer is that the phrase makes a narrower claim than it
seems to — a claim about *tools*, not about *authorship* — and the narrower
claim is checkable against the tree. This page states that claim precisely,
and then answers the question the reader actually cared about.

## The claim the phrase makes

In compiler engineering, "hand-written" is a term of art. Its opposite is not
"written by a machine"; it is "emitted by a parser generator" — a tool like
yacc, bison, or ANTLR that compiles a grammar file into parser code. The term
predates AI code generation by decades, and it is how production compilers
describe themselves:

- [GCC 3.4 release notes](https://gcc.gnu.org/gcc-3.4/changes.html) (2004) —
  "A hand-written recursive-descent C++ parser has replaced the YACC-derived
  C++ parser from previous GCC releases."
- [Clang, *Features and Goals*](https://clang.llvm.org/features.html) — "We
  are convinced that the right parsing technology for this class of languages
  is a hand-built recursive-descent parser."
- [Go 1.6 release notes](https://go.dev/doc/go1.6) (2016) — "Internally, the
  most significant change is that the parser is now hand-written instead of
  generated from yacc."
- [*Crafting Interpreters*](https://craftinginterpreters.com/parsing-expressions.html)
  — "Recursive descent is the simplest way to build a parser, and doesn't
  require using complex parser generator tools like Yacc, Bison or ANTLR. All
  you need is straightforward handwritten code."
- [A 2021 survey of major implementations](https://notes.eatonphil.com/parser-generators-vs-handwritten-parsers-survey-2021.html)
  — "Of the 2021 Redmonk top 10 languages, 8 of them have a handwritten
  parser."

On this axis the README's claim is verifiable. There is no grammar file in the
repository — no `.y`, no `.l`, no ANTLR grammar — and no generator step in the
build: [`src/Lexer.cpp`](../../../src/Lexer.cpp) *is* the lexer and
[`src/Parser.cpp`](../../../src/Parser.cpp) *is* the parser, ordinary C++
compiled like every other file. The one body of generated code in the tree is
the Unicode tables, produced from the Unicode character database into
checked-in C++ sources — generated *data*, not a generated *parser*;
[UNICODE.md](../UNICODE.md) has the pipeline. Why a generator was never an
option here — Raku's grammar changes while it is being parsed — is the subject
of [PARSING.md](../../internals/PARSING.md) and
[CLASSIFICATION.md](../../internals/CLASSIFICATION.md).

## So who wrote the code?

Raku++ is developed by its author working with an AI coding assistant (Claude
Code). The author sets direction, makes the design calls, reviews what lands,
and decides what ships; the assistant writes much of the C++ — and much of the
documentation, this page included.

Nor is this page the disclosure. Claude has been in the repository's public
[contributor list](https://github.com/ash/rakupp/graphs/contributors) from the
start, and the
[launch announcement](https://andrewshitov.com/2026/07/13/raku-the-fastest-raku-compiler/)
says it in prose: "we have great friends and helpers, AI."

In short: the parser is hand-written in exactly the sense GCC's and Go's are.
The hands were not always human.
