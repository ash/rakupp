# Working with modules

A *module* is a reusable chunk of Raku — a file that defines subs, classes, or
roles that other programs pull in with `use`. Raku++ runs both the modules you
write yourself and, crucially, **modules installed from the ecosystem by
[zef](https://github.com/ugexe/zef)** (Raku's package manager). This page is the
practical guide: how `use` works, where Raku++ looks for a module, and how to
write your own.

If you just want to see it work:

```raku
use JSON::Fast;                             # a real zef-installed module
say to-json({ name => 'Ada' }, :!pretty);   # {"name":"Ada"}
```

---

## Using a module installed by zef

Raku++ does **not** ship its own package manager. Instead it reads the *same
module store that zef already populates* for a Rakudo install. So the workflow is:

1. Install the module the normal way, with zef (this needs Rakudo + zef):

   ```sh
   zef install JSON::Fast
   ```

2. Then just `use` it from Raku++ — no extra flags, no reinstall:

   ```raku
   use JSON::Fast;

   my %data = name => 'Ada', langs => ['Raku', 'C++'];
   say to-json(%data, :sorted-keys);

   my $back = from-json('{ "n": 42, "ok": true }');
   say $back<n> + 1;                  # 43
   ```

Under the hood, Raku++ looks for installed modules in the standard locations zef
writes to:

- **`~/.raku`** — your per-user zef install (the usual place `zef install` puts
  things).
- **Homebrew Rakudo's store** — `…/Cellar/rakudo/<version>/share/perl6/site` and
  `…/vendor`, on both Intel (`/usr/local`) and Apple Silicon (`/opt/homebrew`).

You don't configure any of this; if a module is installed, `use` finds it. Many
pure-Raku modules — `JSON::Fast`, `URI`, `Terminal::ANSIColor`, and more — load
and run unchanged. See [the module campaign](dev/V2-MODULES-PLAN.md) for what's
tested and where the current edges are.

> **If a module can't load, your program keeps going.** Raku++ treats a failed
> or unparseable `use` as a *warning*, not a fatal error:
> `===WARNING=== Could not find module 'Foo' (use ignored)`. The rest of your
> program still runs — handy when only one code path needs the module. (Rakudo
> would die at compile time; this is a deliberate Raku++ difference.)

---

## How `use` works

```raku
use Foo;                # load Foo and import what it exports
```

`use Foo;` does two things: it **loads** the module (runs it once), then
**imports** the names it makes public into your program. A module marks a sub or
class public with `is export`:

```raku
# lib/Greet.rakumod
unit module Greet;
sub hello($name) is export { "Hello, $name!" }
```

```raku
use Greet;
say hello("world");     # Hello, world!  — imported by bare name
```

A few more forms you'll see:

- `use Foo <a b c>;` / `use Foo :tag;` — pass an import list to the module, so it
  exports only the selected names (each module decides what the tags mean).
- `need Foo;` — loads the module.
- Pragmas like `use strict;`, `use fatal;`, `use lib …;`, `use experimental :…;`
  are recognised directly and need no file on disk.

A module that is itself a single class or grammar (`unit class …;`) makes that
type available by name:

```raku
# lib/Point.rakumod
unit class Point;
has $.x;
has $.y;
method gist { "($.x, $.y)" }
```

```raku
use Point;
say Point.new(x => 1, y => 2);   # (1, 2)
```

---

## Pointing Raku++ at your own module files

For modules you haven't installed — your project's own `lib/`, a checkout you're
hacking on — Raku++ searches a list of directories. From highest priority to
lowest:

| Source | Example | Notes |
|---|---|---|
| `use lib` in the program | `use lib 'my/libs';` | added to the front, wins over everything |
| `-I` on the command line | `rakupp -I lib app.raku` | Rakudo-compatible |
| `RAKULIB` environment variable | `RAKULIB=libs:more rakupp app.raku` | colon-separated, like a `PATH` |
| the current directory | `lib/`, `.`, `rakulib/` | the defaults, relative to where you run from |
| installed zef/Rakudo modules | `~/.raku`, Homebrew Rakudo | the store described above |

The name maps to a path in the obvious way — `use My::Shapes;` looks for
`My/Shapes.rakumod` — and for each search directory Raku++ tries both the
directory itself and a `lib/` under it (so pointing at a project root works as
well as pointing at its `lib/`). Recognised file extensions are `.rakumod`,
`.pm6`, `.raku`, and `.pm`.

So all three of these find a module in `./lib`:

```sh
rakupp app.raku                     # lib/ is a default, searched automatically
rakupp -I lib app.raku
RAKULIB=lib rakupp app.raku
```

---

## Writing your own module

Put the file where the name says, mark the public parts `is export`, and `use`
it. A module can export subs and hold classes:

```raku
# lib/My/Shapes.rakumod
unit module My::Shapes;

class Circle is export {
    has $.r;
    method area { π * $.r ** 2 }
}

sub describe($shape) is export { "area = {$shape.area.round(0.01)}" }
```

```raku
use lib 'lib';
use My::Shapes;

my $c = Circle.new(r => 2);
say describe($c);        # area = 12.57
```

Anything without `is export` stays private to the module — that's how a module
keeps helper subs to itself.

---

## Bundled shims

A few modules that lean on machinery Raku++ doesn't implement natively ship as
small **shims** in the [`rakulib/`](../rakulib) directory of the source tree
(for example a minimal `DBIish` that drives the `mysql` CLI). Because `rakulib`
is one of the default search directories, running from a Raku++ checkout picks
these up automatically; elsewhere, add it with `-I /path/to/rakupp/rakulib`.

---

## Current status and limits

Reading the zef store and running real modules is the focus of the
[v2.0 ecosystem campaign](dev/V2-MODULES-PLAN.md) — that page tracks which
modules pass, tiered by how thoroughly. The load path is deliberately practical
rather than complete; the notable gaps today:

- **Version/auth selection** (`use Foo:ver<1.2>:auth<…>`) isn't honoured yet —
  Raku++ loads the first matching install of a name.
- **A module's `our` subs import by their bare name**, not under a
  `Foo::name` fully-qualified path; export the names you want callers to use
  with `is export`.
- Modules that rely on **compile-time metaprogramming, slangs, or NativeCall
  bindings** Raku++ doesn't model may warn and load partially (or be ignored) —
  the warning tells you which one, and your program continues.

For the bigger map of how Raku++ relates to the wider ecosystem (the browser
build, the playground, the corpus), see [ECOSYSTEM.md](ECOSYSTEM.md).
