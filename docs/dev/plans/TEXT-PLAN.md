# Plan: text Raku — Perl's job at Perl's speed, with Raku's correctness

*Written 2026-10-15, before any code, as the second v7 pillar
([VERSIONS.md](VERSIONS.md)), the twin of
[NUMERIC-PARALLEL-PLAN.md](NUMERIC-PARALLEL-PLAN.md).*

*The aim is one engine with two strong directions: numbers and text. Raku
began as Perl 6, and Perl's reputation is text munging. Raku already has the
better model:*

- *strings are graphemes, so `"👍🏽".chars` is 1, where Perl's `length` says 2;*
- *Unicode is on by default;*
- *grammars are part of the language;*
- *rakupp adds the Perl one-liner flags.*

*What it does not have is Perl's speed. On the 17 everyday tasks measured
below, Raku++ takes 7.3× Perl's time (geometric mean), and the worst row is
100×. This plan closes that gap without giving up any of the model.*

*What makes other languages good at text, and what Raku borrows from each:*

| Source | What it has |
|---|---|
| Perl | regexes in the syntax; trie-compiled literal alternations; buffered I/O; `tr///` as a table; `-n -p -a -F -i` |
| awk | fields |
| Rust's `regex` crate and ripgrep | literal prefilters; a lazy DFA that finds a match's span before captures are computed; SIMD `memchr`; files split by byte range for parallel search |
| RE2 | linear-time matching |
| Hyperscan | many patterns in one pass |
| Swift | graphemes as `Character`, the model Raku's NFG already has |
| simdutf and simdjson | validating and scanning in wide blocks |

*Raku's own spellings for these are grammars, `.race` and `.hyper`, NFG
strings, `:P5`, and native arrays for whatever the text turns into.*

**Measured:**

- **When and where:** 2026-10-15, Apple M4 (4 performance + 6 efficiency
  cores).
- **Binaries:** Raku++ `5.4.1-66-g49c98013-modified`, the working tree with
  another session's uncommitted edits, copied aside for the sitting; Perl
  v5.34.3 (MacPorts), which BENCHMARKS.md found within 4% of 5.44; Rakudo
  v2026.09.
- **Load average:** 2.5 to 3, so the times are best of 3, interleaved, and the
  ratios matter.
