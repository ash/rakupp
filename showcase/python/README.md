# python — a Python 3 interpreter

A working interpreter for a practical slice of Python 3. The interesting part is
the **off-side rule**: Python delimits blocks by indentation, not braces. A Raku
grammar can't track that directly (it needs a stateful indentation stack that
survives block exit, which rakupp's dynamic variables don't restore), so —
exactly like CPython — a **tokenizer pass turns indentation into explicit
`INDENT` / `DEDENT` markers**, and a normal grammar parses the marker stream.

```sh
build/rakupp showcase/python/python.raku showcase/python/examples/fizzbuzz.py
build/rakupp showcase/python/python.raku --tokens=file.py    # show the marker stream
build/rakupp showcase/python/python.raku --ast=file.py       # dump the AST
build/rakupp showcase/python/python.raku                     # no file → a REPL
```

Five example programs live in [`examples/`](examples/):

| File | Shows off |
|---|---|
| `fib.py`       | recursion, list comprehension, tuple-swap, arbitrary-precision ints |
| `fizzbuzz.py`  | the off-side rule — nested `if`/`elif`/`else` inside a `for` |
| `wordcount.py` | dicts, `sorted` with a `lambda` key, f-strings, `str` methods |
| `sieve.py`     | list building, slicing, `range` with a step, comprehensions |
| `classes.py`   | closures, `lambda`, `map`/`filter`, generator expressions |

All five produce byte-identical output under CPython 3 and under `python.raku` —
including `fib_big(100)` (a 21-digit int, since integers ride on Raku's own
bignums), `0.1 + 0.2` printing `0.30000000000000004` (float literals are IEEE
doubles and repr finds the shortest round-trip), and default string sort order.

## The indentation tokenizer

Run any example with `--tokens` to see it. Source like

```python
for i in range(3):
    if i:
        print(i)
    print("done")
```

becomes a stream where `<IN>` is `INDENT`, `<DE>` is `DEDENT`, and each logical
line ends in a newline:

```
for i in range(3):
<IN>
if i:
<IN>
print(i)
<DE>
print("done")
<DE>
```

The tokenizer keeps an indentation stack: a deeper line pushes and emits one
`INDENT`; a shallower line pops and emits one `DEDENT` per level unwound; at end
of file it unwinds to zero. It also joins physical lines that continue inside
brackets or after a `\`, skips blank and comment-only lines, and leaves the
contents of triple-quoted strings untouched. The grammar then has an ordinary
rule — `suite: NEWLINE INDENT stmt+ DEDENT` — with no indentation logic at all.

## What runs

**Statements.** `if`/`elif`/`else`, `while`/`else`, `for … in …`/`else`,
`break`, `continue`, `pass`, `def` (positional params, defaults, `*args`),
`return`, `global`, `del`, augmented assignment, chained assignment
(`a = b = c`), tuple/list unpacking (`a, b = b, a`), `;`-separated statements,
and one-line suites (`if x: return y`).

**Expressions.** The full precedence ladder: `or`/`and`/`not`, chained
comparisons (`1 < x < 10`), `in`/`not in`/`is`/`is not`, `| ^ &`, `<< >>`,
`+ - * / // % **`, unary `- + ~`, `**`, calls, indexing, slicing (`a[i:j:k]`
with negative and open bounds), attribute-method calls, the ternary
`a if c else b`, `lambda`, and list / set / dict / generator comprehensions
(including multiple `for` clauses and `if` filters).

**Types.** `int` (arbitrary precision), `float` (IEEE double), `bool`, `str`,
`None`, `list`, `tuple`, `dict` (insertion-ordered), `set`, and `range`. Value
equality, truthiness, and `repr` all follow CPython.

**Builtins.** `print` (with `sep`/`end`), `len`, `range`, `int`, `float`, `str`,
`repr`, `bool`, `list`, `tuple`, `dict`, `set`, `sorted` (`key`/`reverse`),
`reversed`, `enumerate`, `zip`, `map`, `filter`, `sum`, `min`, `max`, `abs`,
`round`, `any`, `all`, `ord`, `chr`, `type`, `divmod`, `input`. Plus common
`str` methods (`split`, `join`, `upper`, `lower`, `strip`, `replace`,
`startswith`, `find`, `format`, `zfill`, …), `list` methods (`append`, `pop`,
`sort`, `extend`, `insert`, `index`, `count`, …) and `dict` methods (`keys`,
`values`, `items`, `get`, `pop`, `setdefault`, `update`, …). f-strings support
`{expr}`, `{x!r}`, and format specs (`{x:.2f}`, `{n:05d}`, `{v:,}`); `%`- and
`.format()`-style formatting work too.

## What's out of scope

A slice, not the whole language — the parts left out either need machinery
beyond a showcase or rarely appear in small programs:

- **Classes and objects** (`class`, `self`, inheritance, dunder methods),
  **decorators**, and **`with`**/context managers.
- **`import`** and the standard library.
- **Exceptions** (`try`/`except`/`raise`) — errors print a message and stop.
- **Generators** (`yield`) — generator *expressions* are evaluated eagerly into
  a list; there are no lazy generators or `yield`.
- **Keyword-only args, `**kwargs`, positional-only params**, and starred
  assignment targets (`a, *rest = …`).
- Exact `UnboundLocalError` scoping (a function reads outer names it also
  assigns, rather than raising).

When the parser meets something outside the subset it fails cleanly rather than
guessing.

## How it works

1. An **indentation tokenizer** (`preprocess`) turns source into the marker
   stream described above.
2. A **grammar** (`PyGrammar`) with a proto-rule statement dispatcher and a
   Python precedence ladder parses the stream. A custom `ws` matches horizontal
   space only, so newlines and markers stay significant.
3. An **actions class** builds a plain-hash AST (`%( t => 'kind', … )`).
4. A **tree-walking evaluator** (`exec-stmt` + `pyeval`) runs it, with
   CPython-matching `repr`/`str` formatting and value semantics.

Every program also compiles to a standalone native binary with
`rakupp --exe showcase/python/python.raku -o python.exe` (it bundles the
interpreter, since the evaluator uses `CATCH` for control flow).
