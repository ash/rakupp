# Glossary

Two kinds of word are collected here. Most are the ordinary vocabulary of
compilers — *lexer*, *AST*, *packrat*, *SSA* — which would mean the same in a
book about any other language implementation. The rest are this project's own,
or Raku's: *fat struct*, *decided-once field*, *twigil*, *slip*. Both are
listed together and alphabetically, because when you meet an unfamiliar word in
a chapter you do not yet know which kind it is.

Each entry says what the word means in general, then, where it applies, what it
means *here* — the two are not always the same, and the difference is usually
the interesting part. A chapter reference points at the long version.

## If you have never built one before {-}

Eight terms carry most of the rest, and they are easiest read in the order a
program moves through them.

A compiler is handed **source** — text. The **lexer** reads it character by
character and produces **tokens**: the words of the language, so that the next
stage can think about `my`, `$x`, `=` and `42` rather than about letters. The
**parser** reads that flat run of tokens and works out what groups with what,
producing an **abstract syntax tree** — one node per construct, nested the way
the program means rather than the way it was typed.

What happens to the tree is where implementations differ. Most compilers lower
it into an **intermediate representation**, optimise that, and emit machine
code or **bytecode** for a virtual machine to run. Raku++ does none of that in
three of its four modes: an **interpreter** walks the tree directly, evaluating
each node by visiting its children, which is called a **tree-walking
interpreter**. The fourth mode walks the same tree and writes C++ instead.

Everything else in this glossary hangs off those eight.

\glossletter{A}

**ABI** (application binary interface) — the machine-level contract two
separately compiled pieces of code agree on: how arguments are passed in
registers and on the stack, how a struct is laid out, what a symbol is called.
An API says what you write; an ABI says what the bytes must be. Raku++ defines
one of its own for extension modules (Chapter 36) and restates libffi's by hand
in order to call C (Chapter 35).

**A/B interleaving** — the benchmark discipline of alternating the two versions
under test within a single run, rather than measuring one and then the other,
so that any drift in the machine hits both equally (Chapter 39).

**Abstract syntax tree (AST)** — the tree a parser builds: one node per
construct, with the punctuation that guided the parse discarded. `2 + 3 * 4`
becomes an addition whose right child is a multiplication, so the precedence
now lives in the shape rather than in the text. In Raku++ the AST is the *only*
representation of a program — there is no bytecode or IR beneath it — so it is
both what the interpreter walks and what the C++ emitter reads (Chapter 7).

**Adaptive grammar** — a grammar that the program being parsed can change while
it is being parsed. Declaring `sub infix:<∘>` adds an operator, and tokens
after that declaration parse differently from tokens before it. No fixed parse
table can be built for such a language, which is why every language with
user-declared operators — Raku, Haskell, Prolog, Agda — has a top-down front
end (Chapters 3 and 6).

**Adverb** — Raku's named modifier, written `:name` or `:name(value)`: `:i` on
a regex, `:kv` on a hash lookup. To the parser it is a colon-pair in a
particular position; to the callee it is a named argument.

**Allomorph** — a value that is simultaneously a number and its own string,
such as `IntStr`. Represented as a numeric tag with the original text kept
alongside it (Chapter 8).

**AOT (ahead-of-time)** — compiling before the program runs, as opposed to a
JIT, which compiles while it runs. The `--aot` mode does the *parse* ahead of
time and emits C++ that rebuilds the AST at startup; execution is still a tree
walk (Chapter 25).

**Arena** — a region of memory whose contents are all released together when
the region dies, so nothing inside needs its own free. Everything an extension
creates is allocated in the arena belonging to the call, which is what makes
the extension ABI difficult to leak through (Chapter 36).

**Arity** — how many arguments a routine takes. A *direct-arity call* in the
C++ emitter is one whose count is known at emit time, so the generic
argument-list machinery can be skipped (Chapter 27).

**Associativity** — which way a run of equal-precedence operators groups.
`a - b - c` is left-associative and means `(a - b) - c`; `a ** b ** c` is
right-associative. Raku lets a declared operator state its own, and the
precedence-climbing loop reads it out of the operator table (Chapters 5 and 6).

**Autothreading** — running an operation once per value inside a junction and
recombining the results, so that `1|2 == 2` is true (Chapter 18).

\glossletter{B}

