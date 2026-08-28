# Plan: `--fmt` — a source formatter

**Status: DESIGN APPROVED 2026-08-26 — code not started.** Design probes run 2026-08-26
against `build-arm64/rakupp` (see "What the probes said" below).

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
