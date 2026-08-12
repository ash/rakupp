# The Installer and the Store

`rakupp install Foo` downloads a distribution from the Raku ecosystem, runs
its tests, and installs it — into **the same on-disk store that Rakudo and zef
already share**. That sentence carries the whole design: there is no rakupp
package database, no private format, no import step. The store *is* the
interface between the engines, and this chapter is about both sides of it —
the layout that makes a directory tree function as a protocol, and the
installer that writes it.

The order of construction matters here. Raku++ could **read** the store long
before it could write it: Chapter 31 ends with `use` resolving zef-installed
modules straight out of Rakudo's install tree. Then the zef-compatibility work
gave the engine a **writer** — enough of `CompUnit::Repository::Installation`
that zef itself, running under rakupp, could install things. `rakupp install`
came last, as a thin, careful client of a writer that already existed. Install
is a new front door on machinery the engine already had.

## The layout

A store is one directory — a *prefix* — with this inside:

```
~/.raku/
├── version              the layout version (2)
├── repo.lock            what writers flock before mutating anything
├── sources/<id>         one file per installed module source
├── dist/<dist-id>       one JSON file per distribution: its full META
├── short/<sha1(name)>/  the index: one entry per (module name, dist)
├── resources/<id>       content of each declared resource file
├── bin/<id>             content of each bin/ script
└── precomp/             Rakudo's bytecode cache (rakupp ignores it)
```

Every engine's default chain includes at least two prefixes. Rakudo's is
`home` (`~/.raku`) then `site`/`vendor`/`core` under its own installation;
rakupp searches `~/.raku` first and then every Homebrew Rakudo's `site` and
`vendor` — so a module zef installed for Rakudo is found by rakupp with no
configuration at all. The two installers default to *different ends* of the
shared chain: zef writes Rakudo's site store, `rakupp install` writes `~/.raku`
(`--to` to aim elsewhere). Both engines read both.

The clever part of the layout is `short/`. A `use JSON::Fast` must find one
file among thousands without scanning anything, so the index is **addressed by
hash**: the directory name is the SHA-1 of the module name, and each file
inside it describes one installed distribution providing that name. The file's
*name* is the distribution's id; its *content* is five short lines:

```
$ cat site/short/E0FD4AFC…0FB899A/8270A3B9…41A908A
      # …/short/<sha1("File::Temp")>/<dist-id>
0.0.12                                ← version
zef:raku-community-modules            ← auth
                                      ← api
DBE25863E06AEC6820058FA548E27780308E6C43   ← the sources/ file to load
1375FC65817F5B56701E20EC2F4101028E8363F6   ← a checksum (see below)
```

Resolution is: hash the name, open the directory, pick an entry whose first
line satisfies the `use` version constraint, load `sources/<line 4>`. Three
lines of metadata, one pointer, done. The `dist/<dist-id>` JSON is *not* read
to resolve a `use` — it exists for everything else: `%?RESOURCES` and
`$?DISTRIBUTION` binding, listing, uninstalling.

## Two writers, one reader contract

Rakudo's writer and rakupp's writer produce interchangeable stores, but they
are not byte-identical, and the differences say what the contract actually is:

| | Rakudo / zef | Raku++ |
|---|---|---|
| `sources/` file name | SHA-1 of *module name + dist-id* | SHA-1 of the file's *content* |
| line 5 of a short entry | SHA-1 of the CRLF-normalised source — its precomp layer's validation checksum | the dist-id, repeated |
| `precomp/` | written and maintained | never written, never read |

Neither engine's reader cares about any of these. The `sources/` name is an
opaque token — you read whatever line 4 names; nothing recomputes the hash.
Line 5 is consulted only by Rakudo's precompilation machinery, which rakupp
does not participate in. That is the contract in practice: **filename of the
short entry, lines 1–4, and the blob those point at**. Everything else is a
writer's private convention riding along in a shared file.

