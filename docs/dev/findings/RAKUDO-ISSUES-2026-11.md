# Rakudo's issue tracker, run through Raku++ (2026-11-12)

Rakudo's GitHub tracker is nine years of "this code did the wrong thing", most
of it with the code attached. A closed bug is a test case someone already
minimised. An open one asks whether we have the same bug. This is the first
pass: every issue classified, every snippet that could be pulled out run under
both engines, and every disagreement judged. Nothing was posted or commented
anywhere; the download was read-only.

Anchors: rakudo/rakudo issues as of **2026-11-12** (3,222 issues, #1–#6926,
pull requests excluded); **Rakudo v2026.09**; **Raku++ 5.7.0-111-g56aec789**.
Data and scripts are in [rakudo-issues-2026-11/](rakudo-issues-2026-11/):

- [FIX-LIST.md](rakudo-issues-2026-11/FIX-LIST.md) — every crash, wrong answer,
  missing feature and strictness gap, grouped by area.
- [WINS.md](rakudo-issues-2026-11/WINS.md) — where Raku++ already beats Rakudo,
  and the open Rakudo bugs it shares.
- [issues.tsv](rakudo-issues-2026-11/issues.tsv) — all 3,222 issues with their
  classification.
- [review.tsv](rakudo-issues-2026-11/review.tsv) — the 1,084 judged cases.
- [harness/](rakudo-issues-2026-11/harness/README.md) — how to run it again.

