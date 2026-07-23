# Module findings — the v2 ecosystem campaign's triage log

Findings from running battery modules (ash/raku-module-battery pins) under
rakupp — both rakupp gaps and module-side quirks. Kept here for us; **never
reported upstream to module authors**. Companion docs: V2-MODULES-PLAN.md,
ECOSYSTEM-TOP50.md.

1. **The top-50 is not dependency-closed** (2026-07-22): 12 of 50 dists need
   out-of-set transitive deps (Trap, Hash::Merge, …) or native libraries at
   load time. Action: vendor the REA transitive closure of the working set.
2. **rakupp warns-and-continues on a missing `use`** where Rakudo hard-fails
   at compile time. Good for exploration, wrong for conformance — rakupp
   needs a strict mode (or default) that dies like Rakudo; harness counts
   such loads as PARTIAL meanwhile.
3. **rakupp splits RAKULIB on colon; Rakudo on comma** — phase-1 fix: accept
   the comma form (with `file#`/`inst#` prefixes) in rakupp.
4. **JSON::Fast 0.19 does not parse under rakupp** — RESOLVED 2026-07-22.
   The blame initially fell on `Q/\u/` at Fast.pm6:130, but Q// was innocent
   (and my "proof" was zsh's echo expanding \t itself — always probe with
   files). The real bug: `module M:ver<0.19> { … }` — parseClass had no
   name-adverb handling, so `:ver` failed the brace check and the package
   took the unit-form branch, swallowing the block up to its first `;`.
   Fixed (adverbs parsed + recorded on ClassDecl: ver/auth/api — the fields
   phase-1 resolution needs anyway). Also exposed that the Tier-3 harness
   must count module PARSE warnings as PARTIAL, not LOAD.