(The formulas above are measured, not quoted from documentation: hash a real
store's entries and compare. Line 5 of a 2021-era entry in a long-lived home
store matches *no* current formula — the checksum recipe has changed across
Rakudo versions, which is itself evidence that nothing load-bearing reads it.)

One Raku++ divergence from Chapter 31 needs an update in this light: `use
Foo:ver<1.2+>` adverbs are discarded during lib-path search, but against the
*installed* index the `:ver` constraint **is** honoured — it is checked against
line 1 of each candidate entry. `:auth` and `:api` are still not consulted.

## The writer is the engine's

`CompUnit::Repository::Installation.install` is implemented in C++, in the
same method-dispatch code that serves every other built-in class:

```cpp
// src/Builtins.cpp
// $cur.install($dist, :$force) — write the CURI layout under `prefix`
// (sources/<sha>, short/<sha1(name)>/<dist-id>, dist/<dist-id> JSON,
// resources/, bin/). rakupp reads exactly this to resolve `use`.
```

It computes the dist-id as `sha1(name \0 ver \0 auth \0 api)`, writes each
provided module's source as a content-addressed blob, writes one short entry
per provided name, copies `resources/` and `bin/` payloads, and writes the
`dist/` record with a `files` map — relative path → blob id — which is what
later makes uninstall able to know exactly which blobs are this
distribution's. It returns the dist-id, and that return value is load-bearing:
the installer records it as provenance.

This writer exists because of zef, not because of `rakupp install`. Running
zef under rakupp meant implementing the parts of the `CompUnit` API zef
drives, and `.install` is the heart of it. When the time came to build our own
installer, the choice was already made: use the engine's writer through the
same public spelling zef uses —

```raku
# tools/install.raku
my $repo = CompUnit::RepositoryRegistry.repository-for-spec("inst#$prefix");
my $dist-id = $repo.install($dist, :force($force));
```

**What went wrong.** The first version constructed the repository as
`CompUnit::Repository::Installation.new(prefix => $to)` — which parses, runs,
and installs nothing, silently. `.new` does not thread the prefix through to
the writer, so every file operation targeted `"/sources"` and failed into the
void. The fix is twofold: `repository-for-spec` is the constructor that
carries the prefix, and the writer now *refuses loudly* when its prefix is
empty rather than failing file-by-file in silence. A writer aimed at a shared
store does not get to guess.

## The installer is a shipped Raku program

`rakupp install` and `rakupp uninstall` are not implemented in the binary.
`main.cpp` recognises the two words and rewrites its own argument vector:

```cpp
// src/main.cpp — the same trick `python -m pip` pulls, with a nicer spelling
for (const char* rel : {"/../libexec/rakupp/install.raku", "/../tools/install.raku"}) {
    std::string cand = exeDir + rel;
    if (std::ifstream(cand).good()) { script = cand; break; }
}
```

so the command becomes `rakupp <path>/install.raku …` — a Raku program shipped
with the release (`libexec/rakupp/` in an installed layout, `tools/` in a
checkout). The reasons are worth spelling out, because "write the package tool
in C++" is the default instinct and it is wrong here:

- A compiled `--exe` binary and an embedded `librakupp` must not carry an
  HTTP client, an ecosystem-index parser and a tar reader (Chapter 28 is an
  entire chapter about removing things from the binary).
- The installer changes at ecosystem speed, not engine speed.
- It is dogfood: the project's own tooling running on the interpreter it
  ships, which is the policy everywhere else in `tools/`.

The program is ~600 lines and its shape is a pipeline:

**Index.** `https://360.zef.pm/index.json` — the fez ecosystem's index, one
JSON array of every distribution's META plus an archive path. Cached in
`~/.raku/rakupp-install/` for 24 hours; `--refresh` refetches;
`RAKUPP_INSTALL_INDEX` substitutes a file or URL, which is how the test suite
runs without a network and how a mirror slots in.

**Resolve.** Breadth-first over `depends`, newest satisfying version first,
`:from<native>` and `:from<bin>` dependencies reported but not fetched, plan
reversed at the end so dependencies install before their dependents:

