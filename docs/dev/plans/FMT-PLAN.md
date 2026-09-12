# Plan: `--fmt` — a source formatter

**Status: DONE (2026-09-12). `--fmt` ships with the whole v1 ruleset — R1-R6 —
plus `-i`/`-i.bak`, `--check` and `--diff`; the guide is written
([FMT.md](../../guide/FMT.md)) and the wide sweep is run: 2,434 files over three
corpora, 11 gate refusals, all one recorded family. R5's "infix `=`" was dropped
from the rule (see below). The one thing left is that family — R1 reaching
inside a multi-line `/.../` — written up at the end as its own round.** Design
probes run 2026-08-26 against `build-arm64/rakupp` (see "What the probes said").

Goal: `rakupp --fmt prog.raku` prints the program back in the house style,
provably without changing what it means. The bar is the gofmt one — a tool
you can run on every save without reading its output — which for Raku is a
harder promise than for Go, because **whitespace is semantically
significant** in Raku: `say (1,2)` and `say(1,2)` are different programs,
`%h<key>` and `%h < key` are different programs, `$x++` parses and `$x ++`
does not. A formatter that "cleans up spacing" with grep-level knowledge
will silently rewrite programs. The design below is shaped entirely by that
fact: conservative rules by construction, plus a machine-checked semantic
gate on every single run.

## The architecture decision: spans, not the AST

Two ways to build a formatter:

1. **Reprint the AST** (gofmt's way). Ruled out here: rakupp's AST is a
   semantic tree, not a syntax tree. Comments, POD, whitespace, quoting
   choices, unspaces and heredoc spellings are not in it, so a full reprint
   destroys them. Making the AST lossless is the RakuAST project
   ([RAKUAST-PLAN.md](RAKUAST-PLAN.md)), deliberately postponed — a
   formatter must not be the thing that forces it.

2. **Rewrite whitespace between classified spans** (perltidy's way). The
   source is scanned into a lossless sequence of classified runs — string,
   regex, comment, POD, heredoc body, number, variable, identifier,
   operator/plain — and the formatter edits **only the plain-text
   whitespace between spans**, never the bytes inside a classified span.

We already own the scanner for (2): `--highlight`'s lossless span scanner
in [Highlight.cpp](../../../src/Highlight.cpp), which exists precisely to
classify Raku source without executing it, and knows the value/operator
distinction (`/` as division vs regex, `<` as comparison vs qw/subscript)
that grep-level tools get wrong.

## What the probes said (2026-08-26)

- **The scanner is byte-lossless today.** Concatenating its spans
  reproduced the input byte-for-byte over 341 files (`t/regression/*.raku`,
  `examples/*.raku`, `rakulib/**`, showcase samples). The formatter can
  stand on it.
- **One classification gap: heredocs.** `q:to/END/` bodies are scanned as
  code (identifiers, comments, braces). Losslessly so — but the formatter
  would reindent heredoc bodies, i.e. change string contents. Fixing the
  scanner to mark heredoc bodies as string spans is prerequisite work, and
  improves `--highlight` for free (it currently colours heredoc bodies as
  code too).
- **The semantic gate is nearly free.** `serializeAst` from
  [AstSerial.cpp](../../../src/AstSerial.cpp) already gives a canonical
  byte-blob of the parsed program (`--ast-roundtrip` uses it). Parse the
  input, parse the formatted output, compare blobs: any formatting bug that
  changes meaning is caught before a byte is emitted. One addition needed:
  the writer serializes node line numbers, and formatting legitimately
  moves lines (else-motion, blank-line edits) — so the writer grows a
  `stripLines` flag that writes 0 where it writes `line` today. It is one
  write site; the visitor design means nothing else changes.

## The promise, stated as gates

Every `--fmt` run, on every file, before emitting anything:

1. **Parse gate** — the input must parse. A file that doesn't parse is not
   formatted (exit 3, same code `--ast-roundtrip` uses for it). No
   "best-effort" formatting of broken code.
2. **Semantic gate** — `serializeAst(parse(input), stripLines)` must equal
   `serializeAst(parse(formatted), stripLines)`. On mismatch: emit
   nothing, print an internal-error banner asking for a bug report, exit 5.
   This is a backstop that should never fire — like `--slim=verify`, its
   job is to make the failure loud instead of silent.
3. **Idempotence gate** — `fmt(fmt(x))` must equal `fmt(x)`, checked
   internally on every run (formatting the formatted text is cheap). A
   formatter that oscillates is worse than none.

What the gates do **not** cover: bytes inside comments and POD, which are
not in the AST. Those are protected by construction instead — rules never
edit inside a classified span (the sole exception: trailing-whitespace
strip at end of comment/POD lines, which cannot change rendering).

## The v1 ruleset

Deliberately small; every rule is whitespace-only and alignment-preserving.
The style enforced is the house style already used across `examples/`,
`t/`, and the docs.

- **R1 — indentation.** Leading whitespace of each *statement-start* line
  becomes `4 × bracket-depth` spaces (tabs converted). Bracket depth counts
  only brackets in code spans. A *continuation* line — the previous code
  line ended mid-expression (no `;`, `{`, `}` last) — is not set to a
  computed column: it is **shifted by the same delta as its statement's
  first line**, preserving the author's relative alignment (chained-method
  ladders, aligned grammar rules, multi-line calls survive). Lines inside
  heredocs, multi-line strings, and POD are untouched.
- **R2 — trailing whitespace** stripped (not inside heredocs/strings).
- **R3 — exactly one newline at EOF.**
- **R4 — `else` / `elsif` / `orwith` / `without` on their own line**, at
  the `if`'s indent: `} else {` becomes `}\nelse {`. (House style; also the
  style of every `else` in `examples/`.)
