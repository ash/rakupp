# What the tools were doing, measured rather than assumed

*A review of every instrument this project gates, benchmarks and measures
itself with, done 2026-08-29 at the start of v3.23.0 — the re-baseline release.
It follows [GATES-3.22.md](GATES-3.22.md), which fixed seven gates and then
asked each to prove it could fail.*

The premise of v3.23.0 is that a baseline predating the review is not a
baseline. The premise of this document is one step earlier: **before
re-measuring everything, find out what the measuring instruments actually do.**
Every item below is stated as something that was run, with the number it gave.

**Twenty-five defects fixed, one claim corrected, one command left to run.**

The review found sixteen and left five items for the plan; the five were then
closed in the same sitting (Part 2), and closing them exposed three more root
causes — including why the `build/` trap existed at all, and the fact that one
of the release gates cannot fail. One of the twenty-five is an engine bug, found
by a tool that was being fixed at the time.

---

## The shape that keeps recurring

GATES-3.22 A5 named it once: *"anything that picks a binary by taking the first
path that exists has it."* That was fixed in `perf-guard` and `run-optbench` and
left everywhere else. The review found the same shape in five more places, plus
three variants of a second shape — **a hand-written list that nothing compares
against the thing it lists.**

Both have one root cause, recorded as open item **O2** below: there is no shared
helper, so every tool carries its own copy of this code, and a fix reaches one
copy.

---

# Fixed in this sitting

## 1. The gate commands name their binary by PATH lookup, and three answer

RELEASING.md writes gates 1 and 2 as `rakupp tools/run-roast.raku` and `rakupp
t/run.raku`. Both harnesses test `$*EXECUTABLE` — whichever binary ran them. On
the machine of record:

```
$ which -a rakupp
/Users/ash/raku++/build-arm64/rakupp     # v3.22.0
/usr/local/bin/rakupp                    # v1.0.0   (Cellar, 22 July)
/opt/homebrew/bin/rakupp                 # v0.5.1   (Cellar, 13 July)
```

The first is correct **by PATH ordering alone**. Nothing in either harness's
output named the engine it measured, so a shifted PATH would produce a
plausible, lower number with no tell. Measured, on one Roast file:

```
v3.22.0:  [PASS]  31/31   →  fully-pass 1 / 2
v1.0.0:   [part]  27/31   →  fully-pass 0 / 2
```

This is worse than the A5 case it resembles: A5 reported INCONCLUSIVE, this
reports a number.

**Fixed.** `run-roast.raku` opens with a provenance line and `--list=` writes a
`.meta` sidecar beside the list:

```
run-roast: rakupp 3.22.0 (/Users/ash/raku++/build-arm64/rakupp) | roast b2cbe8a42 (/Users/ash/roast) | 1464 files | workers 4
```

The sidecar is separate from the `.list` on purpose: the list is diffed with
`comm`, which would report a differing header line as a changed path.

## 2. Neither of gate 1's two inputs was recorded

Roast is an upstream checkout that moves. Here it sits at **`b2cbe8a42`
(2026-06-12), 1,464 `.t` files** — and nothing in this repo records that: not
RELEASING.md, not COUNTING.md, not the archived lists, not the CHANGELOG.

Gate 1 *is* a diff of this release's fully-passing list against the previous
release's. If Roast moves between two releases, files appear and disappear and
the diff charges every one of them to the engine. No past release's measurement
can be reproduced, because one of its two inputs is unversioned.

**Fixed** by the banner and sidecar above; RELEASING.md now asks for the
revision in the CHANGELOG.

## 3. `perf-guard`'s standing-debt note carried a third, stale copy of the kernel list

The note under a passing gate read:

```raku
my @debt = <fib asg loopsum hash>.grep({ … });
```

Those are the four kernels that existed before 2026-08-09. `strscan`, `strpass`,
`subcall`, `rats` and `regexloop` were added afterwards, and the note could not
name them however far behind they were. The file even carries the comment that
should have caught it — *"The kernel list, in one place: the run loop and the
gate loop must agree, and they used to carry two hardcoded copies of it"* — the
unification reached two sites of three.

What that cost, measured on this build:

