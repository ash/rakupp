# Plan: slangs — running the ecosystem's grammar mixins, not emulating them

**Status: plan only, no code. User-set direction, 2026-09-09:** "we do not need
to apply slangs natively. Lets instead prepare a plan to implement slangs."
This replaces a Slang::Tuxic emulation written earlier that day (the parser
recognised the name and flipped its two rules); it was reverted the same
session. The twenty engine bugs that emulation uncovered while getting
Text::CSV's suite to pass are general and stayed — see
`t/regression/ecosweep-900-batch.raku`.

## What a slang is, and what actually blocks one here

In Rakudo the compiler's front end **is** a Raku grammar. `$*LANG` holds the
current grammar/actions pair per sublanguage (`MAIN`, `Q`, `Regex`, `Trans`),
and a slang module mixes a role of `token`s into it:

```raku
$LANG.define_slang('MAIN',
    $LANG.slang_grammar('MAIN').^mixin($grammar),
    $LANG.slang_actions('MAIN').^mixin($actions));
```

That runs from a `sub EXPORT`, i.e. **at `use` time, while the importing file is
still being parsed**, and the mixin governs the rest of that lexical scope.

RakuAST is not what makes this work — a grammar-based front end is. RakuAST
changed the *name* of the grammar (`Raku::Grammar`, which is why slangs ship
both a modern and a `::Legacy` role) and nothing about the mechanism. rakupp's
front end is a hand-written recursive-descent lexer + parser in C++
(`src/Lexer.cpp`, `src/Parser.cpp`), so there is no grammar object to mix into.
That is the whole of the problem, and it is unchanged by any RakuAST work.

**A grammar-based front end is not the answer.** Rewriting `Parser.cpp` as a
Raku grammar interpreted by rakupp's own grammar engine would be faithful and
would cost the compile speed the C++ parser exists for. Recorded as a judgement,
not a measurement; if it is ever revisited, measure it against
`GRAMMAR-SPEED-PLAN.md`'s JSON workload first.

## Measurement — how much of the ecosystem this is worth

From the cached zef index, 2026-09-09:

| | count |
|---|---|
| slang distributions published | 16 (+ Slangify) |
| dists transitively behind one | **28** |
| total dists in scope | **45** of 2,530 |

The 28: App::Crag, App::Rak::Complete, Chemistry::Stoichiometry,
DSL::English::DataAcquisitionWorkflows, DSL::FiniteStateMachines,
DSL::Translators, Data::Cryptocurrencies, Data::ExampleDatasets,
Data::Reshapers, Data::Summarizers, Dawa, Grammar::TokenProcessing, JSON-CSV,
LLM::Resources, ML::AssociationRuleLearning, ML::Clustering,
ML::NLPTemplateEngine, Qwiratry (+5 of its own), SSH::LibSSH::Tunnel,
Services::PortMapping, TOP, Text::CSV.

Text::CSV is the load-bearing one: eight of those sit behind it alone.

## The seams — what the published slangs actually mix into

Every one of the sixteen touches a **closed set of nine productions**. This is
the finding that makes a bounded implementation possible: rakupp does not need
an extensible grammar, it needs nine documented decision points.

| production | what it means | slangs using it |
|---|---|---|
| `statement-control:sym<X>` | a new statement keyword | Comments, Forgiven, Otherwise, SQL |
| `number:sym<X>` | a new numeric literal form | Kazu, NumberBase, Roman |
| `value:sym<X>` | a new literal form | Date |
| `term:sym<identifier>` | a term in expression position | Tuxic |
| `methodop` | the postfix `.name(args)` production | Tuxic |
| `routine-declarator:sym<sub>` / `<method>` | how a routine declaration opens | Mosdef, Tuxic |
| `sigilless-variable` | bare-identifier variables | Emoji, Nogil |
| `identifier` / `name` | what may spell a name | Piersing, Subscripts, Slangify |
| `lambda` / `pointy-block-starter` | the arrow that opens a block | Lambda |

Note the shape of the list: four of the nine are *additive* — a new alternative
under an existing proto (`statement-control:sym<sql>`, `number:sym<roman>`) —
and those are strictly easier than the five that *replace* a rule.

## Design

Keep the C++ parser. Give it **slang seams**: at each of the nine productions,
if the current lexical scope has a user rule registered for that seam, run the
rule against the source at the current position before (or instead of) the
built-in production; on a match, consume the span it matched and continue.

Five pieces, in dependency order.

### P0 — a source offset on every token

`Token` (src/Token.h) carries `line` and `col` and no byte offset, and
`Lexer::tokenize()` lexes the whole file up front. A seam needs (a) the byte
offset of the current token so a Cursor can start there, and (b) the ability to
**re-lex from an arbitrary offset** once a user rule has consumed a span the
built-in lexer never saw. This is the load-bearing prerequisite and the one
piece with no workaround. Cost is a `size_t` per token plus a `tokenizeFrom`
entry point; watch the parse benchmark, since Token is copied a great deal.

