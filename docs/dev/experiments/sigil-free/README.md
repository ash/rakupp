# Acme::Sigilless — Raku without sigils (prototype)

A translator from sigil-free Raku to ordinary Raku, and a runner around it.
It works the same on Rakudo and on Raku++. The findings behind it are in
[SIGIL-FREE.md](../SIGIL-FREE.md).

```raku
my words = <apple banana cherry>;      # @words: initialized from a word list
words.push('date');
my ages = { alice => 30, bob => 25 };  # %ages: a Hash literal
ages<carol> = 35;
my double = -> n { n * 2 };            # &double: a routine
say double(21);

class Counter {
    has count = 0;                     # has $.count; `count` in a method is $!count
    method bump(step = 1) { count += step; self }
}
```

## Running

```bash
cd docs/dev/experiments/sigil-free && raku -Ilib bin/sigilless examples/basics.raku
```

`bin/sigilless` translates the program and runs the result with the engine
that runs it (`$*EXECUTABLE`), passing the remaining arguments on. Two options
come before the program's name:

* `--show` prints the translated program instead of running it;
* `--explain` lists every variable, the sigil it got and the evidence for it.

On Rakudo, `use Acme::Sigilless;` at the top of a program does the same from
inside it (the module reads the rest of the file at `use` time, translates it
and runs it). Raku++ parses the whole file before any `use` runs, so there the
runner is the way in.

## The rules

* **A sigil, once.** `my @queue = 1, 2, 3;` declares `@queue`; every later
  bare `queue` is `@queue`.
* **No sigil at all.** `my queue = …` takes its sigil from the evidence:
  the initializer (`[…]`, `{k => v}`, `<a b>`, a comma list, a range,
  `.map`/`.grep`/`.Hash`, a pointy block), the uses (`x[…]`, `x{…}`, `x<…>`,
  `x.push`, `x(…)`, `x++`, `x += …`) and `for x`. Parameters follow the
  same rules, and `*nums` is `*@nums`. A value from an expression of
  unknown kind (`KV.parse(…)`) stays an item, `$`, because subscripts and
  `.push` work on an item too.
* **One name, one kind.** Uses that disagree (`a[0]` and `a<k>`), a second
  declaration of a name, or an inner `$a` under an outer `@a`, are errors
  wherever a bare name has to be resolved.
* **Names that stay what they are:** keywords, the control routines
  (`fail`, `die`, `exit`, `done`, …), the quote words (`q`, `m`, `s`, `rx`,
  `tr`, …), native types, capitalized names (types and constants), and any
  name the program declares as a `constant`, `class`, `subset` or `enum`.
* **A name followed by a term is a call:** `fail 'boom'`, `set <a b>`.
* Attributes: `has count` is `has $.count`; `has !count` is `has $!count`.
* `name => value` quotes the name, `:name(value)` is a pair; strings take
  `"{name}"`.

## Layout

| Path | What |
|---|---|
| `lib/Acme/Sigilless.rakumod` | the lexer, the scope-aware analyzer and the emitter |
| `bin/sigilless` | the runner |
| `examples/*.raku` | sigil-free programs; `*.expected` is the output of their hand-sigiled twin in `reference/`, made on Rakudo |
| `errors/*.raku` | programs the translator must refuse |
| `t/examples.raku` | runs every example through the runner on the engines given |
| `t/errors.raku` | checks the refusals |
| `t/passthrough.raku` | ordinary Raku must come out unchanged |

```bash
cd docs/dev/experiments/sigil-free && rakudo t/examples.raku rakudo rakupp
```

`SIGILLESS_TOKENS=1` prints the lexer's tokens to stderr.
