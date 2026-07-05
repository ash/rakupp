# Building a Raku Compiler — The Journey

This is a memoir of how Raku++ (`rakupp`) came to be: not a changelog, but an
account of the *method* — the loops we ran, the sources we leaned on, and the
principles we held to while growing a from-scratch Raku implementation in C++.

Raku is a large, expressive language, and for most of its life it has had exactly
one mature implementation: Rakudo, on the MoarVM/JVM backends. The question that
started this project was deliberately naïve:

> How far can a clean, dependency-free C++ implementation of Raku get on its own?

Everything below is the story of finding out.

---

## The one principle: Rakudo is the reference, never the source

We set a rule on day one and never broke it: **Rakudo is our reference
implementation, but we do not read its source, and we are not rebuilding it.**

This matters, and it is worth being explicit about. Rakudo is a real Raku
compiler — the standard against which "does this behave like Raku?" is answered.
So we treat it as the north star for *behaviour*. But we deliberately never
opened its code, never ported a data structure, never copied an algorithm. Raku++
is a clean-room implementation: a hand-written lexer, a recursive-descent parser
with a Pratt expression core, a tree-walking evaluator, and — later — a native
code generator, all written from nothing.

The reason this is even possible is that Raku's *specification is executable*. The
motto we kept coming back to:

> **Any compiler that can run Roast can be officially called a Raku compiler.**

So the definition of "correct" was never "what does Rakudo's source do." It was
two things we *could* look at without looking at Rakudo:

