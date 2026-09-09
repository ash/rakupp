# Dogfooding: Raku++ builds itself with Raku

The tools that build, test, measure and publish Raku++ are — wherever it makes sense —
written in Raku and executed by `rakupp` itself. That is deliberate: every run
of the toolchain is a real-world workout for the interpreter, and it keeps the
project honest about what the language implementation can actually do.

## The self-hosted tools

| Tool | What it does | Run as |
|---|---|---|
| [`tools/run-roast.raku`](../../tools/run-roast.raku) | The full Roast harness: runs all 1,464 spec-test files with timeouts, parses TAP, classifies pass/partial/no-TAP/timeout, prints the per-synopsis table that goes into [ROAST.md](ROAST.md). The entire coverage figure is measured *by* Raku++ — and thanks to the ~2 ms startup, the whole 1,464-file suite runs in about 3½ minutes, so refreshing the numbers is a coffee break, not an overnight job. | `build/rakupp tools/run-roast.raku [PATTERN]` |
| [`tools/gen-unicode.raku`](../../tools/gen-unicode.raku) | The UCD parser: reads `UnicodeData.txt`, `NameAliases.txt` and `DerivedNumericValues.txt` (40k+ lines) and generates the C++ character-name / category / numeric-value tables in `src/`. Raku++ literally generates part of its own source. | `build/rakupp tools/gen-unicode.raku` |
| [`tools/run-bench.raku`](../../tools/run-bench.raku) | The benchmark harness behind [BENCHMARKS.md](BENCHMARKS.md): times interp / `--exe` / Rakudo as fresh subprocesses. Also runs under Rakudo, so the harness language can't bias the results. | `build/rakupp tools/run-bench.raku` |
| [`tools/run-optbench.raku`](../../tools/run-optbench.raku) | Same idea for the `--exe -O` optimizer measurements in [OPTIMIZATION.md](../internals/OPTIMIZATION.md). | `build/rakupp tools/run-optbench.raku` |
| [`tools/rc-compare.raku`](../../tools/rc-compare.raku) | The RosettaCode survey ([dev/findings/ROSETTACODE.md](../dev/findings/ROSETTACODE.md)): fetches real programs off the wiki and diffs Raku++ against Rakudo on each. Written to run under either engine. | `raku tools/rc-compare.raku` (or rakupp) |
| [`rakujs/gen-examples.raku`](../../rakujs/gen-examples.raku) | Generates the [Raku.js](../../rakujs/README.md) browser playground's example list (`rakujs/playground/examples.js`) from `examples/*.raku`, JSON-encoding each program with a hand-rolled string escaper. The WebAssembly build regenerates it with `rakupp` on every build — the interpreter produces the data for its own web playground. | `build/rakupp rakujs/gen-examples.raku` |