**Back end** — everything after the tree: the half of a compiler that turns an
analysed program into something that runs. Three of Raku++'s four modes have
almost no back end, because the tree *is* the program; only `--exe` has one,
and what it emits is C++ (Chapters 3 and 26).

**Backtracking** — trying an alternative, and on failure rewinding to where you
started and trying the next. The regex matcher does it constantly and by design
(Chapter 21); the parser does it at exactly four places in seven and a half
thousand lines, each a short speculative probe that restores its position on
failure (Chapter 5). Same word, unrelated machinery.

**Binding** (`:=`) — making a name refer to the same *container* another name
refers to, instead of copying a value into a container of its own. Assignment
writes through a container; binding replaces it (Chapter 12).

**Binding power** — the number a Pratt parser attaches to an operator to decide
whether it claims the expression on its left or yields it. Sixteen named ones
cover Raku's two dozen precedence levels here (Chapter 5).

**Bottom-up parsing** — building the tree from the leaves toward the root,
recognising a construct only once its final token has been read. LR and its
relatives are bottom-up. Raku++ is not (Chapter 3).

**Bundle** — the `--bundle` mode: the source bytes are embedded in a standalone
binary that lexes, parses and interprets them at startup (Chapter 25).

**Bytecode** — a compact instruction set invented for one language and executed
by a loop that switches on an opcode. CPython, the JVM and MoarVM all have one.
Raku++ deliberately has none; Chapter 13 says what the tree walk costs instead,
and Chapter 41 reports the measurement behind the decision.

**Byteset** — a 256-bit bitmap cached on a regex character-class node, which
answers "does this byte match?" without re-deriving the class (Chapter 20).

\glossletter{C}

**C3 linearisation** — the standard algorithm for ordering a class's ancestors
when it has more than one parent, and the order Raku specifies. Raku++ walks
parents depth-first instead, which differs in the diamond cases (Chapter 17).

**Callable** — anything invokable: a sub, a method, a block, a closure. Here it
is one value shape carrying a cached view of its own signature (Chapter 14).

**Calling convention** — the protocol for making a call: where the arguments
go, who cleans up, what a return value looks like. Raku++'s is a `ValueList`
in and a `Value` out, and its cost is the largest single line item in a
compiled call (Chapter 28).

**Capture** — two senses, kept apart by context. In a regex, a parenthesised
group whose matched text is kept (Chapter 21). In Raku, the entire argument
list of a call reified as an object (Chapter 14).

**Closure** — a function together with the environment it was written in, so it
keeps working after that scope has been left. `my $n = 0; my &c = { $n++ }`
yields one. Chapter 12 is the environment it closes over; Chapter 26 is what a
closure has to become in emitted C++.

**Constant folding** — computing at compile time whatever the constants already
determine, so `2 + 3` is emitted as `5`. Measured here and found to be worth
almost nothing on real Raku, which is why the node work went elsewhere
(Chapters 7 and 19).

**Container** — in Raku, the box a variable names, as distinct from the value
inside it. `$x = 5` writes into `$x`'s Scalar container; `$x := $y` makes the
name refer to a different container altogether. Most of what assignment means
in Raku is a fact about containers (Chapter 12).

**Context-sensitivity** — when the same characters mean different things
depending on what came before them. A bare `/` opens a regex where a term is
expected and divides where an operator is expected. Raku++ settles this inside
the lexer, from the lexer's own one-token history (Chapters 3 and 4).

**Control kernel** — in a benchmark, a workload that the change under test
cannot possibly affect, run alongside the one it should. If the control moves
too, the machine moved and not the code (Chapter 39).

**Cooperative control flow** — implementing `return`, `next`, `last` and `when`
with a flag and a frame counter rather than a C++ exception, in the common case
where no callable boundary was crossed (Chapter 15).

**Copy-on-write (COW)** — letting several values share one buffer and
duplicating it only when one of them is written to. `CowStr` does this for
strings; Chapter 9 also records why the C++ standard library gave the technique
up for `std::string`.

**Coroutine** — a routine that can suspend in the middle, hand a value out, and
later resume where it stopped. `gather`/`take` is the Raku construct that wants
one; Chapter 18 shows how it is built without one, and where the substitute
diverges from the real thing.

\glossletter{D}

**Dead code** — code that can never run. In this book it usually means a branch
compiled into the binary that a particular program can never reach, which is
what `--slim` exists to cut (Chapters 7 and 29).

**Decided-once field** — a mutable field on an AST node holding a fact about
the *syntax*, computed on first evaluation and never recomputed. It may never
hold a value that can change (Chapter 19).