```
before:  note: still behind the best ever measured on: fib
after:   note: still behind the best ever measured on: rats +36.2%, regexloop +22.5%,
                subcall +15.4%, strscan +14.6%, fib +8%
```

`rats +36.2%` is the standing debt RELEASING.md calls *"the honest headline, not
`delta`"* and the open question the entire v3.21→v3.23 arc exists to carry. The
gate reported `fib` and stayed silent about it.

**Fixed:** every kernel, worst first, with percentages.

## 4. …and the debt note was skipped entirely on the re-measure path

`perf-guard` re-measures a kernel that reads over tolerance before believing it.
When the second reading is clean it printed *"the first reading did not
reproduce (machine noise)"* and `exit 0` — **past** the debt note.

That path is not rare. Part B of GATES-3.22 measured this gate's own floors:
1.7% run-to-run on an identical binary, plus up to 3.5% from layout. The
tolerance is 5%. The path fired on the first verification run of this sitting.

**Fixed:** one `report-debt` function, called from both success exits.

## 5. `--record` never stamped the field `--check` prints

`perf-baseline.raku` said so itself, in a comment:

> `--record` does not stamp the 'recorded' field below, and `--check` prints it
> — so it must be edited by hand after every record.

That hand-maintained field is the mechanism behind RELEASING.md's documented
blind spot: the baseline went four releases without a re-record (`2026-07-29
(v1.5.1)` while v3.0.0 shipped) and the gate passed the whole time, because
nothing in its output moved.

**Fixed.** `--record` stamps the field, verifies it reads back, names the binary
it measured, and takes `--for=vX.Y.Z` — because gates run *before* the version
bump, so the measured binary still calls itself the previous release:

```
recorded 9 kernels into perf-baseline.raku
stamped: recorded => '2026-08-29 (v3.23.0)'   (measured with …/build-arm64/rakupp)
```

## 6. `%kernels` and `@KERNELS` could drift, and nothing checked

A kernel defined but not listed is never measured and never gated, silently.
**Fixed:** a coverage check, proved by planting one:

```
perf-guard: @KERNELS and %kernels disagree — the gate would measure the wrong set.
  in %kernels but not gated: newkernel
```

## 7. `run-optbench`'s `@benches` is hand-written and nothing compared it to the directory

GATES-3.22 Part C already recorded this — *"a new file dropped into
`tools/optbench/` is never run"* — as the cause of a prove-gates plant that
first reported `MISSED` against a gate that was working correctly. The plant was
rewritten to append to an existing kernel; the drift itself was left.

**Fixed:** a cross-check, proved by planting a file:

```
run-optbench: tools/optbench/ and @benches disagree.
  on disk but never run: planted
A kernel this gate does not run is a kernel it cannot gate.
```

Its scratch binaries were also fixed paths (`/tmp/rakupp-opt-<name>-base`) with
no PID and no cleanup, so two concurrent runs of gate 4 compiled over each
other's binaries and each measured the other's build. Now PID-scoped and
removed on the way out.

## 8. The documentation-example figure was measured with a two-release-old engine

`doc-examples-diff.raku` — which produces README's *"950 documentation examples
byte-identical on both engines"* — defaulted to `build/rakupp` and checked only
that the path was executable. On the machine of record:

```
build/rakupp        v3.20.1, Mach-O x86_64
build-arm64/rakupp  v3.22.0, Mach-O arm64
```

An x86_64 binary runs perfectly well under Rosetta, so the default swept with a
two-release-old engine and said nothing about it. This is A5's shape, third
instance, on a headline figure.

**Fixed:** architecture-filtered search, an explicit refusal for a cross-arch
binary, and a banner:

```
doc-examples-diff: …/build-arm64/rakupp (rakupp 3.22.0) vs raku  (chosen over an earlier candidate: it is the arm64 build)
```

## 9. `eco-sweep.raku`'s documented command does not run

Its own usage line read:

```
rakupp tools/eco-sweep.raku LIST.tsv --out=RESULT.tsv --logs=DIR
```

That prints the usage message and sweeps nothing. Raku's `MAIN` parser stops
treating `--x=y` as an option once a positional has been seen, so `--out` and
`--logs` are never bound and no candidate matches. **Confirmed on both engines**
— this is standard Raku, not an engine bug. The tool behind the *"637 of 2,524
distributions"* figure could not be invoked as documented.

