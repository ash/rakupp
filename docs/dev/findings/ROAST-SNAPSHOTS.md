# Roast snapshots, 2026-08-21 to 2026-09-21

These are the dated per-sitting notes that [ROAST.md](../../status/ROAST.md)
carried while the suite was being worked through, moved here at v5.0.0 so that
ROAST.md can state the current standing on its own. Each paragraph is a record
of one run on one tree and keeps that day's numbers; none of them describes the
engine today. The per-release figures are in [CHANGELOG.md](../../../CHANGELOG.md),
and the method behind every figure is in [COUNTING.md](../../status/COUNTING.md).

_Snapshot 2026-09-21 (latest), main at `9f8ef05` + the four engine hangs: 802 /
1,464 files fully passing; 563 partial, 92 no-TAP, **7 timeout (was 11)**;
208,008 / 219,744 declared assertions (94.7%). The fully-passing list is
IDENTICAL to the same-tree control, file for file, across three sweeps — the
whole of this sitting is in the timeout column and the partial credit behind it.

Four files had been timing out on an engine hang rather than on the clock, each
burning ten seconds of a core every run. All four now finish in well under a
second:

| file | before | after |
|---|---|---|
| `S03-operators/repeat.t` | `[TIME] 12/15` | `[part] 62/63`, 0.07 s |
| `S17-lowlevel/cas.t` | `[TIME] 8/12` | `[part] 20/24`, 0.43 s |
| `APPENDICES/A01-limits/misc.t` | `[TIME] 0/0` | `[part] 2/3`, 0.58 s |
| `integration/99problems-41-to-50.t` | `[TIME] 0/0` | `[part] 8/9`, 0.01 s |

**cas.t was a data race, not a slow test.** rakupp runs `start` blocks in real
parallel, and the P3 torn-copy contract — copy a shared slot out under its own
stripe, store into it under the same one — covered a plain LEXICAL and nothing
else. `cas` on an array element, a hash element or an ATTRIBUTE therefore wrote
the slot under the stripe while every ordinary read of it copied the same Value
unlocked, so a reader could take half an overwritten pointer and addref a
control block that was already gone. The linked list the test builds came out
CYCLIC about one run in three (and segfaulted outright in others), and the walk
that sums it never returned. The contract now covers all four container kinds;
a two-thread probe that alternates one object for another crashed every run
before and survives every run after. The same sitting made `cas` compare by
IDENTITY rather than `eqv`: `eqv` is the structural walk, which descends both
`.next` chains on every failed swap, and would swap on two distinct-but-equal
objects — which is the one thing a compare-and-swap must never do. The shape has
a contract program of its own now, `t/stress/cas-containers.raku`: it segfaults
on every run of the previous binary and is clean under both modes and under
ThreadSanitizer after. All twelve of
the file's linked-list tests now pass; the four that remain want a declared type
enforced on assignment (`my Node $head; $head = Any.new`), which rakupp does not
do for a user class or a subset in any assignment, `cas` or not.

**`multi rule expr(0)` is a candidate, not an overwrite.** Grammar rules were
one pattern per name, so every candidate of a literal-value multi clobbered the
one before it. P47 builds a precedence climber out of `multi rule expr(0)` and
`multi rule expr($p) { <expr($p-1)> … }`: with the base case gone, the recursion
had nothing to stop it and the parse never returned. A literal signature slot is
now a dispatch constraint — the candidates live under a mangled key, a call
evaluates its arguments once and takes the first whose literals all match, and
anything else falls through to the generic candidate. P47 passes; P48 still
fails, on its answers rather than on the clock.

**`xx` no longer builds what it cannot finish.** `NaN` and `-Inf` name no count
and are now the X::Numeric::CannotConvert `x` already gave, in the interpreter
and in compiled code alike (throws-like's diagnostic used to walk the endless
list it was handed instead). A count past ten million elements — `2**62`,
`2**99999`, which the spec sinks on purpose — is generated on demand with its
length recorded, so `.count-only` is exact and sinking costs nothing. The
routine form `infix:<xx>(&block, Inf)` is lazy and calls the block per element,
and an empty slip yields one Nil per repetition instead of never advancing.

