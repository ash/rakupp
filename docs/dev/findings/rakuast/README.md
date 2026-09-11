# The RakuAST oracle baseline — Rakudo 2026.08 over raku-corpus

The Rakudo half of the tree oracle in
[RAKUAST-PLAN.md](../../plans/RAKUAST-PLAN.md) (Part III, *`--rakuast`*),
measured 2026-09-11 before any of the rakupp side exists. What it answers:
for how much real code the oracle produces a tree at all, how big those trees
are, and which `RakuAST::` classes real code uses — the target the view
builder is written against, and the baseline the per-file match percentage
is later computed from.

- **Oracle**: Rakudo 2026.08 as installed (legacy frontend by default;
  `.AST` needs no environment variable). 2026.09 will move the trees, so the
  files here are pinned to 2026.08 by name.
- **Corpus**: raku-corpus at 4bede39 (2026-08-05). Its `compile-status.tsv`
  lists 1,939 paths, 69 of which no longer exist; the remaining **1,870
  programs** are the population.
- **Tools**: [tools/rakuast-oracle.raku](../../../../tools/rakuast-oracle.raku)
  (one child process per program, run from the corpus root) and
  [tools/rakuast-oracle-classes.raku](../../../../tools/rakuast-oracle-classes.raku)
  (the class histogram, one process). Both run under Rakudo — they measure it.

## The numbers

| | |
|---|--:|
| programs | 1,870 |
| tree produced | **1,858** (99.4%) |
| no tree | 12 |
| nodes over the 1,858 | 162,992 (mean 88 per program; largest `course/exercises/associatives/replace-with-antonyms.raku`, 2,718) |
| child time | ≈0.4 s per program, ≈13 min for the corpus |
| distinct classes | 213 (over the 1,699 programs the one-process walk survived — see caveats) |
| classes covering 95% of nodes | 44 |
| classes with ≤3 nodes in the whole corpus | 51 |

The 12 without a tree, none of them a corpus limit:

| count | reason | what it is |
|--:|---|---|
| 4 | `Unexpected undefined NQPMu node` (2), `Cannot find method 'to-begin-time' on object of type NQPMu` (2) | crashes inside the 2026.08 RakuAST frontend — a user-defined `postfix:<!>` used as `5!` / `.!`, and a grammar-actions module; oracle bugs, re-checked per release |
| 7 | `Calling add(Int) will never work with declared signature ($x, $y)` and six like it | book examples that demonstrate a wrong call; `.AST` runs Rakudo's compile-time checks, which rakupp's parse does not (the plan's recorded divergence) |
| 1 | `Table has a mixture of visible and invisible column-separator types` | pod table strictness |

Comparison with Part I's measurement of 44 files from examples/ + showcase/
(24,442 nodes, 125 classes, 39 covering 95%): 6.7× the nodes, 88 more
classes — almost all in the tail — and a 95% head of 44 instead of 39.

Three things in the histogram the plan did not know:

- **`Call::Name::WithoutParentheses` is everywhere** — 4,043 nodes in 1,571
  programs — so the `Call::parenned` bit P1 records is exercised by five of
  every six programs.
- **Four synthesized declarations per program**:
  `VarDeclaration::Implicit::Doc::{Data,Finish,Pod,Rakudoc}` appear once in
  every `CompUnit` with no source token behind them — they join the
  `Blockoid` / `StatementList` / `Type::Setting` list of nodes a 1:1 view
  must emit unprompted.
- **`Regex::Nested` is in the ≤3 tail**: the construct that breaks Rakudo's
  own `.DEPARSE` and `.raku` is rare in this corpus, so the tree walk, not
  the text, remains the comparison surface, but the hole will seldom bite.

## Files

- `oracle-2026.08-per-file.tsv` — one row per program: `file`, `status`
  (`ok`/`fail`), `nodes`, `classes`, `top5` (the five commonest classes,
  `RakuAST::` stripped), `seconds`, `first-error` (the reason line after any
  `===SORRY!===` header). Paths are relative to the corpus root.
- `oracle-2026.08-classes.tsv` — `class`, `nodes`, `files`, most nodes first.

## Caveats, so the numbers are read right

- The class histogram walks all `ok` programs in **one** Rakudo process (a
  child per program would pay a start-up per file). State leaks between
  files inside that process — a redeclared `infix:</>`, a `use` — and 135
  programs whose `.AST` then failed are missing from the histogram (1,699
  walked of 1,834 `ok` at the time). The per-file TSV is the authority on
  which programs tree; the histogram is the vocabulary. It was also taken
  before the 25 per-file rows below were corrected, so it is a slight
  undercount.
- 25 rows were re-measured after two harness defects, both worth knowing
  for the real tool: (1) a `BEGIN { say … }` runs at `.AST` time and its
  output lands on the child's stdout ahead of the result line — the first
  driver read the first line and misfiled four good trees as failures;
  (2) `-I` given as a relative path while `:cwd` moves the child into that
  directory points nowhere — 21 programs that `use` a sibling module
  (`Circle`, `LinguaAST`, …) failed until the include path was absolute.
- `.AST` is the compiler, not the parser: the 7 compile-time-check failures
  above are the concrete form of the plan's trap — every corpus program
  must be self-contained and compilable under the oracle Rakudo.

## `deparse-2026.08.tsv` — the renderer's specification

What `.DEPARSE` answers on Rakudo 2026.08 for 57 constructed nodes, one row per
case (`label`, then the text with newlines escaped). Produced by
[tools/rakuast-deparse-spec.raku](../../../../tools/rakuast-deparse-spec.raku),
which runs under Rakudo because it measures Rakudo; re-run it when the oracle
version moves, and the diff is the work.

The vocabulary is the union of what **Needle::Compile 0.0.12**,
**Intl::Format::Number 0.2.0** and **RakuAST::Utils 0.0.3** name — a grep of
their REA tarballs, 52 distinct classes, 17 of which the P0 registry did not
carry (it now does, at 135 classes). The last row, `needle.equal`, is the whole
tree App::Rak's commonest invocation builds: `handle("equal", …)` and
`wrap-in-block` from Needle::Compile, verbatim, which renders

    -> $_ {
        my $/;
        $_ eq "foo"
    }

Three conventions the table pins, all of them measured rather than assumed:

- **four-space indent, and no trailing newline after a closing brace** — a
  `Blockoid` is `{\n    42\n}`, and a `PointyBlock` the same with its signature
  in front;
- **a statement list ends each statement with a newline**, and separates all but
  the last with `;` — one statement is `42\n`, two are `42;\n"foo"\n`;
- **an explicit `$_` invocant elides.** `ApplyPostfix(Var::Lexical('$_'),
  Call::Method('fc'))` renders `.fc`, the same text `Term::TopicCall` gives,
  while `"foo".fc` keeps its invocant. That is normalization, not loss — `.fc`
  *means* `$_.fc` — but a renderer that does not do it produces a tree of a
  different shape on the way back.
