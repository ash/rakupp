# Raku without sigils

*R&D, 2026-10-07. Rakudo 2026.09 (MoarVM) and Raku++ 5.2.1-101 were probed on
the same snippets. The prototype is in [sigil-free/](sigil-free/README.md).*

The question: how far can an Acme-style module take Raku without `$`, `@`, `%`
and `&`? Two shapes were asked about:

* **A — the sigil once.** It is written at the declaration and dropped
  afterwards; a name may not belong to two kinds of variable.
* **B — no sigil at all.** The kind is inferred from how the variable is used.

Short answer: both work, for ordinary programs, on both engines, with one
translator. The limit is not the parser. It is the jobs a sigil does that
nothing else in the source states, and a short list of syntactic collisions.

## What a sigil does

| Job | `$x` | `@x` | `%x` | `&x` | Without a sigil |
|---|---|---|---|---|---|
| Container and default value | Scalar, `Any` | empty `Array` | empty `Hash` | `Callable` slot | chosen at the declaration (A) or inferred (B) |
| Assignment | stores one item; `$x = 1, 2` is `($x = 1), 2` | copies a list in | copies pairs in | — | Rakudo already parses `x = 1, 2, 3` on a sigilless name as LIST assignment (probe P2) |
| Flattening | `for $x` runs once over an Array it holds | `for @x` runs per element | per pair | — | decided by the container, so by the same choice |
| Binding constraint | anything | `Positional` | `Associative` | `Callable` | follows the choice |
| String interpolation | `"$x"` | `"@x[]"` | `"%x{}"` | `"&x()"` | only `"{x}"`: a bare word in a string is text |
| Namespace | `$x`, `@x`, `&x` coexist | | | | one name, one kind |

The parse never needs the kind. Rakudo's grammar already has a
`sigilless-variable` hook inside `token variable`. For a sigilless name it sets
`$*LEFTSIGIL` to the name's first letter, so `=` is list assignment whatever
the variable holds. Everything that differs lives in the container, which is
why the design question is only "which container".

## What Raku gives without a module

| # | Snippet | Rakudo | Raku++ |
|---|---|---|---|
| P1 | `my \x = $ = 5; x = 6; x++; say x` | `7` | dies: *Cannot modify an immutable Int (5)* |
| P2 | `my \a = [1,2,3]; a = 4, 5, 6; say a.raku` | `[4, 5, 6]` | dies: *Cannot modify an immutable Array* |
| P2x | `my \x = $ = 0; x = 4, 5, 6; say x.raku` | `$(4, 5, 6)` | (dies as P1) |
| P4 | `my $x = 5; my \x := $x; x = 9; say $x` | `9` | `5` |
| P5 | `sub f(\v) { v * 2 }`, `-> \a, \b { a + b }` | works | works |
| P6 | `my $a = 5; sub term:<a> is raw { $a }; a = a + 1` | `6 6` | `6 6` |
| Q3 | `for [[1,2],[3,4]] -> \row { for row { … } }` | inner loop runs **once** per row | runs twice |
| Q8 | `my \list = 3; say list; say list(1,2)` | `3`, `(1 2)`: the sigilless term shadows the bare word only | same |

So Rakudo already has mutable sigilless variables (`my \x = $ = …`) and
sigilless parameters. Two things make that unpleasant to write all day:
- `$ =` on every scalar;
- Q3: a sigilless parameter bound to an Array element holds the element's
  Scalar, so iterating it runs once.

Q3 is the reason a sigil-free design has to choose containers rather than bind
raw values.

## Slang::Nogil

`Slang::Nogil` 1.3 (zef:lizmat) fills `sigilless-variable` with
`<.ident>+ | <.:So>`, behind a keyword check. On Rakudo it does exactly what it
says, and no more:

* `my a = 42; a = 43; a++` works: a mutable Scalar named `a` (not `$a`).
* It only stands in for `$`. `my a = [1, 2, 3]; for a { … }` runs once (Q1),
  as `$a` would.
* Parameters are untouched: `sub f(x)` is still *Invalid typename 'x'*.
* `"a is a"` stays text; `{a}` interpolates.

On Raku++ it fails its own test at `my 👍 = 42` with
*Slang::Nogil: Undefined routine 'check-keywords'*: the token's code assertion
does not see the module's lexical sub. SLANG-PLAN.md's table records it as
27/27.

## The prototype: a translator