**`"a" x 2**32-1` is one allocation.** MoarVM answers that repeat with a strand
and never materialises it; rakupp has no lazy string, and the spec file asks for
a repeat that LIVES, so it builds — but in one allocation and doubling memcpys
rather than 4.3 billion appends. Half a second instead of never. The repeat also
keeps its result NFC-normalized now, which an ASCII base skips entirely.

The read-side stripes cost **+0.5% instructions retired** on a microbenchmark
built to do nothing but read array elements, hash elements and attributes in a
loop (7.198 G → 7.246 G, interleaved, three pairs); real programs read far less
densely than that. Gated file for file against a control built from the same
HEAD: `t/run.raku` fails the identical 24 checks, and no Roast file left the
pass list. The one file that flapped across sweeps, `S17-supply/batch.t`, passes
3/3 in isolation under BOTH binaries — it is the spec-timed supply flapper
COUNTING's timeout section describes._

_Snapshot 2026-09-22, main at `b6d664c`, the Range and Int-Num-Rat semantics
sheets implemented and three alignments past them (`-j6`, one pass): 810 /
1,464 files fully passing (55.3%); 560 partial, 88 no-TAP, 6 timeout; 208,513 /
219,801 declared assertions (94.9%). The three: a declaration naming an
undeclared type is refused, as Rakudo refuses it, which took
**S02-types/int-uint.t** from 0 (it died before its first assertion) to 92/104;
a custom Real numifies through its own `.Bridge` wherever it is used, taking
**S32-num/real-bridge.t** 188/201 to 200/201; and the native containers refuse
a Str and an unboxable Int. The table and the headline figures above are from this run. Six files
joined the list against a baseline worktree built at the parent commit:
**S07-iterators/range-iterator.t** (83/103 to 103/103) and five of **S32-num** —
base.t (47/63), exp.t (64/72), narrow.t (15/17), rand.t (111/113), rounders.t
(137/143), all now whole. **S02-types/range.t** went 216/259 to 256/259 without
joining it. The common cause across all of them: a Range keeps its endpoints as
OBJECTS and a pair of int64 FIELDS it iterates over, and every one of those
files asked a question — an infinite endpoint, a fractional one, a bigint, a
string — that only the objects could answer; and `.base` computed its fraction
through a double, which loses every digit past a mantissa._

_Snapshot 2026-09-21, main at `5becd69` + a third per-file sitting
(`--workers=3 --cpu=3`, one pass): 798 / 1,464 files fully passing (54.5%); 564
partial, 92 no-TAP, 10 timeout; 207,985 / 219,915 declared assertions (94.6%).
Ten files: **S12** 33 to 36, **S32** 150 to 152, **S02** 60 to 61, **S03** 85 to
86, **S09**, **S14** and **MISC** one each.

The one that unlocked more than itself: **a `my class` / `my role` is LEXICAL**.
The name meant nothing outside its block in Rakudo and everything in ours, so
`{ my class R {}; }; R` answered the type. The declaring scope is the class's
`declEnv`'s parent, and it is in scope exactly when it is still on the current
Env chain — the same walk the redeclaration guard already made. The refusal has
to be final: the unit DECLARES the name, so the forward-reference leniency below
would otherwise wave it through on a declaration this scope cannot see. `anon
class C {…}` is the same question with no scope at all — it keeps the name for
`.^name` and installs it nowhere.

Perl 5's method arrow is now named rather than left to fail on a missing block:
a `->` tight on both sides is X::Obsolete, which needs catching in two places —
a bareword reaches the pointy-block branch, a variable ends the statement first
and arrives at the separator check. A quote tight against a name is two terms in
a row (`foo'...'`), but only in an EVAL'd snippet: an unrecognised QUOTE WORD
(`qa"…"`) arrives in exactly that shape, and a whole test file must not die over
one line. That one showed up as a file going from 40 failures to no TAP at all
with nothing leaving the pass list — measure 2's denominator again.

