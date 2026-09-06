# The Grand Review, phase 1 — the source — 2026-09-06

The first of three phases (source → user docs → the Internals book; the plan
is `docs/dev/plans/GRAND-REVIEW-PLAN.md`). Thirteen read-only lanes read the
whole of `src/` — each lane its slice in full plus the documents that specify
it — and reported findings against the Rakudo oracle (2026.08, arm64) with a
kept probe per finding. The coordinator triaged 300-odd findings into gated
batches; every batch is an exact-match script under
`rc-work/review-grand/batches/`, gated in a detached worktree (fix → a
`t/regression` file that passes on BOTH engines → `t/run.raku` → the full
Roast with a per-file join and a union-list diff), and applied to the shared
tree only after its gate read green. The working ledger — every finding, its
batch, every gate result, every reversal — is `rc-work/review-grand/TRIAGE.md`;
the raw lane reports are `rc-work/review-grand/findings/L*.md`.

## Baselines (at 4183921, three clean arm64 runs)

- Roast: 649 / 648 / 646 fully-passing (union of the three: 653 files);
  199,025 / 218,284 declared assertions (91.2%)
- `t/run.raku` 762/762
- Oracle: `/opt/homebrew/bin/raku` = Rakudo 2026.08 (the bare `raku` on this
  box is a different x86_64 binary)

## What the review found, in one paragraph

The dominant defect class is still **copy-then-diverge**, now at a larger
scale: the same rule in two to nine places, each copy having learned something
the others did not (the type lattice in two tables and an isa-ok third; the
negative-subscript rule in twenty-four sites; the regex `#`-comment rule in
three lexer scanners and two compiler loops; the built-in class tables in
seven). The second class is the **silent wrong answer** — a quiet `False`
where Rakudo throws (`spurt` into a missing directory, `IO::Socket::INET.new`
on a refused connection, `@a[$i] = 9` with a negative `$i` writing the last
element, `unique` folding `1`, `"1"` and `1e0` into one, every `\c[name]` with
an unknown name emitting nothing). The third is the **lenient catch** —
`catch (...)` in a hook wrapper swallowing a user's `die` (regex blocks,
LEAVE phasers, user infix fallbacks). Two lessons about the review itself
mattered as much as any fix: a louder parser can turn a partial Roast file
into a no-TAP file, which the union-list gate cannot see and the old
per-file join skipped; and the honest version of a test helper
(`throws-like` checking the exception TYPE) moves forty-one Roast files out of
"fully passing" — a policy question, held for the user, not a batch item.

## Batch A — dead code (zero behaviour)

Shadowed dispatch arms and unreachable branches across Interpreter,
MethodCall{Part2,Part3,Tail}, Lexer, Parser, Builtins; the review's first gate
(Roast 649, `t/run` green) confirmed nothing observable moved. (Left for a
later A2: the `NK::VarDecl` removal and the three AstSerial fields, which need
`kAstSerialVersion` 18.)

## Batch B (+ B2, B2b, B2c) — parser and lexer

- Interpolation chain: `"$n.is-prime()"` printed `7.is-prime()` (the method
  name stopped at the hyphen); one chain scanner now serves `$/`, `$!`, `$0`,
  `$<x>` and `$var`; a subscript after a bare `.name` does NOT commit it (only
  a parenthesised call does — Rakudo, probed on `range.t`).
- `0b12` / `0o89` / `0xfg` were accepted (1 / 0 / 15); `"\xZZ"` emitted a
  silent NUL; `"\c65"` was the string `c65`; `"\c[NO SUCH NAME]"` emitted
  nothing — all refused now, and character names are case-insensitive.
- `rx/ [ ']' ] /` opened a string (a quote inside a GROUP quotes; only inside a
  character class is it a member); a `#` in a regex is a comment to the end of
  the line — and (B2) an EMBEDDED `#`( … )` / `#`[ … ]` ends at its closer,
  in the three lexer scanners (B2) and the two regex-compiler loops (B2b, B2c:
  the character-class member loop had its own blank-skipper).
- `"\c@"`, `"\cA"`, `"\c?"` are the CONTROL characters (they were the letters);
  a user-declared postfix must TOUCH its operand, so `3 !< 2` with a
  `sub postfix:<!>` in scope is the negated `<` (it was `3!` and a word list
  that ran to the end of the file); a heredoc opened on the last line says so.
