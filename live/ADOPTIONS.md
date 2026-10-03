# adoptions — Raku++ inside somebody else's software

[live/](README.md) runs other people's Raku programs on this engine,
unchanged. This file is the other direction: other people's *software* that
reached for the engine — packaged it for an ecosystem of their own, embedded
it in their own page, built it with their own tooling.

Nothing listed here is ours, and nothing here is copied into this repository.
Each entry is a link, a credit, and a plain account of what its author
actually does with Raku++ — including, where there is one, the gap they had to
work around.

The bar is the same shape as an entry in `live/`: **somebody else's decision,
published where a third party can get it without going through us, credited
and linked rather than vendored.** Our own packaging is not an adoption — the
Homebrew tap, the `setup-rakupp` action, the [bindings/](../bindings),
raku.online — those are ours and belong in
[docs/status/ECOSYSTEM.md](../docs/status/ECOSYSTEM.md).

| What | Who | What it does with Raku++ |
|---|---|---|
| [**RakuppLink**](https://resources.wolframcloud.com/PacletRepository/resources/AntonAntonov/RakuppLink/) | Anton Antonov | ships the Wolfram binding as a paclet in Wolfram's own repository |
| [**Raku Playground**](https://fco.github.io/Raku-Playground/?runtime=rakupp) | Fernando Correa de Oliveira | offers rakupp as one of four runtimes, in the browser |
| [**Raku Koans**](https://hankache.github.io/raku-koans/) | Naoum Hankache | runs a course of 105 test-driven koans on Raku.js, in the browser |
| [**rakupp-dsci**](https://github.com/melezhik/rakupp-dsci) | Alexey Melezhik | ports the Raku++ release matrix to another CI |
| [**sibl**](https://github.com/4zv4l/sibl-channel) | 4zv4l | packages rakupp in a personal Guix channel |
| [**321**](https://github.com/nige123/cli.321.do) | Nigel Hamilton | compiles an agent runtime, written in Raku, into one standalone file per system |
| [**iz4**](https://github.com/nige123/cli.iz4.you) | Nigel Hamilton | ships a tool for keeping a project's invariants as Raku++-built files that update themselves |

## RakuppLink — the Wolfram binding as an official paclet

[**RakuppLink**](https://resources.wolframcloud.com/PacletRepository/resources/AntonAntonov/RakuppLink/)
is in Wolfram's Paclet Repository, packaged by Anton Antonov
([source](https://github.com/antononcube/WL-RakuppLink-paclet)) — v1.0.2, MIT,
Wolfram Language 13.3+, context ``AntonAntonov`RakuppLink` ``. So a Wolfram
user reaches Raku with `PacletInstall["AntonAntonov/RakuppLink"]` and never
sees this repository.

What the paclet packages is
[bindings/wolfram/RakuLang.wl](../bindings/wolfram/RakuLang.wl) with every
exported symbol renamed `Raku*` → `Rakupp*` (`RakuppEval`, `RakuppCall`,
`RakuppGrammar`, …), plus in-product reference pages per function and a
resource definition notebook — the parts that make it a paclet rather than a
file you `Get`. The Wolfram half is Anton Antonov's; the engine half is
unchanged, and the `librakupp` it loads is still one the user builds
(`-DRAKUPP_BUILD_SHARED=ON`). Our file stays the source of truth for the
binding; the paclet is its packager's to version.

The same author filed [#38](https://github.com/ash/rakupp/issues/38) — the
`Math::NumberTheory` install that cost twelve engine fixes — so the packaging
and the bug reports arrived in the same week. That is the pattern `live/`
predicts: somebody using the thing for their own purposes finds what we would
not have thought to test.

## Raku Playground — one dropdown, four runtimes

[**Raku Playground**](https://fco.github.io/Raku-Playground/) is Fernando
Correa de Oliveira's browser playground
([source](https://github.com/FCO/Raku-Playground), Artistic-2.0): a
Swift-Playgrounds-inspired learning site — guided *sagas*, an animated puzzle
world, a CodeMirror editor, step-through, English and Português (BR) — served
statically from GitHub Pages, no backend.

It was built on Rakudo compiled to JavaScript, and that is still its default.
The runtime dropdown now offers four: `Rakudo (perl6.js)`, two WASM MoarVM
builds (one wasm32 single-threaded, one wasm64 with threads) — and
**`rakupp (WASM)`**, which is this project's Emscripten build. Pick it in the
dropdown, or arrive at
[`?runtime=rakupp`](https://fco.github.io/Raku-Playground/?runtime=rakupp).

The wiring is in their `raku-worker.js`: a Web Worker does
`importScripts("rakujs.js")`, calls `ccall("rakupp_run", …)` per run and
rebuilds the module afterwards — the same shape as
[rakujs/playground/worker.js](../rakujs/playground/worker.js) here. The pair
is vendored into their site rather than fetched from raku.online; as served on
2026-08-28, `rakujs.wasm` is 4,786,262 bytes and `rakujs.js` 103,353, against
the ~77 MB their README quotes for the Rakudo-to-JavaScript bundle.

Checked 2026-08-28, with `rakupp (WASM)` selected, in Free play:

```raku
say $*RAKU.compiler.name ~ ' ' ~ $*RAKU.compiler.version;
say (1..20).grep(*.is-prime).join(',');
say [+] 1..1000;
```
```
Raku++ 6.d
2,3,5,7,11,13,17,19
500500
```

The first line dates the snapshot: current builds answer `Raku++ 2026.08`,
since `$*RAKU.compiler.version` reports the era of the Rakudo we verify
against.

**What their author had to work around is our gap, not theirs.** The WASM
build has no JavaScript bridge. Under `perl6.js` the playground has
`EVAL :lang<JavaScript>` — that is how Free play renders into the preview pane
and how the puzzle world talks to the page. On the rakupp runtime it cannot, so
the whole simulation runs in Raku and reports over stdout behind an ASCII
sentinel (`@@PZ@@`), on the channel their elevator and snake sagas already
used; and because a prelude is prepended to the user's source, their worker
subtracts a line offset from our `line N` errors to point them back at the
user's own code. Two workarounds, both fair descriptions of something this
engine does not offer an embedder.

## Raku Koans — a Raku course graded by Raku.js

[**Raku Koans**](https://hankache.github.io/raku-koans/) is a course by Naoum
Hankache, the author of [raku.guide](https://raku.guide)
([source](https://github.com/hankache/raku-koans); code Artistic-2.0, artwork
CC BY-SA 4.0). It follows the guide's order through 105 koans in sixteen
groups, from `Test` assertions to grammars: each koan is a short program whose
tests fail until the learner fills in every `___`. The idea is Ruby Koans'.

Every answer is checked in the browser by Raku.js, the WebAssembly build of
this engine. The course downloads it from a Raku++ release and pins it by
checksum — v4.0.1 when this entry was written — and moves to a newer release
only after re-running every koan on it. Every Raku++ release candidate runs
that same check first: gate 6b in [RELEASING.md](../docs/dev/RELEASING.md).

**Where it meets our plans.** Three koans teach code this engine accepts and
Rakudo refuses: two calls Rakudo rejects at compile time as ones that "will
never work", and a `next OUTER` that Rakudo resolves differently. They are in
the v5 error work ([V5-PLAN.md](../docs/dev/plans/V5-PLAN.md), B3). What the
course had to build around — a Raku.js that runs only in a browser, no way to
stop a running program, shallow recursion in a Web Worker — is under "A fresh
WebAssembly build" in [V5-IDEAS.md](../docs/dev/plans/V5-IDEAS.md).

## rakupp-dsci — the release matrix on somebody else's CI

[**rakupp-dsci**](https://github.com/melezhik/rakupp-dsci) is Alexey
Melezhik's port of this repo's
[`.github/workflows/release.yml`](../.github/workflows/release.yml) to DSCI
pipelines: a `.dsci/jobs.yaml` with six jobs — `linux-x86_64`,
`macos-universal`, `windows-msvc`, `windows-mingw`, `wasm` (which builds and
packages Raku.js) and `gcc-portability`.

By its own README it is an example of porting a workflow, not a pipeline
anything here depends on: releases still come out of GitHub Actions. It is
listed because the traffic runs both ways — Melezhik's Sparrow6 is the
software [live/sparrow](sparrow) drives.

## sibl — a Guix channel with a rakupp package

[**sibl**](https://github.com/4zv4l/sibl-channel) is 4zv4l's personal Guix
channel, and `sibl/rakupp.scm` in it is a rakupp package: a `git-fetch` of a
pinned tag with its base32 hash, `cmake-build-system`,
`-DCMAKE_POSITION_INDEPENDENT_CODE=ON`, tests off, Artistic-2.0.

It pins **v1.2.0**, and it is independent of the module this repo ships for
its own Guix gate
([.guix/modules/rakupp-package.scm](../.guix/modules/rakupp-package.scm),
which builds the working tree rather than a tag). Somebody wrote a channel
package without asking us, which is exactly why it is worth recording.

## 321 — an agent runtime shipped as one file

[**321**](https://github.com/nige123/cli.321.do) is Nigel Hamilton's
harness-agnostic agent runtime and its command-line front door (Apache-2.0):
it loads an agent package, picks an installed harness or a deterministic
procedure, enforces capability grants and approvals, and writes an immutable
receipt of each run. It began in Go; in September 2026 it was ported to Raku,
module by module with its test suites, and the Go tree was removed.
[The commit that removed it](https://github.com/nige123/cli.321.do/commit/5fc651b374fda85b42d33de485f8732edb66f064)
lists what was checked first — fixtures reproduced byte for byte, packages and
receipts accepted across the two binaries, identical `help` and `doctor`
output, 794 tests passing against the compiled file — and the Raku++ quirks
met on the way, each worked around at the call site.

Raku++ is how it ships. Its own CI fetches a pinned Raku++ release, checks the
archive's SHA-256, and runs `rakupp --aot --standalone` on each of four
systems — Linux x86-64 and arm64, a universal macOS file, 64-bit Windows —
then runs the whole suite against each compiled file before publishing them
as a GitHub release. The person who installs `321` gets one file and needs no
Raku at all.

Its author has filed thirteen engine bugs here since September, among them
[#80](https://github.com/ash/rakupp/issues/80) (`--bundle` on Windows) and
[#112](https://github.com/ash/rakupp/issues/112) (a `MAIN` imported from a
module under `--exe`).

## iz4 — invariants, installed as one file

[**iz4**](https://github.com/nige123/cli.iz4.you) is the same author's tool
for the few truths a piece of software must never accidentally lose
(Apache-2.0). An `IZ4` file beside the code says what the project is for, who
it is for, and a handful of invariants, each with the reason it must survive,
so that the people and agents who rewrite the code cannot build them away.
The format is a Raku grammar, and the parser is that grammar; `iz4 test`
writes a failing test per invariant that has no evidence yet. Its agentic
commands run through `321`.

It is built exactly as `321` is — `rakupp --aot --standalone` from a pinned,
checksum-verified Raku++, for the same four systems, with the suite run
against each file. Its one-line installer downloads the file for the machine,
verifies it and puts it on `PATH`, and `iz4 update` replaces it in place with
the newest release; only where no file fits does it fall back to a source
install on Rakudo. So the usual way to get iz4 is a Raku program compiled by
Raku++, on a machine with no Raku installed.

## What they have in common

Every one of these pins a snapshot. The playground vendors a July build, the
Guix channel a v1.2.0 tag, the koans, `321` and `iz4` a v4.0.1 release by
checksum, and the
paclet is written against the C ABI in
[include/rakupp/rakupp.h](../include/rakupp/rakupp.h), linking whatever
`librakupp` its user happened to build. A fix landing here reaches none of
them until their author re-vendors or re-pins — worth remembering before
reading a report from one of them as a live bug, and worth remembering the
other way round: that ABI is the one part of this project other people's
builds are compiled against, so it is the part to keep still.

None of this is under our control. Rows can move or disappear without notice;
the links are what they were on the date each was last checked —
**2026-10-03** for 321 and iz4, **2026-09-28** for Raku Koans, **2026-08-28** for
the rest.