**Fixed:** the options now come first in the usage line, with the reason beside
it.

## 10. `ast-opportunity.raku` has the silent variant of the same thing

```
rakupp tools/ast-opportunity.raku FILE... [--rakupp=PATH]
```

With a slurpy `*@files` there is no usage message — the option simply becomes a
positional. Measured:

```
rakupp=DEFAULT files=a.raku,--rakupp=/my/bin
```

It keeps the default binary *and* hands `--rakupp=/my/bin` to the analysis as
though it were a filename. Both engines agree. **Fixed:** the usage line, and a
note saying why the order is not cosmetic.

## 11. `eco-sweep` reported the size of its input, not what it measured

The closing line was `sweep done: $n modules`, where `$n` is the length of the
input list — the same sentence whether it measured 2,524 distributions or zero.
The resume skip is keyed on a name already appearing in `--out`, so pointing a
re-sweep at the previous result file does nothing and says exactly that.

v3.23.0's central task is re-sweeping the ecosystem *"from scratch rather than
from the last verdict file"*. This line is what hid the distinction. **Fixed:**

```
sweep done: 0 measured, 2 skipped (already in $out), 2 in the list -> …
NOTHING WAS MEASURED — every name was already in …. Use a fresh --out for a full re-sweep.
```

## 12. Two tools hardcoded one developer's path to the stale build

`blame-parse.raku` and `triage-dists.raku` both defaulted `--rakupp` to
`'/Users/ash/raku++/build/rakupp'` — an absolute path that works on one machine
and names the x86_64 v3.20.1 build there. **Fixed:** both default to
`$*EXECUTABLE.absolute`, the binary running the tool, as `eco-sweep` and
`run-roast` already do.

## 13. `check-figures.raku` passed on a tree that stated nothing

The tool added in v3.22.0 to close A7's blind spot has the same class of blind
spot one layer up. Its `agree()` helper opened with `return False unless @seen`
— so a `--flag` whose figure the rules did not reach was simply not checked.

**Measured**, against a tree carrying the two standing cells and no Status line
and no example count:

```
$ check-figures --expect=642 --examples=950 --version=3.23.0
check-figures: all headline figures agree at 642
EXIT=0
```

A version and an example count neither of which appears anywhere, reported as
agreement. The file's own header warns about exactly this: *"A checker that
covers one cell of a row family gives the same false comfort as the grep it
replaced."*

**Fixed:** finding nothing is a failure. The same planted tree now exits 1 with
`NOT FOUND: no release version anywhere in the checked docs.`

## 14. Two stale figures that nothing was looking at — and a new check for them

- **`docs/guide/FEATURES.md`** carried v3.21.0's `685 partial` beside v3.22.0's
  `642` fully passing. 642 + 685 + 121 + 15 = **1,463**, one short of the 1,464
  denominator in the same sentence.
- **`docs/status/ROAST.md`**'s v3.21.0 snapshot opened `642 / 1,464 files fully
  passing` in a paragraph whose *own next sentence* says the count repeats at
  643 — and 643 / 1,464 is what that release shipped, per CHANGELOG.md.
  643 + 685 + 121 + 15 = 1,464.

Neither the RELEASING.md grep (no denominator on a bare count) nor
`check-figures` (which read only the `Fully passing` cell and README's
comparison row) could reach either.

**Fixed**, both — and `check-figures` now verifies the bucket arithmetic:
`fully + partial + no-TAP + timeout == denominator`. Unlike every other check in
that tool this one applies to **historical snapshots too**, because it tests
internal consistency rather than currency: a v3.21.0 paragraph is entitled to
v3.21.0's numbers, but they still have to add up to the same 1,464 files. Six
snapshots across two files are checked, and pass.

## 15. Engine bug: a `:g` match did not carry its subject

Found by the check in item 14 while it was being written. It scans for a figure
and then looks *backward* from `$m.from` for the count beside it; on
`docs/status/ROAST.md` — a file full of em dashes — the window opened 67
characters late, swept in text from *after* the match, and accused a paragraph
whose arithmetic was correct.

`.from` was a **byte** offset. Reduced:

```raku
my $s = "— a — b — c";                      # em dash: 1 character, 3 bytes
$s.match(/<[abc]>/, :g)».from;              # rakupp 4, 10, 16   Rakudo 2, 6, 10
$s.match(/c/).from;                         # 10 on both — the single-match path is right
```

One root cause. `.match(:g)`, `.comb(:match)` and the whole s/// adverb family
build their Matches in `substSelect`, which called `Value::matchVal(…)` without
attaching the subject that the `m//` path attaches through its `mk` helper.
Without a subject, `graphemeOff` has nothing to count graphemes in and returns
the byte offset unchanged — correct by accident on ASCII, which is why it
survived. The same omission made `.orig`, `.prematch` and `.postmatch` answer
the **matched text** instead of the subject:

