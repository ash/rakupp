# orbits — where the planets are, and what it costs to go there

An orbit is six numbers. Two for the ellipse — how big, how squashed — three
angles to hang that ellipse in space, and one to say where the body was at a
known moment. That is the entire state of a planet, for ever.

This is those six numbers made computable: Kepler's equation solved by Newton
iteration, the eight planets propagated from Standish's element table, the
geometry of what that looks like from the Earth, and what a Hohmann transfer
between two of them costs.

```sh
build/rakupp showcase/orbits/orbits.raku                    # the solar system today
build/rakupp showcase/orbits/orbits.raku 2030-06-01
build/rakupp showcase/orbits/orbits.raku --map              # an orrery in characters
build/rakupp showcase/orbits/orbits.raku --map --outer
build/rakupp showcase/orbits/orbits.raku --planet=Mars
build/rakupp showcase/orbits/orbits.raku --kepler=0.6       # watch Newton converge
build/rakupp showcase/orbits/orbits.raku --events=Mars 2020 2035
build/rakupp showcase/orbits/orbits.raku --transfer=Earth,Mars
build/rakupp showcase/orbits/orbits.raku --check
```

The same engine runs in a browser, because `rakupp --target=js` compiles it to
JavaScript — see [the book](#the-book).

## What it prints

```
$ build/rakupp showcase/orbits/orbits.raku --transfer=Earth,Mars
Hohmann transfer, Earth to Mars

  departure orbit speed     29.785 km/s
  on the transfer ellipse   32.729 km/s        first burn    2.945 km/s

  arriving at               21.480 km/s
  target orbit speed        24.129 km/s        second burn   2.649 km/s

  total delta-v 5.594 km/s
  time of flight             258.9 days      0.71 years
  launch window every        779.9 days      the synodic period
  target must lead by        +44.3°          at the moment you leave
```

`--map` draws an orrery with the orbits to scale, `--events` lists oppositions,
and `--kepler` shows Newton's method eating the error one digit-doubling at a
time:

```
     M       steps  convergence, |error| per step
   30°           5   1e-01  1e-02  9e-05  4e-09  1e-16
   60°           4   8e-02  2e-03  1e-06  3e-13  2e-16
```

## How it works

| File | What it is |
|---|---|
| [`lib/Orbit.rakumod`](lib/Orbit.rakumod) | the engine — 290 lines, no dependencies |
| [`orbits.raku`](orbits.raku) | the terminal: tables, the orrery, `--check` |
| [`reference/known.tsv`](reference/known.tsv) | 30 values from the almanacs, none of them ours |
| [`web/api.raku`](web/api.raku) | the same answers as plain data, on `globalThis` |
| [`tools/build.raku`](tools/build.raku) | bundle → `rakupp --target=js` → one HTML file |

**Kepler's equation** is the whole difficulty. You know the *mean anomaly* M,
which grows linearly with time; you want the *eccentric anomaly* E, from which
the position falls straight out. They are related by

```
M = E − e sin E
```

and that cannot be inverted in closed form — E is not an elementary function of
M. Kepler said so in 1609 and four centuries have agreed. So `kepler` guesses
and corrects, by Newton's method: the error at a guess is `E − e sin E − M`,
the slope is `1 − e cos E`, and the next guess is the current one minus their
ratio. Started at M it converges *quadratically* — each step roughly doubles
the number of correct digits — so five or six steps exhaust a double even at
e = 0.95. It returns the iteration trail as well as the answer, so a caller
can watch that happen.

**The planets** come from Standish's linear fit (JPL, *Approximate Positions of
the Major Planets*): each element as a value at J2000 plus a rate per century,
good to a fraction of an arcminute from 1800 to 2050. `position` solves Kepler,
places the body in its own orbital plane, and rotates that plane into the
ecliptic by ω, then i, then Ω — three rotations, always in that order.

**Everything else is geometry.** `geocentric` subtracts the Earth's position to
get what we actually see; `oppositions` scans for the turning points of the
elongation; `hohmann` applies the vis-viva equation
`v = √(GM(2/r − 1/a))` at each end of a transfer ellipse; `synodic` is two
clock hands, `1/S = |1/T₁ − 1/T₂|`.

## Is it right?

`reference/known.tsv` holds thirty values taken from the almanacs and the
textbooks — nothing in it was produced by this engine — and `--check` measures
against them, plus twelve internal consistency checks:

```
$ build/rakupp showcase/orbits/orbits.raku --check
all 42 checks passed
```

Those are: the eight sidereal periods, four synodic periods, ten opposition
dates, the textbook Hohmann figures for Earth to Mars, escape and low-orbit
speeds, Kepler's equation solved to better than 1e−11 at seven eccentricities
and twenty-four mean anomalies each, and every planet's radius agreeing with
its own coordinates.

The opposition dates are the sharpest test, because being a day out is
visible. All ten land on the almanac date:

```
Mars       2020-10-13   2022-12-08   2025-01-16   2027-02-19
Jupiter    2022-09-26   2023-11-03   2024-12-07
Saturn     2022-08-14   2023-08-27   2025-09-21
```

## The book

`web/` is the same engine as an interactive textbook:

```sh
build/rakupp showcase/orbits/tools/build.raku
```

Six chapters, and the controls are the point:

| Chapter | What you drive | What it shows |
|---|---|---|
| 1 | **all six elements, plus a view tilt** | the orbit redrawing; inclination stops being a number when you tip the view |
| 2 | e and M | Newton's error collapsing 1e−1 → 1e−3 → 1e−7 → 1e−15; M, E and ν separating as e grows |
| 3 | a date scrubber, a span, a tilt | the orrery; the inner planets bolting round while Neptune barely stirs |
| 4 | a planet, and a range of years | the distance curve with oppositions marked on it |
| 5 | departure and destination radii | Δv and flight time; the Δv flattening past Jupiter while the years do not |

Chapter 1 is the one to start with. Push `e` past 0.8 and the two speed
readouts separate by a factor of ten: a body on a long ellipse crawls near
aphelion and crosses perihelion in a hurry. That is Kepler's second law, and
it is why a launch window is a window.

## What it does not do

Every model is a claim about what can be ignored, and this one ignores two
big things.

**The planets do not pull on each other here.** Each follows its own ellipse
as though the others did not exist, with the drift of the elements standing in
for their combined effect. That is why the outer-planet periods come out up to
0.06% from the published ones: Jupiter and Saturn tug on each other hard enough
to matter, and a linear element fit cannot express it.

**Transfers assume circular, coplanar orbits.** Real trajectory design solves
*Lambert's problem* — given two positions and a flight time, find the orbit
between them — for every pair of dates in a window, and contours the resulting
Δv into a porkchop plot. That is what makes a Mars launch window three weeks
wide rather than an instant. Mars's 1.85° inclination alone adds a few hundred
m/s that chapter 5 does not charge you for.

## What it exercised

Trigonometry, iteration to convergence, a table of physical constants, and
rotations that are wrong if you compose them in the wrong order.

It found one interpreter bug, in the parser rather than the maths: a bare
identifier before a fat arrow was being lexed as a version literal, so

```raku
my %h = v1 => $v1, other => 5;
```

made the key `"1"` instead of `"v1"` and `%h<v1>` quietly returned `Any`.
Rakudo lexes it as an identifier, and rakupp now does too. Silent wrong answers
from a construct that looks obviously correct are the ones worth catching.