4b. **nqp:: compatibility subset** — LANDED 2026-07-23. A gated `use nqp`
   flag (Parser::useNqp_) turns nqp::op(...) into dedicated NqpOp AST nodes;
   nqp::if/unless become native Ternaries, nqp::while/until/stmts/ifnull are
   lazy NqpOps, ~45 leaf ops (int/str/list/hash/attr/cclass) are eager.
   nqp::const::* fold to IntLits at parse time. ZERO-COST when unused: no
   `use nqp` → the branch is never entered, no node exists, runtime is byte-
   identical (verified). Getting JSON::Fast to actually RUN also required:
   package name adverbs (#4), `is repr("…")` recognized+skipped, braced-
   module `is export` surfacing to the importer, the `sub EXPORT(*@_)`
   protocol (returns a Map of &name=>&code imports), hyphenated qualified
   names (`M::from-json` was lexed as `M::from` `-` `json`), and useNqp_
   propagation into string-interpolation sub-parses.
4c. **JSON::Fast `from-json` WORKS** — RESOLVED 2026-07-23, byte-identical to
   Rakudo across arrays, objects, nesting, bools, null, Unicode, negatives,
   exponents (the only diff is the `$[…]` itemization marker in `.raku`, a
   display artifact). Four fixes got it there, each a general correctness win:
   (a) `nqp::bindattr($container,…,'$!reified',$buffer)` now SHARES backing
   storage (lvalue repoint of the shared_ptr) so buffer pushes show through
   the container; (b) `.Numeric`/`.Real` on a Str use the type ladder
   (`"1"`→Int, `"1.5"`→Rat), not a blanket Num; (c) cooperative `return`/
   `last`/`next` escapes `nqp::while`/`nqp::stmts` (the native loops now check
   the flags — was an infinite loop); (d) `--> True`/`False`/`Nil`/literal
   return constraints override a NON-empty body (body runs for side effects,
   literal is returned) — parse-false is `{ $pos = $pos+5 }` with `--> False`.
   Gate: definite-return.t 7→10, misc2 restored. Regression
   t/regression/nqp-json-fast-support.raku.
4d. **JSON::Fast `to-json` WORKS** — RESOLVED 2026-07-23, byte-identical to
   Rakudo (compact, pretty, sorted-keys, escapes, full round-trip, stable
   re-emit). Three more general fixes: (a) `nqp::istype([…], Seq)` is now
   False — an Array is not a Seq (a Seq is `.s=="Seq"`); jsonify tested Seq
   before Positional and looped forever on `jsonify(.cache)`; a real lazy
   `.map` still reports Seq. (b) a plain `my` declared inside a conditional
   BRANCH (Ternary then/else, nqp::if/while/stmts arg) hoists to the enclosing
   block so a SIBLING branch sees it (JSON declares `my int $codepoint` in the
   `\u` branch, assigns it from the `\n` branch) — narrowly scoped: `state`
   excluded, statement-flow `my` untouched (a broad version cratered state.t
   2/25 and gather; the narrow one is clean). (c) a bare `nqp::op` with NO
   parens is a zero-arg call (`nqp::list_i`, `nqp::null`) — was lexed as a
   bareword, so `$escapees := nqp::list_i` wasn't a list and its bindpos_i
   table stayed empty. **JSON::Fast is DONE.** Gate mf9 194,510, clean.
   Regression t/regression/nqp-scope-and-list.raku.
KNOWN TRADEOFF: recognizing+skipping `is repr("…")` means a repr-changing
   class redeclaration no longer throws X::TooLateForREPR (S32-exceptions/
   misc.t:69, −1). Accepted — loading every `is repr()` module is worth far
   more than one niche too-late-repr diagnostic.
5. **rakupp `substr` on long strings is O(position)**: 20k one-char substrs
   near position 4M took 116 s (5.8 ms each) — codepoint scanning from the
   string start each call. Makes naive char-walking parsers quadratic; needs
   an ASCII/byte fast path or offset cache in rakupp. (JSONLite works around
   it by walking a .comb array.)
6. **rakupp RUN-MAIN doesn't val()-allomorph NAMED args**: `Int :$top` with
   `--top=10` dies X::TypeCheck::Binding (positionals are allomorphed;
   nameds arrive as plain Str).


## Tier-2 functional baseline (2026-07-23)

Load count (Tier-3) held at 38/50 both-engines after the JSON::Fast leg —
loading was never JSON::Fast's problem, *functioning* was. Tier-2 measures
whether the advertised API works (battery `tier2/run.sh`, STDOUT diff vs
Rakudo). First 10-module probe: 4 MATCH, 6 DIFF. **Wide run (all 50, 2026-07-23):
19 MATCH, 9 CONFIRMED rakupp bugs, 22 inconclusive (out-of-set deps missing in
the isolated battery, or thin probes).** The 9 confirmed bugs are the actionable
next-leg list: XML `from-xml` (root name empty), URI + Cro::Core `.host` (empty),
MIME::Base64 + Base64 base64 encoding (both wrong — likely one shared
Blob/.encode root cause), URI::Encode not percent-encoding, Color hex parse
(returns black), HTTP::Status message lookup (empty), LibraryMake get-vars
(native). NOTE several MATCHes are thin (`.^name`/`.defined` load-smoke) — the
real functional matches are JSON::Fast, File::Temp, Terminal::ANSIColor,
JSON::Tiny, YAMLish, UUID, OO::Monitors. Battery: scans/TIER2-WIDE.md.

- MATCH: **JSON::Fast** (keystone, both directions), Terminal::ANSIColor,
  File::Temp, Method::Also.
- DIFF — the next module-leg targets, each a rakupp functional bug:
  7. **MIME::Base64.encode-str("hello")** → `AA==` (want `aGVsbG8=`) — encodes
     the wrong bytes (likely `.encode`/Blob handling in encode-str).
  8. **URI::Encode `uri_encode_component("a b")`** → `a b` (want `a%20b`) — no
     percent-encoding happening.
  9. **URI.new(...).host** → empty (want the host) — the URI grammar isn't
     populating accessors.
  10. **Data::Dump `dump([1,2])`** → empty — the exported `dump` produces
      nothing.
  11. **File::Find `find(dir=>".")`** → empty vs a real result.
  12. **HTTP::Status `get_http_status_msg(404)`** → empty (want "Not Found").


## Module fix batch 1 (2026-07-23) — 4 general fixes, +3 modules

Gate mb1 194,513 (allomorphic.t +2), suite 92, zero regressions. Tier-2 19→22.
Regression: t/regression/module-fixes-batch-1.raku.

- **7 (FIXED) MIME::Base64** — two bugs: (a) a Blob/Buf iterates its BYTES in
  `for` (was one item; `for $data -> $b1,$b2?,$b3?` reads the buffer), keyed on
  not-explicitly-itemized; (b) an allomorph (`<8>` IntStr) binds to a native
  `str` param and `str @` array via its Str side (typeMatchesArg + the native-
  array push check). MINOR residual: the bound value keeps the allomorph rather
  than coercing to plain str (stringifies correctly, so modules work).
- **15 (FIXED) URI::Encode** — a Regex in boolean context (`if $rx`) now
  matches the current `$_` (`?$rx` == `$_ ~~ $rx`); was always-truthy.
- **34 (FIXED) Color** — `{ :16($_) }` parsed as a HASH literal; `:16(...)` /
  `:16<...>` is a RADIX literal (16 is no pair key), so the block is CODE. Fixed
  the hash-vs-block heuristic at both sites (statement + expression).

Still open (6): XML `from-xml` root name, URI + Cro::Core `.host` (shared URI
grammar/accessor issue — 2 modules per fix), HTTP::Status `@codes` table,
Base64 (rank 31; exotic `.rotor`/`LAST`/`state`-phaser one-liner), LibraryMake
(native).

## Module fix batch 2 (2026-07-23) — explicit method invocants

Surfaced by URI (URI.rakumod `method parse(URI:D: Str() $str, ...)`, URI/Query
`method ASSIGN-POS(URI::Query:D: $i, Pair $p)`). Three linked fixes; gate inv1
194,551 (+38 over batch 1 — whole S12 files that died partway now run:
methods/chaining 2->15, class/inheritance 8->20, construction 6->11), suite 93,
zero regressions. Regression: t/regression/invocant-coercion-and-smiley.raku.

- **PARSE** an invocant colon after a bare/qualified type (`URI: Str()`,
  `Query:D: $i`) was misread as a named-alias (`:Str(...)`) or `:$named` marker.
  A named alias/marker colon is TIGHT against its key/var; an invocant colon has
  a SPACE after it -- `!peek().spaceBefore` disambiguates (two Parser sites).
- **BIND (the big one)** an explicit invocant param was consuming a POSITIONAL
  argument in bindParams instead of binding to `self`, so `method m($self: $x)`
  called `.m(21)` left `$x` empty ($self ate 21). Now an invocant binds to the
  env's `self` and consumes no positional. Pre-existing; hit every explicit
  invocant with following params.

URI remainder (findings still open): short-name package-relative type lookup --
inside `unit class URI`, bare `Path` must resolve to the imported `URI::Path`
(`URI::Path.new` already works fully-qualified; only the short name misses).
Cro::Core remainder: `package Foo {}` then `role Foo {}` same-name coexistence
(rakupp reports redeclaration -> role never loads -> `$!authority` uncomposed).

## Module fix batch 3 (2026-07-23) — package/role same-name coexistence

Surfaced by Cro::ResourceIdentifier (`package Cro::ResourceIdentifier { our sub
… }` then `role Cro::ResourceIdentifier { … }`). A bare `package`/`module` is a
WEAK namespace declaration -- it only opens the name for `our`-scoped symbols and
may coexist with a later class/role/grammar of the same name that refines it. The
parser's redeclaration check now skips `isPackage` decls (a genuine class/class
or role/role clash still errors). Gate pkg1 194,551 (unchanged; no Roast test
exercises it), suite 94, zero regressions. Regression:
t/regression/package-then-role-coexist.raku.