```raku
$s.match(/<[abc]>/, :g)[0].orig;            # rakupp 'a'   Rakudo '— a — b — c'
```

**Fixed** in `substSelect` (`src/Interpreter.cpp`), five construction sites now
sharing one subject exactly as `mk` does.
`t/regression/global-match-carries-its-subject.raku` covers offsets, `.orig`,
`.prematch`, `.postmatch`, `.comb(:match)` and captures — **and passes
unmodified under Rakudo**, which is what makes it an oracle test rather than a
record of our opinion.

## 16. A FAILED perf verdict did not say what the machine was doing

Found by the gate firing on this sitting's own work. `perf-guard --check`
reported `FAILED (confirmed on re-measure)` naming five kernels 7.6–10.3% slower
than baseline. The engine change in this sitting touches only `substSelect`,
which no kernel calls — so it was checked rather than assumed:

```
load 4.33 on 8 cores (54%)
PRE  (unmodified HEAD)   fib 411.9  asg 178.1  strpass 83.1  subcall 184.9  rats 256.6
POST (with the fix)      fib 428.4  asg 181.2  strpass 81.7  subcall 192.4  rats 260.9
baseline                 fib 377.7  asg 160.4  strpass  73.5 subcall 171.5  rats 237.0
```

**The unmodified HEAD binary fails the gate too**, by the same margin — so the
verdict was the machine. The differences between PRE and POST are mixed in sign
and inside the 1.7% run-to-run floor GATES-3.22 Part B measured.

Two things about the gate itself, though:

- **Both of its defences assume a false alarm is transient.** The re-measure
  step and the load cut are designed against a spike — Spotlight waking up, a
  `pandoc` job. A *sustained* load reproduces, so the re-measure confirms it and
  the verdict comes back `FAILED`, stated confidently, naming kernels.
- **54% load sits just under the 60% cut**, so the INCONCLUSIVE path — the one
  that exists precisely for this — never fired.

**Fixed, as far as is safe to fix here:** the FAILED branch now prints the load
and the cut beside its verdict, which the INCONCLUSIVE branch already did. The
gate's most consequential output was the one saying least about its own
conditions:

```
perf-guard FAILED (confirmed on re-measure): strscan 5.4% …; subcall 9.2% …; rats 6.7% …
Machine at the time: load 4.4 on 8 cores (56% — the inconclusive cut is 60%).
A sustained load defeats BOTH the re-measure and the cut above: it is not a
spike, so it reproduces. Confirm on an idle machine, and A/B against a binary
built WITHOUT the change before believing it.
```

Where the cut should actually sit is a calibration decision with release
consequences, so it is **O5** below rather than a change made in passing.

---
# What the review left open

*All five were closed in the same sitting — see Part 2 below for how, and for
the three further root causes that closing them exposed.*

## O1. Gate 7 has never been proved, and "8 of 8" does not say so

