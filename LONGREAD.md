# Raku++: The Long Read

*How a from-scratch Raku compiler in C++ went from an empty directory to ~82% of
the official test suite — plus a native code generator, a self-hosting toolchain,
and a browser playground — in under three weeks.*

This is the long version of the story. The short version lives in the
[announcement](https://andrewshitov.com/2026/07/13/raku-the-fastest-raku-compiler/);
the disciplined version lives in [docs/dev/JOURNEY.md](docs/dev/JOURNEY.md), which
records the *method* rather than the narrative. This document is the narrative:
what happened, in what order, what each round cost and returned, and why the
numbers moved the way they did.

---

## Where it began

I have been following Raku since it was Perl 6, and more than once over the years
I tried to write a compiler for it. Every attempt stalled the same way. Raku is a
large language — you start with `say "Hello"` and within an evening you are
staring at grammars, junctions, multi-dispatch and the number tower, and you
quietly close the editor. The conclusion was always the same: this is too much
for one person.

What changed is not the language. It is that today we have a new kind of helper.
The whole of Raku++ was written without me typing a single line of its C++. I
described what I wanted, I ran the tests, I pointed at what was broken, and the
code appeared. The role a human plays in this is different from the old one — you
are a director and a reviewer, not a typist — but it is a real role, and the
project is the argument that it works.

The goal, from the first day, was simple and concrete: **a Raku compiler that is
fast and useful** — one that starts instantly and that you would actually reach
for. Not a research prototype, not a proof of concept. Something you run. Work
began roughly a week before the repository's first commit, which landed on
**2 July 2026**; the earliest commits are already a working tree-walking
interpreter rather than a first sketch.

There is a working rule underneath the whole project — **Rakudo is the reference,
not the source** — but it is worth being precise about how it came to be. It was
not a principle we declared on day one. It was simply what happened: we never
needed to open Rakudo's code, so we didn't. Only later did we understand *why*
that was the right way to work, and articulate it as a rule. We treat Rakudo as
the north star for *behaviour* — the answer to "is this really Raku?" — but we
never ported a structure or copied an algorithm from it. Raku++ is clean-room: a
hand-written lexer, a recursive-descent parser with a Pratt expression core, and a
tree-walking evaluator, all grown from nothing.

That independence is only possible because Raku has an *executable
specification*. The motto we kept returning to —

> Any compiler that can run Roast can be called a Raku compiler.

— means "correct" was never "what Rakudo's source does." It was two things we
could look at without ever reading Rakudo: **[Roast](https://github.com/Raku/roast)**,
the official test suite (~1,464 `.t` files of runnable spec), and
**[docs.raku.org](https://docs.raku.org)**, the prose. Between them they *are*
the language, described independently of any one runtime.

---

## The first passing tests, and the first numbers

The earliest work was the core MVP: enough of S02–S04 plus the `Test` module to
make Roast files *run at all*. A Roast file that emits `1..40` but scores `12/40`
is a shopping list — it tells you exactly which twenty-eight things Raku expects
next. A file that produces no output at all is usually one parse error away from
unlocking a whole cluster.

By **5 July** the thing was coherent enough to tag **v0.1.0**, and it fully passed
**252 of 1,464** Roast files. "Fully passes" is the strict bar: every single
assertion in the file must be green, or the file does not count. That number — a
few hundred whole files — was the baseline everything else is measured against.

Then the loop began in earnest, and it never really stopped:

- **6 July** — Raku 6.e features, hyperslices, `HyperWhatever`, dispatch and list
  correctness: 252 → 255.
- **7 July** — a real regex engine came online (recursive-descent parser, CPS
  backtracking matcher). This was the single biggest unlock in the project: it
  took the S05 synopsis from almost nothing to thousands of passing assertions.
  Module export scoping closed S19 to 100%; Unicode number literals closed the
  S15 literals section. Files: 275, then 276 (130,866 / 188,224 assertions).
- **8 July** — steady grind: 277, 278, 279, 280, each commit a handful of
  assertions, each diffed against the previous *set* of passing files, not just
  the count.
- **9 July** — S16 I/O work took it to 291; NativeCall (an `is native` C FFI
  through `dlsym`, no `libffi`) and book-gap fixes pushed to 300 (131,320 /
  189,081).

Around here we forced ourselves to be honest about what "coverage" means, and it
is worth dwelling on, because it is easy to get wrong.

---

## What the numbers actually mean

There are two entirely different questions hiding behind "how much of Raku does it
do," and conflating them flatters you.

**File coverage** — *how many whole Roast files pass every assertion* — is the
harsh one. One stray failure in a 200-assertion file zeroes the whole file. This
number sat around 17% early on and is ~30% now (**440 of 1,464 files**). It is a
coverage figure: how much of the suite is *completely* conquered.

**Per-test rate** — *of every individual test the suite declares, how many pass* —
is the fair one for "correctness on what runs." This is the headline: **~82%**,
or roughly **159,000 of ~194,000** declared tests.

The subtlety we documented in [docs/COUNTING.md](docs/COUNTING.md) is that the
denominator is not fixed. "Declared" means every test any file *tries* to run,
including files that abort before emitting a single result — we recover their
planned count from the source and count all of it as failing. The better the
compiler gets, the more files run far enough to *declare* more tests, so the
denominator **grows with coverage**. Our passing count reads as ~82% against our
own recovered denominator, but only ~77% against the suite's full declared total.
We chose to headline the number that is, if anything, slightly *un*flattering. In
the docs the rule is fixed: report raw numbers, quote both figures, never boast.

The per-test rate itself climbed in visible steps. On **9 July** the honest
all-declared figure was about 57%. Unicode collation (below) moved it to ~80.6%.
`sprintf` corner cases took it to 80.8%. The course and challenge rounds carried
it to ~82%.

---

## The technical spine

The rounds above skip over the machinery each one demanded. A few pieces were
disproportionately hard and disproportionately important.

**The number tower.** Raku promises that `0.1 + 0.2 == 0.3` is `True`, because its
decimals are exact rationals, not floats. That means a hand-rolled
arbitrary-precision `BigInt` (base 1e9, with a `long long` fast path) and an exact
`Rat` sitting underneath every arithmetic operation. A `Rat` whose denominator
overflows 64 bits degrades to `Num` the way Raku specifies — a subtlety that only
surfaced when a Mandelbrot render started producing slightly wrong pixels.

**Unicode, done properly.** This became the single strongest area of the whole
project. Grapheme-correct strings (UAX #29), the four normalization forms
(NFC/NFD/NFKC/NFKD), character names and properties — all generated from the
Unicode Character Database, upgraded to **UCD 17.0**. Then UCA collation from
DUCET 17.0: all **8,271** collation conformance tests pass. `.chars` counting
graphemes rather than codepoints is the kind of thing that is invisible until it
is wrong, and Raku is one of the few languages that insists on getting it right.

**The regex engine.** Not a wrapper around a library — a from-scratch
recursive-descent regex parser feeding a continuation-passing backtracking
matcher, in `src/Regex.{h,cpp}`. Grammars are built on top of it. Later it had to
grow *runtime interpolation* — lexical `:my` variables set mid-match and used
later as pattern atoms, code assertions evaluated against the live match — to
parse real-world YAML.

**From interpreter to compiler.** The project was always "interpret today, grow a
backend later," and the backend arrived. The key insight is a restraint: Raku++
deliberately does *not* implement the grammar-mutating parts of Raku — no custom
slangs, no parse-time operator definitions. That restraint has a payoff — if the
parse tree cannot change at runtime, it can be turned into C++ at build time. So
`rakupp` grew four ways to run a program:

```
rakupp program.raku                       # interpret (default)
rakupp --bundle program.raku -o program   # embed source + interpreter
rakupp --aot    program.raku -o program   # parse ahead, embed the AST
rakupp --exe    program.raku -o program   # transpile to C++, compile native
```

Getting `--exe` to real readiness was a refactor as much as a feature: moving
compiled subs off fixed C++ parameters onto a uniform `ValueList` calling
convention, so named parameters, optionals, defaults, slurpies, and `multi`
dispatch all compile natively. The compiler is validated not against Roast but by
*parity*: compile a program, run it, run the same program under the interpreter,
assert identical output. The interpreter is the oracle for the compiler. An `-O`
flag forwards optimization down to the generated binary.

**Self-hosting.** A milestone that felt better than its size warranted: the
harness that runs Roast, `tools/run-roast.raku`, is itself written in Raku and
executed *by rakupp*. The tool that measures the compiler runs on the compiler.

---

## Beyond Roast: the parts a spec suite never tests

By mid-July, Roast was giving diminishing returns per hour — not because the
compiler was done, but because Roast tests the language *in the small*. It isolates
features. Real programs exercise a dozen modules resolving each other, a database
driver, industrial-volume string munging — things a spec suite simply does not
reach. So we opened a second front: run real Raku software and fix whatever breaks.

**[covid.observer](https://github.com/ash/covid.observer)** — a substantial Raku
web-stats generator — was the first. Getting it to *compile* forced heredocs,
quote-aware regex lexing, literal `multi` parameters, hash-vs-block
disambiguation. Getting it to *run* against a live MySQL database (through a small
pure-Raku shim that shells out to the `mysql` client) forced real module loading,
`use lib`, feed operators, hyper method calls, and enough of the object model to
hold a dozen `CovidObserver::*` modules at once. It now runs end-to-end and writes
real HTML.

**[The Complete Course of the Raku Programming Language](https://course.raku.org/)**
— the book-length course, ~1,500 pages — was the second, and it went much deeper.
Its site generator is written in Raku, and its table of contents is read through
`YAMLish`, an indentation-sensitive YAML grammar that exercises nearly every
advanced regex feature at once. Making that grammar parse is what drove the
runtime-interpolating matcher mentioned above.

The lesson of this front is that a feature can be "done" by Roast's lights and
still be quietly wrong in a way only a real program reveals. Numeric context on an
array, nested hash access, variable pair-keys — all "passing" in Roast while
producing wrong output in real code.

---

## The course snippets: 3,068 tiny programs

Then a sharper idea. The course does not just *have* a generator — it is *made of*
Raku. Every one of its pages is full of fenced code blocks, each a small, complete,
idiomatic Raku program that a human wrote to teach something. That is a test
corpus of a kind Roast is not: idiomatic rather than minimal.

So we extracted every fenced block from the English pages plus the exercise files
and ran each one under *both* engines — rakupp and Rakudo — with stdin closed and
a timeout, and diffed the output. **3,068 comparisons.** Blocks that don't run
under Rakudo (theory fragments, output samples) were discarded; so were
nondeterministic ones. What remained were **148 real divergences** where rakupp
and Rakudo genuinely disagreed.

We fixed them in two rounds — containers and binding, list/Seq typing, associative
gists, junction gists, numeric coercions, quoting adverbs, regex and grammar
corners — and drove the number of genuine divergences from **148 down to 14**. The
full ledger is in
[docs/dev/COURSE-DIVERGENCES.md](docs/dev/COURSE-DIVERGENCES.md). Every one of
those was a bug that neither Roast nor the two big projects had caught, because
nobody had written *that* idiom before in a form we tested.

---

## The Weekly Challenge: 10,428 more programs

If the course is a corpus, [The Weekly Challenge](https://theweeklychallenge.org)
(PWC/TWC) is a firehose. Years of participants' solutions — thousands of small,
real, wildly varied Raku programs, written by many hands with many styles. We ran
**10,428 solutions** through the same both-engines-and-diff sweep.

Only about 6,800 of those are actually comparable — the rest are skipped because
Rakudo itself cannot run them headlessly (missing arguments, modules, or input),
times out, or is nondeterministic. Of the comparable set, the first pass found
**2,663** — 39% — producing byte-identical output to Rakudo. Then fifteen fix
batches, each targeting a cluster the diff exposed:

- MAIN semantics, dispatch constraints, parse-tail handling → 3,295 identical.
- method-gap sweep, `Any` single-item semantics.
- subsets, literal returns, the `ff` flip-flop, post-GLR `map`.
- index positions, IO strictness, `tr///` returning a `StrDistance`.
- parse clusters, iteration semantics, multi-slices, op-name calls, modifier
  scoping, `min`/`max` flattening, `is rw` loop parameters, rotor pairs.
- stacked zip/cross metaops, element-read itemization, hyper postfixes
  (`@w»[0]`, `»++`), string-as-one-item indexing.
- MAIN strictness the whole way down: usage-on-bind-failure exit codes, a
  user `sub USAGE`, CLI allomorphs, and `$*USAGE` byte-identical to Rakudo's
  generated usage text.

("Post-GLR" refers to the **Great List Refactor**, the 2015 redesign of how Raku
lists, arrays, and sequences flatten and containerize. Its most visible rule is
that `map` and friends keep each block's result as a *single* element — only an
explicit `Slip` splices into the surrounding list — and that bare comma-lists are
immutable `List`s while `@`-sigil variables are mutable `Array`s. Matching those
semantics exactly was a recurring theme across the list-typing fixes here and in
the course round.)

The identical count climbed to **4,056 — 60% of the comparable programs** — and is
still moving. Every batch passes a zero-regression Roast gate before it counts, so
the two fronts reinforce each other: the Roast standing rose from 433 to 440 fully
passing files across these same batches. Each batch is a progression row in
[docs/dev/PWC-DIVERGENCES.md](docs/dev/PWC-DIVERGENCES.md).

What the ledger surfaced late is a leverage insight worth keeping: the remaining
mismatches are not evenly spread. Six prolific authors account for about half of
them, because each reuses one personal template across hundreds of solutions —
so fixing one recurring shape corpus-wide clears files by the hundred, not one at
a time. That is the shape of the work now.

The pattern across all three fronts — Roast, real projects, and the two corpora —
is that each has a blind spot the others cover. Roast misses what isn't in the
passing set. Real projects miss what they happen not to use. The corpora find the
idioms nobody isolated. Running all of them, and trusting whichever is currently
pointing at a problem, is what kept the work honest.

---

## Why 100% is the goal, and why it is hard

The goal is 100% of Roast. It is the right goal because Roast *is* the definition —
passing all of it is exactly what it means to be a complete Raku. And it is hard
for reasons that are structural, not incidental:

1. **The denominator grows as you climb.** As shown above, getting better makes
   more files run far enough to declare more tests, so the target you are chasing
   moves away from you. The last stretch is uphill in a way the first was not.

2. **The long tail is the grammar-mutating core we deliberately deferred** — custom
   operators defined at parse time, slangs, runtime grammar edits. These are the
   features that make `--exe` possible *by their absence*, and re-introducing them
   without giving up ahead-of-time compilation is a genuine design problem, not a
   day's work.

3. **Some failures are correctness fixes that lower the count.** More than once the
   number moved the *wrong* way for the *right* reason: a fix exposed a test that
   had been passing by accident. The honest move is to keep the fix and
   re-baseline, which means the graph is not monotonic.

Where the frontier sits today is grammars in the large: turning a parsed YAML match
tree into data through action methods and a second schema grammar — a fresh set of
small gaps, peeled one at a time, which is how everything else here got built.

---

## Running it in production

Somewhere in the middle of all this, the compiler stopped being a thing we tested
and became a thing we *used*.

I have been proofreading the course — a separate story — which means running its
generator again and again, hundreds of times. The generator is Raku. So it runs on
rakupp, in production, regenerating the real site. And this is where the
performance work stopped being an abstraction. A VM-backed engine takes a good
fraction of a second to spin up its runtime; rakupp starts in a few
milliseconds. On a single run that is nothing. On the two-hundredth run of an
edit-regenerate-look loop, it is the difference between a tool that interrupts
your thinking and one that doesn't.

One piece of that pipeline deserves its own mention. The course renders its code
blocks with Pygments, a Python highlighter that, like almost all highlighters,
works by lexing — it matches words against patterns. That is fine until a class
has a method called `role`, at which point Pygments paints `role` as a keyword,
because lexically it cannot tell a method name from a language keyword. But rakupp
*parses*, so it knows the difference structurally. `rakupp --highlight` emits the
exact same CSS classes Pygments does — the course's stylesheet works unchanged —
but assigns them correctly, and does it in ~13 ms where the Python tool took ~110.
It is a drop-in replacement that is simply more right, and it removes a Python
dependency from a project whose whole premise is depending on no one else's
implementation.

---

## Then: what if it ran in the browser?

The idea arrived the way the good ones do — obvious in hindsight. The course
teaches Raku. It is full of runnable examples. What if the reader could *run them*,
right there on the page, with no server and no round-trip?

The interpreter is portable C++ with no dependencies. That is exactly the shape of
thing that compiles to **WebAssembly**. So `rakujs/` builds the *same* `src/`
interpreter — not a reimplementation, the identical C++ — with Emscripten, into a
`.wasm` module that runs Raku entirely in the browser. Semantics are identical to
native `rakupp` and therefore to what Roast validates, because it *is* rakupp.
Nothing in `src/` was modified; the WASM build is purely additive — a thin entry
point exporting `rakupp_run(src)`, a build script, and a self-contained editor.

It has its own constraints. Emscripten's `-fwasm-exceptions` is still uneven across
browsers, so the build ships classic `-fexceptions`; the interpreter leans on C++
exceptions for control flow (every `return`, `next`, `last`), so this matters. The
browser stack is shallower than a native one, so deep recursion is capped around a
couple hundred frames. The WASM runs in a Web Worker so the UI stays responsive —
a live spinner, streaming output, a working Stop button — and the whole thing is
built at `-Oz` for size.

The result is the playground, live at
**[course.raku.org/playground](https://course.raku.org/playground/)**: an editor
with the example programs, syntax highlighting, a theme switcher shared with the
course, and Raku running in the tab. A from-scratch Raku, written in C++ in under
three weeks, executing in a browser with no server behind it.

---

## A nostalgic note

Among the two dozen programs in [examples/](examples/) is `mandel.raku` — the
Mandelbrot set rendered in ASCII, the same demo that shipped with Parrot two
decades ago. Back then a fractal crawling down the terminal was the thing you
showed people to prove a new language was real.

I put it back in, and the first run was slow — noticeably slower than Rakudo.
That was useful: it pointed straight at a real engine problem. Every arithmetic
operation on an exact `Rat` was re-reducing the fraction (a GCD normalization) even
when nothing needed it, so a program doing millions of `Rat` operations paid for it
millions of times. Removing the redundant re-reduction — and letting a `Rat` whose
denominator grows past 64 bits degrade to `Num` the way Raku specifies — turned the
render fast, and sped up everything else that leans on rationals at the same time.
Same maths, same fractal, now in a blink. That — more than any percentage in any
table — is what made this project worth doing. It brings back the taste of a fast computer that answers you the
instant you press Enter, the way computers felt when they were small and programs
were instant and start-up time was a concept you never had to think about.

---

## The method, one more time

None of this came from a wishlist. It came from a loop that has not changed since
the first week:

> Find the failing thing — in Roast, in a real program, in the docs, in a corpus.
> Understand what Raku actually *means*, from the spec and the prose, never from
> Rakudo's insides. Make the smallest change that is *right*, not merely one that
> turns a test green. Run the whole suite and diff the set. Keep the fixes that are
> correct even when the count dips. Write down what was non-obvious.

Run that loop patiently enough, with a helper that never tires of it, and you find
out how far a clean, dependency-free C++ implementation of Raku can get on its own.

The answer, so far, is: further than I would have believed three weeks ago.

*Sources, releases, and full documentation:
[github.com/ash/rakupp](https://github.com/ash/rakupp).*