The snippets themselves are kept outside this repo, in
[raku-corpus-snippets](https://github.com/ash/raku-corpus-snippets)
(locally `/Users/ash/raku-corpus-snippets/`), the companion to raku-corpus for
code other people wrote. That is 1,938 of them under `rakudo-issues/`, each
filed as `{open,closed}/<issue>/<k>`, so case `1447.0` here is
`rakudo-issues/closed/1447/0.raku` there. Each has a Rakudo v2026.09
reference made with raku-corpus's protocol, and the review verdict sits in
`rakudo-issues/index.tsv`. That repository also holds:

- the fetch and extraction scripts, to refresh the set;
- a harness that kills a timed-out snippet's whole process group.

The group kill matters: #5732 and #5733 start child processes, and under
raku-corpus's harness those children outlived their timeout by an hour.

1,777 of those references are deterministic. Raku++ matches 1,264 of them
(820 of 1,126 under `closed/`, 444 of 651 under `open/`), as measured on
2026-11-12. The review verdicts explain nearly every miss:

| review verdict | misses |
|---|---:|
| PPBUG | 133 |
| PPWIN (the reference is Rakudo's bug) | 129 |
| LAX | 95 |
| NOISE | 53 |
| UNCLEAR | 36 |
| INTERNAL | 21 |
| MISSING | 18 |
| SHARED | 15 |
| ERRMSG | 5 |
| PPCRASH | 5 |

Three are unreviewed. To run any engine against the set:
`RAKUPP=/path/to/binary harness/diff.sh`.

## Method

1. **Download.** All issues through the public search API, without a token,
   partitioned by date (≈35 requests). Comments were not fetched: an
   unauthenticated client gets 60 core requests an hour, and there are 3,222
   threads.
2. **Classify.** Each issue was read (title, labels, a 900-character body
   excerpt) and given four codes, from the fixed lists in
   [TAXONOMY.md](rakudo-issues-2026-11/harness/TAXONOMY.md):
   - **area**: 29 codes (regex, oo, dispatch, …);
   - **kind**: bug, crash, hang, lta, perf, feature, design, regression,
     build or docs;
   - **relevance to another engine**:
     - `lang` — observable semantics;
     - `error` — the wording of an error;
     - `tool` — REPL, CLI and the like;
     - `design` — an open spec question;
     - `impl` — Rakudo, MoarVM or JVM internals, the build, Blin;
   - a one-line gist.

   Eight model reviewers did the reading, about 400 issues each, and every
   number was checked against the download.
3. **Extract.** Code was pulled out of the issue bodies: fenced blocks,
   `raku -e '…'` lines, `m:`/`c:` IRC-bot lines and REPL transcripts. That
   gave 3,844 snippets from 2,017 issues; 1,205 issues have none.
4. **Run.** Up to three snippets per issue were run on both engines, 3,358 runs
   each. Every run had:
   - a 10 s timeout;
   - stdin closed;
   - a fresh `HOME`/`TMPDIR`;
   - `sandbox-exec` blocking file writes outside that directory and any
     network beyond localhost.

   Each run was bucketed by exit status and normalised stdout. The 640 runs
   where the engines disagreed were run again, to flag output that changes
   from run to run; 88 did.
5. **Judge.** Eight reviewers judged 1,084 cases against
   [REVIEW-GUIDE.md](rakudo-issues-2026-11/harness/REVIEW-GUIDE.md):
   - every disagreement;
   - every agreement on an open issue (does Raku++ share the bug?);
   - every open-issue run where both engines die and the snippet is not
     obviously broken.

   For an open issue Rakudo is not the reference; the issue's own expected
   behaviour is.
6. **Verify.** I re-ran the important verdicts by hand, from the original
   snippet or a reduction of it, and corrected four
   ([overrides.tsv](rakudo-issues-2026-11/harness/overrides.tsv)). 104 entries
   in FIX-LIST.md carry the ✓ of a hand re-run. The rest are reviewer verdicts
   from recorded outputs.

## The tracker at a glance

| relevance | open | closed | total |
|---|---:|---:|---:|
| `lang` — semantics another engine must match | 409 | 1,047 | 1,456 |
| `impl` — Rakudo/MoarVM/JVM internals, build, Blin | 165 | 898 | 1,063 |
| `error` — wording of errors and warnings | 105 | 184 | 289 |
| `design` — open language questions | 63 | 153 | 216 |
| `tool` — REPL, CLI, `--doc`, Test output | 44 | 154 | 198 |
| **total** | **786** | **2,436** | **3,222** |

"Closed" includes the 13 closed as not planned.

Kinds:

| kind | issues |
|---|---:|
| bug | 1,512 |
| regression | 388 |
| lta | 291 |
| design | 244 |
| feature | 221 |
| build | 152 |
| docs | 141 |
| crash | 110 |
| perf | 100 |
| hang | 63 |

Issue volume peaked in 2018 (1,004 opened, the year after the move from RT).
It has run at 120–280 a year since 2021.

Where the language issues (`lang` + `error`) sit:

| area | total | open |
|---|---:|---:|
| oo (classes, roles, MOP, subsets, enums, coercion) | 255 | 77 |
| syntax (parsing, operators, metaops, junctions) | 217 | 99 |
| lists (List/Array/Seq, laziness, Range, sequence op) | 165 | 33 |
| dispatch (signatures, multis, captures) | 138 | 50 |
| regex | 121 | 36 |
| io | 103 | 26 |
| exceptions | 98 | 25 |
| numeric | 79 | 18 |
| concurrency | 73 | 16 |
| scope | 63 | 23 |
| hashes / QuantHash | 60 | 2 |
| modules | 59 | 18 |
| strings | 58 | 18 |
| control (phasers, loops) | 57 | 19 |
| nativecall | 40 | 16 |
| datetime | 34 | 2 |
| unicode | 26 | 6 |
| pod | 24 | 0 |
| grammar | 23 | 13 |
| everything else | 52 | 17 |

Labels that say something about the tracker itself:

- **"Will be addressed in RakuAST"**: 298 issues, 272 of them still open. These
  are bugs in Rakudo's legacy front end, parked until RakuAST becomes the
  default. **"Fixed in RakuAST"**: 83, of which 72 are still open for the same
  reason.
- **BLOCKER** (263, none open), **regression** (241) and **blin** (75) are the
  release machinery. Before each release, Blin runs every ecosystem module's
  tests against the candidate, and each module that breaks becomes an issue.
- **LTA**: 186 issues, 85 open. These are error-message quality.

## What running the snippets found

Harness buckets, per run (an issue contributes up to three):

| bucket | closed issues | open issues | total |
|---|---:|---:|---:|
| `match` — same stdout, both exit 0 | 814 | 238 | 1,052 |
| `both-die` — both exit non-zero, same stdout | 1,204 | 422 | 1,626 |
| `out-diff` — both exit 0, stdout differs | 151 | 88 | 239 |
| `rk-dies` — only Rakudo fails | 135 | 100 | 235 |
| `pp-dies` — only Raku++ fails | 67 | 22 | 89 |
| `both-die-out-diff` | 10 | 7 | 17 |
| `pp-timeout` / `rk-timeout` / `both-timeout` | 8 / 26 / 30 | 2 / 12 / 7 | 10 / 38 / 37 |
| `pp-crash` / `rk-crash` / `both-crash` | 3 / 3 / 2 | 3 / 3 / 1 | 6 / 6 / 3 |

`both-die` is mostly extraction noise:

- blocks that mix code with its printed output, so both engines say "Two terms
  in a row";
- modules that are not installed (55 runs);
- the network, which the sandbox blocks.

The useful signal is in the disagreements.

Review verdicts:

| verdict | closed | open | cases | issues |
|---|---:|---:|---:|---:|
| **PPCRASH** — Raku++ crashes or hangs | 5 | 4 | 9 | 8 |
| **PPBUG** — Raku++ gives a wrong answer | 147 | 58 | 205 | 160 |
| **MISSING** — Raku++ lacks a feature | 18 | 10 | 28 | 25 |
| **LAX** — Raku++ accepts code Rakudo rightly refuses | 59 | 36 | 95 | 82 |
| ERRMSG — only the error text differs | 4 | 37 | 41 | 38 |
| **PPWIN** — Raku++ right, Rakudo still wrong | 66 | 95 | 161 | 144 |
| **SHARED** — both show the open issue's bug | 4 | 68 | 72 | 63 |
| BOTHOK — both now behave as the issue wants | 1 | 171 | 172 | 132 |
| INTERNAL — exercises nqp/MoarVM internals | 19 | 10 | 29 | 24 |
| UNCLEAR | 22 | 21 | 43 | 36 |
| NOISE — timing, hash order, broken snippet, environment | 56 | 171 | 227 | 184 |

42 of the 205 PPBUG cases (23 issues) are a single bug, F1 below. Without it,
165 cases in 137 issues remain.

## Fix list — confirmed by hand

Every item here was re-run on 2026-11-12 and reproduced. The full list,
including the unconfirmed reviewer verdicts, is
[FIX-LIST.md](rakudo-issues-2026-11/FIX-LIST.md). Rakudo issue numbers are
links into github.com/rakudo/rakudo/issues; *(open)* means Rakudo has not
closed the issue.

### Crashes and hangs (fix first)

- **[#5868](https://github.com/rakudo/rakudo/issues/5868) *(open)*: SIGBUS
  under `.race`.** `(^100_000).race.map` calling a sub that returns
  `Foo.new but role :: { method get-value { $value } }`, where the role closes
  over a lexical, kills Raku++ (exit −10, no output). Rakudo dies with the race
  error the issue reports. A reduction without the closed-over lexical
  survives 2,000 iterations.
- **[#5537](https://github.com/rakudo/rakudo/issues/5537) *(open)*: SIGBUS
  when a user class shadows a core type.** A user `class Rat is Quadruped`
  kills Raku++ partway through the issue's script (exit −10), right after
  the core `Rat` prints. A bare `class Rat is Mu {}; Rat.new` survives.
- **[#3775](https://github.com/rakudo/rakudo/issues/3775): SIGBUS on a regex
  that names itself.** `my $regex; $regex = /… <$regex> …/` dies with exit
  −10, apparently recursing until the stack is gone. It is also a strictness
  gap: Rakudo refuses the bare `…` ("Unrecognized regex metacharacter").
  Unconditional recursion through `<$var>` needs a depth guard that throws.
- **[#5835](https://github.com/rakudo/rakudo/issues/5835) *(open)*: a hang
  instead of an error.** `my $b; $b[1e18] = "meow"` hangs. Rakudo panics; the
  issue asks for an exception, and so should we.
- **`react` that never finishes.**
  - [#4853](https://github.com/rakudo/rakudo/issues/4853): two
    `start react` blocks share a Channel, one `.close`s it, and both end on
    `whenever $ch.closed { done }`. Raku++ keeps ticking the
    `Supply.interval`s forever; Rakudo prints `2 5 Data: 5.2` and exits.
  - [#3686](https://github.com/rakudo/rakudo/issues/3686) *(open)*: a
    repeated `react` over two Channels hangs in Raku++. Rakudo finishes (and
    loses data, which is the issue's bug).

### F1 — a phaser used as a value runs when it is reached, not in its phase

`say "body"; my $x = INIT say "init"` prints `body` first in Raku++; Rakudo
prints `init` first. The same happens with `ENTER <expr>` and even
`BEGIN <expr>`. So the idiom of nearly every Rakudo performance issue,
`say now - INIT now` or `{ …; say now - ENTER now }`, prints about −0.00003.
That is 42 cases in 23 issues, among them
[#1323](https://github.com/rakudo/rakudo/issues/1323),
[#1329](https://github.com/rakudo/rakudo/issues/1329),
[#1368](https://github.com/rakudo/rakudo/issues/1368),
[#1415](https://github.com/rakudo/rakudo/issues/1415),
[#1491](https://github.com/rakudo/rakudo/issues/1491) and
[#1681](https://github.com/rakudo/rakudo/issues/1681).

`my $t = INIT now` alone looks right only because the assignment runs first.
`INIT <expr>` is parsed into a `do` around an INIT-flagged block
(src/Parser.cpp, the `INIT <expr> — a value phaser` branch), which is
evaluated in place. `FIRST <expr>` is not a term at all:
[#4351](https://github.com/rakudo/rakudo/issues/4351) `say FIRST 2` dies with
"Undefined routine 'FIRST'".

### Wrong answers

Operators and metaoperators:

- [#4757](https://github.com/rakudo/rakudo/issues/4757): `2 R- 3 R- 4` gives
  3; Rakudo gives −1 (`R-` is right-associative, like `[R-]`).
- [#6128](https://github.com/rakudo/rakudo/issues/6128),
  [#1616](https://github.com/rakudo/rakudo/issues/1616): `(1,2) RZ (3,4)` and
  `&[RZ,]` are "Undefined routine 'RZ'" / "Unsupported operator"; Rakudo gives
  `((3 1) (4 2))`. R over Z (and over S) is not composed.
- [#4128](https://github.com/rakudo/rakudo/issues/4128): `reduce &[>], 6, 5, 4`
  gives False. A chaining operator must chain under `reduce`, as it does
  under `[>]`.
- [#2217](https://github.com/rakudo/rakudo/issues/2217): `½ ∈ ^2` is True.
  Rakudo says False: a Range becomes a Set of its integers.
- [#2000](https://github.com/rakudo/rakudo/issues/2000):
  `&postcircumfix:<{ }>` is "Undefined routine" when called directly, and
  through a variable `f(%h, "a")` returns the whole hash. So
  `(%h, |@path).reduce: &postcircumfix:<{ }>` fails with a misleading
  "No such method 'reduce' for List".

Parsing:

- [#2243](https://github.com/rakudo/rakudo/issues/2243):
  `LEAVE { say 42 } if False;` is refused: "a block's '}' ends a statement
  only at the end of a line". The #206 rule must not fire when a statement
  modifier follows a block-statement phaser.
- [#1360](https://github.com/rakudo/rakudo/issues/1360),
  [#2078](https://github.com/rakudo/rakudo/issues/2078): an anonymous class
  as the first term of a statement does not take postfixes and infixes after
  it. `class { method sink {…} }.new for ^5` is "Missing block", and
  `class { has $.z is rw }.new.z = 42` is "unexpected operator in term
  position".
- [#5027](https://github.com/rakudo/rakudo/issues/5027): sigilless parameters
  whose names end in `_` (`-> Map \labels_, Int \token_ {…}`) fail with
  "expected { (got '_')".
- [#2336](https://github.com/rakudo/rakudo/issues/2336):
  `my (&op:(Int, Int --> Int)) = …` is a parse error.
- [#3376](https://github.com/rakudo/rakudo/issues/3376):
  `enum E does Positional <a b>` is a parse error.
- [#5901](https://github.com/rakudo/rakudo/issues/5901): `*>>.fmt("%02x")` is
  a parse error.
- Found while probing: `($n++; last if $n > 3) for 1..*` is "Two terms in a
  row". A statement modifier inside a parenthesised statement list is not
  accepted.
- [#1264](https://github.com/rakudo/rakudo/issues/1264): a user
  `sub infix:«"z"»` is invisible to `EVAL` ("Two terms in a row"); Rakudo
  prints `here`.
- [#2129](https://github.com/rakudo/rakudo/issues/2129):
  `sub foo { return 0.flip or die "bar" }().say` prints `(Any)`; Rakudo
  prints `0`.

Objects, roles and the MOP:

- **[#5908](https://github.com/rakudo/rakudo/issues/5908),
  [#6113](https://github.com/rakudo/rakudo/issues/6113): a read-only
  attribute can be assigned.** `$.b = 666` inside a method of a class with
  `has $.b` (not `is rw`) succeeds. Rakudo throws "Cannot modify an immutable
  Int". [#4463](https://github.com/rakudo/rakudo/issues/4463)
  (`method b is raw { $.a }; $o.b = 42`) is the same hole.
- [#2663](https://github.com/rakudo/rakudo/issues/2663): `self!bow` in a
  class's method calls a mixed-in role's private `!bow` instead of the class's
  own.
- [#1285](https://github.com/rakudo/rakudo/issues/1285): a coercion type as a
  role argument does not coerce. `role R[::T] { has T $.c }`,
  `R[Int()].new(c => 42.12)` dies with "expected Int but got Rat"; Rakudo
  gives 42.
- [#6031](https://github.com/rakudo/rakudo/issues/6031):
  `my %x is Set[Int()] = "42"` dies; Rakudo coerces the key. Bag and Mix
  behave the same.
- [#2068](https://github.com/rakudo/rakudo/issues/2068):
  `my Int %h = :42foo; %h.map({ .value = "meows" })` stores the Str. The
  element type is not checked through a Pair's `.value`.
- [#3292](https://github.com/rakudo/rakudo/issues/3292):
  `my \foo = HistoryVar.new; foo = 1`, with a user container class, fails
  "Cannot modify an immutable HistoryVar" instead of calling its STORE.
- [#4543](https://github.com/rakudo/rakudo/issues/4543):
  `&grep does role { method CALL-ME(|) {…} }; grep(Str)` ignores the mixed-in
  CALL-ME.
- [#3976](https://github.com/rakudo/rakudo/issues/3976): a `multi submethod`
  is listed by `.^methods`, not `.^submethod_table`.
- [#2174](https://github.com/rakudo/rakudo/issues/2174),
  [#1959](https://github.com/rakudo/rakudo/issues/1959): `does` is not
  callable as a routine, and `$set does …` dies with "Cannot use 'does'
  operator on a Set".
- [#1855](https://github.com/rakudo/rakudo/issues/1855): the numerator of
  `Rational[IntStr, IntStr]` is an Int, not an IntStr.

Dispatch:

- [#6860](https://github.com/rakudo/rakudo/issues/6860): a user
  `multi infix:<~>(Str() $a, Int:U $b) { "B" }` is not chosen for
  `"A: " ~ Int`; the built-in wins and warns.
- [#4553](https://github.com/rakudo/rakudo/issues/4553): `.assuming` loses an
  optional parameter's default.
- [#4665](https://github.com/rakudo/rakudo/issues/4665),
  [#4666](https://github.com/rakudo/rakudo/issues/4666): **`is cached` never
  caches.** The issue's `pow2` takes 4,094 calls where Rakudo takes 11. It is
  also accepted without `use experimental :cached`, which Rakudo requires.

Lists and sequences:

- [#1447](https://github.com/rakudo/rakudo/issues/1447):
  `roundrobin ((1,2,3),(4,5),(6,7,8,9))`, one list-of-lists argument, gives
  nine one-element lists. The single-argument rule is not applied; with
  parentheses it works.
- [#2485](https://github.com/rakudo/rakudo/issues/2485):
  `<a b c>.head(*.elems - 1)` gives `()`; Rakudo gives `(a b c)`.
- [#3881](https://github.com/rakudo/rakudo/issues/3881):
  `{ $++ * ($ *= -1) } ...^ 21` never stops; Rakudo stops at 20.
- [#1692](https://github.com/rakudo/rakudo/issues/1692):
  `eager Seq.from-loop: {…}, {1}` dies "Cannot eager a lazy list".
- [#1629](https://github.com/rakudo/rakudo/issues/1629):
  `<a b c>.combinations(2.3..^3.6)` drops the 3-element combination.
- [#1355](https://github.com/rakudo/rakudo/issues/1355):
  `-> +l { l }([1,(2,3)]).raku` loses the inner `$( )`.
  [#3621](https://github.com/rakudo/rakudo/issues/3621): `.raku` of an
  itemised Seq or List drops its `$( )` too.
- [#5951](https://github.com/rakudo/rakudo/issues/5951):
  `my @r = [»+«] [[1,2,3],[5,4,3]]` gives `[6, 6, 6]`; Rakudo gives
  `[[6, 6, 6],]`.
- [#5138](https://github.com/rakudo/rakudo/issues/5138):
  `(1..*).Slip.is-lazy` is False.
- [#1399](https://github.com/rakudo/rakudo/issues/1399),
  [#1398](https://github.com/rakudo/rakudo/issues/1398): `.sink for 1..*`
  returns at once. A side-effect-free loop over an infinite range must still
  run forever, as it does with any body that does something.

Regexes and grammars:

- [#1551](https://github.com/rakudo/rakudo/issues/1551):
  `'hello' ~~ TR/e/1/` gives False; Rakudo gives `h1llo`.
- [#1243](https://github.com/rakudo/rakudo/issues/1243):
  `.subparse("ABCDE", :pos(4))` ignores `:pos`, and a failed `.subparse`
  returns Nil where Rakudo returns a failed Match.
- [#4752](https://github.com/rakudo/rakudo/issues/4752):
  `'00000' ~~ m{^ 0 <same> ** 2..4}` never matches.
- [#5799](https://github.com/rakudo/rakudo/issues/5799):
  `"o\c[COMBINING RING ABOVE]" ~~ /o\c[COMBINING RING ABOVE]/` is Nil. A
  literal base-plus-combining pair in a pattern does not match its grapheme.
- [#2791](https://github.com/rakudo/rakudo/issues/2791):
  `<!before \S>` at the end of the string fails inside a grammar rule (five
  of the issue's eight tests).
- [#6810](https://github.com/rakudo/rakudo/issues/6810):
  `my token T { ^ <{$re}> }` declared in a sub cannot see the sub's `$re`.
- [#3410](https://github.com/rakudo/rakudo/issues/3410): an anonymous
  `token { 'a' <?{ say self.pos }> 'a' }` dies with "'self' used where no
  object is available".
- [#3775](https://github.com/rakudo/rakudo/issues/3775) (the first two
  snippets): ｢{｣ corner-bracket quotes with `:my $L` inside a regex fail to
  parse.
- [#2059](https://github.com/rakudo/rakudo/issues/2059): an actions class's
  `FALLBACK` is never called for missing action methods.

Strings, numbers, time and modules:

- [#1460](https://github.com/rakudo/rakudo/issues/1460):
  `"%h».sort.map(*.fmt("%s %s"))"` is not interpolated. A hyper method call
  after `%h` in a string.
- [#1961](https://github.com/rakudo/rakudo/issues/1961):
  `say "count {$++}" for ^3` gives `0 1 2`; Rakudo gives `0 0 0` (each
  closure clone gets its own anonymous state).
- [#2434](https://github.com/rakudo/rakudo/issues/2434): `dd $x % 0e0` prints
  the Failure, and then the program dies with it (exit 1); Rakudo exits 0.
- [#2097](https://github.com/rakudo/rakudo/issues/2097): a leap-second
  DateTime moved to a timezone that is not a whole number of minutes
  (`Int(3600/7)`) loses its `:60` second.
- [#5889](https://github.com/rakudo/rakudo/issues/5889):
  `(require Test).WHO.keys` is empty.

### Missing

- `use Telemetry`. It is a core module in Rakudo, and three issues use it:
  [#1513](https://github.com/rakudo/rakudo/issues/1513),
  [#3844](https://github.com/rakudo/rakudo/issues/3844),
  [#5144](https://github.com/rakudo/rakudo/issues/5144).
- `FIRST` as a value: [#4351](https://github.com/rakudo/rakudo/issues/4351).
- `Routine.prec`: [#4780](https://github.com/rakudo/rakudo/issues/4780).
- `⚛==`, a parse error:
  [#6873](https://github.com/rakudo/rakudo/issues/6873).
- `Metamodel::EnumHOW.add_enum_value`:
  [#2165](https://github.com/rakudo/rakudo/issues/2165).
- `.twigil` on a method's invocant Parameter, which is
  `.signature.params[0]` of a method:
  [#2275](https://github.com/rakudo/rakudo/issues/2275). Every other
  Parameter has it.
- Unconfirmed, from the reviewers:
  - `IO::Socket::Async.listen-path`/`connect-path`
    ([#5358](https://github.com/rakudo/rakudo/issues/5358),
    [#5359](https://github.com/rakudo/rakudo/issues/5359));
  - IPv6 sockets ([#1879](https://github.com/rakudo/rakudo/issues/1879));
  - `role A[::T] does array[T]`
    ([#4361](https://github.com/rakudo/rakudo/issues/4361));
  - nested coercion types such as `Array[Array[Int]]()`
    ([#4517](https://github.com/rakudo/rakudo/issues/4517));
  - `use trace` ([#5546](https://github.com/rakudo/rakudo/issues/5546));
  - `.AST` of a grammar rule
    ([#5479](https://github.com/rakudo/rakudo/issues/5479)).

### Strictness gaps (Raku++ runs what Rakudo refuses)

- **[#1286](https://github.com/rakudo/rakudo/issues/1286): ambiguous multis
  are not detected.** With `multi f(Int \a, Any \b)` and
  `multi f(Any \a, Int \b)`, `f(42, 42)` returns the first candidate's
  answer; Rakudo dies "Ambiguous call". This is the dispatch-level twin of
  the TODO item about a user candidate tying a core one.
- Read-only attributes are assignable
  ([#5908](https://github.com/rakudo/rakudo/issues/5908), above).
- Test's and NativeCall's exports are visible without `use`.
  `is-deeply 1, 1` works without `use Test`
  ([#4967](https://github.com/rakudo/rakudo/issues/4967)), and so does
  `CArray[int32].new` without `use NativeCall`
  ([#1778](https://github.com/rakudo/rakudo/issues/1778)). Rakudo says
  "Undeclared routine". Code written against Raku++ then fails on Rakudo.
- `use v6.e` without `.PREVIEW`
  ([#2783](https://github.com/rakudo/rakudo/issues/2783)), and `is cached`
  without `use experimental :cached`.
- `has Int $.count!` ([#1427](https://github.com/rakudo/rakudo/issues/1427)),
  `sub ($!x) {}` outside a class, and `-> < 1/2> {…}`
  ([#1828](https://github.com/rakudo/rakudo/issues/1828)) are accepted.
- `'x'.substr(/x/, 'x')`
  ([#1314](https://github.com/rakudo/rakudo/issues/1314)): Rakudo answers
  "did you mean 'subst'?".
- `self."!$name"()` calls a private method
  ([#3802](https://github.com/rakudo/rakudo/issues/3802)).
- `await start { return }` exits 0
  ([#2753](https://github.com/rakudo/rakudo/issues/2753)); Rakudo dies with
  "Attempt to return outside of any Routine".
- A missing `;` between two statements on different lines is accepted
  ([#4175](https://github.com/rakudo/rakudo/issues/4175),
  [#6048](https://github.com/rakudo/rakudo/issues/6048), and three more).
  This is the known laxness that 900f273d left alone on purpose. It is listed
  because the tracker shows how often it comes up.

### Performance seen in passing

- **`1,1,*+*...*` is 5× slower than Rakudo.** 40,000 terms take 1.5 s against
  0.28 s ([#2844](https://github.com/rakudo/rakudo/issues/2844)), and the time
  grows quadratically: 20k terms 0.27 s, 40k 1.0 s. The same numbers from a
  `gather` loop take 0.23 s, and `1, *+1 ... *` is instant. The cost is in
  the sequence operator's two-argument generator path, not in bigint
  addition.
- A 30-million-iteration loop of `my $ = …` is about 3.4× slower than Rakudo:
  0.58 s per 3M ([#1416](https://github.com/rakudo/rakudo/issues/1416)).

## Where Raku++ is already right — and where it shares Rakudo's open bugs

[WINS.md](rakudo-issues-2026-11/WINS.md) lists both sets. They are reviewer
verdicts, not yet re-run by hand.

### PPWIN: Raku++ right, Rakudo wrong (161 cases, 144 issues)

43 of these carry the "Will be addressed in RakuAST" label; they are
legacy-front-end bugs that a different front end simply does not have.
Others:

- regex hangs that terminate here: `"" ~~ /(.*)*/`
  ([#4468](https://github.com/rakudo/rakudo/issues/4468)) and `/ <wb>+ /`
  ([#2806](https://github.com/rakudo/rakudo/issues/2806));
- shaped attribute initialisers
  ([#1226](https://github.com/rakudo/rakudo/issues/1226));
- `1 +< -1000000` giving 0
  ([#1707](https://github.com/rakudo/rakudo/issues/1707));
- an `is default` user `infix:<~>` candidate winning
  ([#2548](https://github.com/rakudo/rakudo/issues/2548));
- `EVAL` seeing a native lexical
  ([#4161](https://github.com/rakudo/rakudo/issues/4161)).

Each is worth a regression test in t/, so that it stays a win.

### SHARED: both show an open issue's bug (72 cases, 63 issues)

Most are in oo (17), syntax (13), dispatch (11), regex (7) and lists (7).
Examples:

- LTM with a frugal alternative, `m/ | a+? | a+ /`
  ([#1924](https://github.com/rakudo/rakudo/issues/1924));
- `'textbook'.comb(4/1, 2)` gives `()`
  ([#6030](https://github.com/rakudo/rakudo/issues/6030));
- `try EVAL "fail 123"` still dies
  ([#3301](https://github.com/rakudo/rakudo/issues/3301));
- a `proto` whose `multi` cannot match it is accepted
  ([#1764](https://github.com/rakudo/rakudo/issues/1764)).

These are the places where we could be right before Rakudo is. Read the
issue thread for the agreed answer before changing anything.

### BOTHOK: the open issue no longer reproduces in either engine (132 issues)

The tracker carries a lot of stale open issues: 78 of the cases with the
"Will be addressed in RakuAST" label behave correctly in Rakudo 2026.09
already.

## Insights beyond the snippets

1. **One fix covers a fifth of the wrong answers.** The value-phaser bug (F1)
   is 42 of the 205 PPBUG cases, because it is how Rakudo's own performance
   issues time themselves. It is also the kind of bug that is invisible in
   Roast.
2. **Our misses are in Rakudo's weak areas.** Rakudo's tracker is heaviest in
   oo, syntax, dispatch, lists and regex. Raku++'s wrong answers (PPBUG cases
   other than F1) fall in the same areas: syntax 34, oo 27, lists 17,
   regex 13, dispatch 10.
   Those are where the spec is subtle, not where either implementation is
   careless. They are the areas to probe first with new tests.
3. **Raku++ is more permissive than Rakudo in three systematic ways:**
   - it does not detect ambiguous multis;
   - it lets `use`-scoped symbols (Test, NativeCall) and experimental features
     through without their pragma;
   - it lets a method assign a read-only attribute.

   All three let code that works on Raku++ fail on Rakudo. That is the
   dangerous direction, the one 900f273d calls out.
4. **The release machinery is the lesson in the `impl` third of the tracker.**
   388 regression issues, most from Blin's pre-release ecosystem runs, are how
   Rakudo catches what Roast misses.
   [MODULE-HUNTING.md](../MODULE-HUNTING.md) is our equivalent. Running it
   per release, against the previous release as the oracle, would be the
   direct analogue.
5. **The tracker is a test corpus waiting to be adopted.** 1,295 of the 1,745
   `lang`/`error` issues carry a runnable snippet. The 814 runs where both
   engines already agree on a closed issue are regression tests for free.
   They are now in raku-corpus-snippets `rakudo-issues/closed/` with their
   Rakudo output as the expectation, and `harness/diff.sh` runs them.

## Not covered

- **Comments.** Fixes, golfed reproductions and "this is the intended
  behaviour" decisions often live there. A token, or the signed-in built-in
  browser, would let a second pass read them.
- **1,205 issues without an extractable snippet**, and snippets that need:
  - a module (55 runs);
  - files or arguments;
  - the network, which the sandbox blocks;
  - more than 10 s.
- **Flaky concurrency and timing.** 88 disagreements changed between two runs
  and were set aside.
- **Error-message wording.** ERRMSG was recorded but not reviewed further:
  41 cases, 37 of them on open LTA issues.
- **Only the interpreter.** `--exe`/`--cnp` were not run.