GATES-PLAN Part C lists eight gates to plant, gate 7 among them (*"a
documentation example whose answer changes"*). What shipped is eight **plants**,
with gate 7 absent and `4b` contributing two:

```
2  3  4  4b  4b-reap  1  5  6
```

The count 8 is honest. But README, CHANGELOG and GATES-3.22 all say *"for each
gate a defect it is supposed to catch was injected"*, and for gate 7 that is not
true. `check-figures` is not in the harness either — A7's verification was done
by hand once and is not repeatable.

Two gates of the release checklist therefore have no plant. Either plant them,
or say plainly which are unproved and why.

## O2. There is no shared helper, which is why the same defect keeps recurring

Binary selection, host-architecture detection, cross-arch refusal and
version-reading are now written **six times** across `perf-guard`,
`run-optbench`, `doc-examples-diff`, `run-bench`, `run-roast` and `eco-sweep`,
in four slightly different forms. That is the mechanism behind items 1, 8 and
12, and behind A5 before them: a fix reaches one copy.

The reason on record for not sharing is a note in `t/run.raku`:

> Kept as one self-contained file on purpose: rakupp's module `is export` is
> still flaky for many-sub modules, so the helpers live here inline.

**That note is stale.** Measured: a 30-sub `is export` module sums correctly on
both engines (465 = 1..30), and a realistic helper module — plain
`sub … is export`, `our constant … is export`, and exported `multi`s — produces
byte-identical output on rakupp and Rakudo. A shared `tools/lib/` is available
and was not.

Not done here because it touches every tool at once and belongs in a gated
batch, not appended to a review.

## O3. `build/` is a trap generator

On the machine of record `build/` holds a **v3.20.1 x86_64** binary. Nine tools
name `build/rakupp` in their usage comments as the example invocation, and it is
the first candidate in every search path. It has now produced three distinct
gate failures (A5's perf-guard, A5's sibling optbench, item 8's
doc-examples-diff). Either remove it or keep it rebuilt; leaving a stale
wrong-architecture binary at the most-documented path is the precondition for
the next instance.

## O5. The inconclusive cut may be in the wrong place

60% of the core count let a 54%-loaded machine produce a confident five-kernel
`FAILED` against an unmodified binary (item 16). The options are a lower cut, a
load reading taken *after* the run as well as before, or requiring an explicitly
idle machine for the gate. All three change how often a release is blocked, so
this is the author's call, not a passing fix. Whatever is chosen, the reasoning
belongs in RELEASING.md beside the exit-code table.

## O4. Small things left alone

- `t/run.raku`'s `is export` note (O2) is stale and should be corrected or
  dropped when the shared helper lands.
- The Roast checkout has an untracked `S16-io/lines.testing` left by a run.
  `run-roast`'s per-run scratch directory catches tests writing *relative*
  paths; this one is inside the Roast tree.
- `eco-sweep --reclassify` appends rather than rewrites, so a reclassified
  sweep leaves two rows per distribution in `--out`. Consumers must take the
  last. Harmless today, and worth a note in the tool rather than a fix.
- **A Raku trap this sitting walked into**, worth recording because tooling here
  is written in Raku and the shape is idiomatic-looking:
  `@args.first(*.starts-with('--for=')).?substr(6)` does **not** yield an
  undefined value when nothing matches. The safe call finds a `substr` on `Nil`,
  which coerces to the **empty string** — and `''` is defined, so a following
  `// fallback` never fires. It produced a provenance stamp reading
  `2026-08-29 ()`. Rakudo warns (*"Use of Nil.substr coerced to empty string"*);
  rakupp does it silently, which is arguably the more interesting half. Both
  engines agree on the value, so this is a language trap, not a divergence — but
  the missing warning is worth its own look.

---
# Part 2 — closing the five open items

The five above were left for the plan. They were then done in the same sitting,
and closing them turned up **three more root causes** — including the reason the
`build/` trap existed at all, and the fact that one release gate cannot fail.

## O3 → the reason `build/` was x86_64: **cmake is x86_64**

`build/` was rebuilt from scratch with the documented command — and came back
**x86_64 again**. So this was never a one-off mistake:

```
$ uname -m                       arm64
$ file -b $(which cmake)         Mach-O 64-bit executable x86_64
                                 (/usr/local/bin/cmake — Intel Homebrew)
```

CMake infers the target architecture from its **own** process. The only cmake on
this machine is an Intel-Homebrew build under `/usr/local`, which precedes
`/opt/homebrew` on PATH — so **README.md's "Build from source" instructions
produce a translated binary on the machine of record**, and have all along. That
is the whole explanation for A5, for its optbench sibling, and for item 8: three
gate defects downstream of one x86_64 cmake.

**Fixed in `CMakeLists.txt`**, before `project()`, which is the only place it can
be fixed for everyone: when `CMAKE_OSX_ARCHITECTURES` is not given explicitly,
ask the kernel rather than cmake.

```
-- rakupp: building for the host architecture arm64
```

`hw.optional.arm64` is used rather than `uname -m` for the same reason
`Gate.rakumod` uses it: it answers for the HOST even when the asking process is
translated. Verified that an explicit `-DCMAKE_OSX_ARCHITECTURES` is untouched —
`build-x64`, `build-universal` and CI's `arm64;x86_64` release matrix all
configure exactly as before. `build/` is now arm64 v3.22.0.

## O2 → one helper: `tools/lib/Gate.rakumod`

`host-arch`, `binary-arch`, `binary-version`, `pick-rakupp`, `require-native`
and `provenance-line`, in one file. Six tools migrated onto it — `perf-guard`,
`run-optbench`, `doc-examples-diff`, `run-bench`, `run-roast`, `eco-sweep` — and
every local copy deleted.

The per-tool differences that mattered are parameters, not forks: the release
gates say `INCONCLUSIVE` (exit 2 is their documented "re-run", never a pass)
while the reporting tools say `REFUSED`, and each passes the consequence that
applies to it — for optbench, that a wrong-arch compiler *succeeds* and emits
binaries that crash, so its failure arrives dressed as a correctness bug.

Two things fell out of doing it:

- **`run-bench`'s documented command now works.** It defaulted to
  `build/rakupp`, had the architecture guard, and therefore exited 2 rather than
  measuring — a tool whose own usage line could not be followed.
- **`t/run.raku`'s note is corrected.** It claimed `is export` was "still flaky
  for many-sub modules", which was the standing reason for the duplication and
  is not true. The file stays self-contained for a narrower reason that does
  hold: it runs under the binary *under test*, so loading its helpers through a
  module would make the suite depend on the module system it is testing — a
  broken `use` would present as 580 broken checks.

## O5 → two measured signals, not a load threshold

The question was where to put the inconclusive cut. The answer is that load
average is the wrong instrument: it is a one-minute decayed figure that reads
4.0 on a quiet machine and 4.0 on one fighting this process for a core, and the
gate cannot tell those apart. Two signals it can measure directly were added
instead, both ahead of the existing load check.

**1. The gate's own noise.** `measure()` now returns the spread of its measured
runs alongside the minimum, and the table shows it:

```
kernel      best (ms)   spread
fib            373.3      1.2%
...
spread                    8.8%   (worst kernel; the quiet-machine floor is ~1.7%)
```

If a failing kernel's runs span more than the tolerance it is enforcing, the
reading cannot support a verdict either way — the difference being blamed on the
build is smaller than the difference between two runs of the same build.

**2. Whether the slowdown is localized.** A code regression slows the kernels
that touch the code it changed. Contention slows everything at once and roughly
equally, which keeps each kernel's own spread *small* while inflating every
absolute number — the case signal 1 cannot see, and the case actually observed:
five of nine kernels over tolerance at load 4.33, spanning arithmetic, calls,
strings and Rats, which share no code path. When at least half the gated kernels
are over tolerance, the gate now says so and stops:

```
perf-guard INCONCLUSIVE — 6 of 9 kernels are over tolerance at once.
That is not the shape of a code regression: these kernels span arithmetic,
calls, strings, hashing and regex, and share no code path.
```

Reporting INCONCLUSIVE more readily is safe in the direction that matters:
RELEASING.md defines exit 2 as *"run it again on an idle machine, never a
pass"*, so a genuine broad regression is still blocked — it is simply not yet
*accused*, and an idle re-run will confirm it.

**Verified live**, under a real build running on the same machine: the gate
reported INCONCLUSIVE citing an 8.8% spread where it would previously have
reported a confident 30% regression.

### …and improving the gate broke its own harness

Re-proving gate 3 after the two signals landed, `prove-gates` reported:

```
3   perf-guard --check   MISSED  INCONCLUSIVE (exit 2) — machine too loaded to judge
1 of 3 gates did NOT catch their planted defect
```

Which is a **false accusation against a correct gate** — the exact failure
GATES-3.22 Part C warns about (*"when a gate reports MISSED, suspect the plant
first"*). The gate did not stay green in front of the defect; it declined to
judge, which is the gate working. The harness had only two outcomes and folded
"could not perform the experiment" into "the gate is blind".

**Fixed:** three outcomes — `CAUGHT`, `MISSED`, `UNPROVED` — with `UNPROVED`
exiting 2 and saying so:

```
3   perf-guard --check   UNPROVED  (17s)  INCONCLUSIVE (exit 2) — machine too loaded to judge
0 of 1 gates caught their planted defect.
1 could not be judged this run — the gate declined, which is the gate working,
but it leaves the plant unproved.
Nothing is broken. Re-run these on an idle machine before quoting a score.
```

This mattered more after O5 than before: perf-guard now has three ways to reach
exit 2 instead of one, so the state stopped being rare.

### A third shape the two signals do not catch — and why that is still all right

Running gate 3 against the engine change turned up a case neither new signal
sees: **one kernel, consistently elevated, with a tight spread.**

```
strscan   post  127.7 / 128.4 / 129.4   spread 1.3-1.9%   vs baseline 121.4  → +5.2 to +6.5%
strscan   pre   127.9                   spread 6.4%       vs baseline 121.4  → +5.4%
```

The spread signal cannot fire (1.3% is quiet), and the localization signal cannot
fire (one kernel of nine, not half). So the gate reported `FAILED — strscan 6%
slower`, correctly by its own rules.

**The A/B is what settles it, and the gate's own message is what asks for one.**
The pre-change binary reads 127.9 against the post-change 127.7 — identical, so
the engine change is not responsible; `strscan` simply does not reproduce its
recorded 121.4 on this machine today. That is the same unexplained baseline
movement GATES-3.22 Part B documents, still live: `vs best` reads `strscan
+18%`.

No third signal was added for this. A single kernel a percentage point over
tolerance is exactly the case RELEASING.md already answers — *"a 6% reading is a
re-measure, not a finding"* — and the FAILED message now ends with "A/B against
a binary built WITHOUT the change before believing it", which is the procedure
that produced the answer above. The gate flagged, the message routed, the A/B
resolved it. That is the system working, and adding a signal to suppress it
would make the gate quieter rather than better.

## O1 → the figure checker is planted; **gate 7 cannot be**

Half of this was straightforward. `prove-gates` now carries a `figures` plant —
the defect the checker was written for, ROAST.md's standing cell left at the
previous release's number — and it is caught in under a second. The plant reads
the current value and doctors *that*, rather than hardcoding a number, because
three of v3.22.0's plants rotted when the figures moved.

The other half is the finding. **Gate 7 has no red path at all**, which is why
no plant exists and why writing one would only have discovered that:

| tool | exits |
|---|---|
| `typerun.raku` | 124 (timeout), 127 (no command) — infrastructure only |
| `matrix.raku` | no `exit` |
| `conformance.raku` | no `exit` |
| `divergences.raku` | no `exit` |

Measured: `conformance.raku` and `divergences.raku` both exit 0. RELEASING.md
lists conformance under *"The gates … Every one must pass"*, but there is nothing
for it to fail — it is a **report a human has to read**, and the ±5 flap band it
documents is applied by that human, not by the tool.

`prove-gates --list` now says so under "Release checks with NO plant, and why",
so the harness stops implying a coverage it does not have. The substantive fix —
giving conformance a committed baseline and a real pass/fail criterion — lives in
the **raku.online** repo, and is left for whoever owns that call. Until then,
"every gate detects a planted defect" should read "every gate that *can* fail".

## Post-release: what raku.online needs, and when

Step 6 of RELEASING.md said "republish the site data" and then listed
everything in one block, which made the whole thing look like one all-or-nothing
chore — and it is the step the doc itself calls the one that gets forgotten. It
is now split by what each artifact actually carries:

- **Always** — anything carrying the version number or the release timeline,
  because the release is what changed it: the Raku.js WASM engine, the
  hand-written figures on the front and install pages, `gen-roast-map`, and
  `snapshot` → `gen-dashboard` (in that order, after the tag).
- **On demand** — anything carrying a *measurement*: `/spec`, `/spec/rules`,
  `/modules`, and the content sites. Regenerating these from stale inputs
  writes a new date onto an old number, which is worse than leaving them.
- **Always on a major version** — all of it, with the sweeps re-run *first*.
  A major is the release people actually go and look at, and the wrong one to
  leave a two-release-old ecosystem table on. If a sweep cannot be run, that
  belongs in the CHANGELOG rather than in a page with a fresh timestamp.

Three inaccuracies in the existing instructions were found while writing it
down, each by checking rather than reading:

- **`rakujs/build.sh` copies nothing.** The doc said "copy the pair over"; the
  build writes into `raku++/rakujs/playground/` and **three** files have to be
  moved — `rakujs.js` and `rakujs.wasm` into `www/`, and `examples.js` into
  `www/play/`. `worker.js` must *not* be copied: the site keeps its own variant
  (2,901 bytes against the build's 2,872), so copying it would be a regression.
- **`verify.sh` is in `sites/spec/`, not the repo root** — the ordering note
  named it as though it were at the top level.
- **"`sites/` holds only faq, spec and tour"** — it holds nine directories
  (`6e`, `book`, `examples`, `faq`, `grid`, `modules`, `showcase`, `spec`,
  `tour`). The conclusion drawn from it was still right (nothing generates
  `www/index.html`), but the reason given for it was not.

And one stale count in the doc itself: *"Two more collectors go stale silently —
neither fails"* introduced **three** bullets. Fixed. It is a small thing, and it
is the same defect the figure checker exists to catch, in the prose rather than
in a table.

## O4 → the small ones

- **`eco-sweep --reclassify` rewrites instead of appending.** It left two rows
  per distribution and every consumer had to know to take the last. Verified: 2
  rows in, 2 rows out, where it used to give 4. A measuring run still appends
  line by line, so a wedged sweep keeps what it has already paid for.
- **`run-roast` reports what it leaves in the Roast checkout.** The per-run
  scratch directory *works* — the timestamps prove it: every root-level artifact
  in that checkout is from 14–16 July, before the fix landed, and none since.
  What it structurally cannot catch is a test writing beside its own `.t` file:
  `S16-io/lines.t` does `$*PROGRAM.sibling('lines.testing')`, an absolute path
  in an upstream tree we do not patch. The harness now diffs the checkout's
  untracked set across the run and names what appeared, and the provenance line
  carries the count — because it names a Roast *revision*, and a revision does
  not describe a checkout with files in it the revision never had.

**One command is left for you.** The 28 untracked files (27 of them July
residue) are still there; removing them was blocked by this session's permission
classifier, since it is a destructive operation outside the project:

```bash
git -C /Users/ash/roast clean -f
```

---

# What was run

| | |
|---|---|
| `perf-guard --check` | debt note names 5 kernels (was 1); spread column added; INCONCLUSIVE fires on measured noise |
| `perf-guard --record` / `--record --for=v3.23.0` | stamps `2026-08-29 (v3.22.0)` / `(v3.23.0)`, verifies read-back, names the binary; baseline restored after each test |
| `run-optbench` | exits 0, 8 kernels, drift guard green, on `Gate` |
| `check-figures --expect=642 --examples=950 --version=3.22.0` | passes; bucket arithmetic verified on 6 snapshots in 2 files |
| `run-roast --list=` on a subset | provenance banner, `.meta` sidecar, checkout-residue report |
| `run-bench`, `doc-examples-diff`, `eco-sweep` | all name the binary they use; documented commands run |
| `cmake -S . -B build` | now reports `building for the host architecture arm64`; explicit `-DCMAKE_OSX_ARCHITECTURES` untouched |
| `t/run.raku` | 580 checks, all passed |
| `prove-gates --gate=3,4` | 2 of 2 caught, after the tool changes |
| `prove-gates --gate=figures` | caught in 0s, `exit 1, and it says DISAGREEMENT` |
| `prove-gates --gate=3` under load | `UNPROVED`, exit 2 — the harness's new third outcome, not a false MISSED |
| planted defects, total | 4 planted, 4 caught: perf-guard kernel drift, optbench file drift, check-figures empty sighting, check-figures stale cell |
| A/B, pre vs post engine change | unmodified HEAD fails gate 3 identically under load — the verdict was the machine |

Machine of record: Darwin 24.6.0, Apple clang 17.0.0, arm64, `build-arm64/` (and
now a native `build/`).