- The B gate's lesson: the louder parser turned six PARTIAL Roast files into
  no-TAP files — invisible to the union-list diff and skipped by the old
  per-file join. `gates/join.py` now reports MISSING files; B2/B2b/B2c put
  four of the six back (advent2013-day22.t 6/7 → 9/9). The other two need
  emoji-sequence names (below).

## Batch C1 (+ C1b) — interpreter: control flow, calls, numerics

- LEAVE/KEEP/UNDO: the catch-all that swallowed a phaser's own `die` is gone;
  KEEP/UNDO decide on the block's OUTGOING value (defined and not a Failure);
  a matched `when` inside a loop is a `next` carrying the when-value.
- `--> T` is checked for every declared type (classes, subsets, known type
  names; natives and parametrised `X[…]` excluded); `returns Positional of
  Numeric` records the CONTAINER type (the `of` used to overwrite it).
- The Proc sink rule is a property of the value: `Interpreter::sinkValue()`
  detonates a sunk Failure and throws for a sunk failed Proc — at statement
  level (ANY sunk Proc, not just a bare `run`) and in the test helpers that
  discard a block's result (`throws-like { run … }`). Rakudo sinks
  `await $proc-promise;` too: a `t/regression` file had been failing on Rakudo
  unnoticed; corrected.
- Reduce identities (`[&&] ()`, `[**] ()`, `[lcm] ()`…), `x` refusing
  Inf/big/> 1e15, Num `%%` via fmod, IEEE division by an undefined, `>>=><<`
  keeping the key, Complex primitives that do not throw (`+(3i)` is 3i),
  `9e15`-style integral Nums printing in integer form, BigInt→double exact past
  two limbs, negative shifts, `abs` at LLONG_MIN, NaN `<=>` → Nil.

## Batch C2 (+ C2b) — the method ladder and the builtins beside it

- One exact integral-double→Int (`numToIntExact`): `1e19.floor/ceiling/
  round/truncate/Int/UInt`, `floor(1e19)`, `Int(1e19)` and `1e19.Rat` are
  exact (they saturated at 9223372036854775807; `1e19.Rat` was 1).
- `unique`/`repeated` compare by `.WHICH` identity like `squish`
  (`(1, "1", 1e0).unique` was one element); `chop` and `indices` count
  graphemes; `trim`/`trim-leading`/`trim-trailing` know Unicode White_Space;
  `.base` refuses a radix outside 2..36 (it clamped); negative `substr`
  arguments are `X::OutOfRange` with the `*-N` hint (they wrapped).
- `$fh.put` no longer prints the HANDLE through the universal echo arm;
  `$socket.say` writes the line to the socket; `Complex.Numeric` is itself;
  `$*KERNEL.archname`/`.version` come from `uname` (they were "x86_64"/"0");
  `Hash.new` and `.hash` with an odd count die.
- IO::Path mutators answer a Failure on error (`spurt` into a missing
  directory, `rmdir`, `chmod`, `spurt :createonly` on an existing file — a
  quiet False let the program run on); `spurt`/`mkdir`/`unlink` need an IO
  invocant (`"x".unlink` deleted a file); `open` of a missing file is a
  Failure, not a throw; `created` is the birth time (it shared `modified`'s
  field); `to-posix` is an Instant's; `like` demands a Regex; `lives-ok`
  counts loop control as a death; `is-approx`: a positional tolerance is
  ABSOLUTE, the default relative 1e-6 (absolute 1e-5 near zero).
- Withdrawn after probing: "stat Instants on the `now` clock" — Rakudo's
  `.modified` is raw POSIX too (`.modified.Int == time` on both engines).
- **Withdrawn as policy (C2b):** the honest `throws-like` (checking the
  exception TYPE and the named matchers) — see Decisions.

## Batch C3 (+ C3b, C3c, C3d) — processes, sockets, subscripts