The rest: a FLAT list assigned to a multi-dim shaped array is
X::Assignment::ToShaped, not a size complaint; `augment class A:D`,
`my $a is readonly` and a `whenever` outside a `react` already said the right
thing and only needed their types; a `:delete` SLICE reports one element per key
(Any for a key that was not there, Nil when the container itself is undefined)
instead of leaving gaps; and `:exists:!v` on a POSITIONAL subscript is a no-op
rather than a clashing presentation adverb — `%h<a>:exists:!v` still dies, and
`:k` gets no such pass either way.

Gated file for file against the run below: nothing lost._

_Snapshot 2026-09-21 main at `b9a76b9` + a second per-file sitting
(`--workers=4 --cpu=3`, one pass): 788 / 1,464 files fully passing (53.8%); 574
partial, 92 no-TAP, 10 timeout; 208,005 / 219,915 declared assertions (94.6%).
Fifteen files, worked down the per-file failure counts rather than by chapter:
**S02** 53 to 60, **S26 (POD)** 7 to 10, **S16** 18 to 20, **S04** 32 to 34,
**S05** 39 to 40. The table and the headline figures above are from this run.

Most of it is Raku naming an error it already refused. Consecutive underscores
in a number are X::Comp::Group where a trailing one stays Confused; a numeric
LITERAL must already be the declared type, so `my Int $x = NaN` (and `my Rat $x =
42`, and `my Num $x = 1.5`) is X::Syntax::Number::LiteralType with the type and
the value as objects; a numeric adverb has nowhere to put a second value, so
`:69th($_)` is refused rather than calling the 69; a combining mark tight against
a digit makes a SYNTHETIC numeral, which is a malformed radix number inside a
colon pair and "not a valid number" anywhere else; X::Syntax::UnlessElse carries
the `.keyword` that displaced the `if`; a routine redeclaration suggests a
multi-sub and a package redeclaration does not; a Perl 5 TRAILING regex modifier
(`m/…/i`, `/…/g`) is X::Obsolete, one message per modifier; `&?ROUTINE` outside a
routine is an undeclared name; and X::IO::Closed says what it was `.trying`.

Four are behaviour rather than diagnosis. `.tree(0)` is the identity and
`.tree(*)` is no limit at all. `Whatever.new` is `*` and `HyperWhatever.new` is
X::Cannot::New. `.comb`, `.words` and `.split` on an IO::Path are questions about
the FILE, as `.lines` and `.slurp` already were — they had been combing the path
STRING. And the declarator-pod family: a method's own `#|`/`#=` now reaches
`.^find_method(…).WHY`, a leading and a trailing doc are JOINED rather than
one-or-the-other, a module keeps its doc (a package has no ClassInfo to hang it
on), and a parameter's `#|` is looked up at the PARAMETER's line — asking at
wherever the parse had reached meant the LAST parameter never found its own.
`--doc=Text` is accepted beside the bare `--doc`.

Gated file for file against the run below: nothing lost._

_Snapshot 2026-09-21 main at `f25f793` + the S15/6.d/S03/S32 working
tree (`--workers=4 --cpu=3`, one pass): 773 / 1,464 files fully passing (52.8%);
589 partial, 92 no-TAP, 10 timeout; 207,991 / 219,915 declared assertions
(94.6%). Two more fully-passing chapters: **S15 (Unicode / strings / NFG)**, 77
files to 81, and **6.d (v6.d snapshot)**, 15 to 18; **S03** went 79 files to 85
and **S32** 138 to 150. The by-synopsis table and the headline figures above are
refreshed from this run.

