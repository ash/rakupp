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
| [**rakupp-dsci**](https://github.com/melezhik/rakupp-dsci) | Alexey Melezhik | ports the Raku++ release matrix to another CI |
| [**sibl**](https://github.com/4zv4l/sibl-channel) | 4zv4l | packages rakupp in a personal Guix channel |

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

## What they have in common

Every one of these pins a snapshot. The playground vendors a July build, the
Guix channel a v1.2.0 tag, and the paclet is written against the C ABI in
[include/rakupp/rakupp.h](../include/rakupp/rakupp.h), linking whatever
`librakupp` its user happened to build. A fix landing here reaches none of
them until their author re-vendors or re-pins — worth remembering before
reading a report from one of them as a live bug, and worth remembering the
other way round: that ABI is the one part of this project other people's
builds are compiled against, so it is the part to keep still.

None of this is under our control. Rows can move or disappear without notice;
the links are what they were on **2026-08-28**, the date each was last
checked.