```
$ rakupp install --dry-run JSON::Class
plan (6 distributions, dependencies first):
  JSON::Fast:ver<0.20.1>:auth<zef:timo>   https://360.zef.pm/J/SO/JSON_FAST/d5c8426f…be8b.tar.gz
  JSON::Name:ver<0.0.7>:auth<zef:jonathanstowe>   …
  JSON::OptIn:ver<0.0.2>:auth<zef:jonathanstowe>   …
  JSON::Unmarshal:ver<0.18>:auth<zef:raku-community-modules>   …
  JSON::Marshal:ver<0.0.25>:auth<zef:jonathanstowe>   …
  JSON::Class:ver<0.0.21>:auth<zef:jonathanstowe>   …
dry run: nothing written
```

**Fetch and verify.** Transport is `curl` and `tar` — present on every
platform this targets, TLS certificate verification on (the installer never
passes `-k`). And the archive URLs above contain their own second factor: fez
archives are content-addressed, the path stem *is* the SHA-1 of the tarball.
A fetched archive is hashed and refused on mismatch, so a compromised mirror
or a truncated download cannot become an installed module. When an index
entry carries no checksum in its path, the installer says so out loud rather
than pretending the gate applied.

**Test.** Before a distribution is installed, its own `t/` suite runs under
rakupp — dependencies were installed first, so the tests see them. This gate
earned its keep on its very first live run: JSON::Unmarshal 0.18's suite
failed one test under rakupp, the installer refused the whole chain, and the
failure became an engine bug report (a real bug — named-argument
unmarshalling — since fixed). `--no-test` is the documented override, and it
is a statement about what "installed" means here: not "files copied" but
"demonstrated to work under this engine, on this machine".

**Write.** The engine's `.install`, under a real `flock` on the store's
`repo.lock` (two new builtins, `rakupp-repo-lock`/`-unlock`, because the
store is shared and a half-written short entry is another engine's crash).
Then the returned dist-id is appended to `<prefix>/rakupp-install/owned` —
one line per distribution *this tool* installed. That file is the whole
provenance system, and uninstall is its consumer.

## Update means add

`rakupp install Foo` with a newer version available installs the newer version
**beside** the old one. Nothing is removed; the store's resolution — pick
among short-entry siblings by version — does the rest, and an installed
program pinned to `:ver<1.2.3>` keeps resolving to it. This is not an
implementation shortcut, it is what the layout wants: the store is
content-addressed and append-friendly, and "replace" would be a delete
wearing a trench coat. Reclaiming space is uninstall's job, explicitly.

## Uninstall is garbage collection

Deleting from a shared, content-addressed store is the hard half, which is
why it landed last and why the checker (below) was written *before* the
delete path. `rakupp uninstall Foo` refuses three ways before it touches
anything:

- **Not installed by us.** If the dist-id is not in `rakupp-install/owned`,
  it is zef's (or something else's), and removing another manager's
  installation silently is a surprise in someone else's tool. `--force` for
  people who mean it.
- **Still depended on.** Any other installed distribution whose `depends`
  names a module this one provides blocks the removal, with the dependents
  named.
- **Ambiguous.** Two installed versions match a bare name; name a version.

The removal itself is ordered by failure mode, under the same `repo.lock`:

1. **Index entries first** — every `short/<sha1(name)>/<dist-id>` this
   distribution owns. A crash after this step leaves orphaned blobs, which is
   wasted disk; the reverse order would leave live index entries pointing at
   deleted blobs, which is a broken `use`.
2. **Blobs nothing else references.** The `files` maps of every *other*
   `dist/` record are the mark phase; only unreferenced blobs are swept. Two
   distributions shipping a byte-identical file share one blob under rakupp's
   content-addressed writer, and the suite pins that the shared blob survives
   the first uninstall.
3. **The dist record last** — a crash mid-way leaves a record that still
   describes what remains to finish.

