# FAQ — installing and finding modules

Raku++ **reads the store zef writes**, so the modules you already installed with
Rakudo's zef need nothing at all. "Install a module" means `zef install Foo`, or
Raku++'s own `rakupp install Foo` — the two write the same store — and
everything after that is about Raku++ finding it. This page is the questions
people actually hit; [MODULES.md](../MODULES.md) is the tour, and
[differences.md](differences.md) is where the two engines part company.

## How do I install a module for Raku++?

```sh
zef install JSON::Fast          # the normal way, with Rakudo's zef
rakupp install JSON::Fast       # or Raku++'s own installer, same store
rakupp -e 'use JSON::Fast; say to-json({ a => 1 })'
```

That is the whole workflow. There is no second install step, no Raku++-specific
package format, and nothing to precompile: Raku++ reads the sources out of the
same store and parses them itself. Anything zef put in `~/.raku` before you ever
met Raku++ is already usable — the two engines share the store, in both
directions.

`rakupp install` resolves against the same ecosystem index zef uses, runs each
distribution's own test suite before marking it installed, and writes the same
`~/.raku`, so it is a drop-in for a machine with no Rakudo on it:

```sh
rakupp install Foo:ver<1.2.3>   # a specific version (installs are additive)
rakupp install --dry-run Foo    # print the plan, write nothing
rakupp install --list           # what is installed: identity, installer,
                                # module files, bin wrappers (-q: identities)
rakupp uninstall Foo            # remove what THIS installer put there
```

## Can I install something that is not in the ecosystem?

Yes — give a path or a URL where a name would go.

```sh
rakupp install my-dist          # a checkout: the directory with META6.json
rakupp install ~/src/my-dist    # ./x, /x, ~/x, dists/x, C:\x all work too
rakupp install https://github.com/ash/raku-modules/tree/main/Prompt-Hidden
rakupp install https://host/Foo-1.0.tar.gz
```

A bare word is a path when it names a directory with a `META6.json` in it,
and a module name otherwise, so `rakupp install Foo` does the obvious thing
from either side of a checkout.

The github form takes the URL from your address bar, including the
`/tree/BRANCH/SUBDIR` shape a monorepo of modules produces; without a `/tree/`
it tries `main` and then `master`. Either way the archive is fetched, unpacked
and installed exactly as a directory would be — dependencies from the index,
the distribution's own test suite, the same `~/.raku`.

One thing is genuinely weaker than an index install, and it is worth knowing
rather than discovering: **a URL has no checksum.** A fez archive's URL carries
the SHA-1 of its contents, and `rakupp install Foo` refuses an archive that does
not hash to it. Nothing in an arbitrary URL names the bytes it should deliver,
so there is nothing to check and TLS is the only integrity — which the installer
prints, every time, rather than leaving you to assume otherwise.

`uninstall` does not take a URL: the store knows distributions by name, and
finding the name behind a URL would mean downloading it first. Use the name, or
`rakupp install --list` to see what is there.

