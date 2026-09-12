# Plan: slangs — running the ecosystem's grammar mixins, not emulating them

**Status: plan only, no code. Revised 2026-09-12; the first draft was
2026-09-09.** User-set direction, 2026-09-09: "we do not need to apply slangs
natively. Lets instead prepare a plan to implement slangs." That replaced a
Slang::Tuxic emulation (the parser recognised the module's NAME and flipped
two rules), reverted the same day. The twenty engine bugs that emulation
uncovered while getting Text::CSV's suite to pass are general and stayed —
see `t/regression/ecosweep-900-batch.raku`.

The revision rests on reading all sixteen published slangs' sources, mutsu's
implementation of the same problem, and what the engine has gained since the
draft. Three of the draft's assumptions did not survive that reading; they are
marked below.

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
front end is a hand-written lexer + recursive-descent parser in C++
(`src/Lexer.cpp`, `src/Parser.cpp`), so there is no grammar object to mix into.
That is the whole of the problem, and it is unchanged by any RakuAST work.

**A grammar-based front end is not the answer.** Rewriting `Parser.cpp` as a
Raku grammar interpreted by rakupp's own grammar engine would be faithful and
would cost the compile speed the C++ parser exists for. Recorded as a judgement,
not a measurement; if it is ever revisited, measure it against
`GRAMMAR-SPEED-PLAN.md`'s JSON workload first.

**One family of slangs needs none of this, and already ships.** `use L10N::XX;`
— a program written in German, Japanese or Afrikaans — arrives through
`define_slang` exactly like the others, but what its role carries is a table of
KEYWORD SPELLINGS, not new syntax. Our lexer hands every keyword to the parser
as a plain `Tok::Ident`, so the whole slang is a rewrite over the token stream
between the Lexer and the Parser (`Interpreter::applyL10NSlang`, landed
2026-09-12). Eleven languages run. Two of its mechanics are reused below: the
module is loaded in a **scratch `Interpreter`** before the program is parsed
(`main.cpp`, `applyL10N`), and the effect is **unit-scoped** — from the `use`
to the end of the file — rather than lexical.

## What is already in place (measured 2026-09-12)

| piece | state |
|---|---|
| byte offset on every `Token` | **done** — `Token::off`, added for `--fmt` |
| module load without import, at parse time | **done** — `loadModule(mod, {}, /*doImport=*/false)` from `l10nTokenXform` |
| Slangify's inner-`&EXPORT` protocol | **works** — `use Piersing` runs the inner EXPORT as far as `$*LANG.slang_grammar` (that is the error it dies with) |
| a role's tokens, enumerable by name and text | **done** — `ClassInfo::rules` / `ruleKind` / `ruleOrder` (src/Value.h) |
| a compiled regex matched at an arbitrary offset of a string the caller owns | **done** — `Regex::matchAt(subject, pos, …)` (src/Regex.h) |
| `<name>` inside a token resolving to a C++ callback | **done** — `GrammarHooks::namedRule` / `hasMethod` / `callMethod` (src/Regex.h), the issue-#64 machinery |
| RakuAST nodes with `.DEPARSE` and `.EVAL` | **done** — `RakuAST::Literal.from-value`, `IntLiteral`, `StrLiteral`, `ApplyPostfix`, `Call::Method` (src/RakuAstClasses.cpp) |
| `$*LANG` | **missing** — answers `Any` |
| `$~MAIN` and friends | inert `Grammar` type objects (src/Interpreter.cpp, `registerBuiltins`) |
| `use NQPHLL:from<NQP>` | **fatal** — `:from` is dropped and `NQPHLL` is searched for as a module; Tuxic's `::Legacy` role body does this at load |
| `Raku.legacy` | answers `True` (for the `if` dist); Slang::NumberBase picks its action branch on it |
| a slang module that fails to parse | silently exempted (`Interpreter.cpp`, "Grammar slangs stay exempt") — which is why every slang failure today surfaces as an unrelated parse error in the DIST that used it |

## Measurement — how much of the ecosystem this is worth

From the cached zef index, re-counted 2026-09-12:

| | count |
|---|---|
| slang distributions published | 16 (+ Slangify) |
| dists depending on one DIRECTLY | 5 — App::Crag (Roman, NumberBase), Dawa, Qwiratry, TOP (Otherwise), Text::CSV (Tuxic) |
| dists transitively behind one | **28** |
| total dists in scope | **45** of 2,530 |

