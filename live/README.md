# live — software that already existed

Whole tools from the Raku ecosystem, run under Raku++ exactly as their authors
wrote them. These pages are also browsable at
[raku.online/live](https://raku.online/live/).

**The tools are other people's work, and they are not copied into this
repository.** Each is a published distribution, installed from the ecosystem
the way any user installs it, and left completely alone — not patched, not
vendored, not adjusted to fit. What lives in the directories below is only what
is needed to *drive* one: a scenario, a config, a small corpus, a `README.md`,
a `compare.sh`, and — where the entry measures something — a `bench.sh`. Those
we wrote; the software under test we did not, and each entry credits and links
to whoever did.

That is the difference from [showcase/](../showcase), which holds mid-size
programs written here to exercise one part of the language each. A program
written against an implementation tends to stay inside what that implementation
does well. Software written by somebody else, for their own purposes, does not
— and what it reaches for is not what we would have thought to test.

A third angle is [docs/status/DOGFOODING.md](../docs/status/DOGFOODING.md):
the tools that build, test and measure Raku++ — and the sites that serve the
project's own ecosystem — are themselves Raku programs run by `rakupp`. Here
we run somebody else's software; there we run our own, every day.

| Project | What it is | What it exercises |
|---|---|---|
| [**sparrow/**](sparrow) | [Sparrow6](https://github.com/melezhik/Sparrow6), an automation framework (`zef:sp1983`) | a process per task, in Bash, Raku and Perl — so it measures what starting an interpreter costs |

## The other direction — [ADOPTIONS.md](ADOPTIONS.md)

Everything above is other people's software running on this engine.
**[ADOPTIONS.md](ADOPTIONS.md)** is the mirror image: other people's software
that reached for *the engine* — a Wolfram paclet in Wolfram's own repository, a
browser playground that offers rakupp as one of four runtimes, a Guix channel, a
port of the release matrix to somebody else's CI. Nothing to run there and
nothing checked in there either; it is a record of what other people have done
with Raku++, links and credits only.

## How an entry works

The tool is installed, never checked in: `rakupp install <dist>` or
`zef install <dist>`, both writing the same store. So an entry's directory is
small on purpose — it is a harness, not a copy of anything, and updating the
tool means reinstalling it rather than editing files here.

`compare.sh` is the contract. It takes `rakupp` and `raku` from `PATH` (set
`RAKUPP=` to name a build tree instead), runs the same input under both and
diffs stdout, normalising only what is *supposed* to differ between
two runs (a timestamp, a PID). A green `MATCH` is the whole claim: same
program, same output, no changes on either side.

Where an entry reports numbers, a `bench.sh` beside it produces them, so every
table in this directory is one command away from being checked rather than
believed.

None of these are wired into `t/run.raku`. They need distributions the suite
cannot assume, and several have side effects outside the repository — files
under `$HOME`, spawned processes, sockets. `compare.sh` is the check, run by
hand.

## What they are for

Not a demo reel. Every entry here earned its place by breaking something: a
tool nobody here wrote reaches for corners nobody here would have written a
test for, and the faults it finds are general ones that were simply waiting for
a caller. `sparrow/` cost four fixes on the day it was added, none of them
about Sparrow — an `IO::Path` method's return type, a missing `Proc::Async`
method, a `react` that registered its taps too late, and a parse rule for
`name<key>`. Each is pinned now by a case in `t/regression/`.

So the bar for adding one is not "it looks impressive". It is: **somebody
else's software, installed rather than copied here, run unmodified and
credited, with its output checked against Rakudo's.**
