# docs/

Supplementary and historical documents. For the current state of Raku++, start
at the [top-level README](../README.md) and [GUIDE](../GUIDE.md).

- **[ROAST-GAPS.md](ROAST-GAPS.md)** — classification of everything that still
  blocks a full Roast pass (from a systematic scan of all failing files), with
  a suggested attack order.
- **[ROSETTACODE.md](ROSETTACODE.md)** — Raku++ vs Rakudo on real
  [RosettaCode](https://rosettacode.org/wiki/Category:Raku) programs: the
  `tools/rc-compare.raku` harness, results, and the gaps it surfaces.
- **[PLAN-gil-removal.md](PLAN-gil-removal.md)** — the design plan for removing the
  GIL and reaching true CPU parallelism (incremental steps, risks, status).
- **[JOURNEY.md](JOURNEY.md)** — a memoir of how Raku++ was built: the method, the
  loops, the clean-room stance on Rakudo, and reaching `--exe`. Historical, not
  maintained as current reference.
- **[CONFORMANCE.md](CONFORMANCE.md)** — a dated docs-conformance audit log
  (feature-by-feature against docs.raku.org), with per-pass Roast deltas.
  Historical, not maintained as current reference.
