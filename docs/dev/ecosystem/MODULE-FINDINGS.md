# Module findings — the v2 ecosystem campaign's triage log

Findings from running battery modules (ash/raku-module-battery pins) under
rakupp — both rakupp gaps and module-side quirks. Kept here for us; **never
reported upstream to module authors**. Companion docs: V2-MODULES-PLAN.md,
ECOSYSTEM-TOP50.md, and [MODULE-LOADING.md](../../internals/MODULE-LOADING.md) for how the load path itself
works (finding #2 below is settled there: a failed load is fatal now).

1. **The top-50 is not dependency-closed** (2026-07-22): 12 of 50 dists need
   out-of-set transitive deps (Trap, Hash::Merge, …) or native libraries at
   load time. Action: vendor the REA transitive closure of the working set.
2. **rakupp warns-and-continues on a missing `use`** where Rakudo hard-fails
   at compile time. Good for exploration, wrong for conformance — rakupp
   needs a strict mode (or default) that dies like Rakudo; harness counts
   such loads as PARTIAL meanwhile.
3. **rakupp splits RAKULIB on colon; Rakudo on comma** — RESOLVED 2026-08-01.
   rakupp now accepts BOTH `,` and `:`, so one setting serves both engines
   (a `:` after a lone leading drive letter stays part of the path, for
   Windows). Rakudo's `file#`/`inst#` repo-spec prefixes are still not read.
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


## Module fix batch 7 (2026-07-24) — package-relative names + friends: URI and LibraryMake work

Tier-2 26->27 (URI, LibraryMake now byte-identical; Cro::Core loads fully, its
grammar-ACTIONS layer is the one remaining gap -> host=(Any)). Gate pkgrel2
**194,621 (+15)**: charset +5 (class composition), namespaced +6 (nested
naming), rx +2, our/pseudo-6e/longest-alternative +1 each, S03-metaops/misc.t
now parses (+1). Suite 98, perf slightly better (1.16->1.08 hot loop).
Regression: t/regression/package-relative-names.raku.

Six general fixes:
1. **Package-relative short names** (classAliases_): a qualified class also
   answers to its TAIL when no real class claims it (`use URI::Path` -> bare
   `Path`), guarded so a BUILT-IN name is never shadowed (`X::Roast::Channel`
   must not hijack `Channel`). Wired: NameTerm eval, methodCall Type-invocant
   entry, rtTypeMatch/typeMatchesArg (attr+param constraints), augment lookup.
2. **Nested classes register QUALIFIED** (`class GenericActions` inside
   `class Cro::Uri` is Cro::Uri::GenericActions, ^name included, as Rakudo);
   tctx_.pkgPrefix now covers class/augment bodies; the tail alias keeps short
   references working. Healed the first-gate regressions (augment-supersede,
   lexical.t, channel/basic) via the builtin-guard + alias-aware methodCall.
3. **`use X` inside a class body** loads at declaration — the class-body parser
   WHITELISTED statement kinds and silently discarded UseStmt; URI.rakumod puts
   its `use IETF::RFC_Grammar` lines after `unit class URI`.
4. **.can() reports built-ins**: new/bless/gist/... on any class, parse/subparse
   on grammars (stub callables that dispatch for real if invoked); alias-aware.
   IETF::RFC_Grammar gates its constructor on `.can('parse')`.
5. **Char-class composition with USER tokens** (`<[\-+.] +uri-alpha +digit>`,
   `<+unenc-pchar - [:]>`): user-named parts become subrule alternation with
   lookahead-reject subtraction; kebab-case names no longer split at '-' (was
   parsed as `+uri` MINUS builtin alpha -- the whole RFC 3986 grammar failure);
   `+:N/+:S` property parts approximated; blanks allowed after +/- ops.
6. **Bracketed infix + metaop assignment**: `A [op] B`, `A [op]= B` (left
   target, unlike plain `Rop=` which reverses roles including the target — both
   verified against Rakudo), and `R//`-family routed through applyBinOp so
   short-circuit ops reverse correctly. LibraryMake: `%vars{$k} [R//]= %*ENV{$k}`.
   LibraryMake's "native" label was a red herring -- its `is native` lives in POD.


## Module fix batch 8 (2026-07-24) — the Cro family cluster

**Cro::Core is byte-identical** (Tier-2 28/50): the full functional sweep — URI
parsing (scheme/host/port/path/query/fragment, userinfo, percent-encoded paths),
RFC 3986 relative resolution (dot segments, query-only, network-path refs), and
Cro::MediaType — matches Rakudo output exactly. **Cro::HTTP works under rakupp**:
Request/Response construction, header validation, byte-perfect serialization,
content-type parsing. (The Tier-2 Cro::HTTP probe still shows DIFF only because
RAKUDO cannot compile Cro::HTTP in the battery sandbox — native OpenSSL — while
rakupp answers correctly; rakupp's only remaining warning is inside
IO::Socket::Async::SSL (`whenever` scoping), which needs native TLS anyway.)

Eight general fixes (gate cro2 **194,627 (+6)**, sole delta
integration/lexicals-and-attributes.t 2->8 unlocked; suite 99; perf flat incl. a
grammar-heavy bench; regression: t/regression/cro-family-cluster.raku):

1. `|%hash` in a LIST slips its pairs (`%parts = scheme => …, |$<hier-part>.ast`).
2. `self.bless(...)`/`$obj.new(...)` on an INSTANCE builds a fresh object of its
   class (Cro's `(self ?? $!create !! Cro::Uri).bless(|%parts)`).
3. `<alias=.rule>`: the dot suppresses only the rule-name capture; the alias
   still captures (was: ruleName kept the dot -> matched nothing).
4. Positional captures under a quantifier in GRAMMAR rules are Arrays of every
   occurrence in the action's $/ (`(...)+` -> @$0): ParseNode/MemoEntry now
   carry capReps + listCaps through record/memo/replay, and the action-side
   Match builder emits per-iteration lists. (Both repro variants were broken;
   one "worked" only because $0's span happened to cover the whole run.)
5. `%( $hash, pair )` merges the hash (was: STRINGIFIED it into a key);
   itemized `$hashitem` stays whole (S02 assigning-refs guard, caught by the
   first gate and fixed via ListExpr marking $-var hash elements itemized).
6. A `sub` in a class body is callable from methods — the class-body parser
   parsed-and-DISCARDED it (same family as batch 7's dropped UseStmt);
   Cro::Uri's `sub remove-dot-segments` is called from `method add`.
7. `subset X of Str is export where /…/` — traits between the base type and
   `where` (Cro::HTTP::Cookie).
8. Private-method colon listop `self!client-setup: { … }, :$enc`
   (IO::Socket::Async::SSL), and `\x21..\xFF` hex-escape RANGES in char
   classes (Cro::HTTP header field-content validation).

Remaining Cro gap: native TLS (OpenSSL FFI) for live client/server connections —
out of interpreter scope; everything pure-Raku in the family now runs.

## Module fix batch 9 (2026-07-24) — toward a LIVE Cro server: seven layers

Driving an actual `route {} + Cro::HTTP::Server` app under rakupp surfaced a
fresh onion (none of it TLS-related). Fixed, in order encountered:

1. **Braced-module exports vs builtins**: `is export` subs inside `module … { }`
   were invisible to the export scan (top-level-only), so a name colliding with
   a builtin (`get`!) was withheld — the Router's `get` fell through to the
   STDIN-reading builtin and HUNG. The scan now recurses into class/module
   bodies.
2. **Pointy block as a listop argument** (`get -> { content … }`): `->` added to
   startsListopArg, guarded off in statement conditions (`for @a -> $x {}`).
3. **Dynamic vars now cross METHOD frames**: invokeMethod pushes the caller env
   onto dynStack like callCallableRaw does — `my $*CRO-ROUTE-SET` in `route`
   is now visible inside methods/plugins it calls (contextual.t +12).
4. **Public @./%. attrs assign through their accessor without `is rw`**
   (`.body-parsers = @!body-parsers`); $-attrs still require the trait.
5. **`only method` declarator**; **`state => v` is a pair** not a declaration;
   **`my (:@a, :@b) := %h` named destructuring** (namedBind on VarExpr).
6. **Module-body forward references**: hoistSubs + classes-register-first
   two-pass exec for braced package bodies (Router calls
   router-plugin-register/PluginKey 1400 lines before their definitions).
7. **Parameter introspection**: `.constraints` (literal params like 'greet'),
   `.type` returns a TYPE OBJECT (`=:= Str` comparisons), `.named_names`,
   `.positional`, `.sigil`.

Gate cro3 **194,658 (+31)**: contextual.t +12, pairs.t +9, clone +4,
introspection +2, attributes +2, advent2009-day21 parses (+1). Three honest -1
trade-offs, each an error-expected pedantry case now more permissive:
pointy.t 19 (`{YOU_ARE_HERE}` gating), S09 decl.t (shaped-attr out-of-bounds
check through accessor), construction.t 11 (auto-constructor array
writability). Suite 100. Regression: t/regression/cro-live-cluster.raku.

**Live-server status**: route blocks build, handlers register, plugin config
flows, Server loads. The NEXT blocker is `Variable '$split' is not declared`
from Cro.compose's `++state $split` — the shape resists 5 isolated repro
attempts (state in for-Z-loop in method with colon-call slices all pass), so
it needs in-situ debugging next session. After that: the EVAL'd route-matcher
regex and the reactive (supply/whenever) pipeline are untested territory.
## Batch 10 — the live Cro server WORKS (supply/whenever pipeline + IO::Socket::Async)

`/tmp/cro-live.raku` (canonical hello-world: route + Cro::HTTP::Server + real
TCP client in one process) now runs end-to-end on pristine vendored dists:
`server started` → `HTTP/1.1 200 OK` → `body: Hello, Andrew!` → clean stop.
Sixteen general fixes, in dependency order:

1. **`state` in methods finally has a home**: invokeMethod now creates the
   per-Code stateEnv and splices it into the env chain + sets curStateEnv,
   exactly like callClosure. Before, a method's `state` var landed in the
   CALLER's state env — mainline calls worked by accident (global_), nested
   calls lost the var (Cro.compose's `++state $split`, the batch-9 blocker;
   t5 probe showed one counter shared across four classes).
2. **`.?` maybe-call**: parser set mc->maybe, interpreter never read it. Now
   converts this-invocant/this-method X::Method::NotFound to Nil.
3. **subset `where` smartmatches** (applyBinOp `~~`) instead of boolifying —
   `where Cro::Message | Cro::Connection` (junction of types) works; and
   **infix `~~ SubsetName`** consults subsetMatches via a g_subsetCheck hook.
4. **`X ~~ Y` restores an OUTER `$_` correctly**: when $_ lived in a parent
   scope the restore left a shadowing `$_ = Any` in the current env — inside
   a `when` block this wiped the topic for the rest of the block
   (ConnectionManager's BUILD lost `$!transformer` this way).
5. **On-demand `supply {…}`** : the block is stored, not eagerly run; `.tap`
   wires it for real (tapSupply): emit routes to the tap's callback, nested
   `whenever` opens inner taps that stay live after the block returns, `done`
   closes the activation, CLOSE/QUIT/LAST phasers parse (QUIT/CLOSE added to
   the phaser keyword set + isBlockPhaser) and fire at the right moments,
   implicit completion fires done when the block returned and no inner tap is
   still live (pending counter). Value-context consumers drain eagerly
   (drainSupplyBlock) — legacy semantics preserved; react keeps its old path.
   `Supply.on-close` registers on the live tap when one is active.
6. **IO::Socket::Async**: .listen(host,port) → a Supply that binds/accepts on
   tap (accept worker thread, GIL-parked in accept); connection sockets carry
   .Supply(:bin) (read-worker; EOF fires done and closes the fd), .write/.print
   (kept Promise), .close (SHUT_WR), peer/socket host+port. .connect → kept
   Promise of a connected socket. whenever-over-Promise awaits real
   (PromiseState-backed) promises with the GIL released.
7. **Supply-block env survives its frame**: breakSelfClosures suspended while
   wiring (noCycleBreak_) — a `my sub` in a supply block is callable when an
   I/O worker fires the whenever later (RequestParser's fresh-message).
8. **`INIT my $x = …` statement-form phasers** run in the ENCLOSING scope
   (Block.stmtForm) — Cro::TCP::NoDelay's `INIT my $is-win` pattern.
9. **CArray[T].new/allocate/.elems + nativesizeof + cglobal stub**, and
   callNative passes CArray as a pointer to its packed bytes (nodelay()'s
   setsockopt path — which on Rakudo actually sets IP_TOS, since PROTO_TCP
   is an undeclared bareword that stringifies; we now do the same dance).
10. **Encoding::Registry.find/.register + streaming decoder** (add-bytes,
    set-line-separators, consume-line-chars(:chomp/:eof), bytes-available,
    consume-exactly-bytes, consume-all-chars/-bytes, is-empty); find throws
    X::Encoding::Unknown for unknown names (registry.t 29→35).
11. **Bare `&callable` params require Code** in dispatch — Log::Timeline's
    log($parent,&task) vs log(&task,*%data) picked the wrong multi and called
    .count on the parent.
12. **Anonymous `regex {…}`/`token {…}`/`rule {…}`** are first-class Regex
    VALUES closing over their scope (expression AND statement position — the
    latter is what `EVAL 'regex {…}'` produces). Matching them runs code
    blocks/`<?{…}>` assertions/`:my` decls for REAL in a per-match child of
    the closed-over scope (regexMatch wired mode; no textual $-interpolation).
    That plus `~~` already returning Match = Cro's EVAL'd route matcher works.
13. **Grammar deferred `{ make … }` fixes**: build() runs while the match
    scope is still current (`:my` vars visible — the route matcher's `$cap`);
    proto-token nodes carry the winning candidate (ParseNode.actualRule) so
    the candidate's make-code and `x:sym<y>` ACTION METHOD both fire
    (Cro::Uri::HTTP.parse-request-target's request-target:sym<origin-form>).
14. **`"&encode($x)"` call interpolation** in qq-strings (the route compiler
    builds its matcher source this way); bare `&name` without parens stays
    literal.
15. **Dispatch/OO fixes**: coercion params (`Str(Cool) $v`) no longer type-
    reject at scoring (append-header('Content-length', $int) multi); a role's
    STUB method defers to the class's same-named public attr accessor
    (MessageWithBody's body-serializer-selector stub vs Response's attr);
    positional attributive params (`method set-body($!body)`) write through
    to the invocant (excluded from bindParams' fast path); `.can` answers
    public attr accessors ($handler.can('method')); Capture.new(:list,:hash);
    Signature.ACCEPTS(Capture) (arity window + literal constraints + types +
    required nameds); Mu.return/.return-rw (cooperative return of invocant),
    with `-->` literal constraints statically rejecting `.return` in the body.
16. **Roast repair follow-ups** in the same batch: Supply.on-close via live
    tap (syntax.t 41→42), Encoding registry typed errors (registry.t 29→35),
    `.return` vs literal constraint (misc2.t back to 203).

Gate cro5 **194,745 (+87)**: fully-pass 587 (-2, both accounted: registry.t
went full-29/29 → partial-35/37 while passing SIX more tests, and
socket-accept-and-working-threads.t is the known 15↔14 flapper); no-TAP -1,
timeouts -1. Per-file wins:
S05-metasyntax/regex.t 3→41, attribute-params.t 10→17, subset-6c +4,
nonblocking-await.t noTAP→11, defer-next.t full, advent2012-day10 +3.
Perf: mandel/fib/loop/hash/method-call unchanged (method-heavy slightly
faster). Battery: 28/50 byte-identical (unchanged; remaining DIFFs are
Rakudo-sandbox failures). NOTE: the tier2 battery is driven by
`raku tier2/run.raku` — `sh tier2/run.sh` has an unexported-`R` bug and
reports rp=[] for everything.

**Known divergence (accepted for now)**: state vars in an inline-executed
for-body inside a routine persist across CALLS of the routine (Rakudo clones
the body block per call, resetting them). Same pre-existing behavior as subs;
bites only a compose() that takes the `$split` branch on a second call.

**Next**: Cro::HTTP::Client (needs .then on promises + Connector pipeline),
Supplier::Preserving buffering, content-length'd request bodies through
RawBodyParser, `whenever`-outside-react parse (IO::Socket::Async::SSL,
Log::Timeline::Output::Socket), nqp::bitor_i (CBOR::Simple).

### Batch 10 addendum — Whatever-curry as listop argument

From the Using Raku book's Monte Carlo solution (`sqrt([+] map *², @point)`):
a leading `*` currying through an infix was only accepted as a listop argument
for ranges/`.method`/word-infixes — `map *², @a` (lexed `* ** 2`) was a parse
error, and `map *+2, @a` silently misparsed as `map(*, +2)`. Now a leading `*`
followed by any Op curries for the higher-order list builtins (map, grep,
first, sort, reduce, produce, min, max, sum, classify, categorize,
grep-index, first-index) — same name-gating as the pointy-block-arg rule, so
a general `name * 2` stays multiplication. Verified: mc.raku prints ~3.1416;
S32-list/first.t goes no-TAP(parse error)→16 ok; whatever/map/grep.t
unchanged. Rides the next batch's full gate.

## Batch 11 — probe triage: five parse/dispatch fixes, two stale probes

Fresh triage showed ALL 22 remaining Tier-2 DIFFs have rk=[]. IMPORTANT (see
the batch-11c correction below): rk=[] is NOT a mere "sandbox" artifact —
Rakudo fails these for real reasons (unvendored transitive deps, native libs,
stale probes), and rakupp's non-empty output is UNVERIFIED (produced by
silently ignoring missing `use`). Of the 22, 8 were also outright rakupp
failures; those are the ones the fixes below address, each verified against
the failing module:

1. **`if/elsif EXPR -> $x is copy {`** — traits on an if-binding are consumed
   and ignored (our binding var is already a writable copy). HTTP::UserAgent's
   content-length elsif. (`with` already used the full signature path.)
2. **`< word list >` shields the rule-body scanner** — HTTP::MediaType's tchar
   list holds `' # {` as WORDS; the raw `{…}`-capture treated the quote as a
   string opener and swallowed braces to EOF.
3. **Alternative regex/subst delimiters** (HTTP::Request's `m:i,pat,`).
   Docs: any delimiter but whitespace/alphanumerics/':'(adverbs)/'#'(comment).
   Rakudo probing: bare `m,b,` and `s,b,X,` work, BUT `foo(S,S)` with a
   declared role S is a CALL — Rakudo disambiguates via declared-symbol
   lookup, impossible in one-pass lexing (roast subsignature.t + subst.t
   caught the naive version in the gate). Shipped policy: ADVERBED forms
   accept any documented-legal punctuation delimiter (`m:i;p;`, `s:g=a=b=`);
   bare forms add ',' for m/rx only (documented Raku), while bare `s,`/`S,`
   stay terms/calls — the known scope-sensitive divergence.
4. **Indented POD** — `    =begin comment … =end comment` inside a sub body is
   pod at the virtual margin (Text::Utils had one mid-sub); both =begin and
   =end now accept leading blanks.
5. **Enum trait arguments** — `enum Sort-type is export(:sort-list) < … >`
   (Text::Utils): parseEnum consumed `is export` but not the `(…)` argument.
6. **`!=:=`** lexes as one op (negated container identity), BP_COMPARE; the
   generic `!op` negation in applyArith does the rest. JSON::Class.
7. **`$*RAKU.compiler.version` reports Raku++'s OWN version** (v1.0.0, from
   CMake's PROJECT_VERSION; .release/.id likewise) — rakupp is not Rakudo and
   does not impersonate its release dates. Consequence, accepted: modules that
   gate on Rakudo dates (JSON::Class dies for compiler < v2023.12) refuse to
   load — such Rakudo-specific checks are the module's business, and
   JSON::Class is blocked on AttrX::Mooish anyway. The LANGUAGE version stays
   6.x on $*RAKU.version / .lang-version.
   **REVERSED (215c3e2, v1.5.2): `.version` now answers the ORACLE ERA**
   (`kOracleEra = "2026.07"` in src/Builtins.cpp — the Rakudo the battery and
   spec diff against, bumped when that oracle bumps). "The module's business"
   did not survive contact: the gate is asking *do I have modern semantics?*,
   and byte-identity against Rakudo 2026.07 is precisely the claim it wants —
   answering v1.5.x reads as a pre-2000 Rakudo and blocks every such module.
   `.name` (Raku++) and `.release`/`.id` (the real rakupp version) are how the
   engine identifies itself; user-facing writeup in docs/guide/faq/differences.md.
8. **Blob/Buf are not Stringy in dispatch** — a byte buffer no longer binds a
   `Str` param; `multi sha1(Str)`'s `samewith $str.encode` looped forever when
   the Blob re-matched Str instead of the blob8 candidate. blob8..blob64/
   buf8..buf64/utf8/16/32 added to isKnownTypeName (`--> blob32` returns).
9. **Stale probes fixed** (battery repo): Digest 1.1.0 provides no `Digest`
   module (Rakudo fails the old probe too) → probe now targets Digest::SHA1;
   Data::Dump exports `Dump`, not `dump` → flipped to MATCH immediately.

Tier-2: **29/50** (Data::Dump). HTTP::UserAgent and Text::Utils now PARSE and
answer their probes with plausible output, but rk=[] is Rakudo failing on a
missing dep (Encode) / the module itself — NOT verified-correct (see
batch-11c). Honest new target:
**Digest rk=[20] vs rp=[]** — pure-Raku SHA1 needs element-width typed blobs
(blob32.new packs 32-bit words, .elems counts words, [$i] reads words; our
Blob is a plain byte string). JSON::Class additionally needs AttrX::Mooish
vendored + MOP-level parse work. PDF::Lite needs the PDF dist vendored.

## Batch 11b — typed blobs + the SHA1 pipeline: Digest byte-identical

Digest::SHA1's pure-Raku `sha1("abc")` now returns the exact
`a9993e364706816aba3e25717850c26c9cd0d89d` (Tier-2 Digest MATCH, probe checks
the real hex, not just `.elems`). It exercised a whole cluster of general
fixes:

1. **Element-width typed blobs**: blob16/32/64 (and utf16/32) store
   little-endian WORDS, ofType = uint16/32/64. New Value helpers blobElemSize/
   blobElems/blobWordAt/blobList; every blob surface switched from bytes to
   elements — .new/.allocate (pack LE), .elems/.list/.head/.tail/AT-POS,
   for-iteration, @$blob contextualizer, `my @a = $blob` (coerceArray),
   subscript+slice (5 index sites), and Z/X/hyper list-infix. `.Int`/`.Num`
   of a Blob = its element count (`8 * $msg` = bits). flatten() deliberately
   LEFT keeping a Blob whole — Rakudo's `flat`/`reduce` don't expand it; only
   list-context ops (Z/X, listCtx helper) do.
2. **Radix digit-list `:256[a,b,c]`** = place-value in base (`:256[|@^a]`
   packs bytes into a word); parser + __radix-list builtin; the `{ :256[…] }`
   looksHash heuristic now treats `[` like `(`/`<` after a radix int.
3. **Colon-arg list precedence**: `blob32.new: $H Z+ $M` — a colon method-arg
   is now parsed as ONE expression down past the list infixes (Z/X are looser
   than comma) and a top-level comma ListExpr is splatted; `content 'a', $b`
   still splits. Both public and private (`self!m:`) colon sites.
4. **`( expr; )`** (trailing semicolon in parens) is the grouped VALUE, not a
   1-element list — Rakudo: `(5;)` is `5`. (Digest wraps its reduce in
   `( … ; )`.)
5. **Native-int array wraparound**: `uint32 @W.push(v)` masks v to 32 bits
   (Value::natWidthOfType + mask at push) — SHA1's `@W.push: S(...)` needs the
   overflow. blob32.new already low-masks each word.
6. **Blob is not Stringy** (batch 11) is what stopped `samewith $str.encode`
   from looping — the blob8 multi now wins over the Str one.

Stale probes fixed (battery): Digest → Digest::SHA1 (real hex);
Digest::HMAC → named-arg call (the old positional form fails on Rakudo too).

Tier-2 **30/50**. Honest remaining: Digest::HMAC still DIFFs — SHA1 is now
correct but HMAC's own blob key/msg XOR padding gives
`73752fe1…` vs Rakudo `102900b7…` (a separate blob-op bug, batch 12). PDF::Lite
needs the PDF dist vendored; JSON::Class needs AttrX::Mooish + MOP work.

## Batch 11c — CORRECTION: the DIFFs are not "rakupp-correct, Rakudo-sandbox-fails"

Earlier entries claimed the rk=[] DIFFs were Rakudo dying "in the sandbox"
while rakupp "produces correct output." **That was wrong and unverified.** On
2026-07-23 each rp-non-empty / rk-empty probe was re-run under REAL unsandboxed
Rakudo (same vendored dists, comma-form RAKULIB). Rakudo fails for concrete,
legitimate reasons; rakupp only "succeeds" by silently ignoring missing
modules (`use ignored`), so its output was never compared to a real reference.

Real causes, by module:
- **Missing transitive dep, not vendored in the battery** — Rakudo correctly
  refuses to compile: HTTP::UserAgent→Encode, Config→Hash::Merge→IO::Glob→
  IO::Path::XDG→Log, Sparrow6→Hash::Merge, Test::META→AttrX::Mooish,
  Test::Output→Trap, Date::Calendar::Strftime→Date::Names→Abbreviations.
  rakupp ignores ALL of these and prints an answer anyway.
- **Stale/wrong probe** — the probe calls a routine the module doesn't export;
  Rakudo errors at compile, rakupp swallows it: Shell::Command, Terminal::ANSI
  (`&color` undeclared), File::Find (probe runs against a nonexistent `lib`
  dir — Rakudo throws, rakupp returns True by being too lenient).
- **Native library** — Math::Libgsl::Constants (gsl_version symbol),
  Digest::SHA256::Native (compute_sha256): neither engine can run without the
  C lib built.
- **Module-internal failure on current Rakudo** — OpenSSL / IO::Socket::SSL:
  `No such method 'slurp' for invocant of type 'Slip'` inside the old
  OpenSSL::NativeLib; rakupp is lenient where Rakudo is strict.

Proof the leniency masks bugs: vendoring `Encode` let HTTP::UserAgent's load
COMPLETE under rakupp — which then exposed a real `No such method 'new'` that
the broken partial-load had hidden. So the pre-correction "rp=[HTTP::UserAgent]"
was an artifact, not a correct answer.

**Verified truth: 30/50 are genuine byte-identical** (harness MATCH = both
engines non-empty AND equal). The other 20 are NOT rakupp wins; they are
mostly battery-completeness gaps plus a few stale probes and rakupp
leniency-masks-bug cases.

Honest path forward for the DIFFs (started, not finished): vendor the missing
pure-Raku deps so BOTH engines compile, then compare for real. Fetched into
dists/ (NOT yet wired into harness/tier3-modules.tsv, so the sandbox run is
unchanged): Hash::Merge 2.0.0, Encode 0.0.4, Trap 0.0.5, AttrX::Mooish 1.0.10.
Config still needs IO::Glob+IO::Path::XDG+Log; Date::Names needs Abbreviations.
Also worth doing: make rakupp's "could not find module" a HARD error under a
strict flag, so these masked failures surface instead of producing phantom
output.

## 2026-07-31 — File::Find and Terminal::ANSI cleared under the zef bar

Two more distributions pass their own suites, taking the bar from 16 to 18.
Every fix below is a general interpreter fix; none touches the modules.

**File::Find** (0/1 → 1/1, 29/29 assertions). Three gaps, in the order the
suite hit them:

- `symlink` / `link` did not exist as SUBS. Added; `readlink` deliberately
  stayed a method only, which is where Rakudo has it.
- `symlink` must ABSOLUTIZE its target. A relative target is read by the OS
  relative to the LINK's directory, not the cwd, so the suite's
  `symlink("t/dir1/another_dir", "t/dir2/symdir")` produced a dangling link and
  the whole symlink section silently took its "this OS cannot" branch — four
  tests passing vacuously while Rakudo tested the real thing.
- The `X::IO` exception family could not be CONSTRUCTED. rakupp threw those
  types from its own IO builtins but had no classes behind them, so the suite's
  `X::IO::Dir.new(path => …, os-error => …).throw` — how it mocks a directory
  error — died. All thirteen now exist and compose Rakudo's message text.

A fourth fix came out of the same file: `&dir.wrap(…)` had no effect, because
`&builtin` minted a fresh Callable per evaluation AND a bare call reaches the
builtin through a table lookup that consults no Callable at all.

**Terminal::ANSI** (2/8 → 8/8). Five gaps, each one uncovering the next:

- `unit monitor Foo;` did not parse (only the block form of the declarator).
- `method FALLBACK` was unimplemented.
- A sigilless `constant` parsed as a listop, so `CSI ~ $str` died "Undefined
  routine 'CSI'".
- `@a[1^..3]` ignored the exclusive START (the literal-range subscript path read
  `..^` only).
- Slice assignment DEEP-flattened its right-hand side, so the module's
  `@!chars[$top..$bot] = (|@!chars[$top^..$bot], Nil)` spread a screen row's
  characters across several row slots instead of moving whole rows.
- Nil stored into a container element did not restore the element default, so
  the blanked row rendered as nothing rather than a space.

Method note, again: the last two only became findable by INSTRUMENTING the
module — dumping the screen buffer from both engines after the same calls —
not by reconstructing the expression. Three reconstructions in a row (slip
flattening, exclusive ranges, sparse rows) all reproduced clean.

Two divergences found here and deliberately NOT fixed, both about itemization
rather than these modules:

- `my $r = 1^..3; @a[$r]` — Rakudo treats the itemized Range as ONE index
  (numifying it), rakupp slices with it.
- `Any[1..3]` — Rakudo raises, rakupp answers a slice of undefined values.

## 2026-08-02 — the modinfo showcase: 17 distributions in one program

`showcase/modinfo/` is an ecosystem app rather than a probe: a distribution
inspector (dependency graph, META validation, SHA-1 fingerprints, table/JSON/
YAML/XML reports) whose every layer below the argument parser is an ecosystem
module. Building it surfaced **sixteen general interpreter bugs**, none of them
about the modules themselves. All are fixed in `src/`; Roast 196,937→196,959
(baseline spread ±6, so a small net gain), `t/run.raku` 271/271.

The app is byte-identical under Rakudo and rakupp across all thirteen commands
(`showcase/modinfo/compare.sh`), on the bundled fixture corpus AND on the
battery's 61 real distributions.

Bugs found, in the order the program hit them:

1. **`<# a b>`** — `#` started a comment inside a bare `< … >` word list, so the
   closing `>` and the rest of the line were swallowed. Rakudo reads it as the
   three words `#`, `a`, `b`. (Lexer: skip the comment branch while
   `angleWords_ > 0`.)
2. **`ª`, `µ`, `º` are letters** — the identifier whitelist started at U+00C0,
   so the three Latin-1 letters below it were rejected. Font::AFM keys its
   encoding table on them (`:ª("ordfeminine")`). Same for the **Lm runs of the
   Spacing Modifier Letters block** (ˆ ˇ) — added exactly the Lm sub-ranges, not
   the Sk ones.
3. **`my ::?CLASS:U $x`** — signatures accepted `::?CLASS`; declarations did
   not, and stopped at the `::`. Also skips the `:D`/`:U` smiley after either
   `::T` form.
4. **`/a/ ff /b/`** — a `/` after the flip-flop operator was read as division,
   not as a regex. All eight spellings added to the regex-context keyword set.
5. **`method dispatch:<.?>`** — rejected as an unknown operator category. Rakudo
   accepts the `dispatch` category on a METHOD (and rejects it on a sub, which
   rakupp still does).
6. **`%h.Hash`** — only `.hash` existed. A Hash answers itself; a Map answers a
   mutable copy.
7. **A `%h` / `@a` parameter is a TYPE CONSTRAINT.** Multi dispatch ignored the
   sigil entirely, so `multi f(%d)`, `multi f(@p)` and `multi f($s)` were
   indistinguishable and whichever was declared first took every call. This is
   why `Config.new.read(%data)` routed a Hash into Config's `IO() $path` file
   candidate. The sigil now scores above a blanket coercion.
8. **A bare `proto`/`multi` in a class body is a SUB**, not a method — only
   `multi method`/`submethod` declares one. `proto glob(|) is export {*}` inside
   `unit class IO::Glob` was becoming an unreachable method, so `glob` never
   reached the importer.
9. **`with A {…} elsif B {…} else {…}`** — with/without take part in the
   if-chain; only the `orwith` continuation was handled.
10. **Named destructuring from a CAPTURE** — `my (:$path, :@globbers) :=
    @list.shift` read Hash right-hand sides only, so every name bound Any.
11. **`$x ~~ $obj` never called the object's `ACCEPTS`** — the hook every
    matcher type in Raku is built on. Added for `~~` and for the
    `.grep`/`.first` matcher path; `$x ~~ SomeClass` stays a type check.
12. **`make` inside a protoregex candidate was stolen by `<sym>`.** A built-in
    assertion has no rule pattern to compare against, so the FIFO fallback handed
    it the candidate's block and the candidate got none — `$<match>.made` came
    back empty for every `token x:sym<y> { <sym> { make … } }`.
13. **A nested type named by a partial path** — `Globber::Match` inside
    `unit class IO::Glob` is `IO::Glob::Globber::Match`. The class registry is
    flat, so a qualified name now also resolves against a unique registered
    suffix. Bare names deliberately do not.
14. **Regex composition.** `rx/$base$tail/` where the variables hold REGEXES
    spliced them as literal TEXT. Now a regex value splices as a sub-pattern, and
    a pattern that composes another regex is baked at `rx//` construction time —
    deferring it re-reads the names in the scope of the eventual match, which
    breaks the moment the result is fed back into the same variable, as IO::Glob
    does when it folds a glob's terms into one matcher.
15. **`.split(/$d+/)` never interpolated** — the regex-method path compiled the
    raw source. The `$var` resolution is now one shared routine used by `~~` and
    by every builtin that compiles a pattern.
16. **`.dir(:test)` ignored its matcher** in the METHOD form (the sub form
    honoured it). Also: an exported **sigilless `constant`** was invisible to the
    importing file's parser (`SPACE ~ $word` parsed as a listop call) — see the
    correction below, the first fix for this covered only half the cases;
    **`sub rule(…)`** was
    lexed as a grammar rule declaration, swallowing the routine and everything
    after it; **`[|@a, $x]`** did not flatten the slip; and a hash composer with
    a COMPUTED key (`{ $d.name => True }`) parsed as a block.

The last one is the cautionary tale of the batch: the first version of the
composed-key heuristic accepted any `=>` ahead of the closing brace, which turned
`dies-ok { dt month => 0 }` into a hash and cost 486 Roast assertions. The rule
that works is strictly a postfix chain — a term, then `.name`/`!name`/subscripts,
then the arrow. Two terms in a row is a listop call, not a hash.

Battery note: `Font::AFM 1.24.10` and `AlgorithmsIT 0.0.4` were vendored (both
pure Raku, no further deps) — Text::Utils needs both, and neither is in the
top-50 set.

**Scope note, measured 2026-08-02.** "modinfo runs byte-identically" is not the
same as "these 17 distributions work." Of the 17, **14 needed no fixes at all**
(JSON::Fast, YAMLish, XML, Hash::Merge, the four File::* dists, Digest,
MIME::Base64, Abbreviations, Terminal::ANSIColor, Color, Data::Dump). Nine of
the sixteen bugs belong to **IO::Glob** alone, four to **Font::AFM** (pulled in
by Text::Utils), one to **Config**; the rest were hit by modinfo's own code.

And IO::Glob is still not finished. Its own suite, run against the vendored
copy under both engines:

| file | Rakudo | before | after |
|---|--:|--:|--:|
| absolute.t | 4 | 4 | 4 |
| dir.t | 19 | 0 | 4 |
| double-star.t | 6 | 0 | 2 |
| iterator.t | 9 | 0 | 0 |
| path-dir.t | 13 | 0 | 6 |
| smart-match.t | 21 | 0 | **21** |
| sqlish.t | 18 | 0 | 13 |
| **total** | **90** | **4** | **50** |

smart-match.t is complete (the ACCEPTS + regex-composition work). The gap is
the recursive `**` descent, the iterator protocol, and the rest of `dir`, which
modinfo never exercises — it asks for `*/META6.json` and a flat `*` and nothing
else. Next IO::Glob batch starts at iterator.t.

## 2026-08-02 — CORRECTION: the parser could not see INSTALLED modules

The sigilless-`constant` fix above (finding 16) was tested only against modules
on a lib path — the vendored battery, reached through `RAKULIB`. Run the same
program against the same modules **installed by zef**, which is the ordinary
case, and it still failed:

    $ rakupp showcase/modinfo/modinfo.raku about
    Undefined routine 'SPACE'

Two resolvers existed and only one of them knew about installed distributions.
`findModuleSourceFor` (the loader) searches the lib path and *then* the
installed CompUnit repositories — `short/<sha1(name)>` for the dist entry, then
`sources/<sha1>` for the text. `Parser::scanModuleOps` — which is what reads a
`use`d module at parse time to learn its operators and its exported sigilless
constants — had a hand-rolled copy of only the lib-path half. So for an
installed `Text::Utils`, the parser never found `Text::Utils::Vars`, never
learned that `SPACE` is a term, and compiled `SPACE ~ $word` as a call.

Fixed by deleting the duplicate: the loader's resolver is now exported as
`rakuppFindModuleSource` and the parser calls it, so both paths resolve a
module the same way. Gate: Roast 196,965, `t/run.raku` 272/272, perf-guard OK,
and modinfo is byte-identical across both engines **on the zef-installed
modules**, not only on the vendored copies.

The lesson is the one the duplication audit already made: a second
implementation of "find a module" will drift from the first, and the half that
gets exercised in testing is not the half users hit. It also says something
about how the showcase was verified — every run in the session that built it
set `RAKULIB` at the vendored dists, so the ordinary invocation was never
actually tried until a user typed it.

## 2026-08-02 — two more, both found by just running the programs

**`sub f(--> num)` died "Type 'num' is not declared".** The return-type check
consulted only the BOXED type names; the native lowercase ones (`int`, `num`,
`str`, `int32`, `num64`, `byte`, `atomicint`, …) were absent, so every native
return constraint threw the moment the routine returned a value — an empty body
never reached the check, which is why the declaration alone looked fine. The
parameter-binding path had the correct set all along, in a private static
copy with a comment explaining that natives are resolvable even though
`isKnownTypeName` omits them. Fixed by promoting that set to a shared
`isNativeTypeName()` and calling it from both places. (Second duplicate-resolver
bug of the day; see the correction above.)

**`$*REPO.repo-chain` reported one repository.** It answered the single link it
was called on, and `$*REPO` itself is only `~/.raku` — so a program that walks
the chain to enumerate installed distributions found whatever the user had
installed for themselves and silently missed everything zef had put in `site`.
Rakudo reports home, site, vendor, core. rakupp already computed home/site/vendor
internally in `rakuRepoPrefixes()` for its own `use` resolution; the chain now
reports that list, plus `core` where it exists on disk.

`core` is REPORTED but deliberately not added to `rakuRepoPrefixes()`: rakupp
answers the core types from its own builtins rather than from Rakudo's sources,
so putting core on the resolution path would change what `use` finds. The chain
is a description of the installation, which is a different question from what
this engine chooses to search.

Both surfaced the same way — a program was run the ordinary way, not the way the
test harness runs it. `showcase/modinfo --installed` now agrees byte-for-byte
with Rakudo across `list`, `graph` and `rank` on this machine's 36 installed
distributions.

## 2026-08-02 — URI: 88 → 136 of 222, seven general fixes

`HTTP::Simple` is to depend on `URI`, so URI's own suite came first. Under
rakupp it passed 88 of the 222 assertions Rakudo passes; it now passes 136.
Every fix is a language fix — URI is untouched.

1. **`<- [a..z]>` — a blank between the sign and the bracket.** Whitespace is
   insignificant in a regex, but the negated-class entry test required `-` and
   `[` to be adjacent. `<- [x]>` was therefore not recognised as a character
   class at all, fell through to the assertion branches, and matched empty at
   every BYTE position. `URI::Escape` writes exactly that class, so nothing was
   percent-encoded and non-ASCII paths came back raw.
2. **`<Grammar::rule>` in a plain regex.** A qualified rule reference resolved
   to nothing and took the lenient zero-width branch. URI declares
   `subset Scheme of Str where /^ [ '' || <IETF::RFC_Grammar::URI::scheme> ] $/`,
   so the subset accepted only the empty string. The plain-regex resolver now
   splits the qualified name and matches through the grammar machinery.
3. **An explicit `''` is a zero-width ATOM, not "nothing parsed".** Both
   produced an empty Seq, and parseAlt drops empty branches (correctly — `[ | A ]`
   is cosmetic). So `[ '' || 'zz' ]` lost its first alternative and could not
   match the empty string.
4. **`our subset` crossed a module boundary as a bare type.** Subsets registered
   only under their short name, so an importer writing `URI::Scheme` got a type
   object with no `where` — `~~` was False for every value. They now register
   under the package-qualified name too, as classes already did.
5. **A regex matched against an OBJECT did not stringify it.** `$path ~~ /…/`
   where `$path` is a `URI::Path` compared against the object, not its `.Str`.
   Fixed at every subject position (`~~`, `when`, `.match`, `.subst`, `.comb`,
   `.split`) through one shared `rxSubject` helper.
6. **A `my regex` in a CLASS body was parsed and silently DROPPED.** The
   class-body loop keeps only whitelisted statement kinds, and NamedRegexDecl
   was not among them. URI declares `my regex path-authority { … }` after
   `unit class URI` and matches against it in `!check-path`, so every path
   validation failed with "Could not parse path". (Same whitelist that needed
   SubsetDecl for Color and UseStmt for URI itself — third time.)
7. **`<+:S>` — a Unicode property as a COMPOSED class member — was dropped.**
   The code approximated `:N` as digit and `:L` as alpha and silently discarded
   every other property, with a comment saying "ASCII-liberal is fine". It is
   not: `<+uri-alpha +:N +:S>` is how the RFC grammar admits symbols, so
   `http://host/echo2/☃` would not parse. Properties are now carried as real
   `uprop` members and folded into the same alternation the `+rule` members use.

**A regression this batch caused, and what it exposed.** Fix 2 made YAMLish's
`multi to-yaml(Str:D $d where /^ <!Schema::Core::element> …/)` candidate become
selectable for the first time, and modinfo's YAML export stopped matching
Rakudo — scalars came out unquoted. The cause was not the `where` but named
binding: **a routine that does not declare a named parameter cannot take one**,
and rakupp accepted them silently, so the bare-scalar candidate was winning
calls that thread `:sorted`. Dispatch now rejects an undeclared named — except
where Raku says otherwise: `*%rest`, a `|c` capture, and **methods**, which
carry an implicit `*%_`. URI's `multi method new` relies on that last one for
its legacy `:validating` adverb, and getting it wrong cost 3 assertions and 68
Roast tests before the exemption went in.

Gate: Roast 196,937 → **197,004**, `t/run.raku` 273/273, perf-guard OK, modinfo
byte-identical on both engines. Remaining URI gaps, in size order: query (16),
november-urlencoded (12), directory (8), path (8), mutate (7), escape (5),
missing-components (4), 01 (23 — mostly `.query` and `.segments` behaviour).

## 2026-08-02 (cont) — URI 188 → 205, six more

8. **A grammar RULE is callable as a method.** `G.new.some-rule` with no
   argument runs the rule on an EMPTY cursor and hands back that
   failed-or-successful Cursor; smart-matching a string against it yields the
   cursor's own truthiness, whatever the string. URI's suite asserts exactly
   this (`nok 'foo' ~~ …TOP-non-empty`, `ok '#foo' ~~ …URI-reference` — the
   difference is whether the rule matches ""). rakupp threw "No such method".
   First implemented as an anchored match of the rule against the operand,
   which looked more sensible and was wrong on two of the four assertions —
   measuring Rakudo's actual answers is what settled it.
9. **Proxy is a container, so everything that RENDERS or COMPARES a value must
   read it.** `strOf`, `gistOf`, `rakuRepr`, `valueEqv`, `deepEq` and Test's
   `is` each needed it, including for a Proxy nested in a list. `rakuRepr` is a
   free function with no interpreter to call FETCH with, so the interpreter now
   publishes a `g_deproxy` hook, mirroring the existing `g_subsetCheck`.
   URI::Query returns lists of Proxy containers to keep them immutable.
10. **`$obj<k> = v` never called `ASSIGN-KEY`.** A class implementing the
    container protocol got its reads dispatched (AT-KEY/EXISTS-KEY/DELETE-KEY)
    but not its writes — the assignment fell through to the generic container
    path and silently did nothing, so every URI::Query mutation was a no-op.
11. **`$.name` is `self.name` — a method call, always.** The attribute read is
    only a shortcut for the generated accessor, and it was winning over a real
    method of the same name. URI::Query has a private `$!query` cache beside
    `multi method query`, so `$.query` returned the stale cache the method
    exists to recompute — and after a mutation cleared it, the empty string.
    This one is worth 10 assertions on its own.

Gate: Roast 197,010, `t/run.raku` 273/273, perf-guard OK (fib −12%, asg −12%),
modinfo byte-identical.

**Still open, 17 assertions in 5 files.** `path`/`mutate` (10): `.segments`
comes back `["", ""]` after a path mutation. `query` (4): `$q<foo> = '5', '6'`
keeps only the first element — a list on the right of a subscript assignment.
`authority` (2): an authority-less URI gists `foo://` where Rakudo gives `foo:`.
`escape` (1): `uri-escape(Any)` should return Any.

## 2026-08-02 (cont) — URI 205 → 207, and where it stops

12. **A qualified rule's own captures now come back.** The resolver returned only
    the matched EXTENT, so everything the grammar rule captured inside was
    thrown away. URI::Path reads `$path<segment>` / `$path<segment-nz>` off
    exactly that match to build its segment list, so every mutated path had one
    empty segment. The Match value is now walked back into span form, one level
    of children at a time, with quantified captures kept list-valued.
13. **`@a := <a List>` BINDS — the List stays a List.** The bind path cleared the
    list flag to stop the bound items being flattened together, which also
    demoted a genuine List to an Array. Only a non-List Array is demoted now.
    `URI::Path` binds `@!segments := @segments.List` precisely so the list is
    immutable, and `is-deeply` against `('x','y','z')` compares the type.
14. **An undefined value satisfies no specific type.** `Any` conforms to `Any`
    and `Mu` only, but the type test answered a blanket true for it, so it could
    win a candidate written for a real value: `uri-escape(Any)` picked the
    `Match $s` overload and returned `""` where Rakudo returns `Any`.

**Where this stops: 15 assertions, four independent causes.**

- `path` (5), `escape` (1): a TYPE OBJECT still satisfies any constraint —
  `uri-escape(Str)` picks the `Match $s` overload. The `~~` operator already has
  full type-object conformance (the `typeDoes` table, the numeric/string tower
  and the class ancestry walk, around Interpreter.cpp:11790); dispatch has its
  own, laxer test. Factoring the two together is the fix, and is the right size
  for its own batch rather than the tail of this one.
- `query` (4): `$q<foo> = '5', '6'` keeps only the first element. A list on the
  right of a subscript assignment reaches ASSIGN-KEY as its first item.
- `mutate` (3): a query mutated to a True value re-serialises its old pairs.
- `authority` (2): an authority-less URI gists `foo://` where Rakudo gives `foo:`.

Net for the day: URI 88 → 207 of 222, fourteen general interpreter fixes, no
module touched.

## 2026-08-02 — URI is COMPLETE: 222 of 222

The last batch, and the one the earlier note said deserved its own:

15. **One answer for type conformance.** `~~` knew the whole story — a "does"
    table, the numeric/string tower, the user class/role ancestry walk — while
    parameter dispatch had its own, laxer test that returned a blanket true for
    any type object. Extracted as `typeNameConforms` and called from both, with
    dispatch staying lenient only when one of the two names is not a type we
    know (an unregistered user type must not be dispatched away on our
    ignorance). Third duplicate-resolver bug of the day, and the pattern is now
    unmistakable.
16. **A typed attribute resets to its TYPE, not to Any.** `$!authority = Nil`
    left bare `Any`, so the very next `$!authority .= new(…)` had nothing to
    call `new` on. Attributes are not env vars, so the existing per-variable
    default search never saw them.
17. **A coercion parameter is the LEAST specific match.** `Str() $x` scored the
    same as an exact nominal type, so `multi method authority(Str() $a)` tied
    with `multi method authority(Nil)` and won on declaration order —
    `.authority(Nil)` could never clear the authority. Accepting anything
    convertible now scores nothing of its own.
18. **EVERY subscript target takes the whole comma list**, not just a slice:
    `%h<k> = 1, 2` stores `$(1, 2)`, unlike `my $x = 1, 2`, which is item
    assignment and warns. This is plain Raku and was wrong for ordinary hashes
    and arrays too, not only for URI::Query's `$q<foo> = '5', '6'`.
19. **ASSIGN-KEY reached through a method call.** `$u.query<foo> = True` has a
    method call as the subscript base; only variables and `self` were handled.
20. **A Hash flattens to its Pairs, so an EMPTY one contributes nothing.**
    `flat %new, @new` pushed the hash itself, handing a Hash to code expecting a
    Pair ("No such method 'value'") — URI's `*@new, *%bad` query setter called
    with no named arguments.

**URI 0.3.8 now passes 222 of 222 assertions under rakupp, all 14 files.**
Twenty general interpreter fixes got it there over the day, from 88. Not one
line of URI was touched, and Roast rose across the whole run: 196,937 →
197,023. `t/run.raku` 273/273, perf-guard OK, modinfo still byte-identical.

The prerequisite for `HTTP::Simple` is met.

## 2026-08-02 — HTTP::Simple v0.0.1, and two more general fixes

The first module written in [ash/raku-modules](https://github.com/ash/raku-modules)
rather than borrowed from the ecosystem. It is a real client — the seven
methods, query and form encoding, JSON, basic and bearer auth, redirects with
history and RFC method rewriting, connect and total timeouts, chunked decoding,
a cookie jar, and opt-in retries — and its 81 assertions pass identically under
Rakudo and rakupp, against an HTTP server the test suite runs in-process.

Writing it walked into two engine bugs, both found the same way as everything
else this campaign: run the real code, read the real error.

21. **`next without $x` read `without` as a loop label.** The loop controls
    accept an optional label (`last OUTER`), and the guard for it excluded
    `kBlockKeywords` — which does not contain `with`/`without`, because in every
    other position those start a term. So `next without $x` parsed as
    `next OUTER-style-label` plus a stray `$x` statement, and the NextEx carried
    a label no loop answered to: *"next without loop construct"*. The loop
    controls now consult their own `kStmtModifiers` list. Affects `last`, `next`
    and `redo`, with both `with` and `without`.
22. **`Buf.new($blob)` stored the blob's element COUNT.** A Blob is Positional,
    and the constructor's flattener knew about Array and Range but not about a
    Buf/Blob, so it fell through to `toInt()` — `Buf.new(Buf.new(1,2,3))` came
    out as `Buf.new(3)`. Silent, and exactly the shape that eats an HTTP body.
    Measured against Rakudo rather than assumed: a Blob is accepted only as the
    *sole* argument (the copy candidate); mixed in with plain bytes it is a type
    error, and rakupp now says so too.

Not an engine bug, but worth recording next to them, because it is a trap the
module hit first and the test server hit second: **`"\r\n"` is a single grapheme
in a Raku string**, so a character offset from `.index("\r\n\r\n")` is out of
step with the wire by one position per CRLF. Splitting an HTTP response has to
be done in bytes. Both engines agree; the bug was mine.

Pinned in `t/regression/http-simple-cluster.raku`. Roast 197,023 → 197,025,
`t/run.raku` 275/275, modinfo still byte-identical.

The `https` path is the one part with no test behind it:
`IO::Socket::Async::SSL` is not installed on either engine here, so the client
`require`s it on demand and throws a clear transport error when it is absent,
instead of failing to load at all.

## 2026-08-03 — Digest, and seven fixes behind one wrong hash

`Digest::HMAC` builds every HMAC out of `Blob ~^ Blob`, and `Digest::SHA2` keeps
its whole SHA-256 message schedule in a `state buf32` inside a `reduce`. Between
them they were sitting on seven separate interpreter bugs, none of which is
about hashing. Found by triage rather than by reading: `tier2/triage.raku` in
the battery runs every test file under both engines and clusters the failures by
the FIRST thing rakupp said, so one cause showing up in several distributions is
visible immediately.

23. **`Buf ~^ Buf` answered a `Str`.** The bytes were already right — the earlier
    note in this file said "and gets the wrong bytes", which was wrong. Only the
    TYPE was lost, and that was enough: the padded key went into the hash
    function as a string. The result now carries the LEFT operand's exact type
    (`Blob ~^ Buf` is a Blob, `utf8 ~^ Buf` a utf8, kept in `enumName`), and
    mixing a buffer with a string throws, as Rakudo does in either order.
24. **A type before the parenthesis did not reach the variables.**
    `my uint32 ($T1, $T2)` kept only a per-item type, so the list form declared
    untyped scalars — `my Int ($a, $b)` too. Rakudo also refuses a per-item type
    when the list already has one; we accept it, which is laxer and left alone.
25. **List assignment discarded the native width**, overwriting the container
    instead of storing through it, so even a correctly-typed `my uint32 ($a, $b)
    = …` did not wrap.
26. **`$buf[i] = v` replaced the whole buffer with an Array.** A Buf is a packed
    byte string, so there is no element `Value*` to hand back as an lvalue and
    the generic index path simply overwrote it. Handled in the assignment path
    now, beside `subbuf-rw`: little-endian, truncating to the element width,
    growing with zeroes, and refusing a Blob because a Blob is immutable.
27. **`state $x .= new` re-initialized on every execution.** This is the one that
    made SHA-256 wrong. `state $x = …` was already once-only; `.=` was not, so
    each pass called `.new` on the value the previous pass had left, and
    `(state buf32 $w .= new)` became a plain `Str` on the second iteration. Every
    digest was correct for fifteen rounds and wrong from the sixteenth — the
    first round that reads the schedule back.
28. **Radix literals wider than a long long saturated.** `0xFFFF_FFFF_FFFF_FFFF`
    parsed as 9223372036854775807. The decimal path already promoted to bigint;
    hex, binary and octal went through `strtoll` and clamped.
29. **`:16("FFFFFFFFFFFFFFFF")` answered −1**, accumulating in a `long long`.
    Now a bigint, like the `:16<…>` angle form beside it always was.

**SHA-256 is byte-identical to Rakudo.** `Digest::HMAC` passes its own suite.
Roast 197,060 → 197,074 with no file losing anything, and every gain in the
affected area: S02-types/signed-unsigned-native.t +6, my-6c.t +3, buf.t,
bit.t and native.t +1 each. `t/run.raku` 281/281, pinned in
`t/regression/digest-native-widths-cluster.raku` (which passes under Rakudo).

Two more came out of the 64-bit half straight after:

30. **A 64-bit blob element read back SIGNED.** `blobWordAt` returns a
    `long long`, so a word with its top bit set came out negative — `blob64`
    answered −1 where the value is 18446744073709551615. Every call site now
    goes through `blobElemAt`, which promotes exactly that case to a bigint.
31. **`.polymod` saturated its invocant**, taking `toInt()` on a bigint, so
    `0xFFFF_FFFF_FFFF_FFFF.polymod(256 xx 7)` answered a leading `0x7F` instead
    of `0xFF`. The finite-divisor branch divides in BigInt now; the lazy-divisor
    branch still works in `long long` and wants the same treatment when
    something needs it.

**SHA-224 joined SHA-256** in matching Rakudo byte for byte.

Chasing SHA-512 further turned up two more, both in argument passing rather than
in arithmetic, and both found the same way — instrument the real module, print
what the block actually contains:

32. **`|$x` slipped one level too deep when the value was itemized.** A list held
    in a scalar is itemized by definition, and the one-level splice excluded that
    case, falling through to a RECURSIVE `flatten()`. So
    `my $s = ((1,2),(3,4)); ("A", |$s)` gave five elements where Rakudo gives
    three.
33. **`reduce` deep-flattened every argument** instead of following the one-arg
    rule. Measured against Rakudo: a SINGLE Positional argument spreads
    (`reduce &f, @a`, `reduce &f, 1..4`), several arguments are taken as they are
    (`reduce &f, "I", (1,2), (3,4)` folds over three items, two of them Lists).
    Together these two are why `$block[$t]` was undefined for every `t` but 0 —
    the 16-word block reached the fold as sixteen separate values.

Both verified directly against Rakudo, and Roast-neutral: 197,076 -> 197,079 and
633 -> 634 files, where the whole delta is `S17-channel/stress.t` recovering
from a timeout and the two known random files (`S17-supply/lines.t`,
`integration/advent2012-day13.t`, whose "weighted roll" test flaps on its own).

**Still open:** SHA-512 and SHA-384 remain wrong. The digest changed again with
these two fixes, so the block now arrives intact and something further in is
still off — the `blob64`-typed `reduce` parameter and the untyped `rotr` in that
half of the module are the next places to look. SHA-256 and SHA-224 stay
byte-identical throughout. Also noticed and NOT fixed: rakupp reports `Buf` where
Rakudo reports `Buf[uint32]`, and Rakudo answers the UNTRUNCATED value from
`$buf[i] = v` while storing the truncated one. Neither affects a digest.

## 2026-08-04 — the inside-out view, and DBIish 1 -> 28

Before fixing anything else, the 19 remaining DIFF distributions were triaged by
CAUSE rather than by distribution (`tier2/triage.raku` in the battery, which now
keeps looking past a `not ok` for a real exception and decodes test output as
utf8-c8 — a Digest test emits junk bytes that killed the run outright).

**126 failing files across 19 distributions, and they concentrate hard.** Nine
distributions fail on one or two causes between them; the other ten hold about
fifty distinct causes. The reachable ceiling is 52, not 59: six distributions are
`ENV` (Rakudo itself passes nothing) and one ships no tests.

The cheap tier, ranked:

| dist | files | the single cause |
|---|--:|---|
| DBIish | 14 | `Rakudo::Internals.REGISTER-DYNAMIC` missing |
| LWP::Simple | 10 of 11 | `.Stringy` on an enum (`RequestType`) |
| HTTP::Tiny | 6 of 8 | `Invalid HTTP proxy:` |
| Cro::HTTP | 2 | `Class '' cannot inherit from 'Supplier'` — an ANONYMOUS class |
| NativeHelpers::Blob | 3 | `add_method` on a Pointer, `array_type` on utf8 |

and four one-file distributions: JSON::Tiny (a UTF-16 surrogate pair),
DateTime::Format (RFC 2822 with a timezone), NativeHelpers::Array (wrong type
back), Date::Calendar::Strftime (a deprecation warning we emit and Rakudo does
not). Cross-cutting: `Too many levels of recursion` (Cro::Core + Config) and
`.backtrace` on an Exception (Log::Async, 6 of its 12 files).

Deliberately NOT chased: AttrX::Mooish is the biggest single block at 23 files,
but across TWELVE causes, mostly its own generated accessors — a deep MOP
feature, not a fruit. Log::Async (7 causes) and JSON::Fast (8, all different)
are the same shape.

### DBIish: three fixes, 1 -> 28 of 37

Taking the top item turned out to be three bugs stacked, each hidden behind the
last — which is the standing lesson of this file, and the reason the ranking
above is a bet rather than a promise.

34. **`Rakudo::Internals.REGISTER-DYNAMIC` did not exist**, so DBIish died at its
    first `use`. It is the initializer a module supplies for a process-wide
    dynamic it owns. Rakudo defers the block to the variable's first lookup; we
    run it at registration, which needs no hook in every lookup path and differs
    only in WHEN. An existing binding is left alone rather than overwritten.
35. **`PROCESS::<$x> = …` never reached the process scope from inside a routine.**
    The parser rewrites the qualified name to the bare `$*x` and dropped the
    qualification, so the assignment made a fresh lexical in whatever frame ran
    it and the value vanished on return. Only a mainline assignment had ever
    worked. `VarExpr` now carries `processScoped` and the lvalue installs into
    the process scope. This is the general bug; REGISTER-DYNAMIC merely needed it.
36. **A sigilless loop variable in `while`.** `while self.row -> \r { … }` — only
    the sigilled form was accepted, so the `\` was left for the block parser and
    the whole module failed to parse. `until` shares the fix.

DBIish now passes **28 of its 37 files**, twice what Rakudo manages in this
sandbox (14), but it stays DIFF: two files Rakudo passes still fail, on
`Stub code executed` and `No such method 'convert' for invocant of type 'Hash'`.
The second is the interesting one — `role TypeConverter does Associative` with a
`handles`-delegating private hash dispatches as a Hash rather than as the
composing object. That is a role/Associative issue, not a database one.

Roast 197,079 -> 197,104, 634 files, no losses. The gains are nowhere near
DBIish: `S26-documentation/04a-input-output.t` 0/6 -> 6/6, `S05-mass/stdrules.t`
+7, `S05-mass/rx.t` +5, `S02-literals/quoting-unicode.t` 61/65 -> 65/65, and
`S05-interpolation/lexicals.t` emitting TAP at all for the first time — all of
them the PROCESS:: scoping fix. `S05-metasyntax/unicode-property-pair.t` reads
3/3 -> 2/6, which looked like a loss and is not: the pre-change binary fails that
same assertion, and the earlier 3/3 was a truncated run of the file.

### LWP::Simple: four fixes, 7 -> 18 of 18 — PASS

Second item off the cheap tier, and the first distribution to actually cross the
line this session. Like DBIish it was several bugs stacked, and again none of
them is about the thing the module does.

37. **`.Stringy` did not exist.** It is Mu's string coercion — `self.Str` — and
    died on every type, including the enum values LWP builds its request line
    from (`$rt.Stringy ~ " {$path} HTTP/1.1"`). Forwarded the way `.perl`
    already forwards to `.raku`, with the same escape: a class defining its own
    `method Stringy` keeps it. Roast's `S02-types/undefined-types.t` gained ten
    assertions from this alone.
38. **A listener could not bind by HOSTNAME.** `IO::Socket::INET.new(:listen,
    :localhost<localhost>, …)` fed the name straight to `inet_addr`, which
    answers INADDR_NONE, so the bind failed and `.new` returned Nil with nothing
    to say why. The CLIENT path had resolved names all along — the listener just
    never used the same helper.
39. **`.localport` / `.localhost` were missing.** The port a `:localport(0)`
    listener actually got is only knowable after bind, from the OS, and asking
    for port 0 is how a test avoids guessing a free one. `.localhost` answers the
    name as GIVEN rather than what it resolved to, which is what Rakudo reports.
40. **`.read($n)` returned whatever was in the first packet.** Rakudo's `.read`
    answers EXACTLY `$n` bytes, blocking until it has them or the peer closes;
    `.recv($n)` answers at most `$n`. One `recv()` served both. This is invisible
    until a message straddles a packet boundary — and then a chunked HTTP body
    fails to parse its own chunk header, which is precisely the two files LWP had
    left. Measured against Rakudo rather than assumed: a server writing 4 bytes,
    pausing, then writing 6 gives Rakudo 10 bytes from one `.read(10)` and gave
    us 4.

**LWP::Simple 18/18.** Roast 197,104 -> 197,122, 634 files, no losses:
`S02-types/undefined-types.t` 35/39 -> 45/49, and `S32-io/IO-Socket-INET.t`
emitting TAP at all for the first time (7/9). The socket fixes did NOT move
HTTP::Tiny, HTTP::UserAgent or the Cro pair — those fail on their own causes.
`t/run.raku` 284/284; the regression file passes under Rakudo.

### HTTP::Tiny: a statement prefix takes the whole expression — 2 -> 5 of 10

41. **`try EXPR or die MSG` parsed as `(try EXPR) or die MSG`**, so the `die`
    escaped the very `try` it was written inside. HTTP::Tiny validates a proxy
    it may not have with exactly that idiom (`try $http-proxy.&split-url or die
    "Invalid HTTP proxy: …"`), so **every** construction died.

    Measured against Rakudo rather than reasoned about, because the answer is
    less obvious than it looks: a statement prefix takes the WHOLE remaining
    expression — `do 1, 2` is `(1, 2)`, `do 0 or 5` is `5`, and `[try bad(), 2]`
    has ONE element. `do`, `gather`, `quietly` and `once` all behave the same, so
    the fix is to the shared parse site rather than to `try`.

HTTP::Tiny 2/10 -> 5/10, and Roast is unmoved (197,122 -> 197,121; the −1 is
`S17-promise/in.t` and `S32-list/pick.t`, the timing and random files, against
`S12-introspection/can.t` +1). Three files remain, on three separate causes:
`.cando` on a Method, `.message` on Any, and a destructuring signature in a
`while` pointy block (`while $c.receive -> ( :key($i), :value($res) )`) — that
last one needs `WhileStmt` to carry a signature rather than a single name, which
is a bigger change than it looks and is left for its own pass.

**Battery: 34 of 59** (LWP::Simple over the line), 18 DIFF, 6 ENV, 1 without
tests. The reachable ceiling is 52.

### Housekeeping, worth knowing

The machine had **133 unkillable `rakupp` processes** accumulated over several
days, every one of them the same NativeCall probe
(`sub strdup(int64) is native(Str) {*}; strdup(0)`), wedged in state `UE` —
uninterruptible kernel wait, trying to exit. `kill -9` does not clear them; only
a reboot will. Two things follow: a NativeCall call CAN wedge a process
permanently, which is a bug worth reproducing on its own; and any timing
measured on this machine while they sit there is suspect.

### Cro::HTTP and JSON::Tiny

42. **`class :: is Supplier {…}` was rejected as inheriting from an unknown
    TRAIT.** `Supplier` sits beside `Supply`, `Channel` and `Promise` in the
    known-type list and was simply missing, so a class naming it as a parent fell
    through to the user-trait path and died. Added with `Supplier::Preserving`
    and `Tap`. The anonymous-class part was never the problem — `class :: is
    SomeUserClass {…}` always worked.

    This unblocked Cro::HTTP's two HTTP/2 files, which now fail on frame CONTENT
    instead (`DATA Frame length cannot be less than padding length`) — deeper,
    and left alone.

43. **A single-quoted literal inside a regex was losing its backslashes.** In
    Raku the only escapes inside `'…'` are `\\` and `\'`; `'\n'` is
    backslash-then-n and `'\u'` is backslash-then-u. We applied the DOUBLE-quoted
    escape rules to both spellings, so the backslash was eaten. It hid well:
    `/ '\u' /` still "matched", because it had quietly become `/ u /`.

    JSON::Tiny spells its surrogate-pair rule `'u' <utf16_codepoint>+ % '\u'`.
    With the separator gone, each `\uXXXX` matched as a separate escape, so
    `𝒷` decoded as two lone surrogates instead of one astral
    character. **JSON::Tiny 5/6 -> 6/6, PASS** — one better than Rakudo manages
    here, which fails its own deprecation test.

Roast 197,121 -> 197,125, and 634 -> **635 files**:
`integration/advent2012-day10.t` goes 25/26 -> 26/26. No losses.

**Battery: 36 of 59.** The reachable ceiling is 52.