Cro::Core now loads past the redeclaration + `$!authority` composition; remaining
Cro layer: `Cro::Uri::GenericActions.new` (a grammar-actions class nested in a
package -- next onion layer, deferred).

## Module fix batch 4 (2026-07-23) — statement-level sink semantics

Surfaced by HTTP::Status, which builds its code->message table by creating each
status as a BARE statement (`HTTP::Status.new: 404, 'Not Found'`) whose
`method sink { @codes[$!code] = self }` registers it. rakupp never ran statements
in sink context, so the table stayed empty and `get_http_status_msg(404)`
returned 'Unknown' instead of 'Not Found'.

Three linked fixes (Rakudo sink semantics):
- every TOP-LEVEL statement now runs in sink context (was the default false);
- a discarded FRESH object (a method-call result) with a user-defined `sink`
  method has it invoked. Restricted to method-call results: a value read back
  through a variable or an `is rw` routine is a container, and MoarVM does not
  descend a container to sink its contents (S04-statements/sink.t line 43, a
  `#?rakudo.jvm todo` backend-divergent case) -- the restriction keeps that at
  5/5 while still firing on `Foo.new`;
- non-final statements of a routine body and the final value of a SUNK bare
  block now sink too (execBlock already did this; the callable-body loop and the
  bare-block exec case did not forward sink).

Gate sink2 194,550 (the only delta vs 194,551 is the known
socket-accept-and-working-threads.t flapper -- pre-sink binary gives 14/15/14 on
the same test), suite 95, perf unchanged (2.0s hot loop, was 2.05s). Regression:
t/regression/sink-context-method.raku.

Known residual divergence: a plain (non-rw) sub returning a fresh object, sunk,
does not fire sink (we only fire on method-call results) -- rare; documented.

