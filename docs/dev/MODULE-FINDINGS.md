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
