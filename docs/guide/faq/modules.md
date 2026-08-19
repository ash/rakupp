# FAQ — using modules that zef installed

Short answer: **point `-I` at the repository directory — the one that contains
`sources/`, `dist/` and `short/` — not at any of those subdirectories**, and
Raku++ will load what zef installed there.

```bash
rakupp -I /path/to/rakudo/share/perl6/site -e 'use File::Which; say which("ls")'
```

Everything below is the detail behind that line, including where the directory
is for each way of installing Rakudo.

## Where the repository is

zef does not put modules where you can see them. It installs into a
**CompUnit::Repository::Installation** — a content-addressed store — and the
layout is the same wherever Rakudo came from:

```
share/perl6/site/          ← this is the path to give -I
    short/                 index: module name → the file that provides it
    sources/               the module sources, named by SHA
    dist/                  one JSON metadata file per distribution
    resources/             data files, also named by SHA
    precomp/               Rakudo's precompiled bytecode (Raku++ ignores it)
```

The SHA-named files under `sources/` are not meant to be named directly — the
`short/` index is what turns `File::Which` into the right one. So a path like
`…/site/sources` is one level too deep, and `…/site/sources/A1B2…` is a file, not
a repository.

| how Rakudo was installed | the path to use |
|---|---|
| **rakubrew** | `~/.rakubrew/versions/<version>/install/share/perl6/site` |
| Homebrew | `/usr/local/Cellar/rakudo/<version>/share/perl6/site` (or `/opt/homebrew/…`) |
| `--prefix` build | `<prefix>/share/perl6/site` |
| zef's `--/precompile-install` user repo | `~/.raku` |

Note the extra `install/` level in the rakubrew layout — that is the one that
catches people out.

If you are not sure, ask Rakudo:

```bash
raku -e '.say for $*REPO.repo-chain'
```

Every `inst#…` line it prints is a store Raku++ can read.

## Two spellings, both accepted

Raku++ takes the same repository specs Rakudo does, in `-I`, in `use lib` and in
`RAKULIB`:

```bash
rakupp -I inst#$HOME/.rakubrew/versions/moar-2026.07/install/share/perl6/site …
rakupp -I $HOME/.rakubrew/versions/moar-2026.07/install/share/perl6/site …
```

The `inst#` prefix is explicit ("this is an installation store, not a directory
of `.rakumod` files"); without it Raku++ probes the directory and recognises the
store anyway. `file#` names a plain source directory, which is what a bare path
without a store in it means.

## You may not need `-I` at all

Raku++ consults these repositories by itself, in this order, before anything you
pass:

- `~/.raku` — the user repository, where `zef install --to=home` puts things
- every `~/.rakubrew/versions/*/install/share/perl6/{site,vendor}`
- every Homebrew `Cellar/rakudo/*/share/perl6/{site,vendor}`

So on a machine with rakubrew, `rakupp -e 'use JSON::Fast; …'` finds the module
with no flags. `-I` is for repositories in other places — a `--prefix` build, a
container image, a second checkout.

## "Could not find Foo" when the module is definitely installed

The usual cause is asking for a **distribution** name rather than a **module**
name. They are often the same, and sometimes not:

```bash
rakupp -e 'use Digest'          # Could not find Digest
rakupp -e 'use Digest::SHA1'    # fine — the dist "Digest" provides this
```

Rakudo fails on `use Digest` in exactly the same way, so if you are unsure, try
the same line under `raku`: if it fails there too, the name is the problem, not
the engine. To see what a distribution actually provides:

```bash
raku -e 'say $*REPO.repo-chain.map(*.candidates("Digest::SHA1")).flat.head.meta<provides>.keys'
```

## What still does not work

Of the 35 distributions in one real zef store, 33 load under Raku++ unchanged.
The ones that do not are not a repository problem — they are modules using
language or runtime corners Raku++ has not finished (zef's own `Zef::Client`
chain is the notable one). If a module fails **after** it is found — a parse
error, a missing method — that is a bug worth reporting, with the module name:
<https://github.com/ash/rakupp/issues>.

Raku++ also ignores `precomp/`: it reads the sources and parses them itself, so
a store precompiled by a different Rakudo version is not a problem, and no
precompilation of your own is required. It is why the first `use` of a large
module costs a parse rather than a load — see [caching](../CACHING.md) for the
AST cache that softens that.
