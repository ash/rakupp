# live — software that already existed

Whole tools from the Raku ecosystem, run under Raku++ exactly as their authors
wrote them.

**The tools are other people's work, and they are not copied into this
repository.** Each is a published distribution, installed from the ecosystem
the way any user installs it, and left completely alone — not patched, not
vendored, not adjusted to fit. What lives in the directories below is only what
is needed to *drive* one: a scenario, a config, a small corpus, a `README.md`
and a `compare.sh`. Those we wrote; the software under test we did not, and
each entry credits and links to whoever did.

That is the difference from [showcase/](../showcase), which holds mid-size
programs written here to exercise one part of the language each. A program
written against an implementation tends to stay inside what that implementation
does well. Software written by somebody else, for their own purposes, does not
— and what it reaches for is not what we would have thought to test.

| Project | What it is | What it exercises |
|---|---|---|
| [**sparrow/**](sparrow) | [Sparrow6](https://github.com/melezhik/Sparrow6), an automation framework (`zef:sp1983`) | a process per task, in Bash, Raku and Perl — so it measures what starting an interpreter costs |

## How an entry works

The tool is installed, never checked in: `rakupp install <dist>` or
`zef install <dist>`, both writing the same store. So an entry's directory is
small on purpose — it is a harness, not a copy of anything, and updating the
tool means reinstalling it rather than editing files here.

`compare.sh` is the contract. It runs the same input under Rakudo and under
Raku++ and diffs stdout, normalising only what is *supposed* to differ between
two runs (a timestamp, a PID). A green `MATCH` is the whole claim: same
program, same output, no changes on either side.

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
