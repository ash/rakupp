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
4. **JSON::Fast 0.19 does not parse under rakupp**: `Q/\u/` (slash-delimited
   raw quote) at Fast.pm6:130 — rakupp parse gap ("Confused (got '}')").
   The keystone module is blocked on this one construct. Also exposed that
   the Tier-3 harness must count module PARSE warnings as PARTIAL, not LOAD.
5. **rakupp `substr` on long strings is O(position)**: 20k one-char substrs
   near position 4M took 116 s (5.8 ms each) — codepoint scanning from the
   string start each call. Makes naive char-walking parsers quadratic; needs
   an ASCII/byte fast path or offset cache in rakupp. (JSONLite works around
   it by walking a .comb array.)
6. **rakupp RUN-MAIN doesn't val()-allomorph NAMED args**: `Int :$top` with
   `--top=10` dies X::TypeCheck::Binding (positionals are allomorphed;
   nameds arrive as plain Str).