- **R5 — minimum spacing, never collapsing:** at least one space after `,`
  and around `=>` and infix `=`. *Minimum* is the load-bearing word: runs
  of spaces are never shrunk, so hand-aligned tables (the
  `examples/json.raku` grammar, aligned `=>` pairs) come through intact.
- **R6 — blank lines:** runs of three or more blank lines collapse to two;
  single and double blanks are the author's and stay.

**The do-not-touch list** (rules must be written against it, the semantic
gate enforces it): bytes inside any classified span; whitespace adjacent to
`(` `[` `{` `<` (listop-vs-call, subscript-vs-comparison); around `.` and
postfix/prefix operators; around `:` (colonpairs, adverbs); unspace `\ `;
anything on a line the scanner marked uncertain.

Explicitly **out of scope for v1** (each a possible v2 rule, none blocking
ship): line wrapping at a column, operator-spacing beyond R5's safelist,
normalizing quotes or parens, signature layout, comment reflow.

## CLI surface

`--fmt` is a mode like `--lint`/`--ast` (source tool: `-M`/`-I` illegal,
composable and position-independent per [CLI-PLAN.md](CLI-PLAN.md)).

| invocation | behaviour |
|---|---|
| `rakupp --fmt FILE` | formatted source to stdout (file untouched) |
| `rakupp --fmt -` / `--fmt -e '…'` | stdin / one-liner to stdout — editor & pipeline form |
| `rakupp --fmt -i FILE...` | rewrite in place; a file already formatted is not rewritten (mtime preserved). `-i.bak` keeps backups — the spelling and backup-extension semantics `-i` already has |
| `rakupp --fmt --check FILE...` | write nothing; list files that would change; exit 1 if any — the CI form |
| `rakupp --fmt --diff FILE...` | like `--check`, but print a unified diff of what would change (own small line-diff, no git dependency) |

Without `-i`/`--check`, exactly one input (stdout would interleave
otherwise). Exit codes: 0 clean, 1 `--check` found work, 3 parse error,
4 usage, 5 semantic-gate refusal.

**Zero configuration.** No indent-width option, no rule toggles, no config
file. One style, like gofmt — the value of a formatter is the arguing it
ends.

## Order of work

1. **Factor the scanner.** Move the span scanner out of `Highlight.cpp`
   into shared `SourceScan.{h,cpp}`; `--highlight` becomes a renderer over
   it. Add heredoc-body spans. New regression: the 341-file byte-lossless
   sweep becomes a permanent test, plus heredoc highlight goldens.