Note what provenance does *not* do: nothing stops zef from removing a
distribution rakupp installed. The store has no owner field, and adding one
would break the symmetry that makes the whole arrangement work. `owned` binds
only rakupp's own destructive path; politeness is enforced at home.

## The checker

`rakupp install --check` walks one store and reports; it fixes nothing:

```
$ rakupp install --check
store: /Users/ash/.raku
store check: 3 distributions, 0 broken, 0 unreferenced blobs
```

It distinguishes two severities. **Broken** — an unreadable `dist/` record, a
short entry pointing at a missing dist record, a missing blob behind a live
entry, a provided module with no index entry — each is a `use` that will fail
or a record that cannot be trusted, and any of them makes the exit code 1.
**Unreferenced blobs** are wasted disk, reported and exempt: in a shared
store, a blob this engine cannot account for might be another writer's, and a
checker that "cleans" what it does not understand is how shared state gets
destroyed. There is deliberately no `--fix`.

## What went wrong: damage travels between engines

The checker's first run on a real machine found real damage — and the story
is a compressed lesson in what sharing a store means.

A July experiment (zef running under rakupp, mid-debugging) had left a
`File::Temp` registration in the home store whose source blob no longer
existed: a live short entry pointing at a deleted `sources/` file. It sat
there for a month without a symptom, because **Rakudo's precomp cache was
serving `use File::Temp` without ever touching the store's blob** — the
dangling pointer was behind a cache hit. The first mutation of that store in
a month (the lock file the new uninstall path created) perturbed the mask,
Rakudo revalidated, followed the dangling entry, and began dying *at compile
time* on any `use File::Temp` — while rakupp kept working, because its
reader skips an entry whose blob is missing and falls through to the site
store, where zef's intact copy lives.

Read that again with the roles labelled: a **rakupp**-era write broke
**Rakudo**, a month later, unmasked by a **third** tool's lock file, and the
two engines disagreed about whether anything was wrong because their readers
degrade differently. A shared store is shared blast radius. That is why every
mutation locks, why uninstall orders its deletions by what a crash would
leave behind, why the checker exists and reports the store path it checked —
and why it was written before the code that deletes.

(The cleanup itself was two file moves — the stale entry and its dist record
out of the store — after which both engines loaded File::Temp again. The
checker, the refusals and the ordering all predate the first real damage they
were designed for; the damage predates them all.)

## Interchange, stated precisely

What "rakupp and zef are interchangeable" means, claim by claim, each one
gated in CI or verified on a real store:

| Claim | Evidence |
|---|---|
| rakupp reads zef-installed modules | Chapter 31's resolver; the whole module battery runs on zef-installed deps |
| Rakudo reads rakupp-installed modules | a 7-distribution graph (`License::SPDX` ← `JSON::Class` ← …) installed by `rakupp install` loads under Rakudo from the same store, resources included — `License::SPDX.new.licenses.elems` is 727 under both |
| zef itself runs under rakupp | zef's install path drives the engine's `.install` — the same writer `rakupp install` uses |
| the stores compose | one `~/.raku` holding zef-written and rakupp-written distributions side by side resolves under both engines; the two entry dialects differ only in lines no reader consults |
| mutation is coordinated | both writers `flock` the same `repo.lock` (on Windows rakupp proceeds unlocked, and says so) |

And the honest boundary of the claim: **precompilation does not travel.**
Rakudo's `precomp/` is keyed to its compiler build and rakupp neither writes
nor invalidates it; rakupp's own AST cache (Chapter 29) lives outside the
store entirely, in `~/.cache/rakupp/precomp`, and is off by default. Each
engine's cache is private state layered over shared truth — which the
File::Temp story shows is exactly where the seams are: the store stayed
consistent between engines, and it was a *cache* that made them disagree.

The suite behind all of this is `t/install/run.raku` — 22 checks, fully
offline (a fixture index, local archives, a scratch `HOME`), covering the
plan, the checksum refusal, the test gate, additive updates, every uninstall
refusal, the deletion ordering, shared-blob survival, and `--check` clean
before and after every mutation. It runs in CI on every push.
