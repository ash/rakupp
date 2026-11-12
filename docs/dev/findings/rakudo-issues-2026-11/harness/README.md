# harness/ — how the 2026-11-12 Rakudo issue sweep was produced

Copy this directory to a scratch location before running it: the runner
creates `run/` and `work/` next to `harness/`, and nothing here should write
into the repository. Engine paths are the two constants at the top of
`runall.py` (Raku++'s `build/rakupp`, Rakudo 2026.09 from rakubrew).

| step | command | what it does |
|---|---|---|
| 1 | `python3 -I harness/fetch.py raw/issues.json` | read-only download of every rakudo/rakudo issue (no PRs) through the public search API, partitioned by creation date to stay under its 1,000-result cap; ~35 requests, 10 a minute, no token |
| 2 | `python3 -I harness/extract.py raw/issues.json work/snips.jsonl` | pulls snippets out of issue bodies: fenced code blocks, `raku -e '…'` command lines, `m:`/`c:` IRC-bot lines, REPL transcripts (`> ` lines) |
| 3 | `python3 -I harness/runall.py work/snips.jsonl work/results.jsonl 3 8` | runs up to 3 snippets per issue under both engines, 8 at a time, 10 s timeout, stdin closed, `HOME` and `TMPDIR` a fresh scratch dir, inside `sandbox-exec -f sb.sb` (no writes outside the scratch dir, no network beyond localhost); resumable |
| 4 | `python3 -I harness/compare.py work/results.jsonl raw/issues.json work/verdicts.jsonl` | buckets each run: `match`, `both-die`, `out-diff`, `pp-dies`, `rk-dies`, `pp-timeout`, `pp-crash`, … |
| 5 | `python3 -I harness/rerun.py work/verdicts.jsonl work/snips.jsonl work/rerun.jsonl` | runs every disagreement once more on both engines, to flag output that varies run to run |
| 6 | `python3 -I harness/packets.py 8` | writes review packets (`work/review/in1..8.txt`): issue excerpt, snippet, both engines' results |
| 7 | (review) | each packet judged against [REVIEW-GUIDE.md](REVIEW-GUIDE.md); issues classified against [TAXONOMY.md](TAXONOMY.md) |
| 8 | `python3 -I harness/merge.py` | joins classification (`work/class/part*.tsv`), buckets and review verdicts (`work/review/out*.tsv`, after [overrides.tsv](overrides.tsv) is applied to them) into `work/merged.json` |
| 9 | `python3 -I harness/emit.py <dir>` | writes `issues.tsv` and `review.tsv` |
| 10 | `python3 -I harness/fixlist.py review.tsv PPCRASH,MISSING,PPBUG,LAX` | the grouped markdown of FIX-LIST.md (and `PPWIN,SHARED` for WINS.md); its `V` set is the issues re-run by hand |

Two helpers for checking a finding by hand: `harness/recheck.py 1447.0 2434.0 …`
re-runs named cases (issue.snippet) sandboxed on both engines, and
`harness/both.sh FILE` runs each line of FILE as a `-e` program on both.

[overrides.tsv](overrides.tsv) holds the four reviewer verdicts corrected by hand.

Snippets are code from strangers. The sandbox profile is what makes a bulk run
safe; keep it. Network access is denied, so issues about sockets or HTTP land in
`both-die`.