2. **`stripLines` on AstSerial's writer** (one write site) + a unit check
   that two shifted-but-identical programs compare equal.
3. **`Fmt.cpp`**: span stream → line model → R1–R6, with gates 2 and 3
   built into the entry point, not the CLI. Includes the small line-diff
   renderer `--diff` uses (plain LCS over lines, unified-diff output).
4. **Wire the mode**: `Mode::Fmt` in `main.cpp`, `-i`/`--check` legality,
   goldens in `t/run.raku` (one per rule, one per refusal path, flag-order
   composition like the `--highlight` goldens).
5. **The sweep bar.** Format every `.raku`/`.rakumod` in this repo's
   corpora and the raku-corpus checkout (the ~9,350-file bar DeclCheck
   set): zero gate trips, 100% idempotent. Then the live-fire proof:
   format a copy of `t/` and `examples/` and run the full local suite on
   the formatted tree — it must pass identically.
6. **Docs**: `docs/guide/FMT.md`, the `--fmt` row in
   [CLI.md](../../guide/CLI.md), FEATURES/README sync.

Standing gates apply per batch: zero Roast regressions (step 1 touches
shared code), full local suite, `perf-guard --check`.

## Decisions (settled at design review, 2026-08-26)

- **D1 — R5's reach:** minimum-spacing for `,` / `=>` / infix `=`, never
  collapsing runs. Decided as written.
- **D2 — blank lines:** collapse runs of 3+ blank lines to 2 (rule R6);
  single and double blanks untouched.
- **D3 — `--diff`:** ship in v1 — `--check`'s companion, printing a
  unified diff from a built-in line diff (no git dependency).

## Step 1, landed 2026-09-12 — and one thing re-checked before standing on it

**The architecture decision was re-tested, not assumed.** The plan rules out
reprinting the AST because comments and POD are not in it, and notes that
"making the AST lossless is the RakuAST project… deliberately postponed". That
project has since landed in full — so the obvious question is whether the
ruling still holds, and it does: `Q[# a comment\nmy $x = 1;  # trailing\nsay
$x].AST.DEPARSE` answers `"my $x = 1;\nsay $x\n"`. **Both comments are gone.**
A RakuAST reprint would be a formatter that deletes every comment in the file,
so the span architecture stands for exactly the reason first given.

**The scanner is still byte-lossless**, re-measured on today's code over the
repo's corpora: **672 files, zero differences** (the 2026-08-26 probe was 341).
That sweep is now `t/regression/highlight-lossless.raku` rather than a
one-off — the property is what `--fmt` stands on, so it is checked on every
run, and the case refuses to pass vacuously if it finds fewer than 50 files.

**The heredoc gap is closed.** A `q:to/END/` body was scanned as CODE — its `#`
came out as a comment and its `if` as a keyword — which is lossless but would
have let the formatter reindent string contents. The fix is a pending-terminator
queue rather than an inline scan, because the opener does not own the body: the
`;` in `my $t = q:to/END/;` still belongs to the opener's line, so the body is
drained at the next newline. That handles the cases an inline scan would have
got wrong, all verified: two heredocs opened on one line take their bodies in
order, `qq:to` as well as `q:to`, and an unterminated body runs to EOF without
hanging or losing a byte. `--highlight` renders heredocs correctly now as a
side effect, which is the improvement the plan predicted.

**Deliberately not done yet**: factoring the scanner out of `Highlight.cpp` into
`SourceScan.{h,cpp}`. It is a pure refactor with no user-visible change, and it
wants a second consumer to shape its interface — so it lands with `Fmt.cpp`
(step 3) rather than before it. The heredoc fix moves with it when it does.

Gates: `t/run.raku` 867/867, `t/slim/run.raku`.

## Steps 2-4, landed 2026-09-12

**`stripLines`** is on the serializer — four write sites, not the one the plan
estimated, all the same shape — and the version did not move: it changes what
the WRITER may emit, never how the reader reads, so the precomp cache is
untouched.

**`Fmt.cpp`** carries the three gates inside `formatSource()`, not in the CLI,
so every caller gets them. **The semantic gate earned its keep immediately**:
it caught my own line model reaching into a heredoc body, twice, before a byte
reached a file. Both bugs were the same misjudgement about what "untouchable"
means, and the ruleset only became right at the third try:

