# Linting (`--lint`)

`rakupp --lint FILE` parses a program and runs a set of static-analysis rules
over the AST, **without executing it**. It is meant to catch the class of
mistakes that are visible before run time — a variable declared and never used,
code that can't be reached, a string compared with a numeric operator.

```
rakupp --lint FILE          # analyze a file
rakupp --lint -e 'CODE'     # analyze a one-liner
rakupp --lint FILE -q       # suppress the trailing summary line
```

Each finding is printed to stdout as

```
FILE:LINE: warning|note: message [rule-id]
```

and a one-line summary goes to stderr. Runnable, one-rule-per-file demos are in
[examples/lint/](../../examples/lint).

> **The rules below never stop a program** — they are the advisory half, and
> you may ignore any of them. `--lint` does also report the one static check
> that is *not* advisory: the undeclared-variable check, which refuses to run
> the program at all. It is not a lint rule and is not opt-in — an undeclared
> variable is a compile error, so it is reported before the program starts,
> under `-c`, and by the compile modes. `--lint` prints it as an **error**
> rather than a warning, because a file that cannot compile should not be
> reported as though it merely had a smell. See
> [CLI.md](CLI.md#undeclared-variables-are-refused-before-the-program-runs).

## Errors, warnings, notes, and the exit code

Findings have one of three severities:

- **error** — the program will not compile. Only the undeclared-variable check
  raises one; no lint rule does.
- **warning** — almost always a real defect, but the program still runs.
- **note** — advisory; the construct is legal and often intentional, but worth a
  second look.

`--lint` exits **2** if the file will not compile — a parse error, or any
error-severity finding — **1** if it produced only warnings, **0** if it
produced only notes or nothing, and **4** on a usage error. Notes never fail the
run, so `--lint` drops into a CI step or a pre-commit hook and only breaks the
build on the high-confidence findings; the two failing codes let a hook tell
"this will not build" from "this could be tidier".

## Rules

| Rule | Severity | Flags |
|------|----------|-------|
| `undeclared-variable` | error | A variable used that nothing declares — the program will not compile. Not a lint rule: this is the compile-time check ([CLI.md](CLI.md#undeclared-variables-are-refused-before-the-program-runs)) reporting through `--lint` as well, so that analysing a file never says less than running it would. |
| `unused-variable` | warning | A `my`/`state` lexical that is declared and never referenced anywhere in its scope (reads, writes, and interpolation all count as a reference). |
| `unused-routine` | warning | A lexical `sub` that is never called and never taken as a `&`-value. Skips `MAIN`/`USAGE` (the runtime calls them), `our`/`is export` routines, methods, and multis. |
| `redeclaration` | warning | A second `my` of the same name in the same scope — the first binding becomes unreachable. |
| `unreachable-code` | warning | A statement that follows an unconditional `return`/`last`/`next`/`redo`, or a bare `die`/`exit`, in the same block. |
| `self-assignment` | warning | `$x = $x` — assigning a variable to itself, usually a typo for a nearby name. |
| `constant-condition` | warning | An `if`/`unless` whose condition is a literal (`if False`, `unless 0`, `if True`), so the branch is dead or unconditional. |
| `numeric-cmp-of-string` | warning | A numeric comparison (`==` `!=` `<` `<=` `>` `>=`) with a non-numeric string literal, e.g. `$s == "yes"` — almost certainly meant `eq`/`lt`/`gt`. |
| `new-arg-matches-no-attribute` | warning | A literal named argument to `LocalClass.new(...)` that matches no public attribute — the default constructor binds nameds to public attributes and **silently ignores** the rest, so a typo'd name (`name => …` for `has $.na`) is invisible at runtime. Fires only when construction is fully understood from the file: the class and its whole in-file ancestry (parents *and* roles) declare no custom `new`/`BUILD`/`TWEAK`, and every ancestor is itself declared in the file. Private-only attributes count as no match (they are not settable from the default `new` either). |
| `unused-parameter` | note | A signature parameter that the body never uses. Advisory, because uniform callback/dispatch signatures and interface conformance routinely carry parameters a given routine ignores. Skips slurpies, the invocant, and `$_`. |
| `redundant-return` | note | An explicit `return` as the final statement of a routine — a block already yields its last expression. |

## Design: conservative by construction

A linter is only useful if its warnings are trusted, and Raku is dynamic enough
that an over-eager analyzer produces noise that trains you to ignore it. The
rules here are built to under-report rather than over-report — a missed warning
is acceptable, a false one is not.

Concretely:

- **Contextual and twigil variables are never flagged.** `$_`, `$/`, `$!`, the
  numeric match vars, dynamic `$*x`, compile-time `$?x`, attribute `$.x`/`$!x`,
  and `$^a` placeholders are all set, captured, or resolved by machinery the
  linter doesn't model, so it leaves them alone. Only ordinary named lexicals
  participate.
- **Interpolation counts as use.** A variable that appears only inside `"…$x…"`
  is used; the analyzer walks the parts of every interpolated string.
- **`EVAL` and symbolic references disable the "unused" rules.** If a compilation
  unit contains `EVAL`/`EVALFILE` or a `::($name)` symbolic reference, a name
  could be reached by a route the linter can't see, so `unused-variable` and
  `unused-routine` stand down for that unit.

The undeclared-variable check takes the same principle further, because it has
to: a false warning is an annoyance, but a false *refusal* means a working
program will not start. It stands down for `EVAL`, `::($name)`, `require` and
an import it cannot resolve, is silent inside the lexical scope of a
`no strict`, and it reports a name only when the source declares it nowhere.
- **Loop and binder variables aren't required to be used.** `for ^10 { … }`,
  `if EXPR -> $x { … }`, `given … -> $y { … }` — a topic or binder you don't
  read is idiomatic, not a mistake, so those are tracked (their uses resolve)
  but never reported.
- **Only unconditional terminators trigger `unreachable-code`.** A statement
  modifier (`return … if …`) parses as a conditional, so it is not treated as a
  terminator.

### Known limitations

The analyzer works from the parsed AST, so it inherits the parser's blind spots.
Two exotic, rarely-used constructs can produce a spurious finding: adverbial
variable names (`my $x:foo<a b>`, whose adverbs the parser drops) can look like a
`redeclaration`, and the deprecated `will`-phaser trait syntax
(`my $x will next { … }`) can look like `unreachable-code`. Both are essentially
absent from real code. Reaching a routine purely through a runtime dispatch
table (`%handlers{$op}()`) is invisible to `unused-routine`; if that pattern is
central to a program, expect a false positive there and rely on the `&`-value
and by-name call detection for everything else.