- A child killed by a signal reports exitcode 0 and the signal (it was −1 and
  0): one fold of the wait status (`procStatusFold`), one split into the two
  fields (`storeProcStatus`) at every Proc store; `.signal`, `.so`, `?$proc`,
  `.pid` and the sink rule read them. `Proc::Async :w` feeds the child (every
  `.print`/`.say`/`.write` answered True and reached nobody; the child read OUR
  stdin); `.close-stdin` closes the pipe; writing without `:w` is
  `X::Proc::Async::OpenForWriting`. `IO::Socket::INET.new` throws on a failed
  socket/bind/connect (it answered Nil and `$s.print` on Any passed silently);
  an unsupported `:family` is refused (it was validated then ignored).
- A negative subscript is out of range (Rakudo: `X::OutOfRange`; `*-N` is the
  way to index from the end) — twenty-six sites wrapped Python-style or
  answered quietly. Probed form by form: a SINGLE read is a quiet Failure
  (`@a[$n]`, `(1..5)[$n]`, `@mx[0;$n]`, `$/[$n]`, `$blob[$n]`, `.map(*[$n])`);
  a SLICE with a negative index throws at once; every write throws
  (`@a[$n] = 9` wrote the last element; `@a[$n, 0] = 9, 8` dropped the key);
  `"@a[$n]"` throws; `:exists`/`:v` stay quiet; `@empty[*-1]` stays a quiet
  Failure. S02-types/array.t 71/80 → 78/80.

## Batch C4a, C4b (+ C4c) — one type lattice; sequences, imports, CALLER::

- `typeAncestry` is the one built-in tower: Bool IS an Int (`True.isa(Int)`,
  `Bool.^mro` lists Int), Str does Stringy, an allomorph is Allomorph, Str
  AND its number (`IntStr ~~ Str` was False); `typeNameConforms` consults it
  after its role table; isa-ok's private table no longer says a Blob is a Buf.
  S02-types/bool.t 59 → 60/66.
- `...` with Int seeds at or past 2**53 walks exactly (the double walk deduced
  a 0 step and repeated the seed forever; the eager loop's endpoint test
  compared doubles too); `use Mod :tag` imports that tag ONLY — a plain
  `is export` is `:DEFAULT` and is not implied (S11-modules/import-tag.t 9 →
  11/12); `-I file#/dir` names a directory (the `#` guard never matched a
  repo spec); `CALLER::<$y>` reads the CALLER's frame — the parser had
  rewritten it to the plain `$y`.

## Batch E (+ E2) — the regex engine

- Escapes inside a character class are the classes (`<-[\h\v]>` admitted a
  space: `\h \v \e \f \a \b \0` were the letters); `\h` includes NBSP and the
  Zs block, `\v` NEL/LS/PS, `\N` is the complement of the LOGICAL newline.
- `|` binds tighter than `||`: `a | ab || c` is `[a | ab] || c` — each `||`
  group is its own longest-token alternation (one `||` used to make the whole
  bracket first-match).
- `token c { . }` matches a newline and keeps CRLF whole; `^^` does not match
  in the void after a final newline and `$$` matches at the end only without
  a preceding newline (S05-metachars/newline.t 13/15 → 15/15, line-anchors.t
  19 → 20/24); inline `:r`/`:ratchet` set the ratchet (`:r` matched a literal
  `r`); an unknown `\y` is a compile error (it matched "y").
- A `die` inside a regex block — `{ … }`, `<?{ … }>`, wired blocks, a grammar
  token's block — leaves the parse: five `catch (...)` hook sites re-raise a
  RakuError after their scope restores (`{ die }` used to parse on). Two of
  rakupp's own conversions stay quiet as before: a block's compile error
  (`&?ROUTINE` in a regex, `:my $a=2;` — parser gaps, ledger) and loop
  control the EVAL turns into X::ControlFlow (`{ last }` in a regex block:
  Rakudo exits the loop, we neither exit nor die — ledger).
- Withdrawn (E2): "an unknown `<subrule>` throws" — `<commit>` is a Rakudo
  built-in we lack and the loud rule killed S05-mass/rx.t at test 18 of 756;
  the repo already gates this behind `use v6.e` (t/regression/6e-gating.raku),
  which is where the loud form belongs once the built-in list is complete.

## Batch F — the command line, the FFI switch, the language server

- `RAKUPP_OPT=--watch` spawned an unbounded chain of watchers and never ran
  the program: the child carries a mark and refuses the inherited flag.
