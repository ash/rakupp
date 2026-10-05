# Pass lists for Rakudo's and mutsu's own test suites

Beyond Roast, two other projects' test directories are run on the same
harness: Rakudo's `t/` and mutsu's `t/`
(`tools/run-roast.raku --suite=rakudo|mutsu`). Neither is a specification —
each project tests itself — so the bar is the set of files **Rakudo** fully
passes, and this directory holds that set next to the one Raku++ passes.

As with Roast, the suites are not copied into this repository: the lists are
file paths relative to each checkout's `t/`, and the checkouts are pinned below.

## The lists

| File | What it holds | Files |
|---|---|---|
| `rakudo-t.rakudo.list` | Rakudo's `t/` files Rakudo fully passes — the bar | 361 of 382 |
| `rakudo-t.rakupp.list` | the same suite, files Raku++ fully passes | 184 of 382 |
| `mutsu-t.rakudo.list` | mutsu's `t/` files Rakudo fully passes — the bar | 5,654 of 6,209 |
| `mutsu-t.rakupp.list` | the same suite, files Raku++ fully passes | 3,758 of 6,209 |

The file counts are after `--suite` keeps each project's internals out: for
Rakudo's `t/` the back-end directories, NativeCall, Telemetry and every file
that uses `nqp`; for mutsu's `t/` the NativeCall directories and, the same way,
every file that uses `nqp`.

Measured 2026-10-04 with Rakudo v2026.09 and Raku++ 5.2.1, against the pins
below. The Raku++ lists move as fixes land: each is updated to every file that
has passed since, so a flaky file does not drop out and back in.

Pins:

- Rakudo `t/` at rakudo/rakudo `6f5479cb746f8165ab4d59760770544e28483e8f`
- mutsu `t/` at tokuhirom/mutsu `49442b3792eaa91c6f28b77120b69c4cea3f053d`

## Reading them

The work list — files Rakudo passes and Raku++ does not (2,033 in mutsu's
suite, 178 in Rakudo's):

```bash
cd docs/status/suite-lists && comm -23 mutsu-t.rakudo.list mutsu-t.rakupp.list
```

A regression check after a change — files Raku++ passed before and no longer
does, which must be empty:

```bash
cd docs/status/suite-lists && comm -23 mutsu-t.rakupp.list /path/to/new.list
```

Files only Raku++ passes (135 in mutsu's suite, 1 in Rakudo's) are files
Rakudo itself fails; they are not part of the bar.

## What is worth passing

Passing on Rakudo does not make a test a statement about the language: mutsu's
tests pin down what Rakudo does, details included. A random audit of 100 gap
files (2026-10-04) classed what their FAILING assertions need: 85 language
behaviour, 6 the RakuAST node API, 3 Rakudo internals, 4 Rakudo quirks, 2 only
Rakudo's exact message wording.

Rakudo's `t/` was classified whole the same way (2026-10-05): of its 229 gap
files, 145 are language behaviour, 38 RakuAST, 5 6.e-only, 30 Rakudo internals
(CORE symbol lists, `Rakudo::Internals`, nqp/QAST, dump formats, precomp), 6
quirks and 5 prose. Those 41 are listed in `rakudo-t.excluded.tsv`.

The work is the language behaviour, RakuAST and anything 6.e. A file whose
failures are only Rakudo's prose, a Rakudo quirk or a Rakudo internal is listed
in `mutsu-t.excluded.tsv` with the reason, and is not chased. A file that mixes
the two is fixed for its language part.

Rakudo's suite runs from Rakudo's checkout root, whose own `lib/` holds Rakudo's
copies of NativeCall, experimental and the like; `--suite=rakudo` sets
`RAKUPP_NO_CWD_LIB=1` so Raku++ does not pick those up from its default `./lib`.

## Measuring again

Rakudo's suite takes about a minute on either engine:

```bash
RAKUDO_ROOT=/path/to/rakudo build/rakupp tools/run-roast.raku --suite=rakudo -j2 --list=new.list
```

mutsu's suite is measured in shards by top-level directory, so that no single
run is long, at `-j2`. The eight shards together are every file:

```bash
for p in "oo/" "lang/ tooling/ fixtures/ io/" "collections/" "vm/ control/" \
         "routines/" "modules/ exceptions/ grammar/" "regex/ types/" "concurrency/ rakuast/"; do
    MUTSU_ROOT=/path/to/mutsu build/rakupp tools/run-roast.raku --suite=mutsu -j2 \
        --list=shard.list $p && cat shard.list >> new.list
done
sort -u -o new.list new.list
```

Use `rakudo tools/run-roast.raku …` to measure Rakudo. Under Rakudo the suite
takes about 25 minutes, and a few of its files leave work behind:

- **Rakudo children that never end.** Some mutsu tests start `rakudo -e …`
  one-liners — a left-recursive grammar among them — which outlive the test the
  harness timed out. Look for `rakudo -e grammar …` processes with parent 1
  afterwards.
- **Files in the checkout.** Heap snapshots (`heap-snapshot-*.mvmheap`), an
  `H<…>` file, and `precomp/` directories under `t/fixtures/dist-selectors/` and
  `t/fixtures/lib-precedence/inst/`. Check `git status` in the mutsu checkout.