## Module fix batch 5 (2026-07-23) — XML: named backref + indirect-type param + coercion attr

Three fixes let XML load and `from-xml` parse byte-identically (returns 'a' for
`from-xml("<a><b>hi</b></a>").root.name`, was 'a a'):
- **`$<name>` named backreference** (regex engine): in match position `$<name>`
  is a backreference to the named capture -- match its captured text literally,
  create NO new capture. rakupp was consuming only `$` (var="$") and re-parsing
  `<name>` as a fresh capture, so XML's close-tag rule
  `'</' $<name> '>'` captured the tag name twice. General regex fix.
- **`::(EXPR)` indirect type as a PARAMETER constraint** (parser): the expr forms
  (`~~ ::(EXPR)`, `::(EXPR).meth`) already resolved; the param form
  `method reparent(::(q<XML::Element>) $parent)` did not parse. Now parsed;
  left unconstrained (the type is dynamic).
- **coercion-type attribute** `has IO::Path() $.filename` (parser + AttrDecl.coerce):
  parsed and declared (was 'Attribute $!filename not declared'). NOTE: the
  coercion itself is not yet applied -- an assigned value keeps its own type
  (filename stays Str, not IO::Path); XML doesn't use filename so it's cosmetic.

Gate xml1 194,551 (only delta is the socket flapper recovering 14->15), suite 96,
zero regressions. Regression: t/regression/xml-triad-backref-indirect-coerce.raku.

## Module fix batch 6 (2026-07-23) — Base64 end-to-end: six general fixes

Base64 (rank 31) now encodes AND decodes byte-identically to Rakudo across all
pad cases, the :uri alphabet, and round-trips. Its dense one-liner style
surfaced six general gaps:

1. **`do with (EXPR) { $^a }`** — the statement-form with-block now hands the
   topic to a single-placeholder block (the modifier form already did).
2. **FIRST/NEXT/LAST in .map blocks** — loop phasers now fire with loop
   semantics when a block is driven by .map: FIRST once (was: per call, as an
   ENTER-alike), NEXT after each body, LAST after the final one, all in the
   invocation env so block params are visible (Base64's LAST reads its $c).
   One-shot `loopPhaserCtl_` set by the driver, consumed by callCallableRaw;
   zero cost when the block has no loop phasers (single body scan in map).
3. **Blob/Buf listy methods** — `.rotor`/`.batch`/`.pairs`/… expand a Blob to
   its BYTES (toList + the two scalar-as-one-element-list wrap sites); `for`
   iteration was fixed in batch 1, method paths were not.
4. **Multi dispatch, three scoring fixes**: (a) a candidate whose REQUIRED
   named is supplied now beats a positionally-typed rival (+16; Rakudo's sort
   treats requiring-a-named as narrower, ahead of positional types) — Base64's
   `:$pad!`/`:$uri!`/`:$str!` adverb multis rely on it; (b) a supplied named
   must TYPE-match its constraint or the candidate is out (`Bool:D :$pad!`
   does not bind `:pad('')` — the pad-rewrite chain relies on this to
   terminate); (c) alias keys count as supplied (`:buf(:$bin)!` satisfied by
   `:bin`).
5. **`|c` captures and nameds** — a capture now ABSORBS unclaimed named args
   (no more "Unexpected named argument" when a |c is present) and CARRIES them
   as namedArg pairs, so `samewith(|c)` re-passes them. Previously nameds were
   silently dropped from captures.
6. **`/@array/` regex interpolation** — an array in a regex matches its
   elements as a longest-first literal alternation (LTM over literals), wired
   at three compile sites (regexMatch `~~`, substSelect, and the
   comb/split/contains path). Base64 decodes via `$str.comb(/@alpha/)`.

Gate b64 **194,606 (+55)** — big honest wins in S05 regex files (litvar +8,
exhaustive +4, utf8-c8 +36, spurt +22). S06-currying/named.t 26->6 and
slurpy.t -2 are NOT functional regressions: those tests passed VACUOUSLY
before (the capture dropped the primed named -> .assuming primed nothing ->
the unchanged sig text happened to equal rakupp's lossy rendering of the
expected signature literal; the primed CALL then died). Now the capture
carries the named, priming works (the call succeeds), and the tests fail
honestly against rakupp's lossy .signature.raku introspection — a separate,
pre-existing gap, logged as a fix candidate (assuming-result signatures should
keep primed nameds with defaults; signature literals should render defaults).

Suite 97, perf: hot loop unchanged, multi-dispatch microbench slightly faster.
Regression: t/regression/base64-sextet.raku. Tier-2: 25/50.