- The single-dash long-option courtesy runs at every option position and
  compares the part before `=` (`-I lib -exe f.raku` ran `-e xe`;
  `-env-file=x.env` ran `-e nv-file=x.env`); the completion table knows
  `--watch`; `RAKUPP_FFI=1/on/yes/true` means the default search (it silently
  disabled libffi). Withdrawn after the gate: `-c`/`--lint` reading a program
  on stdin — rakupp's contract is "a mode with no source is a usage error"
  (t/run.raku pins it); Rakudo reads stdin there (ledger L12 F13).
- `--lsp` no longer crashes on hostile JSON: a nesting cap, a no-throw `\u`
  parser, a guarded message parse and a Content-Length ceiling — the same
  four defences the MCP/Jupyter servers' JsonLite already had. The REPL
  drops the internal "EVAL parse error:" prefix.

## Gate discipline — what this review added to the method

- **Join per file, and count the MISSING.** A file that dies at compile time
  has no status line; the union-list diff cannot see a partial file's loss.
  `rc-work/review-grand/gates/join.py BASE.txt NEW.txt` reports lost
  assertions, shrunk denominators, missing files and new timeouts.
- **Never launch a gate chain with `nohup`.** SIG_IGN for SIGHUP survives exec
  into every child, so a test's `.kill` (SIGHUP by default) cannot end a
  `sleep` and the await runs the full 60 s — a fake failure. The chains run
  through a wrapper that resets SIGHUP and `setsid()`s.
- **Gate on a quiet box.** A full Roast beside Rakudo probes and a build
  doubled the timeouts (12 → 24); every moved file is re-run alone before it
  is believed.
- **Run a new regression file on Rakudo FIRST.** Three existing files had
  frozen rakupp's own wrong answer (a Num rendering, `spurt :createonly`
  answering False, an awaited killed Proc not throwing) and were failing on
  Rakudo unnoticed.
- **`tools/run-roast.raku --list=FILE` WRITES the fully-passing list**; a
  subset run is the positional PATTERN args.

## Final gates (the shared tree, abd3d11 + every batch, built into build-arm64/)

| gate | pre-review (4183921) | post-review |
|---|---|---|
| Roast fully-passing | 649 (union of three runs: 653) | **660** (worktree runs of the same code: 660–661; the two files "removed" against the union, concreteness.t and 99problems-51-to-60.t, timed out and pass alone) |
| Roast assertions | 199,025 / 218,284 | **200,172 / 219,450** (+1,147; 61 files up) |
| `t/run.raku` | 762/762 | **773/773** (nine new grand-review regression files, each also passing on Rakudo) |
| files lost | — | none beyond the deliberate drops below |

