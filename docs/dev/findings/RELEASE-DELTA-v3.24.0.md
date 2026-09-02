# The shipped v3.24.0 against this tree, and the 2% it hides

*2026-09-02, Darwin 25.5 / Apple M1, load average ~5 (an ordinary desktop
session: WindowServer, a browser, chat apps). Every ratio here is
**instructions retired**, minimum of five runs, which is what makes a busy
machine measurable at all — see
[PERF-CAMPAIGN.md](../experiments/PERF-CAMPAIGN.md) §"Measure instructions,
not milliseconds".*

The question: how does the working tree compare with what a user actually
downloads? Four binaries, so that "code" and "how it was built" do not get
confused with each other:

| binary | what it is |
|---|---|
| **release** | `rakupp-macos-universal.tar.gz` from the v3.24.0 GitHub release, CI-built, arm64 slice |
| **tag-local** | v3.24.0 built here, `cmake -DCMAKE_BUILD_TYPE=Release`, in a worktree |
| **HEAD** | 36 commits later, this session's starting point |
| **now** | HEAD plus the uncommitted `ValueList` work |

## The CI build is not the story

`release` and `tag-local` are the same source. They measure within 1% of each
other on 25 of 26 kernels, so the released binary is not paying for being a
universal build, and comparing against it is fair. The exception is `bigint`,
where the release retires 5.2% more instructions than a local build of the
same commit — a codegen difference in the two-architecture build, worth
knowing before anyone reads a `bigint` number off a release binary.

## Now against the release

| kernel | instr | cycles | | kernel | instr | cycles |
|---|---:|---:|---|---|---:|---:|
| arrayops | 1.291 | 1.313 | | hashfill | 1.044 | 1.072 |
| sortnums | 1.188 | 1.177 | | regex | 1.035 | 1.019 |
| sortby | 1.141 | 1.126 | | arraypush | 1.029 | 1.025 |
| strcat | 1.139 | 1.087 | | objects | 1.021 | 1.050 |
| multimeth | 1.136 | 1.142 | | objnew | 1.017 | 1.047 |
| call | 1.120 | 1.070 | | hash | 1.004 | 1.004 |
| textsplit | 1.112 | 1.125 | | regexloop | 0.998 | 0.994 |
| fib | 1.083 | 1.040 | | rats | 0.992 | 0.996 |
| privmeth | 1.082 | 1.059 | | streq | 0.986 | 1.074 |
| method | 1.076 | 1.032 | | **asg** | **0.986** | 0.996 |
| strscan | 1.076 | 1.044 | | **loopsum** | **0.983** | 0.989 |
| attrread | 1.069 | 1.047 | | bigint | 1.046 | 1.053 |
| strpass | 1.052 | 1.011 | | | | |
| subcall | 1.051 | 1.012 | | | | |

Wall clock on the longer programs, best of five: a 1M-element `map`/`grep`/
`reverse` pipeline 0.91 s → 0.76 s, a 2M-push-and-sum program 1.91 s →
1.66 s.

## The two kernels that went backwards did so before this session

Splitting the delta into "the 36 commits since the release" and "this
session" separates them cleanly:

| kernel | tag-local → HEAD | HEAD → now |
|---|---:|---:|
| loopsum | 0.982 | 0.999 |
| asg | 0.985 | 1.000 |
| attrread | 0.979 | 1.084 |
| multimeth | 0.985 | 1.139 |
| privmeth | 0.986 | 1.093 |
| grammar JSON parse, api.json | 0.906 | 1.013 |
| grammar JSON parse, strings.json | 0.881 | 1.005 |

The left column is below 1.000 on nearly every kernel: **the commits between
v3.24.0 and HEAD cost 1-2% broadly, and 9-12% on grammar parsing.** This
session's work more than covers it everywhere except the shapes it does not
touch, which is why `asg` and `loopsum` still read below the release.

## One line, 34 instructions per statement

Bisected on `asg` (`my $x = 0; for ^2_000_000 { $x = $x + 1 }`), whose
instruction count repeats to ±0.02%. The step is at
**7af7584** ("the top-100 closure's roots, twenty-odd faults deep"):
3.455 G → 3.523 G, +68 M over 2 M iterations.

Narrowed by loop-body shape, all at that pair of commits:

| body | before | after |
|---|---:|---:|
| `{ }` | 1.658 G | 1.657 G |
| `{ $x }` | 2.674 G | 2.741 G |
| `{ $x = 1 }` | 2.944 G | 3.011 G |
| `{ $x = $x + 1 }` | 3.457 G | 3.523 G |

The empty body is unchanged and every non-empty body moves by the same
+67 M, so the cost is **once per statement executed**, not per variable or
per operation. It is this line, added to the `ExprStmt` arm of
`Interpreter::exec`:

```c++
tctx_.curStmtExpr = e;   // the statement's own expression (see cooperative next/last/redo)
```

Deleting it in a scratch build puts every number back exactly
(`{ $x }` 2.673 G, `{ $x = $x + 1 }` 3.455 G). `tctx_` is thread-local, and
on macOS a thread-local access is a real call to `_tlv_get_addr` — already
the second-heaviest symbol in an interpreter profile. Placed at the top of
the arm, it forces a resolution the rest of the arm would otherwise have
folded into one.

**Deleting it is not the fix**: the store is load-bearing for cooperative
`next`/`last`/`redo`, which read `tctx_.curStmtExpr` to tell a bare loop
control from one nested inside an expression. The fix is to make it cheaper —
set it only for statements whose expression can contain a bare loop control
(a parse-time property of the node), or hoist the thread-local resolution to
the top of `exec` where the rest of the function already needs it. Neither is
done here; this file is the measurement, not the repair.