1. **[Roast](https://github.com/Raku/roast)** — the official Raku test suite,
   ~1,464 `.t` files of executable specification.
2. **[docs.raku.org](https://docs.raku.org)** — the prose language documentation.

Between them, they *are* Raku, described independently of any one runtime. That
gave us a way to target the language rather than a particular implementation of
it — and to know, at every step, whether we were right.

---

## Three feedback loops

Features never came from a wishlist. They came from one of three loops, and the
interesting thing — the thing that made progress steady rather than random — is
that the three loops surface *different* gaps.

### Loop 1 — Roast: the specification as a test suite

The primary loop. Run the suite, bucket every file into *fully-pass / partial /
no-TAP / timeout*, and go hunting. Roast is unusually good at showing you the
cheap wins: a file that emits `1..40` but scores `12/40` is telling you exactly
which forty things Raku expects and which twenty-eight you already do. A file that
produces no TAP at all is usually one parse error or one unimplemented construct
away from unlocking a whole cluster.

This loop drove the backbone of the language, roughly in this order:

- the **core MVP** — S02–S04 constructs plus the `Test` module — enough to make
  Roast files *run at all*;
- the **number tower** — a hand-rolled arbitrary-precision `BigInt` (base 1e9,
  with a `long long` fast path) and an exact `Rat`, so `0.1 + 0.2 == 0.3` is
  `True` the way Raku promises;
- **Unicode** — grapheme-correct `.chars` (UAX #29), NFC/NFD/NFKC/NFKD
  normalization, character names, properties — all generated from UCD 15.1. This
  became, and remains, the strongest single area (~95% of the S15 assertions);
- **operators and metaoperators** — reductions `[+]`, zip/cross `Z`/`X`, hypers,
  junctions, set operators (including the Unicode spellings `∪ ∩ ∈`);
- a **real regex engine** (`src/Regex.{h,cpp}`) — a recursive-descent parser and a
  CPS backtracking matcher — which was the single biggest unlock, taking S05 from
  nearly nothing to thousands of passing assertions;
- **grammars** on top of that engine, then **multi-dispatch**, **Whatever-currying**,
  **phasers**, **exceptions**, and the long tail of everything else.

A discipline emerged here that we kept: after every change, run the *whole* suite
in the background and diff the sorted list of fully-passing files. Not "the count
went up" — the actual set. That is how you catch the fix that gains three files
and silently breaks one. More than once the count moved the *wrong* way for the
*right* reason: a correctness fix exposed a test that had been passing by
accident, and the honest move was to keep the fix and re-baseline.

### Loop 2 — real projects: the parts Roast never tests

Roast tests the language in the small. Real programs test it in the large — and
they exercise things a spec suite simply doesn't: a dozen modules resolving each
other, a database driver, file I/O, a `MAIN` with subcommand dispatch, string
munging at industrial volume.

Two real projects drove enormous, pervasive hardening:

- **[covid.observer](https://github.com/ash/covid.observer)** — a substantial Raku
  web-stats generator. Getting it to *compile* forced heredocs, quote-aware regex
  lexing, literal `multi` parameters, hash-vs-block disambiguation, and much more.
  Getting it to *run* against a live MySQL database (through a small pure-Raku
  `DBIish` shim that shells out to the `mysql` client) forced real module loading,
  `use lib`, feed operators, hyper method calls, and enough of the object model to
  hold a dozen `CovidObserver::*` modules at once. It now runs end-to-end and
  writes real HTML pages.

- **the Raku course generator** — which reads its table of contents through the
  YAML parser `YAMLish`. This one is still open (see the frontier, below), but it
  pushed the grammar engine further than any Roast test ever did: indentation-
  sensitive parsing with runtime lexical variables inside regexes.

The lesson of Loop 2 is that a feature can be "done" by Roast's lights and still
be quietly wrong in a way only a real program reveals. Numeric context on an
array, nested hash access, variable pair-keys — these were all "passing" in Roast
while producing wrong output in real code. Real projects find those.

### Loop 3 — the documentation: conformance, not just coverage

The third loop is the most deliberate. Roast tells you *whether* something works;
it does not tell you whether it works *the way the language actually defines it*.
For that we walked [docs.raku.org](https://docs.raku.org) section by section
against our own feature list, and asked of each entry: does our behaviour match
the *intended semantics*, or merely a plausible approximation?

This audit found things the tests had missed — because the tests that would have
caught them weren't in the passing set yet, or because the behaviour was
technically "not wrong" but not right either. A sampling of what it turned up and
fixed: `.raku` producing gists instead of round-trippable representations; `^^`
returning a `Bool` instead of the operand; `~~` against a `Callable` never
invoking it; `when /regex/` falling through; `proceed`/`succeed` being no-ops;
read-only accessors silently accepting writes; enums that were plain integers with
no type identity; `*@`/`**@`/`+@` slurpy semantics all collapsed into one. The
single highest-impact find of the whole audit — a `CATCH { default {…} }` block
that never ran inside a sub, a `try`, or a pointy block — came straight out of
reading the exceptions documentation and asking "does ours do exactly this?"

Each of the three loops has a blind spot that the other two cover. Roast misses
what isn't in the passing set. Real projects miss what they happen not to use.
Docs miss what they under-specify. Running all three, and trusting the one that's
currently pointing at a problem, is what kept the work honest.

---

## Self-hosting the harness

A small milestone that felt disproportionately good: the test harness that runs
Roast is itself written in Raku (`tools/run-roast.raku`) and executed *by rakupp*.
The tool that measures the compiler runs on the compiler. It reimplements the
original Python harness byte-for-byte in output, and getting there required its
own set of features — process spawning with timeouts, directory listing, argument
handling — which then paid for themselves across the rest of the suite. There is
a particular satisfaction in a system that can be turned to measure itself.

---

## A highlighter that reads the language

A late addition made a point the whole project had been circling. The course
renders its Raku code blocks with Pygments — a Python syntax highlighter that,
like almost every highlighter, works by *lexing*: it matches words against
patterns and colours them. That is enough until it isn't. In a class with a
method called `role`, Pygments paints `role` as the `role` keyword — because
lexically it cannot tell a method name from a language keyword. The word is the
same; only the *structure* distinguishes them, and a lexer has no structure.

But rakupp already knows the structure — telling a method call from a keyword is
exactly what a parser does. So `rakupp --highlight` emits the same CSS classes
Pygments does (the course's stylesheet works unchanged) but assigns them with the
compiler's own knowledge: a name after `.` or `method` is a method, never a
keyword. It is a drop-in — same input on stdin, same class-based HTML out — that
is simply *more correct*, and as a bonus it removes a Python dependency from a
project whose whole premise is not depending on anyone else's implementation.

There is a discipline worth naming here too. The highlighter is a *separate*,
lossless scanner, not the parser proper — because highlighting has a requirement
the parser doesn't: it must never fail. Course snippets include deliberate
fragments (`{ . . . }` placeholders) that are not valid programs; a highlighter
that threw on them would be useless. So it reuses the parser's *knowledge* — the
keyword set, the sigil rules, what counts as a builtin — without inheriting its
intolerance for malformed input. Every byte of the input reappears in the output,
coloured or not, a property checked by round-tripping hundreds of real course
blocks. That is what makes it safe to swap in — and it does the job in ~13 ms of
startup where the Python tool took ~110 ms.

---

## Reaching `--exe`: from interpreter to compiler

The project was always "a tree-walking interpreter today, designed to grow a
compiler backend later." The backend arrived, and the path to it is instructive.

The key observation is that Raku++ deliberately does *not* implement the
grammar-mutating parts of Raku — no custom slangs, no parse-time operator
definitions, no runtime grammar edits. That restraint has a payoff: the subset of
the language we *do* handle is static enough to compile ahead of time. If the
parse tree can't change at runtime, it can be turned into C++ at build time.

So beyond interpreting, `rakupp` grew four ways to run a program:

```
rakupp program.raku                     # 1. interpret (default)
rakupp --bundle program.raku -o program # 2. bundle: embed source + interpreter
rakupp --aot    program.raku -o program # 3. AOT: parse ahead, embed the AST
rakupp --exe    program.raku -o program # 4. native: transpile to C++ and compile
```

Getting `--exe` to real readiness was a refactor as much as a feature. The pivot
was moving compiled subs and methods off fixed positional C++ parameters and onto
a uniform `ValueList` calling convention, with per-parameter binding emitted from
the signature — which is what let named parameters, optionals, defaults, slurpies,
and `multi` dispatch all compile natively rather than only interpret.

The validation method for the compiler is different from the language itself, and
worth stating: `--exe` is validated by **parity**, not by Roast. Compile a program
and run it; run the same program under the interpreter; assert the outputs match.
The interpreter is the oracle for the compiler. That parity sweep is what surfaced
the last mismatches — a couple of codegen-only gaps that the interpreter handled —
and confirmed that the native path and the interpreted path agree.

The point of `--exe` is not raw speed benchmarks. It is that the same clean-room
engine that reads Raku can also *emit* a standalone native executable for the
statically-analysable core of the language — starting fast, with no runtime
dependency — which is a different and useful shape for a Raku program to take.

---

## Where the frontier is now

At the time of writing, Raku++ fully passes **251 of 1,464** Roast files (~17%),
with **119,831 / 164,239** reached assertions passing. Those numbers are a
coverage figure and a correctness-on-what-runs figure respectively; they measure
different things and are quoted for different purposes (see
[ROAST.md](ROAST.md)).

The live frontier is grammars in the large. The course generator reads its TOC
through `YAMLish`, an indentation-sensitive YAML grammar that exercises nearly
every advanced regex feature at once: lexical `:my` variables set mid-match and
used later as pattern atoms, code assertions evaluated against the live match,
runtime quantifier bounds, runtime subrule arguments. Making that grammar *parse*
required building a runtime-interpolating matcher — a genuine engine feature — and
it now parses real YAML: scalars, maps, multi-line maps, documents. The remaining
work is the layer *after* parsing: turning the match tree into data through the
module's action and `concretize` methods, where the tail runs through the object
model and a second schema grammar. That is a fresh set of gaps, each small, being
peeled one at a time — which is exactly how everything else here got built.

The method has not changed since the first week. Find the failing thing — in
Roast, in a real program, or in the docs. Understand what Raku actually means, from
the spec and the prose, never from Rakudo's insides. Make the smallest change that
is *right*, not merely one that turns the test green. Run the whole suite and diff
the set. Keep the fixes that are correct even when the count dips. Write down what
was non-obvious.

That loop, run patiently enough, is how you build a language.
