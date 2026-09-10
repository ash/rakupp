# Working with modules

A *module* is a reusable chunk of Raku — a file that defines subs, classes, or
roles that other programs pull in with `use`. Raku++ runs both the modules you
write yourself and, crucially, **modules installed from the ecosystem** — by
[zef](https://github.com/ugexe/zef) (Raku's package manager) or by Raku++'s own
`rakupp install`, which write the same store. This page is the practical guide:
how `use` works, where Raku++ looks for a module, and how to write your own.

If you just want to see it work:

```raku
use JSON::Fast;                             # a real zef-installed module
say to-json({ name => 'Ada' }, :!pretty);   # {"name":"Ada"}
```

---

## Using an installed module

Raku++ reads the *same module store that zef populates* for a Rakudo install,
and `rakupp install` writes that store too — so a module installed either way is
usable by either engine, and one installed before you ever met Raku++ needs
nothing at all. The workflow is:

1. Install the module — the normal way with zef (this needs Rakudo + zef), or
   with Raku++'s own installer:

   ```sh
   zef install JSON::Fast
   rakupp install JSON::Fast      # the same store, no Rakudo needed
   ```

2. Then just `use` it from Raku++ — no extra flags, no reinstall:

   ```raku
   use JSON::Fast;

   my %data = name => 'Ada', langs => ['Raku', 'C++'];
   say to-json(%data, :sorted-keys);

   my $back = from-json('{ "n": 42, "ok": true }');
   say $back<n> + 1;                  # 43
   ```

`rakupp install` resolves against the same ecosystem index zef uses and runs a
distribution's own test suite before marking it installed. It also takes a
**directory** or a **URL** instead of a name, for a module that is not in the
ecosystem — or not in it yet:

```sh
rakupp install ./my-dist          # a checkout: the directory with META6.json
rakupp install https://github.com/ash/raku-modules/tree/main/Prompt-Hidden
                                  # a github page URL, monorepo subdirectory
                                  # and all — the URL from the address bar
rakupp install https://host/Foo-1.0.tar.gz         # or a release tarball
```

A URL is fetched and unpacked and then treated exactly as a directory would be:
same dependency resolution, same build hook, same test suite, same store. What
differs is integrity — nothing in a URL names the bytes it should deliver, so
there is no checksum to verify and the installer says so. An index install does
verify: a fez archive's URL carries its SHA-1 and a mismatch is refused.

Its full option list — version pins, `--dry-run`, `--list`, `uninstall`,
`reinstall`, `test` — is in [CLI.md](CLI.md#installing-modules). How much of the ecosystem runs today:
all 2,530 distributions, each with its sweep verdict, are listed at
[raku.online/modules/ecosystem](https://raku.online/modules/ecosystem/).

Under the hood, Raku++ looks for installed modules in the standard locations
these installers write to:

- **`~/.raku`** — your per-user store (the usual place `zef install` and
  `rakupp install` put things).
- **rakubrew's stores** — `~/.rakubrew/versions/<version>/install/share/perl6/site`
  and `…/vendor`, for every version rakubrew has installed. (Note the extra
  `install/` level, which the other layouts do not have.)
- **Homebrew Rakudo's store** — `…/Cellar/rakudo/<version>/share/perl6/site` and
  `…/vendor`, on both Intel (`/usr/local`) and Apple Silicon (`/opt/homebrew`).

A store only appears in that chain when it exists, and a failed `use` prints the
whole list it searched — so if the store you expect is not in the message, that
is the thing to fix rather than the module. A store anywhere else is one `-I`
away; see [the modules FAQ](faq/modules.md).

You don't configure any of this; if a module is installed, `use` finds it. Many
pure-Raku modules — `JSON::Fast`, `URI`, `Terminal::ANSIColor`, and more — load
and run unchanged. See [the module campaign](../dev/ecosystem/V2-MODULES-PLAN.md) for what's
tested and where the current edges are.

> **A `use` that fails is fatal**, as in Rakudo: the program stops and exits
> non-zero rather than running on without the module. Raku++ used to treat a
> missing or unparseable `use` as a warning and carry on, which read as
> convenient — you could run a program whose one unused code path wanted a
> module you did not have. In practice it mostly hid real failures: a module
> that half-loaded produced phantom output that looked like a working program,
> and the module campaign kept mistaking those for passes. Failing loudly costs
> nothing you cannot get with a `try require`.

---

## How `use` works

> Raku++ can **cache a module's parse** so later runs skip it (`use XML` goes
> 16.0 ms to 5.7 ms). Off by default — `rakupp --precomp-modules=on` enables it;
> see [CACHING.md](CACHING.md).

> This section is the practical view. For what the compiler actually does — the
> `Env` a module lives in during its load, why its AST is executed once and then
> kept only as storage, why calling a module routine is not a distinct
> operation, and the full list of divergences from Rakudo — see
> **[internals/MODULE-LOADING.md](../internals/MODULE-LOADING.md)**.

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
use lib 'lib';
use Greet;
say hello("world");     # Hello, world!  — imported by bare name
```

(Raku++ searches `lib/` by default and Rakudo does not, so the `use lib` line is
what makes the example run on both. The search path is in full below.)

A few more forms you'll see:

- `use Foo :tag;` — import only the names the module marked `is export(:tag)`.
  A plain `is export` is the `:DEFAULT` tag, so a bare `use Foo` brings it and
  `use Foo :tag` does **not**; write `use Foo :DEFAULT :tag` for both.
  `is export(:MANDATORY)` names always come. This matches Rakudo exactly.
- `use Foo <a b c>;` — a positional list, which Raku hands to the module's own
  `sub EXPORT` to interpret. Rakudo refuses the form when the module has no
  `EXPORT` sub; Raku++ then reads the words as tag names instead, which is a
  convenience that does not travel.
- `need Foo;` — loads the module without importing, so you call `Foo::bar` by
  its full name. (Raku++ imports on `need` as well, so a program that calls the
  short name after a `need` runs here and fails under Rakudo.)
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
| `-I` on the command line | `rakupp -I lib app.raku` | Rakudo-compatible, including the repo spellings — `-I file#/dir` for a plain directory of module files, `-I inst#/path` for an installation store |
| `RAKULIB` environment variable | `RAKULIB=libs,more rakupp app.raku` | paths separated by `,` or `:` — both accepted |
| the current directory | `lib/`, `.`, `rakulib/` | the defaults, relative to where you run from |
| installed modules | `~/.raku`, Homebrew Rakudo | the shared store described above |

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

A few modules that read **MoarVM's own memory layout** — not a Raku API, so no
other implementation can satisfy them either — ship as small **shadows** in the
[`rakulib/`](../../rakulib) directory of the source tree. Today that is
`NativeHelpers::Blob`, `NativeHelpers::CStruct` and `NativeHelpers::Pointer`,
whose ecosystem versions
find a Blob's or a CStruct's data by scanning MoarVM object headers; the shadows
keep the same surface and get the same pointers from the engine instead. Because
`rakulib` comes before the installed module store on the search path, an
installed copy of one of these names does not shadow the shadow. Running from a
Raku++ checkout picks them up automatically; elsewhere, add the directory with
`-I /path/to/rakupp/rakulib`.

(The directory's other file, `JS.rakumod`, is not a shadow: it is the
interpreter's stub for the `use JS` surface of [`--target=js`](JS.md).)

A dist whose own name is shadowed is skipped by `rakupp install`, with a note —
its ecosystem original cannot run under Raku++, and installing it would only put
a broken copy behind the shadow. **This is the one place the shared store is not
shared**: a Rakudo reading the same store does not find the skipped dependency
and must `zef install` it itself, or `use DBIish` there compiles and then fails
at `connect` with *Could not find NativeHelpers::Blob*.

Everything else is the real distribution: `DBIish` and its `DBDish::SQLite`,
`DBDish::mysql` and `DBDish::Pg` drivers run unmodified, and their own test
suites pass under Raku++ exactly as they do under Rakudo (820 assertions across
28 files, no difference — measured at DBIish 0.6.8 with all three servers
running).

---

## Built in: `Data::Native`

The mirror image of a shim. A shim is a Raku file that stands in front of a
module name; `Data::Native` is a module name the **compiler answers itself**,
from its own built-ins:

```raku
use Data::Native;

say to-json({ ok => True });            # JSON
say from-csv("a,b\n1,2\n", :headers);   # CSV
say sha256-hex('abc');                  # digests and HMAC
say uncompress(compress('big'.encode));  # zlib / gzip / raw deflate
say crypt_random_buf(32);               # bytes from the OS CSPRNG
```

Nothing to install, and nothing is loaded: no file is read, no dependency is
resolved, so the `use` is not on the program's start-up path at all. Five tags —
`json csv digest zlib random` — and thirty-two names, every signature copied
from the ecosystem module it stands in for, so moving a program to or from
`use JSON::Fast` is a one-line edit.

**It is portable, which is the point.** On any other engine the same line loads
a distribution of that name which composes the usual modules — `JSON::Fast`,
`Digest::SHA2`, `Compress::Zlib`, `Crypt::Random` — so a program written against
it runs on Rakudo too, once `zef install Data::Native` has put the distribution
there. (That install is what Raku++ does *not* need, and the asymmetry is the
whole feature.) Each tag exports a `*-backend()` sub that says which
implementation answered:

```raku
use Data::Native;
say json-backend();     # 'core' on Raku++; 'JSON::Fast' on Rakudo
```

A program using it also **compiles to a standalone binary**: there is nothing to
embed and nothing to find at run time, so `--exe --standalone` builds it.

The compiler stands aside — and the installed distribution wins — when the `use`
is versioned, when a search path names it (`-I`, `use lib`, which is what makes
`rakupp test <Dist>` test the distribution), or when the installed version is
**newer than the interface this engine implements**. That last rule is how the
distributions get released on their own schedule; installing them otherwise is
harmless and changes nothing.

Full guide: **[DATA-NATIVE.md](DATA-NATIVE.md)**.

---

## Current status and limits

Reading the zef store and running real modules is the focus of the
[v2.0 ecosystem campaign](../dev/ecosystem/V2-MODULES-PLAN.md) — that page tracks which
modules pass, tiered by how thoroughly. The load path is deliberately practical
rather than complete; the notable gaps today:

- **`:auth` is not honoured.** `use Foo:ver<1.2+>` does work: it selects the
  newest installed version that satisfies the request and fails the `use` when
  none does, as in Rakudo. `:auth<…>` is accepted and ignored, so two installs
  of one name that differ only by author are not told apart, and a request
  Rakudo would refuse loads here. (A `:ver` on a module found as a plain file
  under `-I`/`use lib` also fails here, where Rakudo loads the file regardless.)
- **Importing is coarser than Rakudo's.** A module's whole scope is published to
  the importing program, so its `my` subs, its non-exported `our` subs and its
  classes are all reachable by their bare names — not just what it marked
  `is export`. Code that works here may need real `is export` markings to work
  under Rakudo. (One carve-out: a non-exported sub whose name collides with a
  built-in stays module-private, so it can't shadow the built-in for you.) The
  same publication carries a module's own **imports** onward: what `Aye` got
  from its `use Bee` is callable in a program that only says `use Aye`. Rakudo
  keeps a module's imports to itself, so a program leaning on that needs its own
  `use Bee` to run there.
- **A module's `BEGIN` blocks and top-level code run on every run.** The *parse*
  can be cached ([CACHING.md](CACHING.md)), which is most of the cost, but Rakudo
  additionally serialises what its compile-time code produced and Raku++ does
  not.
- Modules that rely on **compile-time metaprogramming, slangs, or NativeCall
  bindings** Raku++ doesn't model will fail to load — and a failed load is
  fatal, by design: a `use` that silently vanished used to leave the program
  running against a half-built state.

For the bigger map of how Raku++ relates to the wider ecosystem (the browser
build, the playground, the corpus), see [ECOSYSTEM.md](../status/ECOSYSTEM.md).