The four S15 files were four separate rejection bugs: an identifier may not
start with a non-ASCII digit (`X::Syntax::Variable::Numeric`) or a combining
mark (`X::Syntax::Malformed`) in a declaration; a general `:36<…>` radix number
is `X::Syntax::Malformed` where a `0b`/`0o`/`0x` prefixed literal is
`X::Syntax::Confused`, and typing both the same way traded three passes for
nine; `uniprop("")` is Nil, not the empty string; and a `todo` in force when a
`subtest` starts must not leak into the subtest's own tests.

6.d cost four: a shaped array's fixed dimensions are checked on READ and on
`:delete`, not only on assignment (`:exists`/`:kv`/`:p`/`:k`/`:v` still answer
softly); `%#08x` zero-pads in FRONT of the `0x` prefix at 6.c/6.d ("000000x1",
not "0x000001" — octal reads the same either way, binary does not); `%f` renders
the value's shortest round-trip decimal and rounds or zero-pads THAT string, so
`%.50f` of 1.115 is 1.115 followed by zeros and `%.2f` of it is 1.12; and
sprintf now raises `X::Str::Sprintf::Directives::{Count,BadType,Unsupported}`
where it used to format an (Any), render a Junction's eigenstates or echo an
unknown directive back as text.

The Count check found one real bug elsewhere: the lexer captured a `s{…} = EXPR`
replacement up to the first top-level comma, so `s{a} = join "|", 1, 2` lost the
list operator's arguments. A parenless listop owns those commas now.

The other seventeen came from working down the per-file failure counts in the
two chapters closest to 100%. In **S03**: `xor` in sink context sinks each of
its operands, so the constant among them is reported; feeding an endless list
into an `@` target dies where `my @a = 0..Inf` does not; the `$*TOLERANCE` a
Complex comparison uses is RELATIVE to the real part with no floor of 1;
`++++$x` matches no candidate, because `prefix:<++>` returns a value and not the
container; `succ`/`pred` know the superscript digits, which are not a contiguous
codepoint run; `eqv` refuses two lazy iterables of the same type; and flip-flop
state is keyed per CLONE, so a `sub` declared inside a loop body starts fresh
each time round. In **S32**: a term called as a routine (`pi()`, `e()`) is
X::Undeclared rather than an undefined routine; `end`/`kv` are one-argument
protos; `.push` and its family on a scalar are a NoMatch; `.batch(0)`'s
X::OutOfRange carries `.got`; only integers are prime, and a Complex becomes
Real first; a superscript run after an Nl/No numeral is the power operator
(`²¹²` is 4096) but never inside a `< … >` word list; the imaginary half of a
numeric string needs a digit of its own, so `"3+Infi"` is refused where
`"3+Inf\i"` is not; and `-i` has a POSITIVE zero real part.