* *"a line containing a classified span"* — makes every line of code
  untouchable, because every one contains a keyword. The formatter became a
  no-op and R2 silently did nothing;
* *"a line whose first span is classified"* — same, for every line that opens
  with a keyword. This is the one the gate caught: a heredoc's FIRST body line
  looked editable (the newline before it came from the plain run after the
  opener), R2 stripped its trailing spaces, and the program changed;
* **a line whose first byte comes from a span that is classified AND
  MULTI-LINE** — a heredoc body, a multi-line string, a POD block. Both halves
  are load-bearing.

**The CLI**: `--fmt FILE` to stdout, `--check` (names files that would change,
exit 1), `--diff` (the built-in unified diff, no git). Refusals exit 3 (parse)
and 5 (gate), and a gate refusal prints an internal-error banner asking for a
report, because it should never fire.

**The sweep, at this ruleset**: 672 files of the repo's corpora — **670
formatted with zero gate refusals**, 3 legitimately changed (two missing a
final newline, one with trailing whitespace).

**Two refused to parse, and correctly**: `t/fixtures/uses-modules.raku` uses an
exported operator `⊕` whose module is not on the path, so the parser cannot
read it — `-c` refuses the same file identically. `-I` does not help, because
`formatSource` parses standalone and loads nothing. So a file whose SYNTAX
comes from a module cannot be formatted, which the plan's "`-I` is illegal for
`--fmt`" implies without saying: worth stating, and worth revisiting if a real
project hits it.

Gates: `t/run.raku` 868/868, `t/slim/run.raku`.

## Steps 5-6 — R1, R4, R5, `-i`, and the wide sweep, landed 2026-09-12

The remaining v1 rules, the flag that makes the tool usable in anger, and the
sweep that judged them. The sweep is written up with the rules because it is
what shaped them: four separate bugs, and every one of them was found by
running the formatter over other people's code, not by reading it.

**R1 (indentation)** is `4 × bracket depth`, and the depth is counted from the
line's CODE SKELETON — the bytes that came from plain spans — never from its
text. A `{` inside a string or a comment is not a block, and counting one
indents the rest of the file by a level. A *continuation* line is **shifted by
however far its statement's first line moved, never set to a computed column**.
That is deliberate: a chained-method ladder, an aligned argument list and a
column of grammar rules were written that way on purpose, and a formatter that
recomputes those columns destroys the only thing they had.

What counts as a continuation took two corrections, both from the sweep:

* an **opening** bracket ends the statement, the way `{` does. What follows a
  trailing `(` or `[` is a new level to indent, not a continuation to shift.
  Without that, an `if` written inside a parenthesised expression kept the
  author's column while its body was re-indented under it;
* a line with **no code characters at all** — one that is entirely a string, a
  regex or a comment — says nothing either way, so the answer carries over.
  Reading it as "a statement ended here" made the `}` after a block's final
  string expression a continuation of that string, and shifted the brace by the
  string's own indent.

**R4 (else-motion)** splits `} else {` into `}` and `else {`, and runs **before
R1** so that R1 indents both lines it makes. The other way round they kept
whatever column the joined line had and the next run of the formatter moved the
`else` — an oscillation the idempotence gate caught on real modules.

**R2 (trailing whitespace)** now trims only the whitespace that is CODE.
`editable` is a per-line answer and this is the one place that is not enough: a
line can begin in code and end inside a literal. `token TOP { ` has a trailing
space that belongs to the rule body, and trimming it changed the pattern.

**R5 (minimum spacing)** cost this step its worst bug, and the fix changed what
the rule *is*. It reads at least one space after a `,` and around a `=>` — and
the first version found them by reading characters. `<=>` CONTAINS the bytes
`=>`. So `1 <=> 2` was spaced into `1 < => 2`, a different program; the semantic
gate caught every one and the repo sweep went from 1 gate refusal to 23, on
files like `examples/wordcount.raku` that have no grammar in them at all.

R5 now asks the LEXER which bytes are a `,` or `=>` **token**, and moves nothing
else. Three things that took measuring:

* **`Token::col` is not a byte position.** It runs several columns ahead of the
  token it belongs to — +3 on the very first token of a file, and the drift is
  not constant. Good enough for a diagnostic that names a line, useless for
  arithmetic. `Token::off` was added for this: the byte just past the token,
  stamped in `Lexer::make`, so the start is `off - text.size()`. The mark is
  laid only when those bytes really do spell the token, so a wrong offset makes
  R5 go quiet rather than wrong.
* **The lexer alone is not enough.** It tokenizes the inside of a word quote, so
  `<vp 1,2,3 hi>` came back with a comma token in it and R5 rewrote a string
  literal. Both witnesses have to agree: the lexer says a token starts here, the
  span scanner says this byte is code.
* **A fat arrow glued to a zip/cross metaoperator is left alone.** `1,2 X=> 3,4`
  is a metaop; `1,2 X => 3,4` is a parse error.

The ruleset above also promised R5 would space **infix `=`**. It is NOT in, and
on the evidence of `<=>` it should not go in as written: `=` is the one byte
that begins two dozen operators (`==`, `=>`, `=:=`, `+=`, `//=`, `~~=`, `=~=`),
and `:=`/`::=` end with it. The token-anchored form makes it tractable — ask the
lexer for a bare `Tok::Op` whose text is exactly `=` — but it is a new rule with
its own sweep to earn, not a line to slip into this one.

R5 also has to run **early**, before R1 and R4. It is the one rule that
addresses bytes by position, and both of those rewrite a line's leading bytes —
the per-byte mask stopped lining up with the text the moment either had run.
The final order is **R2, R5, R4, R1, R6**: R2 first because taking characters
off the END leaves every earlier index alone, so R5's marks still line up.

**`-i` / `-i.bak`** rewrite in place, over a list of files. It needed its own
argument arm: the perl-style `-i` cluster is gated `mode == Mode::Run` because
it belongs to `-n`/`-p`, and so is the "`-i` is only meaningful with `-n` or
`-p`" diagnostic. A file that is already formatted is not rewritten, so mtimes
survive a whole-tree run.

### The wide sweep

| corpus | files | already formatted | would change | won't parse | **gate refusals** |
|---|---|---|---|---|---|
| this repo | 757 | 589 | 167 | 2 | **1** |
| raku-corpus | 954 | 805 | 116 | 33 | **0** |
| installed ecosystem dists | 723 | 220 | 490 | 3 | **10** |

The ecosystem column is the interesting one: 723 modules nobody here wrote,
picked by nothing but "it is installed". It started at 18 refusals and the four
fixes above took it to 10.

**Live fire**: 703 files formatted IN PLACE across `t/regression examples tools
showcase` (158 rewritten), then `t/run.raku` run against the formatted tree —
**868/868**, and the tree restored with `git checkout --`. That is the evidence
the rules do not change programs; the gates are what made it cheap to get there.

### The one family that is left — R1 reaches inside a multi-line regex

All 10 remaining ecosystem refusals, and the repo's one
(`showcase/raku/raku-grammar.raku`), are the same shape:

```raku
our subset Scheme of Str
    where /^ [
           ''
        || <IETF::RFC_Grammar::URI::scheme>
    ] $/;
```

**A multi-line `/.../` is not one span.** The scanner colours its *parts* — the
`''`, the `<assertion>` — and leaves the rest plain, which is the right answer
for highlighting and the wrong one for the line model. So the interior lines
look like code, R1 re-indents them, and the regex literal changes. The gate
refuses every one; no file is ever written wrong.

Fixing it properly means telling the formatter the EXTENT of a multi-line
literal, which the scanner does not currently report and the classification
cannot be changed to report without changing how every regex is highlighted.
The cheap route is the lexer again — `Token::off` has a natural twin in the
token's start offset, and a token whose range spans a newline is exactly the
thing whose interior must not be touched. Not built: it is a second change to
the shared `Token`/`Lexer` surface for a 1.4%-of-third-party-files case that
already fails safe, and it should be its own round.

Gates: `t/run.raku` 868/868, `t/slim/run.raku`, `t/regression/fmt-basics.raku`,
`t/regression/highlight-lossless.raku`.