Each command prints its own full usage when you give it no arguments. If you
want zef itself on such a machine, zef runs under Raku++ too — `rakupp
/path/to/zef install Foo` — with the caveats in
[MODULES.md](../MODULES.md#current-status-and-limits).

## `rakupp install` says it cannot find install.raku

That message comes from v3.26.0 or earlier. The installer used to be a Raku program
shipped beside the binary — `libexec/rakupp/install.raku` in an installed
layout, `tools/install.raku` in a checkout — and the binary looked only there,
so a `rakupp` copied on its own into a container, or installed by a route that
dropped `libexec/` (Homebrew's prebuilt macOS binary did), had no installer at
all. Either upgrade, or put the file from the **same release** back beside the
binary.

Since then the installer is carried inside the binary and the message cannot
appear: `rakupp install` works from a lone executable, wherever it sits. So
does `rakupp doc`, which carries its two guides the same way.

## Where does it look?

At the standard zef locations, and you never have to configure them:

- `~/.raku` — the per-user store, where `rakupp install` and a plain
  `zef install` both put things
- every `~/.rakubrew/versions/*/install/share/perl6/{site,vendor}`
- every Homebrew `Cellar/rakudo/*/share/perl6/{site,vendor}`
- `lib`, `.` and `rakulib`, for the program's own files
- the `rakulib` beside the `rakupp` binary itself, which is how a checkout's
  bundled shims are found without an `-I`

**The failure message is the list.** When a `use` fails, Raku++ prints every
place it looked, in order:

```
$ rakupp -e 'use Nope'
Could not find Nope in:
    lib
    .
    rakulib
    /path/to/rakupp/../rakulib
    /Users/ada/.raku
    /usr/local/Cellar/rakudo/2026.08/share/perl6/site
    /usr/local/Cellar/rakudo/2026.08/share/perl6/vendor
```

If the store you expect is not in that list, that is the bug to chase — not the
module. A store only appears when it exists, so an empty
`~/.rakubrew/versions` contributes nothing.

## My store is somewhere else

Point `-I` at the **repository directory** — the one containing `short/`,
`sources/` and `dist/` — not at any of those subdirectories:

```sh
rakupp -I ~/.rakubrew/versions/moar-2026.08/install/share/perl6/site -e 'use Foo'
rakupp -I inst#$HOME/rakudo-dev/share/perl6/site                     -e 'use Foo'
```

Both spellings work, in `-I`, in `use lib` and in `RAKULIB`. Rakudo's `inst#`
prefix says "installation store"; without it Raku++ probes the directory and
recognises one anyway. `file#` names a plain directory of `.rakumod` files.

Not sure where yours is? Ask Rakudo — every `inst#…` line is a store Raku++ can
read:

```sh
raku -e '.say for $*REPO.repo-chain'
```

The layout differs by installer, and the easy one to miss is **rakubrew's extra
`install/` level**: `~/.rakubrew/versions/<ver>/install/share/perl6/site`, where
Homebrew has `Cellar/rakudo/<ver>/share/perl6/site`.

## The files in there are named like `A1B2C3…` — do I use those?

No. A zef store is content-addressed: `sources/` holds the module text under a
SHA name, `dist/` one JSON file per distribution, and `short/` is the index that
turns `JSON::Fast` into the right SHA. You always name the *repository root*;
the index does the rest. (`precomp/` is Rakudo's bytecode — Raku++ ignores it,
which is why a store precompiled by another Rakudo version is never a problem.)

## "Could not find Foo" and I am sure it is installed

Nine times in ten the name asked for is the **distribution**, not a **module**
it provides. They are often the same and sometimes not:

```sh
rakupp -e 'use Digest'          # Could not find Digest
rakupp -e 'use Digest::SHA1'    # fine — the dist "Digest" provides this
```

Rakudo refuses `use Digest` in exactly the same way, so try the failing line
under `raku`: if it fails there too, the name is the problem, not the engine.

## Can I use a module I am working on, uninstalled?

Yes — that is the `lib` entry in the list above, so a checkout laid out the
usual way needs nothing at all:

```sh
cd MyModule && rakupp -e 'use MyModule; …'      # lib/MyModule.rakumod
rakupp -I../Other/lib -e 'use Other'            # somewhere else
```

## It is found, but then it breaks

That is worth reporting — a parse error or a missing method *after* the module
is located is an engine gap, not a path problem. Of the 35 distributions in one
real store, 33 load unchanged; what usually fails is compile-time
metaprogramming, slangs, or NativeCall bindings Raku++ does not model, and
`use Foo:ver<…>` is honoured — the newest installed version that satisfies it wins — while `:auth<…>` is accepted and ignored. The list of known edges
is in [MODULES.md](../MODULES.md#current-status-and-limits); new ones belong at
<https://github.com/ash/rakupp/issues> with the module name.