Gated against the run below, file for file: nothing lost. One near-miss worth
recording — the `eqv` refusal first fired on two RANGES as well, which killed
`S02-types/range.t` at test 185 of 259. No file left the pass list, and it was
measure 2's denominator that gave it away, exactly as
[COUNTING](../../status/COUNTING.md#watch-measure-2s-denominator-not-just-its-numerator) says to expect._

_Snapshot 2026-09-21 (later still), main at `6aba9e9` + the S19 working tree
(`--workers=4 --cpu=3`, one pass): 749 / 1,464 files fully passing (51.2%); 614
partial, 92 no-TAP, 9 timeout; 207,912 / 219,915 declared assertions (94.5%).
The sitting was **S19 (Command-line)**: 6 fully-passing files to 7 and its one
partial to none, which is the whole reachable chapter. Rakudo passes 7 of these
8 as well, and it is the same 7 — the file neither engine passes,
`01-dash-uppercase-i.t`, is a Pugs-era test that wants `$*OS` and
`$*EXECUTABLE_NAME` (removed from the language; no other Roast file names
either), a global `@*INC` (replaced by `$*REPO`), and a `run $string` that
shells out and honours `>` (Raku's `run` does neither). It is absent from
Rakudo's `spectest.data` and from
[rakudo-2026.08.list](../../status/roast-lists/rakudo-2026.08.list), and it is one of the 31
files in the ceiling section above.

The cause was in `applyRakudoFudge`, not on the command line. A `#?rakudo todo`
in front of a column-0 block was read as a one-test todo; roast's own `fudge`
instead recurses into the block — "do all in block as one action" — and prefixes
`todo(<reason>);` to every test statement inside it. `04-negation.t` puts three
`is_run`s under one such directive, so two of its three tests ran exposed
against a bar Rakudo's fudged spectest shields. The directive is suite-wide, not
an S19 one: 41 block-form `todo`s sit in 31 files, and the fix also shields 4
more tests in `S12-enums/basic.t` and 3 in `S03-operators/identity.t`. All three
deltas reproduce standalone. Gated against a clean build of `6aba9e9` measured
the same way: no file lost. The two files gained beyond `04-negation.t` —
`S15-nfg/concat-stable.t` and `S17-supply/syntax-nonblocking-await.t` — pass in
isolation under either build, the load flappers COUNTING's timeout section
describes. The by-synopsis table above and COUNTING's measures are deliberately
not refreshed from this run: the build is stamped `-modified`, and those figures
want a clean one._

_Snapshot 2026-09-21 (later), main at `a23f1ba` + the S13 working tree
(`--workers=4 --cpu=3`, one pass): 747 / 1,464 files fully passing (51.0%); 615
partial, 92 no-TAP, 10 timeout; 207,830 / 219,677 declared assertions (94.6%).
**S13 (Overloading) is a fully-passing chapter**, 5 files to 7. Gated against the
run below: no file lost. The 19 fewer assertions EMITTED are accounted for file
by file — `S17-promise/start.t` flapped from 44 to 1 under load (3 real failures
either way, measured in isolation), against +17 from `metaoperators.t` running at
all and +7 from `S03-metaops/reverse.t` reaching further. Not a release run._

_Snapshot 2026-09-21, main at `0f77ba5` + the S03 working tree (`--workers=4
--cpu=3`, one pass): 745 / 1,464 files fully passing (50.9% coverage); 616
partial, 93 no-TAP, 10 timeout; 207,843 / 219,677 declared assertions (94.6%).
The sitting was **S03 (Operators) alone**, 48 fully-passing files to 79 and 97%
of assertions to 99%, with its no-TAP count falling 13 → 5. Gated against a clean
build of `0f77ba5` measured the same way (714 files): 32 files gained, and the
only file in the baseline's pass list absent from this one is
`S17-channel/stress.t`, which passes in isolation three times out of three — the
load flapper COUNTING's timeout section describes, not a regression. Everything
outside S03 moved only where an operator fix reached it (S02 +12 assertions, S05
+21, S32 +88). Not a release run._

_Snapshot 2026-09-20, main at `a4291988` (`--cpu=5`, one pass, 37 s): 705 /
1,464 files fully passing (48.2% coverage); 648 partial, 101 no-TAP, 10 timeout;
205,100 / 219,626 declared assertions (93.4%). Not a release run — the figures
above were re-measured because the standing ones dated from v4.0.1 and the
dashboard reads this file. Against that tag the suite gained 29 files and 4,257
assertions, the bulk of it **S17 concurrency, 46 fully-passing files to 74**,
with its no-TAP count falling 9 → 3 as the supply and promise fixes of the
preceding days landed. Two files went backwards and are not yet fixed:
`S15-literals/identifiers.t` 7/7 → 5/7 and `S15-literals/numbers.t` 49/49 →
46/49, all five lost assertions being rejection tests that the lexer's
character-class predicates now wrongly accept. The per-commit gate could not see
them: it diffs against the immediately preceding commit, and these landed
earlier in the same 84-commit span._

_Snapshot 2026-09-07, main at `35c9691` (`--workers=4`, five passes): 660 /
1,464 files fully passing (~45% coverage); 672 partial, 117 no-TAP, 15 timeout.
COUNTING's rule is to quote the repeating profile, and the first three passes did
not repeat — the band was 661 / 660 / 658 / 662 / 660 with 13 / 14 / 22 / 12 / 15
timing out, on a box carrying another session's build load. 660 repeats, so every
figure above is from that pass; the union of the five is 662 files. Not a release
run: the standing figures were re-measured after the Grand Review's phase-1 fixes
(REVIEW-GRAND.md) and the three engine bugs its phase 2 turned up. The
documentation-example and ecosystem figures in README's table were NOT
re-measured here and still carry their v3.25.0 values._

_Snapshot 2026-09-03, the v3.25.0 release run (`--workers=4`, three passes):
651 / 1,464 files fully passing (~44% coverage); 683 partial, 117 no-TAP,
13 timeout. The file count repeats at 651 (band 651 / 651 / 650). No file
regressed: the union of the three passes, diffed against v3.24.0\x27s union, is
empty in the regressed direction and gains four (`S04-statements/loop.t`,
`S09-typed-arrays/native-decl.t`, `S15-nfg/concat-stable.t`,
`integration/advent2012-day03.t`); the one file that moved between passes,
`S03-operators/scalar-assign.t`, was `[TIME]` in the pass that lost it. Measured
on `v3.24.0-51-g4d873a8` against Roast `b2cbe8a42` — the same Roast revision
v3.24.0 used, so the list diff is an engine comparison and nothing else._

_Snapshot 2026-09-01, the v3.24.0 release run (`--workers=4`, three passes):
646 / 1,464 files fully passing (~44% coverage); 685 partial, 119 no-TAP,
14 timeout. The file count repeats at 646 (band 645 / 646 / 646). No file
regressed: the union of the three passes, diffed against v3.23.0's union, is
empty in the regressed direction and gains three (`S12-attributes/mutators.t`,
`S12-methods/lvalue.t`, `S32-io/out-buffering.t`). Measured on
`v3.23.0-64-g8ba790a` against Roast `b2cbe8a42` — the same Roast revision
v3.23.0 used, so the list diff is an engine comparison and nothing else. An
EARLIER three passes on this release's code, before two regressions were fixed,
read 640 / 641 / 640: the file LIST is what showed those, since that band
overlaps v3.23.0's own._

_Snapshot 2026-08-29, the v3.23.0 release run (`--workers=4`, three passes):
643 / 1,464 files fully passing (~44% coverage); 685 partial, 121 no-TAP,
15 timeout. The file count repeats at 643 (band 640 / 643 / 643). No file
regressed: the union of the three passes, diffed against v3.22.0's union, is
empty in both directions. Measured on `v3.22.0-6-g17b17a8` against Roast
`b2cbe8a42` — the first release whose Roast revision is recorded, in the run's
own banner and in a `.meta` sidecar beside the archived list._

_Snapshot 2026-08-29, the v3.22.0 release run (`--workers=4`, three passes):
642 / 1,464 files fully passing (~44% coverage); 686 partial, 121 no-TAP,
15 timeout. The file count repeats at 642 (band 642 / 642 / 644). No file
regressed: every file passing in the previous full run passes in at least one of
the three, and the files that vary between passes are all `S17-*` concurrency and
scheduler tests sitting near the 10-second per-file timeout — each one `[TIME]`
in the pass that lost it, and passing when run alone. The fully-passing file
LISTS are archived per release in
[roast-lists/](../../status/roast-lists/), so the next release diffs against data rather than
re-parsing this output; the three passes' union is kept beside the quoted pass
for exactly the flap described above. This is also the first run in which no
status line was corrupted: at `--workers=4` the children's TAP diagnostics used
to splice into the parent's output, consistently four a run, because
`Proc::Async` INHERITS an untapped stderr._

_Snapshot 2026-08-29, the v3.21.0 release run (`--workers=4`, four passes):
643 / 1,464 files fully passing (~44% coverage); 685 partial, 121 no-TAP,
15 timeout. The file count repeats at 643 (band 643 / 642 / 639 / 643 — the
639 came from a pass during which the OS resumed Photos analysis and Spotlight
indexing, and its timeouts rose to 20). No file regressed: every file passing
in the previous full run passes in at least one of the four, and the six that
vary between passes are all timeout-prone concurrency and exit tests._

_Snapshot 2026-08-27, the v3.20.0 release run (`--workers=4`, three passes
on an idle box): 638 / 1,464 files fully passing (~44% coverage); 687
partial, 121 no-TAP, 18 timeout. The file count repeats at 638 (band
638 / 638 / 637) and the figures quoted are from a repeating-profile pass.
Eleven files newly full in every pass (`S29-context/evalfile.t`,
`S06-multi/positional-vs-named.t`, `S17-supply/lines.t`/`words.t` among
them); the per-file diff against the v3.7.0 published map documents every
drop — five are the harness-timeout family (each passes standalone,
`S15-normalization/nfc-concat.t` at 2943/2943), one is a log-interleaving
artifact verified full standalone, and `integration/advent2012-day14.t` is
the release's one understood regression: the engine no longer invents a
step for an underivable sequence, and that file's sieve was passing on a
guessed step that put 9 into a list of primes — lazy seed streaming is the
noted follow-up._

_Snapshot 2026-08-24, the v3.7.0 release run (`--workers=4`, five passes):
633 / 1,464 files fully passing (~43% coverage); 692 partial, 124 no-TAP,
15 timeout (the band was 633 / 631 / 633 / 633 / 633, and each pass drops
exactly ONE file to the 10-second timeout — a DIFFERENT file every time, so
the union of the five is 634 and `comm -23` against the v3.6.0 published map
is empty: nothing regressed in any pass. Every file some pass dropped scores
100% run alone, `S03-operators/scalar-assign.t` (4 assertions) three times
over; the one agreed gain is `S32-list/map_function_return_values.t`. Two
files left renamed `.SKIP` in the checkout since before v3.6.0 —
`S04-statements/try.t` and `S12-construction/destruction.t`, both described
above as measured in-run — were restored for this release, which is why the
denominator reads 1,464 rather than the 1,462 a run would otherwise report.)_

_Snapshot 2026-08-21, the v3.6.0 release trio (`--workers=4`): 633 / 1,464
files fully passing (~43% coverage); 692 partial, 123 no-TAP, 16 timeout
(the scheduler/io timing files flap between pass and timeout under runner
load; the three runs gave 631 / 629 / 633 files and the figures quoted here
are the run whose fully-passing list contains every file the others passed).
The file-list diff against the last published map (v3.14.0 — the v3.5.0
release skipped the site republish, which this release makes up) is CLEAN:
the one file below the baseline in all three runs,
`S32-list/map_function_return_values.t`, is a timing-marginal file that
scores 2/2 re-run alone — the documented timeout flutter, not a regression.
`S12-methods/class-and-instance.t`, which HAD regressed in the v3.5.x cycle
(a stale forward-reference record re-ran the class body on a missed method
call, 13 tests against a plan of 12), is fixed this release and back to
[PASS] 12/12. This snapshot adds
`S12-construction/destruction.t` at 6/6 — the DESTROY protocol landed that
day (instances of DESTROY-declaring classes are registered at construction and
swept child-class-first on `$*VM.request-garbage-collection`, at allocation
pressure, and at program end) — and `S32-str/fc.t` back at 12/12 after the
ASCII fold-case fix. Reached-assertion
pass rate 198,647 / 205,087 (see caveat above — not a coverage figure).
S05-substitution is a fully-passing subchapter (67222.t, match.t, subst.t).
S05-modifier/Perl_0–10 — the 918-assertion `m:P5` corpus generated from perl's
own re_tests — passes fully on real Perl-5-syntax matching; before the `:P5`
adverb landed these files skip-all'ed, so the totals don't move but the skips
became genuine passes._