Deliberate per-file drops, each documented in TRIAGE.md: S04-declarations/
will.t 10/19 → 9/17 (a `will leave` in a class body now dies loudly where the
catch-all swallowed it — the will-phaser scoping bug is visible), S32-str/
utf8-c8.t 59/65 → 51/54 (the honest `spurt` Failure on an APFS-refused
utf8-c8 name ends the file), S05-modifier/ignoremark.t 37 → 36/45 (test 39's
lowercase character name used to emit NOTHING; the real Prepend mark now
reaches the ignoremark path, which lacks it), S32-num/real-bridge.t 189 → 188
(the honest `is-approx` tolerance on a custom Real's `.log`), and the two
emoji-name files below. The other gains and losses of every intermediate run
were timing flappers, re-run alone.

## Decisions for the user

1. **Batch T — the honest `throws-like`.** Checking the exception TYPE and the
   named matchers (`message => /…/`) is what Rakudo's helper does; here it
   moves 654 → 613 fully-passing Roast files (our exception objects lack many
   of Rakudo's attributes; we throw X::AdHoc where Rakudo throws a typed
   exception) and would lower the ecosweep greens. The code is written
   (batches/C2.py's throws-like arm, reverted by C2b); the numbers are
   measured. Apply, or keep the vacuous helper and say so in the docs.
2. **Emoji sequence names.** `\c[woman gesturing OK]` and `\c[united states]`
   are named emoji sequences; the name table has UnicodeData + NameAliases
   only. Two files from unicode.org (emoji-sequences.txt,
   emoji-zwj-sequences.txt, Unicode 17) are needed — a download, so not done
   here. Until then S02-literals/char-by-name.t and S03-operators/bit.t stay
   no-TAP with the (correct) "Unrecognized character name" error.
   NamedSequences-17.0.0.txt is on the box (perl 5.44's unicore) if plain
   named sequences are wanted too.
3. **Batch D (codegen, L9's 31 findings) and batch J (the JS backend, L10's
   43):** refuse via `unsupported()` versus real fixes; `--verify`'s stderr
   policy; the `sprintf` tie-rounding side. Not started; the lane reports are
   the spec.
4. **`Supply.merge` with a live supply** now REFUSES instead of silently
   dropping the live source (Rakudo merges); making it work is a
   supply-plumbing job.
5. **L3 F2/F8** (arity for methods and blocks; typed `@`/`%` binds) are
   battery-risk items deliberately not attempted.

## Deferred, with the design notes in TRIAGE.md

- **Modules:** the two module resolvers (the parser's copy lacks META6
  `provides`, the shadow ordering and `:ver`); BEGIN/CHECK/END inside routines
  run in place (needs the INIT-style program walk for three more phasers); the
  `use` leak (imports publish to the global scope — a second `use Mod` sees a
  tagged sub the first import brought); `spawnPromise`'s GIL twin.
- **Regex:** byte offsets on the grammar path, list-valued captures
  (`$<w>=(\w)+`, `%`-separators), the ratchet capture leak, `<{…}>`/`<&name>`
  as no-ops, the NFA ranker's class pruning beyond ASCII, `<<`/`>>`/`<ws>`
  beyond ASCII, the lookaround sub-state losses, proto-probe side effects,
  `<( )>` rollback, unescaped interpolation pasting, the seven class tables,
  the nine capture record/rollback copies, `<same>`, `<nosuch>` behind 6.e.
- **Interpreter:** a decl-with-init as the last statement of a sunk block
  leaves the block's value undefined (KEEP ran UNDO); routine bodies still KEEP
  on "no exception"; a supplied `where` runs twice at dispatch; `will` phasers
  in class bodies; `!≃`-style meta-negation of a user infix; `.does(Buf)` on a
  Blob value (the `~~` twin says False); Range arithmetic; `tr///`; the
  `:p`/`:c` adverbs; the eager sink bug.
- **Processes/IO:** IPv6 (`getaddrinfo`, `sockaddr_storage`); the
  kill-before-exec race (a CLOEXEC exec-sync pipe in spawnChildStart);
  S17-procasync/encoding.t's C1-era shrink; sized buffers' signedness.
- **CLI/tooling:** `-M` spliced before a Pod/`#!` first line (needs the
  interpreter preload path); `… | rakupp -c` (the no-source usage-error
  contract versus Rakudo's stdin read — the user's call); the REPL's
  trailing-operator continuation (judgment call); the install store's dangling `short/` entry, the phase-hash
  `depends`, the half-written store; the Lint/DeclCheck twin walkers; `num32`/
  `int32 is rw` out-parameters; `$?MODULE`.
- **Compiled forms (`--exe`, JS):** the twins listed by L3 F13/F17, L2 F8,
  L7 F20 and the whole of L9/L10 — batch D/J after the decision above.

## Doc impact — the worklist for phases 2 and 3

Every finding carries a doc-impact tag in the lane reports; the ones the
fixes above make urgent: REFERENCE.md's escape table (`\h \v \N`, class
escapes, `\cX`), the anchor and adverb lists (`^^`/`$$`, `:r`); the book's
subscript chapters (negative indices are out of range; `*-N`); the
Proc/Proc::Async pages (`:w`, `.signal`, the sink rule, `await` of a killed
child); NETWORKING.md (the constructor throws; IPv4 only); CLI.md (`-c` on
stdin, `--watch` refused in RAKUPP_OPT's child, the single-dash courtesy,
`-M`'s splice); FFI.md (`RAKUPP_FFI=on`); MODULE-LOADING.md (`is export` is
`:DEFAULT`; the `provides` gap); the Test-helper page (what `throws-like`
checks today, pending decision 1); the Internals book's chapters on the
type lattice (one table), the negative-subscript policy, the regex hook
sites (a `die` leaves the parse), and a new `--target=js` chapter (L10's
outline).