A slang can only decide at the declaration, while the parser stands on
`my x`, and the evidence for B comes later in the file. Rakudo's slang
actions are also Rakudo internals (QAST or RakuAST), and Raku++ runs a slang's
tokens but not its actions. A source-to-source translator has none of these
problems:

* it sees the whole unit before choosing anything;
* the same code runs on both engines;
* its output is ordinary Raku that `--show` prints, so every decision can be
  inspected.

It only **inserts** characters, so the output has the input's lines and every
compiler message points at the line that was written.

It is about 1,150 lines of Raku in three parts:

* **Lexer.** It tracks whether a term or an operator comes next: that is
  what separates `/` the division from `/…/` the regex, and `<` the comparison
  from `<…>` the word list. Strings, `{…}` inside double quotes (lexed as
  code), heredocs, pod, comments, regexes and quote constructs are handled.
* **Analyzer.** It keeps lexical scopes. Declarations come from
  `my`/`our`/`state`/`has`, from signatures and from pointy blocks. Each bare
  name is resolved to the innermost declaration in scope, and the analyzer
  collects evidence at every use.
* **Decision.** A declared sigil wins. Without one, the evidence picks the
  sigil:

| Evidence | Kind | Strength |
|---|---|---|
| `x++`, `x += 1`, `++x` | `$` | hard |
| initializer or assignment: `[…]`, a comma list, `<a b>`, a range, `^n`, `xx`, `.map`/`.grep`/`.sort`/`.split`/`.words`/…, `*x` | `@` | hard |
| `{k => v}`, `%(…)`, `.Hash`/`.classify`/… | `%` | hard |
| `-> …`, `sub …`, a `{…}` block | `&` | hard |
| a number or string literal | `$` | hard |
| `for x` | `@` | hard, weakest |
| `x[…]`, `x.push` (and the other array mutators) | `@` | soft |
| `x{…}`, `x<…>` | `%` | soft |
| `x(…)` | `&` | soft |
| an initializer of unknown kind (`f()`, `KV.parse(…)`) | keep `$` | — |

Soft evidence names a use that a `$` serves too: `$x[0]`, `$x<k>` and
`$x.push` all work on an item holding the right thing, and autovivify from
`Any`. So soft evidence picks the kind only when nothing hard is there and
the value does not come from an expression of unknown kind.

Without that rule, `my parsed = KV.parse(…); parsed<pair>` became
`my %parsed = …`, which copies the Match into a Hash and loses it.

When several kinds are equally strong, it is an error. A soft use the chosen
kind cannot serve is an error too (`my thing = []; thing<key>`).

### Results

| Check | Rakudo | Raku++ |
|---|---|---|
| 10 sigil-free programs against the output of hand-sigiled twins (made on Rakudo) | 10/10 | 10/10 |
| 6 programs that must be refused, refused with the right message | 6/6 | 6/6 |
| every `.raku`/`.rakumod`/`.rakutest`/`.t` in this repository (1,333 files) translated: output identical to input | 1,333/1,333 | 1,333/1,333 |
| corpus time, one process | 39.5 s | 48.2 s |
| `showcase/js/js.raku` (2,173 lines), whole run | 2.8 s | 2.4 s |

The pass-through check is how the lexer was hardened. Ordinary code must
come back unchanged, so any difference is a lexing or resolution mistake.
The first run changed 77 files and refused 21. Every difference was traced
to a rule, and those rules are now the list under *Names* and *The lexer*
below.

`--explain` on `examples/basics.raku`:

```
$greeting      line 2    inferred  line 2: initialized from `'Hello'` (a single value)
$count         line 3    inferred  line 3: initialized from `3` (a single value); line 4: `count ++` updates a single value
@words         line 7    inferred  line 7: initialized from `<apple banana cherry>` (a word list); line 8: `.push` mutates an array; line 9: `words[…]` indexes it; line 10: `for words` iterates it
%ages          line 13   inferred  line 13: initialized from `{ alice => 30, bob => 25 }` (a Hash); line 14: `ages<carol>` looks up a key; line 15: `ages{…}` looks up a key
&double        line 17   inferred  line 17: initialized from `-> n { n * 2 }` (a routine); line 18: `double(…)` calls it
$total         line 20   inferred  line 20: initialized from `0` (a single value); line 21: `total +=` updates a single value
```

`examples/matrix.raku` is Q3 resolved: `for rows -> row { for row … }` infers
`-> @row` from `for row`, and the inner loop runs per element.

## Where it stops

**Strings.** Only `"{x}"` interpolates. `"x"` is the letter x, and `"$x"` is
still the sigiled form. A sigil-free language cannot tell a word in a string
from a variable.

