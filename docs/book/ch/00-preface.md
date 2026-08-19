# Preface {-}

This is a book about the inside of a compiler.

Raku++ is a hand-written implementation of the Raku programming language,
written in C++17, with no third-party dependencies. It lexes, parses,
interprets, and — in one of its four run modes — transpiles Raku to C++ and
compiles it to a native binary. It carries its own regular-expression and
grammar engine, its own Unicode subsystem built from the pinned UCD tables, its
own arbitrary-precision arithmetic, a foreign-function interface that loads
libffi at run time rather than linking it, a C ABI for native extension
modules, and a concurrency runtime with a global interpreter lock that can be
switched off.

None of that is unusual for a language implementation. What is unusual is that
all of it fits in about fifty thousand lines of source that one person can read,
and that almost every non-obvious decision in it was made against a
measurement. This book is an attempt to write down both: the mechanisms, and
the reasons.

## Who this is for {-}

You should be comfortable reading C++ and comfortable reading Raku, but you do
not need to be an expert in either, and you certainly do not need to have
implemented a language before. Where a piece of compiler folklore is
load-bearing — precedence climbing, Thompson construction, packrat memoisation,
copy-on-write — it is explained where it is used rather than assumed.

Three kinds of reader were in mind:

- Someone who wants to **work on Raku++** and needs a map before they can
  usefully change anything.
- Someone building **their own interpreter or compiler**, who wants a worked
  example of a dynamic language implemented in a static one — including the
  parts that went wrong.
- Someone who writes **Raku** and wants to know what the machine underneath is
  actually doing when they write `given`/`when`, or a grammar, or `is native`.

## What this book is not {-}

It is not a manual for the language, and not a manual for the `rakupp`
command. Those exist: the guide in `docs/guide/` covers using Raku++, and
`docs/guide/REFERENCE.md` is the exhaustive per-routine lookup sheet. Nor is it
a specification of Raku; the reference implementation, Rakudo, and the Roast
test suite hold that role, and this book is careful to say *where Raku++
diverges from them* rather than pretending it does not.

It is also not a tour of every function. The source is the normative reference,
and it moves. What is written down here is the **shape** — the data structures
that everything else is built on, the invariants they rely on, and the reasons
a particular design was chosen over the obvious alternative.

## How to read it {-}

The nine parts are ordered the way a program flows through the system: source
text, then the tree, then the values, then execution, then the specialised
engines, then the back ends, then the boundaries with the outside world. Read
straight through and it is a narrative. Read a single part and it should still
stand on its own; cross-references say where the rest of a thread lives.

If you are here for one thing in particular:

| You want | Start at |
|---|---|
| the overall map | Chapter 1, then Chapter 25 |
| what kind of compiler this is | Chapter 3 |
| how source becomes a tree | Part II |
| what a runtime value *is* | Chapter 8 |
| how a Raku call actually happens | Chapters 14 to 16 |
| regexes and grammars | Part V |
| the native compiler | Part VII |
| Raku in a browser | Chapter 31 |
| installing modules, and the store zef shares | Chapter 33 |
| calling C, or being called from it | Chapters 35 and 36 |

## Conventions {-}

Source excerpts are tagged with the **file** they come from:

```cpp
// src/Value.h
static Value integer(long long x) { Value v; v.t = VT::Int; v.i = x; return v; }
```

They are lightly trimmed for the page — an ellipsis, a dropped comment, an
elided error string — but are otherwise verbatim. **File names, not line
numbers, are the anchors.** The code moves; grep for the function or for the
quoted line.

Raku code is shown as Raku:

```raku
my @primes = (2..*).grep(*.is-prime);
say @primes.head(5);          # (2 3 5 7 11)
```

Measured numbers carry their conditions. Unless a chapter says otherwise, they
were taken on arm64 macOS with Apple clang, using the project's benchmark
policy: interleaved A/B runs, a discarded warm-up, medians or minima of several
repetitions, and a *control* kernel that the change under test cannot affect. A
number without a control is an anecdote, and this book tries not to print
anecdotes.

Sections headed **What went wrong** are not decoration. Several of the designs
here are the second or third attempt, and the discarded attempts are usually
more instructive than the surviving one.

## A word about honesty {-}

Raku is a very large language, and Raku++ does not implement all of it. About
ninety per cent of the declared Roast suite passes. The method resolution order
is a depth-first walk rather than C3 linearisation. Multiple dispatch resolves
ties by declaration order instead of raising an ambiguity error. Modules publish
their whole environment to the global scope rather than only their exports.
Macros, `RakuAST`, and slangs are not implemented at all.

Every one of those is stated in the chapter where it belongs, under a heading
that says so. A book about compiler internals that only described the parts
that work would be a brochure.
