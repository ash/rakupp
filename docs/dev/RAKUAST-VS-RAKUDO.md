# RakuAST here and in Rakudo — the same API over a different thing

Both engines answer `.AST`, `.DEPARSE`, `.EVAL`, `visit-children` and
`.rakudoc`, over classes with the same names and the same ancestry. Underneath
they are not the same object at all, and almost every difference in this
document follows from one sentence:

> **In Rakudo, RakuAST *is* the compiler front end. In Raku++ it is a view built
> on demand over a parse that has already happened.**

This page is for someone who knows Rakudo's RakuAST and wants to know what they
get here — a module author deciding whether their code will run, or a
contributor about to extend the view. The design argument and the measurements
are in [plans/RAKUAST-PLAN.md](plans/RAKUAST-PLAN.md) Part I; the implementation
log is Part IV. This is the comparison, not the history.

## The structural difference

Rakudo's front end parses Raku into RakuAST and then compiles RakuAST to
bytecode. The tree is the compiler's own working representation: `$*LANG` holds
the grammar that produced it, `$*R` resolves names against it, and a program
that gets hold of a node is holding a piece of the compiler. That is what makes
macros and slangs possible there, and it is why RakuAST becoming the default
front end (2026.09) is a compiler change rather than a library change.

Raku++ parses Raku into its own AST — a semantic tree tuned for a tree-walking
evaluator — and runs that. `.AST` builds a **RakuAST-shaped view over that
tree**, node by node, when a program asks for one. Nothing is built otherwise.

### Why, in one measurement

Making RakuAST the internal tree was costed rather than argued about. For

```raku
sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }
```

| representation | nodes |
|---|---:|
| rakupp (`--ast`) | 17 |
| RakuAST (`.AST`, counted with `visit-children`) | 39 |

**2.3× the nodes**, and the surplus sits in the hot loop: the inner expression
`fib($n-1) + fib($n-2)` is 9 nodes here and 19 there — **~2.1× the node visits**,
each an extra pointer chase into a separately allocated object. This engine
walks its tree on every call, forever, so that is not a one-off parse cost.

The view keeps the API and skips the bill. Measured at v3.28.0, interleaved
against v3.27.0 built from source: **worst +2.7%, mean −1.3% over sixteen
kernels** — RakuAST costs the ordinary path nothing, because nothing is built
until `.AST` is called.

## What follows from it

### There is no RakuAST → AST compiler

Upstream, a tree you construct or mutate is what the compiler consumes next.
Here the bridge back is **`.DEPARSE` plus the ordinary parser**: `.EVAL` renders
the tree to Raku source and parses it again, in the caller's scope. Constructed
trees run, and `Q[…].AST.EVAL` round-trips.

The cost is that anything `.DEPARSE` cannot render is not evaluable, and
anything the text cannot carry is lost across the bridge. In exchange, there is
one renderer to keep correct instead of a second compiler.

### The tree is a tree here and a graph there

A Rakudo RakuAST node links **upward and sideways**: a declaration knows its
containing block, a statement list knows its comp unit, a resolver hangs off the
unit. So enumerating a node's node-valued **attributes** is not a walk down a
tree — it is a walk over a graph, and it keeps climbing back up into parts of
the program it has already been.

The first version of our dumper did exactly that, on the reasoning that
attribute order is deterministic on both engines. What that cost, measured:

| twelve-line program | nodes reported |
|---|---:|
| walking attributes | **45** — of which **19** were the same handful of blocks, reached again from underneath |
| walking `visit-children` | **26** |

Nearly half of that first figure was the walk meeting itself.

And duplication is the mild failure. Those links are **cyclic** — a declaration
knows its containing block, the block knows its statements, and one of those
statements *is* the declaration — so an unbounded descent does not terminate:
what grows is not the node count but the number of distinct paths to the same
nodes. One 40-line program was left running for **twenty minutes** and had not
finished; it was not going to. That is the number's only claim — how long it was
allowed to run before a `.WHICH`-keyed visited set was added to bound it, not
how long the walk takes. There is no "how long it takes".