Text::CSV is the load-bearing one: eight of the 28 sit behind it, Data::Reshapers
among them. (A 2026-09-12 session recorded that chain as running through
Hash::Merge; it does not — Hash::Merge 2.0.0 has no dependencies and passes.
Data::Reshapers' recorded failure is `Error while compiling module Text::CSV
(line 223)`, i.e. Tuxic.) Text::CSV's 2,053-line module has ~300 spaced call
sites and ~130 spaced method calls; its tests use the same style on every line.

Today all sixteen slang dists fail their own suites (`docs/dev/findings/ecosweep/
sweep-2530.tsv`), except Slang::Comments, whose test only loads it.

## What the token bodies actually contain — the finding that reshapes the plan

The draft said "run the slang's token against the source at the seam". Reading
the sixteen, the bodies are of two kinds, and the kind decides everything:

**Self-contained** — a regex over source characters, nothing else:

```raku
token number:sym<roman>  { '0r' <( <[ I V X L C D M Ⅰ .. Ⅿ ↀ ↁ ↂ ↇ ↈ \w ]>+ }   # Slang::Roman
token number:sym<base>   { $<prebase>=<[₀₁₂₃₄₅₆₇₈₉]>+ $<value>=[ \d+ [ '.' \d+ ]? ] | … }  # Slang::NumberBase
token value:sym<date>    { [ \d ** 4 ] '-' [ \d ** 2 ] '-' [ \d ** 2 ] }       # Slang::Date
token sigilless-variable { <.:So> }                                               # Slang::Emoji
token pointy-block-starter { '->' | '→' | '<->' | '↔' | 'λ' }                    # Slang::Lambda
token identifier { <.ident> [ <.apostrophe> <.ident> ]* <[?!]>? }                # Slang::Piersing
```

**Host-referencing** — calls into Rakudo's OWN grammar productions and reads
the compiler's dynamic variables:

```raku
token term:sym<identifier> {                                                     # Slang::Tuxic
    <identifier> <!{ $ident eq 'sub'|'if'|… || $*R.is-identifier-type([$ident]) }>
    <?before <.unspace>|\s*'('> \s* <![:]> <args>
}
token methodop(Mu $*DOTTY) { [ <longname> | <variable> | <quote> … ] \s* <.unspace>? [ <args> | ':' <args=.arglist> … ] }
rule statement-control:sym<for> { <.block-for><.kok> … <EXPR> <pointy-block> [ otherwise $<otherwise>=<.pointy-block> ]? }  # Slang::Otherwise
```

A self-contained token can be RUN by rakupp's regex engine as it stands. A
host-referencing one cannot be run without supplying `<args>`, `<EXPR>`,
`<pointy-block>`, `<longname>`, `<variable>`, `<quote>`, `<identifier>`,
`<.unspace>` and the dynamics `$*R`, `$*W`, `$*QSIGIL`, `$*DOTTY`, `$*IN-DECL`
— that is, a Raku-grammar surface over the C++ parser. The draft's "nine seams"
table hid this split; it also mis-filed Slang::Otherwise as additive (it
*replaces* `for`).

The complete survey, by what each slang's role declares:

| slang | production(s) | body | action produces | registers via | tier |
|---|---|---|---|---|---|
| Roman | `number:sym<roman>` | self-contained | `RakuAST::Literal.from-value` (modern) / `QAST::IVal` (legacy) | Slangify | 1 |
| NumberBase | `number:sym<base>` | self-contained | `RakuAST::Literal.from-value`, branch chosen by `Raku.legacy` | Slangify | 1 |
| Kazu | `number:sym<kazu>` | self-contained (CJK numerals, own grammar for the value) | `RakuAST::IntLiteral.new` | Slangify | 1 |
| Date | `value:sym<date>` | self-contained | `RakuAST::ApplyPostfix(StrLiteral, Call::Method "Date")` | Slangify | 1 |
| Piersing | `identifier`, `name` | `<.ident>`, `<.apostrophe>`, `<morename>` — three trivial host rules | none | Slangify | 1 |
| Subscripts | `identifier` | `<.ident>`, `<.apostrophe>` | none | Slangify | 1 |
| Emoji | `sigilless-variable` | self-contained | none | Slangify | 1 |
| Lambda | `pointy-block-starter` / `lambda` | self-contained | none | Slangify | 1 |
| Nogil | `sigilless-variable` | `<.ident>+ \| <.:So>` plus a code assertion using `nqp::` ops and `$*IN-DECL` | none | Slangify | 2 |
| Tuxic | `term:sym<identifier>`, `methodop`, `routine-declarator:sym<sub>` | host-referencing, about a dozen productions + 3 dynamics | none (`Mu`) | Slangify | 3 |
| Mosdef | `routine-declarator:sym<sub>` / `<method>` | `[ <.routine-sub> \| lambda \| 'λ' ] <.end-keyword> <routine-def=.key-origin(…)>` | none | Slangify | 4 |
| Otherwise | `statement-control:sym<for>` (replace) | host-referencing | `callsame.replace-otherwise(…)` on the For node | Slangify | 4 |
| Qwiratry (Mold, Topic) | `routine-declarator:sym<mold>`, `<wrapper>`, `statement-control:sym<givenroot>` | host-referencing | RakuAST actions | Slangify | 4 |
| Comments, Dawa | none — actions only (`use Slangify Mu, Actions`) | — | rewrites the host's For node body | Slangify | out |
| Forgiven, SQL, AltTernary | legacy `statement_control:sym<…>` / `infix:sym<…>` | NQP, `QAST::`, `%*LANG<MAIN>` | QAST | `$*LANG` directly | out |
| Predicate | (2018, cpan; source not located) | — | — | — | out |

Thirteen of the sixteen register through Slangify, so one `$*LANG` surface
covers them; the three that call `define_slang` themselves need the same
object under the same name and are out of scope anyway.

## The precedent: mutsu's ADR-0026

mutsu (the other from-scratch Raku, a Rust recursive-descent parser) shipped
this in August 2026 and Text::CSV's suite passes there (32 files, 22,696
assertions). Its design, verified in `src/runtime/slang_activation.rs` and
`src/parser/stmt/simple/slang_modes.rs`:

1. `use X` where X's source `use`s Slangify runs X's whole load — mainline plus
   the Slangify-generated inner `&EXPORT` — in a **fresh interpreter on a fresh
   thread**, with a compile-time `$*LANG` bound.
2. `$*LANG` is a minimal object: `slang_grammar`/`slang_actions` answer opaque
   handles whose `.^mixin` only RECORDS the role; `.^name` deliberately does not
   start with `Raku::`, so Slangify hands over the `::Legacy` role.
3. `define_slang` reads the recorded roles' declared rule NAMES and maps each
   onto a hand-written parser mode: `term:sym<identifier>` → spaced call,
   `methodop` → spaced method call (`.m (args)` and `!m (args)`),
   `routine-declarator:sym<sub>` → no-op, `identifier`/`name` → a trailing
   `?`/`!` on a routine name. **An unknown rule name is a hard compile-time
   error naming the rule** — never a silent ignore.
4. The mode is unit-scoped: importers of a unit that used the slang are
   unaffected, and EVAL strings parse in the stock grammar.

Its ADR rejects executing the token bodies ("a different project, out of all
proportion to any current need") and rejects keying on the module's name
("name-keyed native provision in disguise — the bundled module would be dead
code"). What it does is key on what the module DOES: the module is loaded,
Slangify's registration runs for real, and only the interpretation of "this
rule was overridden" is native.

That is honest about its limit: the mode for `term:sym<identifier>` re-states
Tuxic's exclusion list (`sub if elsif while until for` + type names) in Rust,
because it does not read the body. It is one step past the emulation the user
rejected, not the whole distance — the plan below says which slangs it is the
right answer for and which can do better.

## Design — one activation path, two kinds of seam

### A. Activation (shared by everything)

A pre-pass over the token stream, exactly where `applyL10NSlang` runs today
(`main.cpp`, `Interpreter.cpp` for modules and EVAL):

1. For each `use <Module>` statement, decide whether the module is a slang.
   `scanModuleOps` already reads every used module's source at parse time; a
   slang is one whose source contains `Slangify` or `define_slang` (a text test
   on a string already in hand — no extra I/O; a module can have any name:
   `Qwiratry::Mold::Slang`, `Dawa`).
2. Load it in a **scratch `Interpreter`** (as `applyL10N` does) with `$*LANG`
   defined in that interpreter's globals. The inner EXPORT runs for real.
3. `$*LANG` is an object with `.slang_grammar($name)`, `.slang_actions($name)`,
   `.define_slang($name, $g, $a)`, `.slangs` (Forgiven's spelling), and a
   `.^name`. The handles' `.^mixin(role)` records the role. `define_slang`
   collects, from each recorded grammar role, `ClassInfo::rules` — (rule name,
   token text, params) — and from each actions role the method of the same
   name.
4. **Answer `.^name` as `Raku::Grammar` / `Raku::Actions`**, the modern branch
   (the draft left this open). Every modern action produces a RakuAST node,
   which rakupp models and can `.DEPARSE`; every legacy action produces QAST
   and touches `$*W`, which rakupp has no reason to grow. `Raku.legacy` must
   answer `False` while a slang's action runs (NumberBase branches on it);
   it stays `True` elsewhere for the `if` dist's sake.
5. Every collected rule name is looked up in a fixed table of seams. **Unknown
   → compile-time error naming the slang and the rule** ("Slang::Otherwise
   overrides `statement-control:sym<for>`, which rakupp cannot apply"). This
   replaces the silent `Slang::*` exemption, whose effect today is that the
   failure lands as a baffling parse error in Text::CSV, not in Slang::Tuxic.
6. Re-lex the unit from the byte offset just past the `use` statement
   (`Token::off`) with the lexer seams armed, and parse with the parser modes
   armed. Unit-scoped, from the `use` to the end of the file, as L10N is and
   as mutsu is; Rakudo's lexical scoping differs only for a `use` inside a
   block. A module parsed under a slang records the slang's source in
   `opScanned_`, so its precomp entry is invalidated when the slang changes —
   the mechanism operator-scanned modules already use.

The scratch interpreter stays alive for the parse: tier-1 actions are called
on it, and their RakuAST results deparsed there.

Prerequisite, before anything else: `use X:from<NQP>` becomes a no-op. Rakudo
has NQPHLL and QAST; nothing here needs them, and Tuxic's `::Legacy` role
body executes its `use NQPHLL:from<NQP>` at module load regardless of which
role Slangify later picks.

### B. Tier 1 — literal seams, executed for real

For `number:sym<X>`, `value:sym<X>`, `identifier`, `name`,
`sigilless-variable`, `pointy-block-starter`: the slang's own token is run by
the regex engine, at the lexer's or parser's current position, on the source
the parser owns (`Regex::matchAt`, or `grammarParse` with `subparse` when the
action needs a Match with named captures — NumberBase reads `$<prebase>`,
`$<value>`, `$<postbase>`).

* `number` / `value` (Roman, NumberBase, Kazu, Date): in `Lexer::lexNumber`'s
  position and at a plain term start, try each armed token first. On a match,
  call the action with the Match; it makes a RakuAST node; `.DEPARSE` it and
  lex the resulting text in place of the span (`0rXIV` → `14`, `2024-01-01` →
  `"2024-01-01".Date`). The slang defines both the syntax and the value;
  rakupp hard-codes nothing about roman numerals. This is the draft's P4
  "desugar", and it turns out to be free.
* `identifier` / `name` (Piersing, Subscripts, Slangify's own test fixture):
  where `Lexer::consumeIdentChars` builds a bareword, and where the parser
  reads a routine name, try the armed token first and take its span as the
  identifier. The three host rules the bodies call — `ident`, `apostrophe`,
  `morename` — are supplied through `GrammarHooks::namedRule`.
* `sigilless-variable` (Emoji): at the declarator site ("expected variable after
  declarator" in `Parser.cpp`) and at term start, try the token.
* `pointy-block-starter` (Lambda): at operator-lexing position, try the token;
  a match that is `<->`/`↔` lexes as `<->`, anything else as `->`.

Each armed seam costs a first-character test at token starts; the LTM prefix
set the NFA already computes gates the regex call. Unarmed: one bool.

### C. Tier 2 — the same, with a code assertion

Slang::Nogil's `sigilless-variable` is `<.ident>+ | <.:So>` behind
`<?{ check-keywords($/) if $*IN-DECL }>`, and `check-keywords` reads
`$/.orig`, `$/.from`, `nqp::findnotcclass`, `nqp::const::CCLASS_WORD` and
`::{$identifier}:exists`. Tier 1's machinery plus: a Match whose `.orig` is the
unit's source, `$*IN-DECL` set by the declarator site, and those nqp ops (the
parser already lowers `nqp::` calls, `makeNqpOp`). Tried after tier 1; if the
assertion is the wall, Nogil waits.

### D. Tier 3 — parser modes keyed on the rule name (Tuxic)

Tuxic's three bodies call about a dozen host productions and three compiler dynamics.
Running them means a Raku-grammar surface over the C++ parser AND a default
action for each production (Tuxic passes `Mu` for actions; Rakudo's stock
action builds the call from `$<identifier>` and `$<args>`). That is mutsu's
"different project". For Tuxic — and only for it, in this plan — take
mutsu's road:

| rule declared | parser mode |
|---|---|
| `term:sym<identifier>` | `name (args)` is a call with those args (`Parser.cpp`, the `!cur().spaceBefore` test before `parseCallArgs` in the bare-identifier term), except `sub if elsif while until for` and type names — Tuxic's own list |
| `methodop` | `.name (args)` and `!name (args)` are method calls with those args (`Parser.cpp`, the `.method(args) — tight only` site) |
| `routine-declarator:sym<sub>` | no-op — `sub foo (…)` already parses |

The seam table records that these three are name-keyed, and why. The module
is loaded, Slangify's registration runs, and the modes exist only because
`define_slang` saw those three names. If a second slang ever declares the
same names with different bodies, this tier is wrong for it and says so.

### E. Tier 4 — a host-production shim, if a dist earns it

Mosdef (`def` for `method`, `lambda`/`λ` for `sub`) needs four productions:
`routine-sub` ('sub'), `routine-method` ('method'), `end-keyword` (a word
boundary), and `routine-def`/`method-def` via `key-origin` — "stop here; the
parser's routine parse continues from this offset". With that shim the token
body itself says that `def` means `method`; no mode re-states it. The same
shim, grown to `identifier`, `unspace`, `args`, `longname`, `variable`,
`quote`, `arglist` and a `$*R` with `.is-identifier-type`, is the road to
running Tuxic's bodies instead of tier 3, and to Otherwise (`EXPR`,
`pointy-block`, `block-for`, `kok` + `.replace-otherwise` on the For node) and
Qwiratry. Each is sized when a dist behind it is worth it — tier 4 is
measured need, not a promise.

Out of scope, stated: Forgiven, SQL, AltTernary (legacy NQP/QAST throughout);
Comments and Dawa (actions over the host's own AST nodes, no grammar change);
Predicate (source not located).

## Phasing

| phase | deliverable | gate |
|---|---|---|
| 0 | spike: `use Slang::Roman; say 0rXIV` prints 14 through the real token, the real action and `.DEPARSE`, in a scratch interpreter | the one-liner, then Roman's `t/01-basic.rakutest` (7 subtests) |
| 1 | activation (§A) + `$*LANG` + `:from<NQP>` no-op + the unknown-rule error; no seams beyond the spike's | all 16 dists either activate or name the unsupported rule; `rakupp -e 'use Slang::Tuxic'` says which rule; Roast unchanged; `perf-guard --check` |
| 2 | tier 1: Roman, NumberBase, Kazu, Date, Piersing, Subscripts, Emoji, Lambda; tier 2 if Nogil's assertion runs | each dist's own suite; Slangify's own test (Piersing fixture); App::Crag re-measured |
| 3 | tier 3: Tuxic's three modes | Slang::Tuxic 8/8; Text::CSV's suite (32 files); then Data::Reshapers and the seven others behind Text::CSV, re-measured the ECOSWEEP way |
| 4 | tier 4 shim, Mosdef first | Mosdef's suite; then a decision per remaining dist |

Phases 2 and 3 each end with a re-measured ecosystem count, not a claim.

## Risks, honestly

* **Unit-scoped, not lexical.** A `use Slang::X` inside a block governs the
  rest of the file here. L10N and mutsu accept the same; noted, not hidden.
* **The re-lex.** Re-lexing from the `use` statement's offset restarts the
  lexer in its default state; a `use` inside a heredoc, `q:to` or POD is not a
  position the lexer can restart from, and the pre-pass skips those (they are
  not statements). Mid-file restart for any other reason is not needed.
* **Arbitrary Raku at parse time.** A tier-1 action runs user code inside the
  parse, in the scratch interpreter. It wants the guard rails `BEGIN` has here
  (no `--exe` surprises, errors reported with the slang's name).
* **Tier 3 is name-keyed.** Said above; the seam table says it too. The user
  rejected emulation by module name on 2026-09-09; this keys on the rule
  names a real registration produced, which is what mutsu chose and calls
  load-bearing. If that is still emulation, tier 4's shim for Tuxic is the
  alternative, at roughly a week against a day, and Text::CSV waits for it.
* **Ceiling.** 45 dists, only if each passes its own suite afterwards.
  Unblocking is not converting. Tiers 1–3 put 11 slang dists, Text::CSV, the
  eight behind it and App::Crag within reach — about 20 — and the 2026-09
  batches say to expect fewer.
