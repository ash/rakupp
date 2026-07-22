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
4c. **JSON::Fast now runs its real parser** but has residual nqp-op edge bugs
   (2026-07-23): `\u0041` escape decode ("invalid hexadecimal char") and a
   value-path off-by-something on the qualified entry — likely native `str`/
   `int` typed locals interacting with our ops, or an ordat/substr boundary.
   Next module leg: trace from-json on `[1,2]` + `"x\u0041"` and fix the
   specific ops; the machinery is all in place.
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
