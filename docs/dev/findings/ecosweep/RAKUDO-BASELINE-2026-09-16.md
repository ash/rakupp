# What Rakudo passes on this machine — the ceiling, measured

Every number this project has published about the ecosystem has been a pass
count against a denominator of 2,529, with no measurement of what the reference
engine itself passes on the same hardware. This is that measurement, and it
changes what the denominator should be.

## The numbers

All 1,540 distributions that rakupp does **not** pass, run under **Rakudo
2026.08** on the same machine, through the same harness — `tools/install.raku`,
the same verdict classification, the engine as the only variable.

| | |
|---|---|
| Rakudo passes, of what rakupp fails | **794** (51.6%) |
| rakupp passes | 997 |
| **ceiling on this machine** | **1,791 of 2,529** — 71% |
| unreachable by either engine | **738** |

Those 738 want libgsl, fontconfig, `/sbin/ldconfig`, a network, or are simply
broken. Publishing against 2,529 charges this engine for libraries the machine
does not have; **1,791 is the honest denominator**, and 997 of 1,791 is 56%
where 997 of 2,529 is 39%.

**1,791 is a FLOOR.** 146 rows (9.5%) came back `other` — the bucket the
classifier falls into when it recognizes nothing in the log — and hold no
verdict either way. Some of them are passes.

Rakudo's own verdicts on the 1,540:

| verdict | count |
|---|---|
| pass | 794 |
| self-fail | 413 |
| other | 146 |
| dep-fail | 80 |
| build-fail | 60 |
| dep-build-fail | 39 |
| timeout | 7 |
| fetch-fail | 1 |

## The queue this produces

`reachable-queue-2026-09-16.tsv` is the 794 — every distribution Rakudo passes
here and rakupp does not, carrying rakupp's own first error. That is the work
list: each one is *provably* reachable, so no time goes into a `libgsl` dead end.

Clustered by how rakupp fails:

| our failure mode | count |
|---|---|
| a plain assertion — a wrong VALUE, no error raised | **365** |
| other error | 178 |
| parse error | 100 |
| No such method | 70 |
| undefined routine / name | 39 |
| (no first error recorded) | 21 |
| type check | 19 |
| native library | 2 |

Nearly half answer something, and the answer is wrong. Nothing cheaper than
running the suite finds those — see [../../MODULE-HUNTING.md](../../MODULE-HUNTING.md),
which is why its stage 3 exists.

## How it was run

```bash
# a shim so the sweep's own harness runs under Rakudo unchanged
#   $1 == "test"; the rest is `--to=<store> <name>`
exec raku tools/install.raku --test-only "$@"

rakupp tools/eco-fresh/sweep-fresh.raku --store=<copy of the seeded store> \
    --logs=<dir> --out=<out.tsv> --timeout=180 --rakupp=<shim> <list.tsv>
```

Two shards under `nice`, each with its own copy of the seeded store (one shared
store races on installs). About five hours for 1,340; a 200-distribution sample
run first gave 48.5%, within the final figure's confidence band.

**This did not work before 2026-09-16.** `tools/install.raku` handed CompUnit an
`InstallableDist`, which this engine accepts and Rakudo refuses — it types the
parameter `Distribution` — so under Rakudo the installer could not install a
*dependency*, and every distribution needing one died before it was tested. The
first sample put **86 of 200 into `other`** and implied a ceiling ~250 too low,
which read as "1500 is the wall". Composing the role fixed it (commit fa85533).
At 43% of a sample, `other` is never a finding.

## Files

- `rakudo-baseline-2026-09-16.tsv` — all 1,540: `name rakupp rakudo first-error-rakupp`
- `reachable-queue-2026-09-16.tsv` — the 794 Rakudo passes and we do not