**Declarative prefix** — the leading part of a regex alternative made only of
literals, character classes and quantifiers, up to the first construct that
needs code to run. It is what longest-token matching ranks by (Chapter 23).

**Desugaring** — rewriting a convenient surface form into the smaller construct
it stands for, so that the rest of the compiler only ever sees the smaller one.
Raku++ does very little of it: the tree keeps the shape the programmer wrote,
because four run modes read the same tree.

**DFA (deterministic finite automaton)** — a matcher with a single current
state and no choices to make, so it reads each input character exactly once and
never backtracks. Fast, but incapable of expressing captures, embedded code or
recursion — which is why Raku's regexes cannot be compiled into one (Chapter 3).

**Dispatch** — deciding which piece of code a call actually runs. Raku layers
several kinds: name lookup, multiple dispatch on the argument types, method
dispatch through the class hierarchy, and re-dispatch with `callsame`
(Chapter 16).

**Dragon Book** — *Compilers: Principles, Techniques, and Tools*, the standard
text. "A compiler in the Dragon-Book sense", in Chapter 3, means one with the
classical pipeline of IR, optimisation passes and code generation — which three
of the four modes here are not.

**Dynamic scope** — a name resolved by walking the *call* stack rather than the
enclosing text, so what it means depends on who called you. Raku spells it with
the `*` twigil: `$*OUT`. Contrast lexical scope (Chapter 12).

\glossletter{E}

**Eigenstate** — one of the values inside a junction (Chapter 18).

**Emscripten** — the toolchain that compiles C and C++ to WebAssembly. Its
`-fexceptions` mode routes C++ throws through JavaScript, which is what bounds
recursion depth in the browser build (Chapter 31).

**Environment** — the run-time structure holding one scope's variables, with a
pointer to its parent; resolving a name means walking that chain. Raku++ has no
separate symbol table — the environment is it (Chapter 12).

**Epsilon transition** — an edge in an automaton that consumes no input. The
longest-token automaton treats certain regex constructs as one, on the grounds
that the real matcher will enforce them later (Chapter 23).

\glossletter{F}

**Fat struct** — the `Value` design: a single struct carrying a type tag and a
field for every kind of payload, several of which may be live at once, rather
than a class hierarchy or a variant. Chapter 8 is the entire argument.

**FFI (foreign function interface)** — the machinery for calling a function
written in another language, here C. Raku spells it `is native`; Raku++
implements it by loading libffi at run time rather than linking against it
(Chapter 35).

**Flip-flop** — Raku's `ff`/`fff` operator, which is off until its left side is
true and then stays on until its right side is. It needs one bit of state per
occurrence in the source, which is why there is a map keyed on the AST node
(Chapter 18).

**Free list** — a chain of released blocks of one size, kept for reuse instead
of being handed back to the allocator. There is one per capacity for small
argument lists, and the frame pool is the same idea for scopes (Chapter 12).

**Front end** — everything before the tree: lexing, parsing, and whatever
analysis happens on the way through. Raku++'s is a hand-written
recursive-descent parser with a Pratt expression core (Chapter 3).

\glossletter{G}

**Garbage collection** — reclaiming memory automatically by tracing what is
still reachable from the roots. Raku++ has none: lifetime is `shared_ptr`
reference counting, which is simpler, is deterministic, and leaks reference
cycles (Chapter 1).

**Gate** — a check that a release must pass, run automatically rather than
eyeballed. The book uses the word for the correctness gates (Roast, the example
suite, compiler agreement between the run modes) and for the performance gate,
which fails a build on a regression against a recorded baseline (Chapters 1
and 39).

**GIL (global interpreter lock)** — a single lock that only one thread may hold
while touching interpreter state. Raku++'s is engaged lazily, on first
concurrent use, and can be switched off (Chapter 37).

**GLR, Earley** — parsing algorithms that pursue every possible reading at once
and produce a parse forest, leaving ambiguity to be resolved afterwards.
Raku++ produces one tree and settles ambiguity where it meets it (Chapter 3).

**Grammar** — in theory, the rules defining which strings a language admits. In
Raku, also a language feature: a class whose methods are regexes, usable as a
parser (Chapter 22). The book uses both senses; context separates them.

**Grapheme** — what a reader would call a character, which may be several
Unicode code points (`e` followed by a combining acute). Raku indexes strings
by grapheme; Raku++ stores them as UTF-8 bytes, and Chapter 24 is largely about
the gap between the two.