- **Data:** a 63 MB, 500,000-line access log; a 28 MB, 400,000-line word
  corpus; and a 16 MB, 200,000-line copy of the corpus with Cyrillic and
  accented letters. The scripts are in the
  [appendix](#appendix-the-measurement).

---

## The numbers a stranger can re-measure

1. **The text suite** (17 tasks, each with a line-for-line Perl twin and the
   same checksum): **the geometric mean at or below Perl's time, and no task
   worse than 2×.** Today the geometric mean is 7.3× and the worst is 100×
   (`.trans`). Rakudo reads 13.3×.
2. **Lines stream:** `for $file.IO.lines` runs in memory that does not grow
   with the file, and `.IO.lines.head(3)` reads one block. Today a 63 MB file
   costs 470–490 MB of RSS before the first line arrives. Perl uses 2.9 MB.
3. **Parallel text beats Perl outright:** counting the status codes of the log
   through `.IO.lines.race` takes less than Perl's single thread (0.144 s).
   Today it takes 1.062 s, and the serial loop 2.074 s.
4. **Grammars at 10 MB/s:** the pure-Raku JSON grammar of
   [GRAMMAR-SPEED-PLAN.md](GRAMMAR-SPEED-PLAN.md) (`jg.raku`) parses `api.json`
   in ≤ 10 ms. That would be faster than Perl's pure-Perl `JSON::PP`, which
   takes 26 ms. Today it takes 108 ms (0.93 MB/s); Rakudo takes 199 ms.

---

## Where it stands — measured

### The suite

Best of 3, in seconds. Each Raku task is one idiomatic loop over `.IO.lines`
(the appendix has them all). Rakudo's `sprintf` hit the 120 s limit.

| Task | What it does | Raku++ | Perl | Rakudo | Raku++ ÷ Perl |
|---|---|---:|---:|---:|---:|
| `lines` | count lines | 0.262 | 0.027 | 0.206 | 9.7× |
| `contains` | `.contains('POST')` | 0.352 | 0.044 | 0.250 | 8.0× |
| `regex-cap` | `/^ (\S+) .*? '" ' (\d ** 3) ' '/`, count `$1` | 2.074 | 0.144 | 5.905 | 14.4× |
| `alt` | `/ checkout \| login \| admin \| report \| feed /` | 0.763 | 0.130 | 10.091 | 5.9× |
| `icase` | `m:i/ 'mozilla' /` | 0.533 | 0.078 | 6.509 | 6.8× |
| `split-sum` | `+.split(' ')[9]`, summed | 0.971 | 0.322 | 1.321 | 3.0× |
| `subst` | `.subst(/\d+/, 'N', :g)` | 3.067 | 0.832 | 23.298 | 3.7× |
| `tr` | `.trans('a..z' => 'A..Z')` | 8.921 | 0.089 | 8.775 | **100×** |
| `uc` | `.uc.chars` | 0.469 | 0.049 | 0.667 | 9.6× |
| `write` | `$o.say("> " ~ $_)` to a file | 1.345 | 0.060 | 1.076 | **22×** |
| `sprintf` | 10⁶ × `'%08.3f\|%-6s\|%x'` | 3.085 | 0.223 | >120 | 13.8× |
| `join` | push `.substr(0, 10)`, then `.join` | 0.411 | 0.065 | 0.468 | 6.3× |
| `wordfreq` | `%h{$_}++ for .lc.words`, top 3 | 1.383 | 0.392 | 2.095 | 3.5× |
| `slurp-words` | `.slurp.words.elems` | 0.204 | 0.403 | 0.273 | **0.5×** |
| `uni-chars` | `.chars` | 0.391 | 0.100 | 0.160 | 3.9× |
| `uni-uc` | `.uc.chars` | 1.874 | 0.158 | 0.291 | 11.9× |
| `uni-regex` | `+.comb(/<:Lu>/)` | 1.038 | 0.191 | 1.861 | 5.4× |

**What already works:**

- **rakupp's regex rows are 3–13× faster than Rakudo's.**
  - The matcher has search prefilters: the set of possible first bytes,
    `memchr` when only one byte can start a match, and literals every match
    must contain.
  - Literal alternations go through an LTM automaton.
- **`slurp-words` beats Perl by 2×.**
- **The one-liner flags exist:** `-n`, `-p`, `-a`/`-F`, `-0`/`-0777` and
  `-i[.ext]`. Rakudo has only `-n` and `-p`.
- **`:P5` regexes run on the same engine** and pass 918 Roast assertions
  taken from Perl's own `re_tests`.

**What Rakudo shows is possible under the same model:** it beats Raku++ on
`lines`, `contains`, `write`, `uni-chars` (2.4×) and `uni-uc` (6.4×).

### Parallel forms (Raku++; Perl has nothing comparable to set against them)

| Task | Serial | `.race(:batch(4096))` | Speedup |
|---|---:|---:|---:|
| `regex-cap` | 2.074 | 1.062 | 2.0× |
| `split-sum` (`.map(…).sum`) | 0.922 | 0.681 | 1.4× |
| `contains` (`.grep(…).elems`) | 0.352 | 0.275 | 1.3× |
| `wordfreq` (`.map(*.lc.words.Slip).Bag`) | 1.383 | 1.097 | 1.26× |

Rakudo's `regex-cap-race` gives a **wrong answer**: 199,953 where the right
count is 199,959. Its workers share one `$/`, as the hyper FAQ records.
Raku++ gives each call its own `$/`, and counts right.

### One-liners, the same log

| | Raku++ | Perl |
|---|---:|---:|
| `-ne '… if .contains("POST")' file` | 0.31 s | 0.10 s |
| the same, from a pipe (`< file`) | 3.43 s | — |
| `-ane '… if @F[8] eq "404"'` | 1.15 s | 0.35 s |
| `-pe 's:g/\d+/N/'` | 4.39 s | 0.88 s |

### Grammars

Best of 5, on the GRAMMAR-SPEED corpus:

| Parser | `api.json` (100 KB) | `strings.json` (112 KB) |
|---|---:|---:|
| `jg.raku` on Raku++ | 108 ms | 69 ms |
| `jg.raku` on Rakudo | 199 ms | 154 ms |
| Perl's `JSON::PP` (hand-written, pure Perl) | 26 ms | |
| Raku++'s native codec (`JSON::Native`) | 0.65 ms | |

### What the probes found

**Line input is eager: `.IO.lines` reads and splits the whole file first.**

- **The cost:** a 63 MB log took 470–490 MB of RSS with `.IO.lines`, `$fh.lines`
  or `$fh.get` alike. Even `.lines.head(3)` read everything (0.27 s, 492 MB).
- **The profile:** the time is in `std::ostream << streambuf*` and
  `istreambuf_iterator`. The file is copied into a string stream before it is
  split.
- **The handle:** it keeps the split lines in its hash (`"lines"`, `"pos"`,
  `"line-eols"`), and `fhAppendToFile` reads them to place writes on `:rw`
  handles ([Builtins.cpp:5239](../../../src/Builtins.cpp)). That coupling is
  why the reader is eager.
- **A file larger than about an eighth of RAM cannot be read line by line.**

**`$*IN.lines` streams, but slowly.** `.head(2)` stops on infinite input, but
the log took 3.18 s through stdin: 6.4 µs a line, 12× the file path and about
100× Perl. Every `cat log | rakupp -ne …` pipeline pays that.

**Output is unbuffered.**

- **The cost:** every `say` to a file handle is one `write(2)`, and the
  `write` row's profile is mostly the syscall. Perl and Rakudo buffer file
  output.
- **The handle is a hash:** each write first looks up `"mode"`, `"path"`,
  `"wfd"` and more.

**`.trans` is the general algorithm for every map.**

- **The profile:** `memcmp` and `std::string::compare`. At each position every
  key is tried, longest match first
  ([MethodCallPart3.cpp:6173](../../../src/MethodCallPart3.cpp)).
- **The fix:** `'a..z' => 'A..Z'` is a 256-byte table in Perl, and could be
  here.

**`.` in a regex always computes a grapheme boundary.** `K::Any` calls
`uniClusterEndUtf8` for every character
([Regex.cpp:3976](../../../src/Regex.cpp)), even when the next byte is ASCII.
The class arm a few lines away has that fast path. In `regex-cap`, `.*?` walks
most of each line, so `uniClusterEndUtf8` and `gbProp` top the profile.

**`sprintf` goes through libc.** The profile is `__dtoa`, `__vfprintf`,
`uselocale` and `pthread_setspecific`, which is a locale switch on every call.
`std::to_chars` formats `%f`, `%e` and `%g` exactly, without a locale.

**Non-ASCII text loses the ASCII fast paths entirely.** `uni-uc` is 11.9× Perl
and 6.4× Rakudo. `uni-chars` is 3.9× Perl and 2.4× Rakudo.

**Missing beside Perl and Rakudo:**

- Perl's `-00` paragraph mode. `-00` reads like `-0` today.
- `Str.naive-word-wrapper`, which Rakudo has.

---

## Phases

Ordered by payoff against complexity. T1 comes first because every per-line
task stands on it: the `lines` row alone is 0.26 s of every task's time.

| Phase | What | Complexity | The gap it closes |
|---|---|---|---|
| T0 | The suite with Perl twins, the Rakudo column, a quiet baseline | S | — |
| T1 | I/O: streaming block reader for files and `$*IN`; buffered writes | M–L | `lines` 9.7×, `write` 22×, the pipe path 12×; number 2 |
| T2 | String operations: `.trans`/`tr` tables, `.uc`/`.lc`, `sprintf`, `.index`, `.split`, `.words` | S–M each | `tr` 100×, `sprintf` 13.8×, `uc` 9.6× |
| T3 | The regex engine: ASCII stepping, literal tries, `:i` search, lazy captures, find-then-capture | S → XL | `regex-cap` 14.4×, `alt` 5.9×, `icase` 6.8×, `subst` 3.7× |
| T4 | Parallel text: block-parallel line reading, per-worker Bags, `-j N` one-liners | M–L | number 3 |
| T5 | Grammars at 10 MB/s | L–XL | number 4 |
| T6 | Text ↔ numbers: the bridge to the numeric pillar | M | fields into `num` arrays at memory speed |
| T7 | Unicode at near-ASCII speed | M | `uni-uc` 11.9×, `uni-chars` 3.9× |
| T8 | Parity: `-00`, `naive-word-wrapper`, whatever the census finds | S each | |

### T0 — Measure first (no engine change)

- **The suite:** `tools/bench/text/` holds `text.raku`, `text.pl` and a
  generator with a fixed seed, for all three files. The Unicode file's
  generator is seeded too; the one used here was not.
- **The harness:** it runs each task in each engine, checks that the checksums
  agree, takes the best of N, interleaved, and prints the table and the
  geometric mean.
- **BENCHMARKS.md:** its "vs Perl 5" section grows from 3 kernels to this
  suite.
- **The baseline:** on a quiet machine, with peak RSS per task.

### T1 — I/O

**A streaming line reader** shared by `.IO.lines`, `$fh.lines`, `$fh.get`,
`$*IN.lines`, `-n` and `-p`:

- **How it reads:** `read(2)` in 64 KB blocks, `memchr` for the separator, and
  UTF-8 validation per block. A block that is pure ASCII needs no decoding.
  Each line is a fresh `Str` cut from the block.
- **What it keeps:** `:chomp`, `nl-in` (multi-character separators included),
  encodings, `.lines` after `.get`, and `.seek`/`.tell`.
- **`:rw` handles:** today they compute their write position from the stored
  line array. They need a byte offset instead. Making that change is the
  first step.
- **Laziness:** a lazy `.lines` stops reading at `.head(3)`.

**Buffered output:**

- **The buffer:** a write handle buffers 64 KB.
- **When it flushes:** on `.close`, `.flush`, program exit (`exit`, `die`,
  END), and before `run`/`shell`/`Proc::Async` start, so a child's output
  never overtakes the parent's.
- **`$*OUT`:** to a terminal it stays line-buffered. To a pipe or a file it
  buffers, as Rakudo's does.
- **`$*ERR`:** unbuffered.

**The handle's state** moves from hash keys into the handle's own native
struct (the `ext` payload `WriteFd` already starts), so a write is a
`memcpy` into the buffer.

Exit: `lines` and `write` within 1.5× of Perl. Number 2. `rakupp -ne` from a
pipe within 1.5× of the same over the file.

### T2 — String operations

Each is small, and each keeps the general path for everything it does not
cover:

- **`.trans` and `tr///`:** when every key and value is one ASCII character
  (ranges included), use a 256-byte table and one pass. That is `tr`, 100× →
  ~1×. Multi-character keys keep the LTM path.
- **`.uc`, `.lc`, `.fc`, `.tc`:** an ASCII byte loop when the body's cached
  `allAscii` says so. Non-ASCII goes to T7.
- **`sprintf` and `.fmt`:** `std::to_chars` for `%f %e %g` (locale-free, and
  exact for the shortest round-trip). No `uselocale` per call. The format
  string is parsed once and cached on the call node.
- **`.contains` and `.index`:** `memmem`, or a two-way search for long needles.
- **`.split` with a literal separator:** `memchr`/`memmem`, cutting views from
  one source.
- **`.words` per line:** the slurped form is already faster than Perl. The
  per-line call takes the same path.
- **`.substr` and `.join`:** they belong to INTERP-SPEED (the per-call costs).
  This plan only checks that no copy is taken that a view would avoid.

Exit: `tr`, `uc`, `sprintf`, `contains` and `split-sum` within 1.5× of Perl.

### T3 — The regex engine

Small and certain first, structural last:

1. **ASCII stepping (S):** `.` and the classes advance one byte when the next
   byte is ASCII and the pair is not CR LF. That is the check the class arm
   already makes ([Regex.cpp:3193](../../../src/Regex.cpp)). A subject the body
   knows is all ASCII and free of CR skips the grapheme machinery for the
   whole match.
2. **Literal alternations as a trie (M):** a `|` whose branches are literals,
   or start with them, gets a multi-literal prefilter: a trie, or
   Aho–Corasick past a branch count. That is what Perl compiles
   `checkout|login|admin` into. The LTM automaton (LtmNfa.cpp) already holds
   the branches' declarative prefixes, so the trie can come from it.
3. **`:i` literal search (S–M):** ASCII case-folded `memmem` for a required
   literal. Today `:i` turns the first-byte prefilter into "both cases plus
   every high byte".
4. **Lazy captures (M):** a match whose `$/` is read only through `$0`/`$1`, or
   not at all (`if /…/`, `.grep(/…/)`), records capture positions and builds
   Match objects when they are read.
5. **`s:g///` and `.subst(:g)` in one pass (M):** the output is built in a
   single buffer, with a literal replacement copied without evaluation.
6. **Find, then capture (XL; measure first):**
   - **The idea:** for patterns with no code blocks, subrules,
     backreferences or lookarounds, a lazy DFA (as in RE2 and Rust's `regex`)
     finds the match's span in linear time. The backtracker then runs only
     inside that span, to fill captures.
   - **What LTM changes:** LTM and NFG decide which match is "the" match. The
     DFA's acceptance rule must reproduce Raku's semantics (leftmost, and
     longest token for `|`), not POSIX or Perl's.
   - **The prerequisite:** a profile of the share that is tree-walking overhead
     after items 1–5, since item 6 might not be needed.

Exit: `regex-cap`, `alt`, `icase` and `subst` within 2× of Perl after items 1–5.
The decision on item 6 is written here, with its numbers.

### T4 — Parallel text

This is the part Perl and awk cannot follow. It depends on T1, and on the
numeric plan's P5 (the closure tax on workers).

- **Block-parallel reading:**
  - **What:** `$file.IO.lines.race` and `.hyper` split the file into byte
    ranges at line boundaries. Each worker reads, validates and splits its own
    range, so no thread splits all the lines.
  - **Ordering:** `hyper` keeps the order by range, and `race` gives
    batches as they finish.
  - **Precedents:** ripgrep's parallel search, GNU parallel's `--pipepart`.
- **Per-worker reductions:**
  - **`.race.map(…).Bag`, `.classify` and `.sum`** fold per worker and merge at
    the end. This is the text half of the numeric plan's P4, and `wordfreq` is
    its benchmark.
- **`-j N` for `-n`/`-p`, opt-in:**
  - **What:** the per-line body runs on N workers.
  - **Ordering:** `-p` output stays in input order, with batches reassembled
    as `hyper` does.
  - **When it falls back:** a body with `state` variables, phasers or writes to
    outer variables runs serially, with a note on stderr.
- **Correctness:** each call already gets its own `$/`. That stays a gate, as
  a stress program comparing a `race` count with a serial one, because
  Rakudo gets it wrong.

Exit: number 3. `regex-cap` through `.race` at ≥ 3× the serial loop.

### T5 — Grammars at 10 MB/s

[GRAMMAR-SPEED-PLAN.md](GRAMMAR-SPEED-PLAN.md) owns the details. This pillar
sets its number (number 4) and adds what T3 brings:

- **ASCII stepping and lazy captures** apply to grammars unchanged. A token's
  Match is built only when an action or `$<name>` reads it.
- **Scan-only tokens:**
  - **Which:** a `token` with no captures, code, subrules beyond inlinable
    single-character ones, or backreferences.
  - **How:** it runs as a DFA over its whole body, compiled from the LTM
    automaton's machinery, not only its declarative prefix.
  - **What that covers:** most of a JSON grammar's tokens (strings, numbers,
    whitespace).
- **Action dispatch** costs are measured with `jg.raku --raw`, which leaves
  out the actions, and go to the dispatch work of V6 if they dominate.
- **The comparison that matters to users:** a grammar written plainly in Raku
  against a hand-written parser in Perl. `JSON::PP` at 26 ms is the bar.

### T6 — Text ↔ numbers: the bridge

The point of putting two pillars under one engine is the pipeline between them:
read text, cut fields, turn them into numbers, compute.

- **Converting fields:** `+$s`, `.Num` and `.Int` on field strings take the
  fast parse (`fastParse64`, `std::from_chars`), with a fallback for
  allomorphs, Rats and underscores.
- **`».Num` and `».Int` into a native target** (`my num @col = @fields».Num`)
  write words (numeric P1).
- **CSV columns go straight into native arrays:** the CSV codec (DataCsv.cpp)
  learns to return one column as `array[num]` or `array[int]`.
- **Numbers back to text:** `@a.fmt('%.3f', ',')` over a packed array is one
  `to_chars` loop.
- **The showcase:** a per-minute latency histogram from the access log, read
  through `.race`, fields into a `num` array, statistics by the numeric
  kernels. It is written in Raku, measured against Perl, and against Python
  with NumPy.

### T7 — Unicode at near-ASCII speed

Raku's correctness is the strength. Rakudo shows it can also be fast: 1.6× and
1.8× of Perl on `uni-chars` and `uni-uc`, against Raku++'s 3.9× and 11.9×.

- **Counting graphemes:** skip ASCII runs a word at a time, and look up the
  grapheme-break property only where the next codepoint could extend a
  cluster.
- **Case mapping:** a table lookup per codepoint, with special casing (ß →
  SS, final sigma) only where it applies. Normalisation only when the result
  could be non-NFC.
- **`<:Lu>` and the other property classes:** a two-level bitmap per property,
  not a lookup per call.
- **Correctness stays the gate:** the S15 Unicode suite (91,752 assertions)
  after every change.

### T8 — Parity

- **`-00`:** paragraph mode, as in Perl. It is only a separator for the T1
  reader.
- **`Str.naive-word-wrapper`:** Rakudo has it.
- **The census from T0:** every row of the Perl twin that needed a workaround
  in Raku is a candidate.

---

## Gates, every phase

- **Roast:** zero regressions, the S15 Unicode suite and S32-io included.
- **The rest of the standing suite:** `t/run.raku`, `t/stress/` in both modes,
  and the TSan job for T4.
- **`perf-guard --check`:** `strscan`, `textsplit` and `regex` are kernels in
  it already.
- **The text suite:** checksums identical across Raku++, Rakudo and Perl, and
  every row re-measured after each phase.
- **The `--exe` and `--cnp` lanes:** they share the runtime, so their output
  is compared for every suite task.

## Non-goals

- **Changing Raku's semantics:** NFG positions, LTM alternation and Match
  shapes stay exactly as they are. Speed comes from fast paths that give the
  same answer, never from a different answer.
- **Linking PCRE2, RE2 or Hyperscan.** A second engine with different
  semantics would make `:P5` and Raku regexes disagree.
- **A regex JIT before T3 item 6's measurement says the tree walk is the
  cost.**
- **Perl compatibility beyond `:P5` and the one-liner flags.** Raku is not
  trying to run Perl programs.

## Risks

- **NFG under fast paths:**
  - CR LF is one grapheme.
  - A combining mark after an ASCII letter extends it.
  - `:ignoremark` compares base characters.
  - Every ASCII shortcut checks the next byte, and S15 is the gate.
- **The streaming reader:** `:rw` positions, `.seek` after reading, `nl-in`
  arrays, decoding errors halfway through a file, and `.lines` called twice
  each get a test before the switch.
- **Buffered output:**
  - Lost output on abnormal exit: flush on every exit path, and on signals
    that end the process.
  - Interleaving with `$*ERR` and with subprocesses: flush before spawning.
  - TTY behaviour: line-buffered.
- **The DFA (T3 item 6) has to reproduce Raku's choice of match exactly.** A
  differential test against the backtracker over generated patterns comes
  before it is switched on.

---

## Appendix: the measurement

The data (Perl, `srand(42)`):

```perl
srand(42);
my @m = qw(GET GET GET POST PUT DELETE HEAD);
my @st = (200,200,200,200,301,302,304,404,404,500);
my @w = qw(index about blog post user api v1 v2 search item cart checkout static img css js login logout admin report data feed rss page doc help);
my @ua = ('Mozilla/5.0 (X11; Linux x86_64)', 'curl/8.4.0', 'Mozilla/5.0 (Macintosh; Intel Mac OS X 14_1)', 'Googlebot/2.1');
open my $o, '>', 'access.log' or die;
for my $i (1..500_000) {
  my $ip = join '.', map { int rand 256 } 1..4;
  my $p = join '/', map { $w[rand @w] } 1..(1+int rand 4);
  printf $o qq{%s - - [15/Oct/2026:%02d:%02d:%02d +0300] "%s /%s%d.html HTTP/1.1" %d %d "-" "%s"\n},
    $ip, int rand 24, int rand 60, int rand 60, $m[rand @m], $p, int rand 1000, $st[rand @st], int rand 100000, $ua[rand @ua];
}
close $o;
my @dict = map { my $l = 2 + int rand 9; join '', map { chr(97 + int rand 26) } 1..$l } 1..20000;
open $o, '>', 'words.txt' or die;
for (1..400_000) { my @ws = map { my $x = $dict[int(rand(1) ** 3 * @dict)]; rand() < 0.1 ? ucfirst $x : $x } 1..(5 + int rand 10); print $o join(' ', @ws), ".\n" }
```

`uni.txt` is the first 200,000 lines of `words.txt`, with half of each of
`a e o k s u n p` replaced by `а é ö к ß ü ñ р`, and 15% of word-initial
letters uppercased.

The Raku side (`text.raku MODE [FILE]`):

```raku
sub MAIN(Str $mode, Str $file = 'access.log') {
    my $t0 = now;
    my $chk;
    given $mode {
        when 'lines'      { my $n = 0; for $file.IO.lines { $n++ }; $chk = $n }
        when 'contains'   { my $n = 0; for $file.IO.lines { $n++ if .contains('POST') }; $chk = $n }
        when 'regex-cap'  { my %h; for $file.IO.lines { %h{~$1}++ if / ^ (\S+) .*? '" ' (\d ** 3) ' ' / }; $chk = %h.sort.map({ "{.key}={.value}" }).join(',') }
        when 'alt'        { my $n = 0; for $file.IO.lines { $n++ if / checkout | login | admin | report | feed / }; $chk = $n }
        when 'icase'      { my $n = 0; for $file.IO.lines { $n++ if m:i/ 'mozilla' / }; $chk = $n }
        when 'split-sum'  { my $s = 0; for $file.IO.lines { $s += .split(' ')[9] }; $chk = $s }
        when 'subst'      { my $len = 0; for $file.IO.lines { $len += .subst(/\d+/, 'N', :g).chars }; $chk = $len }
        when 'tr'         { my $last; for $file.IO.lines { $last = .trans('a..z' => 'A..Z') }; $chk = $last }
        when 'uc'         { my $len = 0; for $file.IO.lines { $len += .uc.chars }; $chk = $len }
        when 'write'      { my $o = open 'out.tmp', :w; for $file.IO.lines { $o.say("> " ~ $_) }; $o.close; $chk = 'out.tmp'.IO.s }
        when 'sprintf'    { my $len = 0; for ^1_000_000 -> $i { $len += sprintf('%08.3f|%-6s|%x', $i / 7e0, $i % 1000, $i).chars }; $chk = $len }
        when 'wordfreq'   { my %h; for $file.IO.lines { %h{$_}++ for .lc.words }; $chk = %h.elems ~ ' ' ~ %h.sort({ $^b.value <=> $^a.value || $^a.key cmp $^b.key }).head(3).map(*.key).join(',') }
        when 'slurp-words' { $chk = $file.IO.slurp.words.elems }
        when 'join'       { my @parts; for $file.IO.lines { @parts.push(.substr(0, 10)) }; $chk = @parts.join('|').chars }
        when 'split-sum-fn'   { $chk = $file.IO.lines.map(*.split(' ')[9]).sum }
        when 'split-sum-race' { $chk = $file.IO.lines.race(:batch(4096)).map(*.split(' ')[9]).sum }
        when 'contains-race'  { $chk = $file.IO.lines.race(:batch(4096)).grep(*.contains('POST')).elems }
        when 'regex-cap-race' { my $b = $file.IO.lines.race(:batch(4096)).map({ / ^ (\S+) .*? '" ' (\d ** 3) ' ' / ?? ~$1 !! Empty }).Bag; $chk = $b.sort.map({ "{.key}={.value}" }).join(',') }
        when 'wordfreq-race'  { my $b = $file.IO.lines.race(:batch(4096)).map(*.lc.words.Slip).Bag; $chk = $b.elems ~ ' ' ~ $b.sort({ $^b.value <=> $^a.value || $^a.key cmp $^b.key }).head(3).map(*.key).join(',') }
        when 'uni-chars'  { my $n = 0; for $file.IO.lines { $n += .chars }; $chk = $n }
        when 'uni-uc'     { my $n = 0; for $file.IO.lines { $n += .uc.chars }; $chk = $n }
        when 'uni-regex'  { my $n = 0; for $file.IO.lines { $n += +.comb(/<:Lu>/) }; $chk = $n }
    }
    printf "%-15s %8.3f s  chk=%s\n", $mode, now - $t0, $chk.Str.substr(0, 60);
}
```

The Perl twin (`text.pl MODE [FILE]`; the `uni-*` modes open with
`<:encoding(UTF-8)`):

```perl
if    ($mode eq 'lines')    { my $n = 0; while (<$fh>) { $n++ } $chk = $n }
elsif ($mode eq 'contains') { my $n = 0; while (<$fh>) { $n++ if index($_, 'POST') >= 0 } $chk = $n }
elsif ($mode eq 'regex-cap') { my %h; while (<$fh>) { $h{$2}++ if /^(\S+).*?" (\d{3}) / } $chk = join ',', map { "$_=$h{$_}" } sort keys %h }
elsif ($mode eq 'alt')      { my $n = 0; while (<$fh>) { $n++ if /checkout|login|admin|report|feed/ } $chk = $n }
elsif ($mode eq 'icase')    { my $n = 0; while (<$fh>) { $n++ if /mozilla/i } $chk = $n }
elsif ($mode eq 'split-sum') { my $s = 0; while (<$fh>) { $s += (split / /)[9] } $chk = $s }
elsif ($mode eq 'subst')    { my $len = 0; while (<$fh>) { chomp; s/\d+/N/g; $len += length } $chk = $len }
elsif ($mode eq 'tr')       { my $last; while (<$fh>) { chomp; tr/a-z/A-Z/; $last = $_ } $chk = $last }
elsif ($mode eq 'uc')       { my $len = 0; while (<$fh>) { chomp; $len += length uc } $chk = $len }
elsif ($mode eq 'write')    { open my $o, '>', 'out.tmp' or die; while (<$fh>) { print $o "> $_" } close $o; $chk = -s 'out.tmp' }
elsif ($mode eq 'sprintf')  { my $len = 0; for my $i (0..999_999) { $len += length sprintf('%08.3f|%-6s|%x', $i / 7, $i % 1000, $i) } $chk = $len }
elsif ($mode eq 'wordfreq') { my %h; while (<$fh>) { $h{$_}++ for split ' ', lc } my @t = (sort { $h{$b} <=> $h{$a} || $a cmp $b } keys %h)[0..2]; $chk = scalar(keys %h) . ' ' . join ',', @t }
elsif ($mode eq 'slurp-words') { local $/; my $s = <$fh>; my $n = () = $s =~ /\S+/g; $chk = $n }
elsif ($mode eq 'join')     { my @p; while (<$fh>) { push @p, substr($_, 0, 10) } $chk = length join '|', @p }
elsif ($mode eq 'uni-chars') { my $n = 0; while (<$fh>) { chomp; $n += length } $chk = $n }
elsif ($mode eq 'uni-uc')   { my $n = 0; while (<$fh>) { chomp; $n += length uc } $chk = $n }
elsif ($mode eq 'uni-regex') { my $n = 0; while (<$fh>) { my $c = () = /\p{Lu}/g; $n += $c } $chk = $n }
```

The memory figures read `/usr/bin/time -l` (maximum resident set size). The
profiles are `sample PID 2` on a running process, ordered by time at the top of
the stack.