### P1 — the registration interface

Thirteen of the sixteen slangs register through **Slangify**, which is a thin
`sub EXPORT` over `$*LANG.define_slang(...)`. Supporting Slangify's interface
therefore covers most of the ecosystem at once:

* `$*LANG` — an object answering `.slang_grammar($name)`, `.slang_actions($name)`,
  `.define_slang($name, $grammar, $actions)`, and `.^name`.
* `.^name` decides which role the slang hands over: Slangify passes the
  `legacy-grammar` when the name does not start with `Raku::`. **rakupp should
  answer the name whose role is easier to interpret, and that choice should be
  made by reading both roles of Tuxic and Mosdef, not guessed.**
* `.^mixin(role)` need not produce a real grammar — it can produce a marker that
  records "these tokens, for this seam", since the tokens are what the seams
  consume.

The remaining three use `$~MAIN` directly and need the same object under that
name.

### P2 — `use` of a slang runs at PARSE time

A slang's EXPORT must run while the importer is still being parsed. rakupp
executes `use` at runtime, but the parser already peeks into a module at parse
time for a related reason: `Parser::scanModuleOps` (src/Parser.cpp:517) resolves
a module through the same search the loader uses and scans it for the operators
it exports, so `SPACE ~ $word` parses correctly against an installed
Text::Utils. **That is the hook location and the precedent.** A slang module is
recognised the same way, its role bodies extracted, and its tokens registered
against the seams for the rest of the enclosing scope — the lexical stack shape
`monkeyScopes_` already uses.

### P3 — running a user token against the source

rakupp has a complete Raku grammar/regex engine already (it is how user
grammars work). A seam builds a Cursor over the compilation unit's source at
the current offset and calls the registered token, exactly as
`RxCursorCall` does for `<.method>` subrules (see the issue #64 work). Two
things must be true and should be proved on a spike before anything else is
built:

1. a Cursor can be created over text the parser owns, at an arbitrary offset;
2. a token body that is *itself* Raku code — including `{ … }` blocks and
   `<?before …>` assertions, both of which Tuxic uses — runs correctly there.

### P4 — what a match produces

Two options, and the cheap one is worth trying first.

* **Desugar (start here).** The seam hands back the matched span and a
  replacement *source string*, which the parser re-lexes and parses normally.
  Covers every additive seam and most replacing ones. No actions API, no AST
  builder, no new node kinds.
* **Actions.** The slang's actions class builds a node through a small builder
  API. Faithful, and needed only by a slang that must produce a shape the
  desugar cannot express. Defer until a real dist demands it.

## Phasing

| phase | deliverable | gate |
|---|---|---|
| 0 | spike: Cursor over parser-owned source at an offset, running one hand-written token | the spike itself |
| 1 | P0 token offsets + `tokenizeFrom` | full Roast, parse benchmark unchanged |
| 2 | `$*LANG` object + Slangify's EXPORT contract, no seams wired | `use Slang::X` loads and registers without error |
| 3 | the four ADDITIVE seams (`statement-control:sym<X>`, `number:sym<X>`, `value:sym<X>`, plus `lambda`) | Slang::Roman, ::NumberBase, ::Kazu, ::Date, ::SQL, ::Otherwise, ::Forgiven, ::Comments, ::Lambda run their own suites |
| 4 | the five REPLACING seams (`term:sym<identifier>`, `methodop`, `routine-declarator`, `sigilless-variable`, `identifier`/`name`) | Slang::Tuxic, ::Mosdef, ::Nogil, ::Emoji, ::Piersing, ::Subscripts; then Text::CSV and the 28 behind it |
| 5 | actions (P4b), only if a dist needs it | — |

Phases 3 and 4 each end with a re-measured ecosystem count, the standard way
(`ECOSWEEP` runbook), not with a claim.

## Risks, honestly

* **P0 is a hot-path change.** A byte offset per token is cheap in isolation and
  Token is copied constantly; if the parse benchmark moves, the offset goes in a
  side table keyed by token index instead.
* **Re-lexing mid-file is where the bugs will be.** Heredocs, `q:to`, POD blocks
  and the quote sublanguages all carry lexer state that an arbitrary restart
  point does not have. Expect the seams to be restricted to positions where the
  lexer is in its default state, and expect that restriction to be discovered
  rather than designed.
* **A slang's token body is arbitrary Raku running at parse time.** Whatever
  the compile-time execution story ends up being, it wants the same guard rails
  `BEGIN` has here.
* **45 dists is the ceiling**, and only if every one of them passes its own
  suite afterwards — unblocking is not converting, which the 2026-09 batches
  have shown repeatedly. Ten to fifteen converted dists is the honest
  expectation for phases 3–4 together.