**Greedy, possessive** — a greedy quantifier takes as much as it can and hands
characters back under backtracking; a possessive one takes as much as it can
and hands nothing back. Raku's `token` and `rule` make their quantifiers
possessive — see *ratchet* (Chapter 20).

\glossletter{H}

**Hand-written** — of a lexer or parser: written directly as code, rather than
generated from a grammar file by a tool. The alternative is a **parser
generator** — yacc, bison, ANTLR, CPython's pegen — which reads a grammar and
emits state tables. Raku++'s lexer and parser are hand-written C++: there is no
grammar file, no generated table and no generator in the build. This is not a
shortcut small projects take because they must; Clang and GCC are hand-written
too, for the two standard reasons — a generated parser is hard to give good
error messages, and awkward to make context-sensitive. The price is that every
rule is code somebody maintains. The return, here, is a parser that can change
its own operator table in the middle of a parse and point an error at an exact
token (Chapter 3).

**Handle** — an opaque `RkValue` in the extension ABI: a token the extension
passes back to the runtime, so that it never sees a `Value` (Chapter 36).

**Heredoc** — a quoting form whose terminator is a word the programmer chooses
and whose body runs to the line bearing it. It is a lexer problem, because the
extent of the body cannot be known from the opening delimiter alone
(Chapter 4).

**Hoisting** — making a declaration take effect earlier than its position in
the text. Named subs are hoisted here, which is why a sub needs no forward
declaration — but the hoist is done by the interpreter rather than the parser,
which is exactly why a user-declared *operator* must still appear before its
first use (Chapters 3 and 6).

**Hyper operator** — Raku's `»` and `«` metaoperators, which distribute an
ordinary operator over the elements of a list: `@a »+« @b` (Chapter 18).

\glossletter{I}

**Inlining** — replacing a call with the body of what it called. The emitter
inlines a few `int64` arithmetic shapes itself and leaves general inlining to
the host C++ compiler, which is much of the point of emitting C++ at all
(Chapters 26 and 27).

**Interning** — keeping one canonical copy of each distinct value from a
*closed* vocabulary and passing a small identifier around instead. Method names
and similar fixed vocabularies are interned here. An open vocabulary must never
be, because the table is append-only and would grow an entry per distinct
runtime value (Chapter 10).

**Intermediate representation (IR)** — a form between the tree and the machine,
invented so that optimisation passes have something regular to rewrite:
three-address code, SSA, a control-flow graph. Raku++ has none of them, which
Chapter 3 states as a classification and Chapter 41 revisits as a trade.

**Interpreter** — a program that executes another program directly, rather than
translating it into something else first. See *tree-walking interpreter*.

**Invocant** — the object a method is called on: the thing to the left of the
dot. Much of method dispatch is guarded on its type (Chapter 16).

\glossletter{J}

**JIT (just-in-time compilation)** — compiling parts of a program into machine
code while it is already running, guided by what it is observed to do. MoarVM
has one. Raku++ has none; `--exe` compiles ahead of time instead (Chapter 3).

**Junction** — a Raku value holding several values at once, `1|2|3`, which
operations distribute over. See *autothreading* and *eigenstate* (Chapter 18).

\glossletter{L}

**Lazy evaluation** — producing elements only when they are asked for, which is
what makes an infinite sequence a usable value. Chapter 18 covers `Seq`,
`gather`, and the lazy tail.

**Lexer** (also **scanner**, **tokenizer**) — the pass that turns a stream of
characters into a stream of tokens, so the parser can work in words rather than
letters. Raku++'s runs to completion and returns a flat vector before a parser
exists at all (Chapter 4).

**Lexer hack** — the arrangement in C compilers where the parser feeds its
symbol table back to the lexer, so the lexer can tell a type name from an
ordinary identifier. It is named for how unpleasant it is. Raku++ has no such
loop: the lexer has finished before the parser starts (Chapter 3).

**Lexical scope** — a name resolved by the enclosing text, fixed when the code
is written rather than by who calls whom. `my` variables are lexical. Contrast
dynamic scope (Chapter 12).

**LL, LR** — the two classical parser families. LL is top-down and builds a
leftmost derivation; LR is bottom-up and table-driven. LL(1) means one token of
lookahead suffices. Raku++ is in the LL family but is neither LL(1) nor LL(k)
for any fixed k — and, because its grammar changes mid-parse, it is outside the
classification altogether (Chapter 3).