**Pairs.** `x => 1` quotes `x`, as Raku does. `:x` is `:x(True)`, and the
`:$x` shorthand has no sigil-free spelling; write `:x(x)`. In a signature,
`:x` is the named parameter `:$x`.

**Names.** A sigil is also a namespace, so the free names shrink:
- not available as variables: keywords; the control routines (`fail`, `die`,
  `exit`, `done`, `take`, `emit`, …); the quote words (`m`, `s`, `q`, `rx`,
  `tr`, `qx`, …); native and NativeCall types (`int`, `str`, `size_t`,
  `buf8`, …); capitalized names;
- `my say = …` is refused. Rakudo allows `my \say`, after which the bare word
  means the variable: legal, but unreadable;
- a name followed by a term is a list-operator call (`fail 'boom'`);
- `x(…)` on a variable declared with `$`/`@`/`%` calls a routine named `x`,
  as in Raku.

**Copy or alias.** `my b = a` copies when `b` becomes `@b` and aliases the
Array when it becomes `$b`. The inference sees which kind, never which
intent.

**What is not translated:**
- regex bodies (`/…/`, `rx{…}`, `token { … }`), including code blocks inside
  them;
- `qq{…}` and heredoc bodies;
- strings handed to `EVAL`;
- modules: only the program's own file is translated. A sigil-free module
  loaded with `use` would need a `CompUnit::Repository` that translates
  before compiling.

**The compiler's variables keep their sigils:** `$_`, `$/`, `$0`, `$!`,
`$*OUT`, `@*ARGS`, `$^a`. `.method` on the topic already needs none.

**Messages.** A compiler error names the translated variable (`$count`), at
the right line.

**Cost.** The runner starts a second process: the translator's and the
program's.

## On each engine

**Rakudo** runs a `use` while it parses, so a module can take over the rest of
its importer. The importer's text is in `$*PROGRAM`, and `sub EXPORT` at the
compunit's top level runs at that point. `use Acme::Sigilless;` translates the
rest of the file, runs it and exits with its exit code. Line numbers are kept:
a `die` on line 12 reports line 12.

**Raku++** lexes and parses the whole unit before running any `use`, so the
sigil-free text is rejected before the module gets a turn. There the runner
(`sigilless prog.raku`) is the way in, unless the engine grows a hook. A
pre-lex source rewrite already exists for `use L10N::XX`
(`Interpreter::applyL10NSlang`), and that is the precedent.

### Raku++ divergences found on the way

| # | Snippet | Rakudo | Raku++ |
|---|---|---|---|
| 1 | P1: `my \x = $ = 5; x = 6` | assigns | *Cannot modify an immutable Int (5)* |
| 2 | P2: `my \a = [1,2,3]; a = 4, 5, 6` | `[4, 5, 6]` | *Cannot modify an immutable Array* |
| 3 | P4: `my $x = 5; my \x := $x; x = 9; say $x` | `9` | `5` |
| 4 | Q3: `-> \row` over `[[1,2],[3,4]]`, then `for row` | once per row | twice per row |
| 5 | `my $x; $x.push(1); say $x.raku` | `$[1]` | `[1]`; likewise `${:k(1)}` against `{:k(1)}` |
| 6 | `method !regex() {…}` or `method !rule() {…}` followed by another method | fine | the next method disappears: *No such private method '!after'* |
| 7 | `say <= a>` | `(= a)` | *Missing required term after infix* |
| 8 | Slang::Nogil 1.3, its own test | passes | *Undefined routine 'check-keywords'* |

Rows 1–4 are the sigilless-container family: a sigilless name bound to a
container should keep that container. The prototype avoids rows 6 and 7 in its
own source.

## Decisions open

1. **Publish it?** The prototype is a working `Acme::Sigilless`: a
   translator, a runner, and the `use` form on Rakudo. Publishing, the name,
   and where it lives are the user's call.
2. **Raku++ hook.** A general seam would make `use Acme::Sigilless;` work on
   Raku++ too: a module offering a source filter for the rest of the unit.
   That is an engine feature for a module's sake, which is the user's call
   ("module shims").
3. **A slang instead of a translator** (Rakudo only, lexical, no second
   process). It would need actions in QAST or RakuAST and would read ahead
   through `$/.orig` the way Nogil's keyword check does. It would also be tied
   to the frontend Rakudo happens to run.
4. **The eight divergences** above, independent of the module.
