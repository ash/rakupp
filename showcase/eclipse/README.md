# eclipse — predicting solar and lunar eclipses

Given one integer — the number of New Moons since 6 January 2000 — this
program will tell you that the Sun goes out over Iceland on 12 August 2026 at
17:46 UT, that the eclipse is total, that its shadow axis passes 5,733 km from
the centre of the Earth, and that it belongs to saros series 126, which began
in 1179 and will end in 2459.

No ephemeris file, no table of past eclipses, no network. A few hundred sine
terms and some modular arithmetic.

```sh
build/rakupp showcase/eclipse/eclipse.raku                 # the next dozen eclipses
build/rakupp showcase/eclipse/eclipse.raku 2026            # one year
build/rakupp showcase/eclipse/eclipse.raku --solar 1900 1950
build/rakupp showcase/eclipse/eclipse.raku --saros=139     # one family, beginning to end
build/rakupp showcase/eclipse/eclipse.raku --phases=2026-08
build/rakupp showcase/eclipse/eclipse.raku --seasons 2024 2027
build/rakupp showcase/eclipse/eclipse.raku --explain=2026-08-12
build/rakupp showcase/eclipse/eclipse.raku --check
```

The same engine also runs in a browser, because `rakupp --target=js` compiles
it to JavaScript — see [the interactive book](#the-book) below.

## What it prints

```
$ build/rakupp showcase/eclipse/eclipse.raku 2026
date       UT     kind   type                   mag    gamma  saros  notes
------------------------------------------------------------------------------
2026-02-17 12:13  solar  annular              1.000  -0.9738    121  central
2026-03-03 11:34  lunar  total                1.149  -0.3773    133  totality 57m
2026-08-12 17:46  solar  total                1.000  +0.8989    126  central
2026-08-28 04:13  lunar  partial              0.925  +0.4990    138  partial 197m
```

`--seasons` answers the question the table raises — why only four, and why in
those two clusters. Each row is one New Moon; the column is `sin F`, the
Moon's distance from a node, and the bracketed band is the window inside which
a shadow can reach anything at all:

```
$ build/rakupp showcase/eclipse/eclipse.raku --seasons 2025 2026
new moon           sin F  node --------------------------+-------------------------- node
2025-08-23        +0.288                      [          |        . ]
2025-09-21        -0.241                      [   *      |          ]
2025-10-21        -0.702           .          [          |          ]
...
2026-07-14        +0.634                      [          |          ]        .
2026-08-12        +0.151                      [          |    #     ]
2026-09-11        -0.374                     .[          |          ]
```

`--explain` walks one prediction from `k` to the verdict, term by term: the
mean phase, each correction in minutes, Delta T, gamma, u, and the comparison
that decides between total and annular.

## How it works

Four files, and the arithmetic is all in the first one.

| File | What it is |
|---|---|
| [`lib/Eclipse.rakumod`](lib/Eclipse.rakumod) | the engine — 550 lines, no dependencies |
| [`eclipse.raku`](eclipse.raku) | the terminal: tables, the ASCII season strip, `--explain`, `--check` |
| [`web/api.raku`](web/api.raku) | the same engine's answers as plain data, registered on `globalThis` |
| [`tools/build.raku`](tools/build.raku) | bundle → `rakupp --target=js` → one self-contained HTML file |

**Time.** `julian-day` and `calendar-date` convert between calendar dates and
Julian Days, Gregorian after 1582 October 15 and Julian before it. `delta-t`
is the Espenak & Meeus piecewise fit for TD − UT, the gap between the uniform
clock the theory runs on and the rotating Earth's own.

**Phases.** `k` counts New Moons from the one of 2000 January 6; `k + 0.5` is a
Full Moon. `mean-phase` is a straight line in `k`; `phase-terms` returns the
periodic corrections of Meeus chapter 49 as `name => days` pairs — 25 lunar and
solar terms plus the 14 planetary ones — and `phase-jde` is simply their sum
added to the mean. Handing back the terms rather than a number is what lets the
book switch them off one at a time: with none of them the answer is ten hours
out, the largest two recover all but twenty minutes of that, and the last dozen
are worth seconds each.

**Eclipses.** `eclipse-at($k)` rejects the month outright when `|sin F| > 0.36`
— five New Moons in six — and otherwise computes two numbers from Meeus
chapter 54, then hands them to `classify-solar` or `classify-lunar`, which are
pure functions of those two numbers and nothing else:

- **gamma**, the least distance between the shadow axis and the centre of the
  Earth, in Earth radii. Under 0.9972 the axis lands on the Earth and the
  eclipse is central somewhere; past 1.5433 + u nothing reaches us at all.
- **u**, the radius of the Moon's umbral cone where it crosses the plane
  through the Earth's centre. Negative: the cone still has length to spare, and
  the eclipse is **total**. Positive: it ran out above the ground, and the Sun
  shows as a ring — **annular**. In the sliver between, the curve of the Earth
  decides, and the same eclipse is total in the middle of its track and annular
  at both ends: **hybrid**.

For lunar eclipses the same two numbers give the umbral and penumbral
magnitudes and, from the chord of the Moon's path across each shadow circle,
the duration of every phase.

Because the classifiers take only γ and u, the book can put both on sliders and
send hypothetical values through the very same code a real eclipse goes
through. That is the difference between reading that the hybrid band is
`0 < u < 0.0047` and finding it by hand.

**Saros.** Eclipses 223 lunations apart form a family; consecutive family
numbers are one *inex* (358 lunations) apart. So `k = k0 + 223a + 358b` and the
series number is `s0 + b`. Since 358 ≡ 135 (mod 223) and 135 × 38 ≡ 1
(mod 223), the series number is `38 × (k − k0) + s0` reduced mod 223 — a
modular inverse, computed once, in place of a lookup table:

```raku
sub saros-series($k, Bool :$lunar) is export {
    my ($k0, $s0) = $lunar ?? (229, 129) !! (218, 145);
    my $n = (38 * (floor($k) - $k0) + $s0) % 223;
    $n <= 0 ?? $n + 223 !! $n;
}
```

`saros-run` walks a family from either end, 223 lunations at a time, and stops
where `eclipse-at` stops answering. Series 139 comes out as 71 eclipses from
1501-05-17 to 2763-07-03; series 136 as 71 from 1360-06-14 to 2622-07-30. Both
match NASA's catalogue exactly.

## Is it right?

[`reference/catalogue.tsv`](reference/catalogue.tsv) holds 64 eclipses entered
by hand from NASA's *Five Millennium Canon* — date, kind, type and saros
number, none of it produced by this engine. `--check` reproduces them:

```
$ build/rakupp showcase/eclipse/eclipse.raku --check
near  2015-04-04  catalogue says total, the series says partial (magnitude 0.9956)

63 of 64 catalogued eclipses reproduced exactly (date, type and saros)
1 marginal case documented in reference/catalogue.tsv
```

Phase instants were checked separately against published New and Full Moon
times: 1977-02-18 03:37, 2000-01-06 18:14, 2017-08-21 18:30, 2024-04-08 18:21
UT — all to the minute, as are the four phases of March 2025.

The one disagreement is the point of the exercise. The lunar eclipse of
4 April 2015 was total by 0.0006 of a magnitude, with a totality of four
minutes and 43 seconds; the truncated series here calls it partial by 0.004.
A series whose stated precision is minutes cannot adjudicate a four-minute
totality, and saying so is more useful than printing digits that are not there.

What is genuinely out of reach is *where*. A path of totality needs Besselian
elements — the shadow axis in Earth-fixed coordinates, sampled through the
event, corrected for the observer's height and the flattening of the Earth.
This program answers *when*, and what kind.

## The book

`web/` is the same engine as an interactive textbook, seven chapters with
every number computed live in the browser:

```sh
build/rakupp showcase/eclipse/tools/build.raku
```

Three steps, and the middle one is why this showcase exists:

1. **bundle** — `lib/Eclipse.rakumod` and `web/api.raku` are concatenated into
   `build/engine.raku`, with `use` and `unit module` stripped, because
   `--target=js` takes a single file.
2. **transpile** — `rakupp --target=js --standalone build/engine.raku -o
   web/engine.js`. 690 lines of Raku become 425 KB of JavaScript with the
   runtime inlined: classes, `Rat` arithmetic, `sprintf`, hash slices, `%h<…>`,
   `sort`, `sqrt`, the lot.
3. **inline** — `web/index.html` plus that JavaScript becomes
   `web/eclipse.html`, one file with no dependencies that can be opened from
   disk or dropped on any static host.

`web/api.raku` is the whole browser-facing surface — a hash of closures, one
per widget, published with `use JS`:

```raku
JS<eclipse> = %api;
```

so the page calls `eclipse.list(2001, 2050, 'solar')` or `eclipse.saros(139,
false)` and gets plain arrays and objects back. There is no server: opening
`web/eclipse.html` runs Raku.

The chapters are the derivation in order — the two months and the ±0.36
window; `k` and the correction terms; gamma on the fundamental plane; the
total/annular/hybrid classification as a scatter plot of every eclipse of a
century; the Earth's shadow with the Moon's path drawn through it; a whole
saros family with gamma drifting from pole to pole across thirteen centuries;
and Delta T, the one quantity in the calculation that cannot be computed.

Most of them are things to move rather than read:

| Chapter | What you drive | What it shows |
|---|---|---|
| 1 | a scrubber over the window of months | eclipse seasons arriving 19 days earlier each year |
| 2 | a checkbox per correction term, all 26 | the series converging — switch one off, watch the error |
| 3 | γ and u on sliders | the verdict flipping total → hybrid → annular → partial → nothing |
| 5 | γ and u again, over the Earth's shadow | the Moon's path and every duration, recomputed |
| 6 | a stepper along a saros family | 6585.32 days per step, γ drifting a fixed amount each time |
| 7 | a year slider from 1000 BC to 2500 | ΔT, and the longitude error it costs |

## What it exercised

The engine is arithmetic-heavy and object-light, which is a different load
from the other showcases: no grammar, no I/O to speak of, and a hard
requirement that a `Num` computed under the interpreter and the same `Num`
computed in a browser agree to the last digit.

Writing it turned up one `--target=js` bug — a multi-word hash slice
(`%a<t e m mp f om>`) was emitted as a single item rather than a list, so
every angle after the first came out `Nil` and every trigonometric term
silently became zero. The transpiled engine dutifully reported an eclipse
every month, all with gamma exactly 0. It is fixed in `src/codegen/Js.cpp`;
the shape of the failure — plausible output, no error — is the argument for
checking a transpiler against the interpreter on a program whose answers are
independently known.