**Longest-token matching (LTM)** — Raku's rule for choosing among a `proto`
regex's alternatives: the one whose declarative prefix reaches furthest wins,
not the one written first. Chapter 23 is the two rankers that implement it.

**Lookahead** — how many tokens beyond the current one a parser must see to
decide what to do. Several Raku constructs need more than one, which is what
places the parser outside LL(1) (Chapter 3).

**Lookaround** — a regex assertion that tests what lies ahead or behind without
consuming it (Chapter 21).

**Lowering** — translating a construct into a simpler, more machine-like form,
usually in a pass of its own. There is no lowering step between the tree and
any of the four run modes here, which is why a language feature is implemented
once rather than four times (Chapters 3 and 25).

**Lvalue** — an expression denoting a place that can be written to, as opposed
to a value. `@a[0]` is one, and the interpreter has a separate evaluation mode
for producing them (Chapter 12).

\glossletter{M}

**Maximal munch** — the scanning rule that the longest spelling wins, so that
`<=` lexes as one token rather than as `<` then `=`. Raku++ gets the same
effect from an operator table ordered longest-first by hand (Chapter 4).

**Memoisation** — caching a result against the arguments that produced it, so a
repeat is a lookup. Sound only when the result really is a function of the key:
a grammar rule that reads a dynamic variable is not a function of (rule,
position), which is what the `dynDep` flag records (Chapter 22).

**Metaoperator** — in Raku, an operator that takes another operator as its
argument: `Z+`, `X~`, `R-`, `»+«`, `[+]`. The lexer knows the shapes; the tree
keeps them as ordinary nodes (Chapters 4 and 7).

**Mixin** — attaching a role to one object at run time, so that object alone
gains the methods: `$x does Serialisable` (Chapter 17).

**Model gap** — a regex construct the longest-token automaton builder could not
model, as distinct from one that genuinely ends the declarative prefix. A gap
forces a fallback to the other ranker (Chapter 23).

**Multiple dispatch** — choosing among several routines of the same name by the
types of *all* the arguments, not merely the invocant. Raku spells it `multi`.
Raku++ scores the candidates and breaks ties by declaration order, where Raku
specifies an ambiguity error (Chapter 16).

\glossletter{N}

**NFA (nondeterministic finite automaton)** — an automaton permitted to be in
several states at once, which is what lets a single linear scan answer "how far
could each of these alternatives reach?". Built here by Thompson construction,
and used for exactly one job: ranking longest-token alternatives. It never
matches on the engine's behalf (Chapters 3 and 23).

**NFG (normalization form grapheme)** — Raku's string model: text is normalised
on the way in, and indices count graphemes rather than bytes or code points
(Chapter 24).

**Node specialisation** — caching on an AST node the decision its syntactic
shape implies, so the second evaluation skips the dispatch the first one did.
A fast path on a tree walk, not a compilation step (Chapter 19).

**NQP (Not Quite Perl)** — the small Raku subset Rakudo is written in. Raku++
implements a subset of its `nqp::` ops, so that code written against them runs
(Chapter 34).

\glossletter{O}

**Opcode dispatch loop** — the `switch` at the heart of a bytecode VM. Measured
here at 0.32 ns, against a tree-node visit costing 46 to 85 ns; that ratio is
the number behind the decision not to build one (Chapters 39 and 41).

**Operator table** — the parser's map from an operator's spelling to its
precedence, its associativity and the node it builds. Raku++'s is mutated
during the parse and rolls back at scope exit (Chapters 5 and 6).

**Oracle** — a trusted implementation whose output a test is checked against.
Rakudo is this book's principal oracle, and a file verified against it records
which version it was verified with (Chapter 39).

\glossletter{P}

**Packrat memo** — the cached match of a grammar rule at a position.
Legitimate for a ratcheting rule, because such a rule does not backtrack
(Chapter 22). A **packrat parser** is one that memoises every rule at every
position this way; the main parser here memoises nothing, which is one of the
reasons it is not a PEG parser.

**Pad** — the storage for one scope's variables, laid out once per scope rather
than looked up by name on every access. The term is Perl 5's, and so is much of
the design (Chapter 40).

**Parse forest** — the set of all valid trees for an ambiguous input, which is
what GLR and Earley parsers produce. Raku++ produces one tree (Chapter 3).