Beyond the repo, the same principle drives validation against real
applications: the [covid.observer](https://github.com/ash/covid.observer) site
builder and the [raku-course](https://github.com/ash/raku-course) static-site
generator (with the real zef-installed `YAMLish` module) are run unmodified
under rakupp — each surfaced gaps that Roast never would have.

## Two Raku programs live *inside* the binary

The tools above run beside the interpreter. Two run inside it: `rakupp install`
and `rakupp doc` are not C++ subcommands, they are Raku programs compiled into
the executable, which loads the text out of itself and interprets it.

| Tool | What it does | Reached as |
|---|---|---|
| [`tools/install.raku`](../../tools/install.raku) | The module installer, ~1,840 lines: fetches the fez ecosystem index, resolves names to versions to distribution URLs, checks archives against their content-addressed hashes, unpacks, runs each distribution's own test suite, and writes the `CompUnit` store — plus `--list`, `--check`, `--gc`, `reinstall`, `uninstall`, installing from a local path, and the REA archive fallback. | `rakupp install Foo::Bar` |
| [`tools/doc.raku`](../../tools/doc.raku) | Offline symbol lookup (`go doc`, `perldoc -f`): scans [REFERENCE.md](../guide/REFERENCE.md) and [FEATURES.md](../guide/FEATURES.md) for a builtin, method, operator or syntax form and prints the entry with its heading trail. Both guides are compiled in with it, so the answer needs no files on disk. | `rakupp doc trim` |

`cmake/EmbedTools.cmake` turns those two files — and the two guides — into byte
arrays at build time, listed as the custom command's `DEPENDS`, so editing
`install.raku` rebuilds one translation unit and nothing generated is ever
committed. They stay ordinary files: `rakupp --lint tools/install.raku` works,
and so does running one directly.

This is the sharpest form the principle takes in the project. The engine's own
package manager is written in the language the engine implements, so
`rakupp install` cannot work unless the interpreter is correct about JSON
parsing, version-range comparison, `run` with captured output, path handling
and its own `CompUnit` API — and every user who installs a module exercises all
of it. It is also the most demanding shape: a bug here is not a failing test,
it is a user unable to install anything.

There is a real constraint behind the choice, and it is worth stating precisely
because the obvious version of it is wrong. The engine carries **no** HTTP
client, TLS stack, tar reader or ecosystem-index parser, in any language:
fetching is `curl` and unpacking is `tar`, both in a subprocess. That is a
property of the installer's design, not of it being Raku — a C++ installer
shelling out the same way would carry no network code either. What being Raku
buys is dogfooding, and an 1,840-line program that reads as a program.

## Serving the ecosystem

The project's own public face is Raku all the way down. Every page on
[raku.online](https://raku.online/) — the tour, the specification and Raku
Rules, the FAQ, the internals book, the module handbook — is produced by a
generator written in Raku and executed by `rakupp`, and the site is previewed
over a web server that is itself a Raku program.

| Tool | What it does | Run as |
|---|---|---|
| `sites/{tour,spec,grid,faq,book,ecosystem,examples,showcase}/build.raku` plus `sites/spec/rules.raku`, in the separate [ash/raku.online](https://github.com/ash/raku.online) repo | ~7,800 lines of Raku that turn the sources into the whole of raku.online: page shells, navigation, search indexes, syntax highlighting, the coverage meters, the example gallery, the showcase. `./build.sh` drives them all. | `rakupp build.raku --clean` |
| The same generators with `--verify --oracle=raku` | Runs every documented example through **both** engines and fails the build when they disagree, so no page can claim output the interpreter does not produce. The example gallery's `--capture --oracle=raku` is the same discipline for whole programs: every `examples/*.raku` is executed and its output must match Rakudo's byte for byte. | `rakupp build.raku --verify --oracle=raku` |
| [`showcase/rakus/rakus.raku`](../../showcase/rakus) | The static HTTP server the built site is previewed on before it is published — raw `IO::Socket::INET`, a thread per connection, correct `application/wasm` so the browser can stream-compile the engine. Published to the ecosystem as `App::Rakus`. | `rakupp showcase/rakus/rakus.raku 8973 www` |

`rakus` is the *preview* server, not the production one: the generated `www/`
is committed and **GitHub Pages serves the public site**, straight from `main`
with no build step in CI. Nothing about raku.online depends on running a Raku
web server in production — what `rakus` is for is looking at a build the way a
browser will, on a machine that has just made it.

So a page about Raku++ is generated by a Raku program run by Raku++, checked
by running its examples under Raku++, and read back over a Raku web server run
by Raku++ before it goes out — and once published it executes those examples
in the reader's browser on the same interpreter compiled to WebAssembly.

## Why bother

**It finds real bugs.** A test suite exercises features one at a time; a real
program exercises them together, at volume, in unplanned combinations. Bugs
found *by these tools themselves* while building Raku++:

- `.kv.reverse.hash` mangled pair re-keying — every element became a key with
  an `Any` value instead of consecutive elements pairing up. Found the day
  `gen-unicode.raku` used the idiom to build a name→index map; fixed, and the
  generator's idiomatic line now regenerates byte-identical tables.
- `my @a` re-evaluated in a loop *condition* wrongly re-initialized the array
  (`until $iter.push-exactly(my @a, 3) =:= IterationEnd` never accumulated) —
  found via Roast's own `Test::Iterator` helper, which the harness runs.
- The Roast harness needed multiple TAP plans per file handled
  first-plan-wins, `run(:timeout)` with subprocess capture, and `@*ARGS` —
  each implemented because the harness required it.
- The RosettaCode comparator needed heredocs (`q:to/…/`), quote-aware regex
  lexing, and `run` with closed stdin before it could even fetch and time its
  first task.
- The spec site's oracle gate caught **18 divergences from Rakudo** across 260
  dual-verified examples the first time it ran — mechanically, not by eye —
  and 17 of them are fixed
  ([dev/findings/SPEC-DIVERGENCES.md](../dev/findings/SPEC-DIVERGENCES.md)).
- Writing `rakujs/gen-examples.raku` surfaced a missing `IO::Path.relative` —
  the generator wanted it for a status line and hit `No such method`. A small
  gap Roast hadn't flagged; implemented since (`src/MethodCallPart3.cpp`), and
  it answers what Rakudo answers.

**It proves the claims.** "Raku++ runs real Raku" is easy to say; a
1,400-line harness, a UCD parser chewing a 40k-line data file, and a benchmark
runner all executing under the interpreter every day is harder to fake.

**It keeps speed visible.** The generator takes ~3 minutes under the
tree-walker — a standing, concrete reminder of where interpreter throughput
is spent (and a ready-made profiling workload).

## What is *not* dogfooded (yet)

- The compiler itself is C++ — Raku++ does not compile Raku++. The generated
  Unicode tables are the only part of `src/` produced by Raku (the installer
  and doc tool are Raku *carried by* the binary, not C++ generated from Raku).
- The other Unicode generators (`tools/gen_unicode_gb.py`, `_norm`, `_coll`,
  `_props`, `_scripts`, `_blocks`, `_bidi`) are still Python — straightforward
  candidates for porting the same way `gen_unicode.py` was replaced by
  `gen-unicode.raku`.
- CI/packaging scripts (Homebrew tap) are shell.