`visit-children` is the syntactic children in source order, and it is the only
walk that means the same thing on both engines — it is also what made the two
engines' dumps comparable at all. Use it. A tool that enumerates attributes
instead will behave differently here, and on Rakudo may not finish.

### `.parent` answers `Nil` — and now says so

`.parent` is public API upstream and answers `Nil` for every tree a program can
actually obtain. Nothing to reproduce, so nothing was built — and that was the
mistake: with no method of its own, the call fell through to the generic
`.parent`, which reads any invocant as a path, and a node answered
`IO::Path.new(".")` where Rakudo answers `Nil`.

Fixed 2026-09-12. The lesson generalises to the rest of this surface: **"not
implemented" has to be said out loud, or the fallback answers for it** — a
wrong answer is worse than a missing one, because the caller cannot tell.

### `@*LINEAGE` belongs to the walker

ASTQuery's descent is `visit-children` plus `@*LINEAGE`, and it is tempting to
read that as an engine feature. It is not, on either engine: Rakudo does not
populate it inside `visit-children` (measured). It is an ordinary dynamic the
visitor re-declares as it descends.

### Macros and slangs are still out

They need the parser to run user code mid-parse and rewrite its own grammar,
which a view cannot offer. See [plans/SLANG-PLAN.md](plans/SLANG-PLAN.md).

**The exception, and it is a real one:** `use L10N::XX;` writes a whole program
in German, Japanese or Afrikaans, and works here. Upstream that is a slang mixed
into `$*LANG`; here it is a rewrite of the token stream between the Lexer and
the Parser, because what an L10N slang carries is a table of *keyword
spellings*, not new syntax — and this lexer hands every keyword to the parser as
a plain identifier. A slang that only renames things needs no grammar.

Note the direction of that difference: on Rakudo 2026.08 those modules need
`RAKUDO_RAKUAST=1`, because they target the RakuAST front end and it is not yet
the default. Here they need no flag. See [the FAQ article](../guide/faq/l10n.md).

## How close the view is, and how that is known

`rakupp --rakuast FILE` prints our tree; `tools/rakuast-oracle-dump.raku` prints
Rakudo's in the same serialization, so the comparison is a `diff`
(`tools/rakuast-diff.raku` sweeps and scores).

**72.9% — 17,854 of Rakudo's 24,499 nodes, over the 39 of 59 raku-corpus
programs both engines can tree.** The view also builds **5,132 nodes Rakudo's
tree has not**, and that count is published beside the percentage on purpose:
the metric is recall, and recall alone is gamed by adding nodes.

The largest gap is the whole **`Regex::*` subtree** — grammars refuse by name.

Two traps in measuring this, both of which cost real time:

* **Score with `diff(1)`, never position by position.** Line-N-against-line-N
  read 1.2% where a real diff read 41.6% on the same trees.
* **`.AST` compiles in the CALLER's scope**, like `EVAL`. Parsing the same
  source twice in one process made 18 of 59 programs die with "Redeclaration of
  symbol 'JSON'" — the tool refusing itself, not Rakudo refusing the program.

## If you are porting a module

| your module | here |
|---|---|
| builds `RakuAST::` nodes and `.EVAL`s them | works |
| calls `.AST` on source and reads the tree | works, to the coverage above |
| walks with `visit-children` | works |
| renders with `.DEPARSE` | works — but see the rendering note below |
| reads `.rakudoc` for documentation blocks | works |
| declares a `macro` | no |
| installs a slang, other than an L10N keyword table | no |
| needs `$*R` / to *be* the front end (e.g. FINALIZER) | no — this is the structural limit, not a gap to fill |

**Rendering is not byte-identical.** Both engines deparse to the same *program*,
not the same *text*: we write `"Hallo, {$name}!"` where Rakudo writes
`"Hallo, $name!"`, and Rakudo marks a required parameter `-> $n!` where we write
`-> $n`. Compare trees or behaviour, not deparsed strings.

## The gates that keep this honest

`t/regression/rakuast-{view,deparse,eval,visit,doc,registry,l10n}.raku`, plus
the oracle sweep above. The `visit` case is checked against a recorded dump of
Rakudo's own walk (`docs/dev/findings/rakuast/`), so a divergence in the walk
shows up as a diff against the reference engine rather than against our
expectations of it.