**Parser** — the pass that turns a stream of tokens into a tree, deciding what
groups with what (Chapter 5).

**Parser generator** — a tool that reads a grammar file and emits a parser. See
*hand-written* for why there is none in this build.

**PEG (parsing expression grammar)** — a grammar formalism with ordered choice,
in which the first alternative that matches wins and ambiguity cannot arise by
construction; usually paired with packrat memoisation. Raku++ has neither the
ordered-choice formalism nor the memo (Chapter 3).

**Peephole optimisation** — a local rewrite that inspects a small window and
improves it, with no knowledge of the program as a whole. Everything the
`--exe` optimizer does is peephole-class by any standard classification
(Chapter 27).

**Pratt parser** — an expression parser that resolves precedence with one loop
and a table of binding powers, instead of one mutually recursive function per
precedence level. Also called precedence climbing, or top-down operator
precedence after Pratt's 1973 paper. It is what lets `parseExpr` fit on a page
despite two dozen precedence levels (Chapters 3 and 5).

**Precedence** — which operator binds tighter, so that `2 + 3 * 4` is 14 and
not 20. See *Pratt parser* and *binding power*.

**Proto** — in Raku, the declaration that names a family: `proto sub` for a
multi family, `proto regex` or `proto token` for an alternation resolved by
longest-token matching (Chapters 16 and 23).

**Proxy** — a Raku container whose reads and writes run code you supply, so an
ordinary-looking variable can compute its value on access (Chapters 8 and 12).

**Publish** — the step at the end of module loading that copies a module's
environment into the global one. Raku specifies exporting selected symbols;
this publishes all of them, and Chapter 32 says so.

\glossletter{R}

**Ratchet** — the property of `token` and `rule` that their quantifiers are
possessive and their matches commit, so that no backtracking crosses them. It
is what makes memoising a rule sound (Chapters 20 and 22).

**Recursive descent** — parsing by writing one function per construct, each
calling the functions for its parts, so that the C++ call stack mirrors the
tree being built. The oldest and by far the most common way to write a parser
by hand (Chapters 3 and 5).

**Reference counting** — freeing an object when the number of references to it
falls to zero. Raku++ uses `shared_ptr`: deterministic, cheap to reason about,
and unable to collect reference cycles (Chapter 1).

**Register allocation** — deciding which values live in machine registers. A
back-end pass Raku++ does not have, because it emits C++ and lets the host
compiler do it (Chapters 3 and 26).

**Regression** — a change that makes something that used to work stop working,
or something that used to be fast stop being fast. The performance gate exists
to fail the build on the second kind (Chapter 39).

**REPL** — read–eval–print loop. Raku++'s shares its session machinery with the
MCP server and the Jupyter kernel (Chapter 38).

**Roast** — the official Raku test suite, and the specification in practice.
Results against it are reported two ways, because either number alone misleads
(Preface, Chapter 39).

**Role** — a bundle of methods composed into a class at compile time, or into a
single object at run time as a mixin. Raku's answer to multiple inheritance
(Chapter 17).

\glossletter{S}

**Seam** — an accessor that is the only route to a piece of data, so that the
object defining it can be swapped for a stub at link time. It is what makes a
Unicode table cuttable (Chapter 29).

**Self-hosting** — implementing a language in itself. Rakudo is self-hosted
through NQP, and therefore needs a bootstrap; Raku++ is written in C++ and does
not (Chapter 3).

**Sigil** — the character that opens a Raku variable name and declares what
shape it holds: `$` item, `@` positional, `%` associative, `&` callable. Mostly
a parse-time fact here (Chapters 4 and 12).

**Sigspace** — the `:s` adverb and the `rule` declarator, under which
whitespace written in a pattern means "match optional whitespace here".
Implemented by wrapping atoms with a `<ws>` subrule at compile time
(Chapter 20).

**Single-pass** — reading the source once and doing the work on the way
through, with no separate analysis pass over the tree afterwards. The front end
here is single-pass, which is also why there is no whole-program analysis
(Chapter 3).

**Sink context** — a statement whose value is discarded, signalled down the
evaluation so that an assignment need not materialise its result (Chapter 13).

**Slang** — in Raku, a swapped-in sublanguage that changes how later source is
parsed. It requires user code to run during the parse, and is not implemented
here (Chapters 3 and 6).

**Slip** — `|@a`: a list that splices into the surrounding list or argument
list instead of nesting inside it (Chapter 12).

