# modinfo — a Raku distribution inspector

`modinfo` reads `META6.json` files, builds the dependency graph, validates the
metadata, fingerprints the source, and reports in table / JSON / YAML / XML.

It is the **ecosystem showcase**: almost nothing below the argument parser is
hand-rolled. Seventeen distributions from the ecosystem's most-depended-on list
do the work, and the program runs unchanged on Rakudo and on Raku++.

```sh
build/rakupp showcase/modinfo/modinfo.raku list
build/rakupp showcase/modinfo/modinfo.raku deps Gadget
build/rakupp showcase/modinfo/modinfo.raku path JSON::Fast --installed
build/rakupp showcase/modinfo/modinfo.raku rank --path=~/dists --top=20
build/rakupp showcase/modinfo/modinfo.raku export --format=xml --out=/tmp/report
```

`path` answers where a module (or every module of a distribution) lives on
this machine: the real file under a scanned checkout, or the installation
store's content-addressed source blob for `--installed` — resolved from the
same repository chain the engine itself searches.

## What it is built on

| Module | What it does here |
|---|---|
| **JSON::Fast** | reads every `META6.json`; writes the JSON report |
| **YAMLish** | reads the config file; writes the YAML report |
| **XML** | builds the XML report as a document, then indents the tree |
| **Config** | layered configuration with dotted-path access (`report.width`) |
| **Hash::Merge** | merges those layers: defaults < file < command line |
| **IO::Glob** | finds `*/META6.json`, and the installed-distribution database |
| **File::Find** | walks `lib/` to catch files `provides` forgot |
| **File::Which** | locates the engine `modinfo about` is running under |
| **File::Directory::Tree** | creates the `--out` directory |
| **File::Temp** | atomic writes: temp file in place, then rename |
| **Digest** (SHA-1) | per-file content hashes |
| **MIME::Base64** | turns those into npm-style `sha1-<base64>` integrity strings |
| **Abbreviations** | `modinfo show JSON::F` resolves to `JSON::Fast` |
| **Text::Utils** | `wrap-paragraph`, `commify`, `list2text` |
| **Terminal::ANSIColor** | named styles |
| **Color** | the green→red heat gradient on the count columns |
| **Data::Dump** | `--debug` |

Config ships only a NULL parser — every real format lives in its own
distribution — so `modinfo.raku` defines a `Config::Parser::YAMLish` class that
satisfies Config's parser interface on top of YAMLish. Composing two ecosystem
distributions that were never written for each other is the point.

## Commands

| Command | What it prints |
|---|---|
| `list` | one row per distribution: version, auth, dependency counts, provides |
| `show <dist>` | one distribution in full, with its fingerprint |
| `deps <dist>` | what it depends on, as a tree, cycles cut and labelled |
| `rdeps <dist>` | what depends on it, same shape |
| `graph` | roots, leaves, topological build order, cycles, external references |
| `rank` | distributions ranked by number of dependents |
| `check [<dist>]` | metadata validation; exits non-zero when anything is an ERROR |
| `export` | the whole model as `json`, `yaml`, `xml` or `all` |
| `about` | what modinfo is built on, and which engine is running it |

`<dist>` may be an exact name, its shortest unique abbreviation (which is what
Abbreviations computes for the whole set), or any unique prefix — `modinfo show
Core` and `modinfo show Corelib` agree.

## Where the distributions come from

Two sources, and both engines read them the same way:

```sh
modinfo list --path=DIR     # unpacked distributions under DIR (the default)
modinfo list --installed    # Rakudo's installation database (~/.raku/dist/*)
```

`rank` over a directory of unpacked distributions reproduces the measurement
that produces an ecosystem's "most depended-on" list — the same reverse-
dependency count that picked the modules in the table above.

## The bundled corpus

`fixtures/dists/` holds six small distributions, and they are what makes the
two-engine claim checkable rather than hopeful:

| Fixture | Why it exists |
|---|---|
| `Corelib`, `Widget`, `Gadget` | a clean three-level chain; Gadget also names an out-of-set dependency, so the graph has an external edge |
| `Loopy` ↔ `Knot` | a deliberate dependency cycle: the topological sort must fail on exactly these two, and the tree walk must cut the loop |
| `Rusty` | deliberately broken metadata — `version` is `*`, no description or license, a `provides` entry pointing at a missing file, a duplicated and a self-referential dependency, a `lib/` file nothing declares, a missing resource |

`modinfo check` finds thirteen distinct problems in `Rusty` and none in the other
five.

## Both engines, byte for byte

```sh
RAKUPP=build/rakupp sh showcase/modinfo/compare.sh
```

runs all thirteen commands under `raku` and under `rakupp` and diffs STDOUT.
`about` is left out on purpose — it reports the engine it is running under, so
the two runs are *supposed* to differ there.

The same comparison holds on a real corpus: over the 61 unpacked distributions
of the module battery, `list`, `graph`, `rank`, `check` and all three export
formats are byte-identical under both engines.

If the distributions modinfo depends on are not installed, point `MODLIB` at a
comma-separated list of `lib` directories that hold them — both engines accept
the same `RAKULIB` spelling:

```sh
MODLIB=$(ls -d ~/dists/*/lib | paste -sd, -) RAKUPP=build/rakupp sh compare.sh
```

## Determinism

A report that two engines can be diffed against has to be reproducible, and two
things in Raku are not, unless you make them so:

- **Hash order.** Every listing sorts — distributions, provides, dependencies,
  findings, and the JSON/YAML keys. `IO::Glob`'s `.dir` yields filesystem order,
  so the scan sorts its results before reading anything.
- **XML attributes.** An element's attributes live in a `Hash`, so serialising
  two of them puts them in an unpredictable order — a run-to-run difference, not
  just an engine one. The report's own indenter emits attributes sorted.

There are no timestamps anywhere in the output, for the same reason.

## Configuration

`modinfo.yml` (or `~/.modinfo.yml`, or `--config=FILE`) sets the defaults:

```yaml
scan:
  path: fixtures/dists
report:
  width: 92
  color: true
rank:
  top: 20
```

Command-line options win over the file, which wins over the built-in defaults.