**Slurpy** — a parameter that takes everything left over: `*@rest`, `*%opts`
(Chapter 14).

**Source-to-source compiler** (also **transpiler**) — a compiler whose output
is another language's source rather than machine code. `--exe` emits C++ and
hands it to the host compiler (Chapters 25 and 26).

**Specificity** — the score a multi-dispatch candidate is given, which decides
which candidate wins (Chapter 16).

**SSA (static single assignment)** — an IR discipline in which every variable
is assigned exactly once, which makes many optimisations easy to state.
LLVM's IR is in SSA form. Raku++ has no IR, and therefore no SSA (Chapter 3).

**Stash** — Raku's package symbol table, reachable through `.WHO`. There is no
per-module stash object here (Chapters 17 and 32).

**Static analysis** — inspecting a program without running it. `--lint` and the
undeclared-variable gate are the two instances in this book (Chapter 38).

**Superinstruction** — a fused node kind standing for a common pattern of
several. The approach node specialisation deliberately did not take
(Chapter 19).

**Symbol table** — a compiler's map from a name to what it denotes. Raku++ has
no separate one: the run-time environment chain is the only such map
(Chapter 12).

**Syntax-directed** — of a translation: driven by the shape of the construct
being recognised, with the action attached to the rule that recognised it,
rather than performed by a later pass over a finished representation.

\glossletter{T}

**Tagged union** — one struct able to hold any of several payload types, with a
tag field saying which is live. `Value` is a loose one — loose because several
payloads may be live at the same time (Chapter 8).

**Thompson construction** — the standard method for turning a regular
expression into an NFA: one small automaton per operator, glued together with
epsilon transitions. Used here for the longest-token ranker and nothing else
(Chapters 3 and 23).

**Three-address code** — an IR of instructions with at most three operands,
`t1 = a + b`. Named in this book only among the things that do not exist here
(Chapter 3).

**Token** — one lexical unit: a number, an identifier, an operator, a string
literal. The lexer's output and the parser's input. Raku's `token` declarator
is an unrelated thing — a non-backtracking regex (Chapters 4 and 20).

**Top-down parsing** — building the tree from the root downwards, deciding what
you are about to read before you have read it. Recursive descent is the
hand-written form, and it is what an adaptive grammar requires (Chapter 3).

**Trait** — in Raku, a declarative modifier applied at declaration:
`is rw`, `is native`, `is tighter`. Several of them are instructions to the
parser rather than to the interpreter (Chapters 6 and 12).

**Transparent** (of a regex construct, in ranking) — treated as an epsilon
transition by the longest-token automaton, because the real matcher will
enforce it (Chapter 23).

**Tree-walking interpreter** — one that executes by recursively visiting the
nodes of an AST, with no instruction stream in between. The simplest design
that can run a real language, and what Raku++ is in three of its four modes
(Chapters 3 and 13).

**Trigger** (in `--slim`) — a construct implying the program can run code the
scan never saw, so nothing may be cut. An AST node the scanner does not model
is one (Chapter 29).

**Trivially relocatable** — of a type: its bytes may be moved to a new address
without the source being destroyed, and the result is as if it had been
move-constructed and the original then destroyed. `Value` is; libstdc++'s short
`std::string` is not. It is what lets the list container grow with a `memcpy`
(Chapters 8 and 12).

**Twigil** — the second character of a Raku variable name, after the sigil:
`*` dynamic, `!` and `.` attribute, `^` placeholder, `?` compile-time
(Chapter 4).

\glossletter{U}

**UCD (Unicode Character Database)** — the data files the Unicode Consortium
publishes. Raku++ pins a version and generates its tables from them at build
time (Chapter 24).

**UTF-8** — the variable-width encoding strings are stored in. A grapheme index
is not a byte offset, and Chapter 24 is largely about the distance between the
two.

\glossletter{V}

**Virtual machine (VM)** — the program that executes a bytecode. MoarVM is
Rakudo's. Raku++ has none (Chapter 3).

\glossletter{W}

**WebAssembly (Wasm)** — the portable binary instruction format browsers
execute. The whole runtime is compiled to it with Emscripten (Chapter 31).

**Whatever** — Raku's `*` in term position, which turns the expression around
it into a closure: `*.is-prime` is a one-argument block (Chapter 18).

**Wrapper stack** — the `&routine.wrap({…})` layers on a Callable, run
outermost first, each able to `callsame` into the next (Chapter 16).
