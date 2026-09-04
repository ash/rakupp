# fourier — building shapes out of sine waves

Any repeating shape, however unlike a wave it looks, is a sum of sines. This
is that claim made computable: the coefficients in closed form, the integrals
that produce them, the discrete transform that goes the other way, the fast
version of it, and — because a claim this strong deserves it — fifteen exact
identities the engine can be held to.

```sh
build/rakupp showcase/fourier/fourier.raku                       # a square wave, built up
build/rakupp showcase/fourier/fourier.raku --wave=triangle -n=15
build/rakupp showcase/fourier/fourier.raku --coefficients=square -n=15
build/rakupp showcase/fourier/fourier.raku --converge=square
build/rakupp showcase/fourier/fourier.raku --gibbs
build/rakupp showcase/fourier/fourier.raku --spectrum=3,7,11
build/rakupp showcase/fourier/fourier.raku --dft=1,0,-1,0
build/rakupp showcase/fourier/fourier.raku --bench=512
build/rakupp showcase/fourier/fourier.raku --check
```

The same engine runs in a browser, because `rakupp --target=js` compiles it to
JavaScript — see [the book](#the-book).

## An oscilloscope in characters

```
$ build/rakupp showcase/fourier/fourier.raku -n=7
square wave, the first 7 harmonics

    |    ##                          ##
    |   #  ##    ####      ####    ##  #
 +1 |........****....******....****.......
    |  #                                #
    |
  0 |#                                    #
    |
    |                                      #                                  #
 -1 |                                     ........****....******....****.......
    |                                        #  ##    ####      ####    ##  #
    +--------------------------------------------------------------------------
     0                               half a period                       1

  # the partial sum   . the waveform it is chasing   * where they agree

  RMS error 0.224503
```

The flat parts settle down quickly. The corners never quite do, and the
ringing at the cliff is chapter 4 of the book below.

## How it works

| File | What it is |
|---|---|
| [`lib/Fourier.rakumod`](lib/Fourier.rakumod) | the engine — 258 lines, no dependencies |
| [`fourier.raku`](fourier.raku) | the terminal: the scope, the tables, `--check` |
| [`web/api.raku`](web/api.raku) | the same answers as plain data, on `globalThis` |
| [`tools/build.raku`](tools/build.raku) | bundle → `rakupp --target=js` → one HTML file |

Everything is periodic with period 1, so the nth harmonic is `cos(2πnt)` and
`sin(2πnt)` and there are no stray scale factors anywhere:

```
f(t) = a₀/2 + Σ [ aₙ cos(2πnt) + bₙ sin(2πnt) ]
```

**Synthesis.** `wave-coefficients` has the closed forms — the integrals done on
paper once — and `series-value` sums the first N of them. The interesting part
is not the formulas but how fast they shrink, because that is the whole story
of how well the series works:

| Waveform | Coefficients | Fall-off | Because |
|---|---|---|---|
| square | `bₙ = 4/πn`, odd n | 1/n | it jumps |
| sawtooth | `bₙ = 2(−1)ⁿ⁺¹/πn` | 1/n | it jumps |
| triangle | `bₙ = 8(−1)^((n−1)/2)/(πn)²`, odd n | 1/n² | continuous, but kinked |
| rectified sine | `aₙ = −4/π(4n²−1)` | 1/n² | continuous, but kinked |
| pulse, duty d | `aₙ = 2 sin(πnd)/πn` | 1/n, in a sinc envelope | it jumps, twice |

A discontinuity costs 1/n; a corner costs 1/n²; genuine smoothness costs less
than any power. Smoothness is compressibility, which is why JPEG exists.

**Analysis.** `analyse` goes back the other way by doing the integrals
numerically — multiply the signal by a sine and average, and every other
harmonic cancels over a full period. `--check` uses it to confirm the closed
forms above, and it caught two of them wrong the first time it ran.

**The transforms.** `dft` is the definition, n outputs of n terms each. `fft`
is Cooley and Tukey's 1965 observation that a transform of length n is two of
length n/2, glued with one rotation each:

```raku
my @even = fft(@x[(0, 2 ...^ $n)]);
my @odd  = fft(@x[(1, 3 ...^ $n)]);
for ^($n div 2) -> $k {
    my $w = -TAU * $k / $n;
    my $t = Complex.new(cos($w), sin($w)) * @odd[$k];   # the twiddle factor
    @out[$k]         = @even[$k] + $t;
    @out[$k + $half] = @even[$k] - $t;
}
```

For n = 4096 that is 49,152 operations instead of 16,777,216. `--bench` times
them against each other; in the browser the same two functions race live.

## Is it right?

Every check has an exact answer to compare against, so `--check` is a real
proof obligation rather than a smoke test:

```
$ build/rakupp showcase/fourier/fourier.raku --check
ok   FFT agrees with the DFT definition
ok   the inverse transform gives the signal back
ok   a pure tone of 6 cycles puts amplitude 1 in bin 6
ok   and nothing anywhere else
ok   Parseval for a square wave, 100 harmonics
ok   Parseval for a square wave, 1000 harmonics
ok   the closed-form coefficients of a square wave are its integrals
ok   the closed-form coefficients of a sawtooth wave are its integrals
ok   the closed-form coefficients of a triangle wave are its integrals
ok   the closed-form coefficients of a pulse wave are its integrals
ok   the closed-form coefficients of a rectified wave are its integrals
ok   the Gibbs constant 2*Si(pi)/pi
ok   the 201-harmonic peak sits on it
ok   square coefficients fall off exactly as 1/n
ok   triangle coefficients as 1/n^2

all 15 checks passed
```

The Gibbs one is the nicest. Add harmonics to a square wave and the partial
sum gets closer everywhere except at the jump, where it always overshoots by
about 9%. More terms make the overshoot narrower, never shorter, and the peak
converges to an exact constant:

```
 harmonics      peak value       overshoot
         3       1.2004218        10.0211%
        21       1.1796688         8.9834%
       201       1.1789877         8.9494%
     limit       1.1789797         8.9490%
```

That limit is `2·Si(π)/π`, and the engine computes `Si(π)` by the same midpoint
rule it uses everywhere else rather than looking the number up.

## The book

`web/` is the same engine as an interactive textbook:

```sh
build/rakupp showcase/fourier/tools/build.raku
```

Bundle the engine with `web/api.raku`, transpile with
`rakupp --target=js --standalone`, inline the result into `web/fourier.html`:
one file, no dependencies, no server. Six chapters, and every one of them is
something to drag:

| Chapter | What you drive | What it shows |
|---|---|---|
| 1 | harmonics, 1 to 99; the waveform; a pulse's duty | corners appearing out of smooth sines |
| 2 | the duty cycle again, and a log scale | the sinc envelope breathing; 1/n and 1/n² as different slopes |
| 3 | **sixteen sliders, one per harmonic** | build any wave you like; Parseval's energy tracks it |
| 4 | harmonics, with the zoom locked to the ringing | the overshoot narrowing while its height refuses to move |
| 5 | three tone frequencies, three amplitudes, noise | tones found under noise; a tone past n/2 aliasing back down |
| 6 | the transform length, 2⁴ to 2¹² | n² against n log n, timed on your own machine |

Chapter 3 is the point of the exercise: the earlier chapters tell you which
amplitudes to use, and then you get to choose them yourself and see what comes
out. Load the square wave and kill just the third harmonic; or set every odd
harmonic to the same height instead of letting them fall off as 1/n.

## What it exercised

Complex arithmetic, recursion over array slices, closures, and a hard
requirement that a `Num` computed under the interpreter and the same `Num`
computed in a browser agree to the last digit.

It found three `--target=js` bugs, all of them in that last category, and all
of them fixed in the same commit:

- **`@x[(0, 2 ...^ $n)]` was one item, not a slice.** The emitter's
  `isSliceIndex` knew about `@a[1..3]` and `@a[@i]` but not about a sequence,
  so `fft` recursed on a one-element list containing a list, and node died
  building a 169-million-element array. Same family as the `%h<a b>` bug the
  eclipse showcase found.
- **`(1, 3 ...^ 2)` never terminated.** The runtime's sequence generator only
  tested *equality* with the endpoint, so a step that jumped straight over it
  ran forever. It now stops when it crosses, which is what the interpreter has
  always done — twelve sequence forms now agree exactly.
- **`Complex.new` was refused** by the runtime's constructor table. `1 + 2i`
  worked; `Complex.new(1, 2)` did not.

None of the three would have shown up in a program that did not recurse over
slices or construct complex numbers by name, which is the argument for
showcases that reach for different corners of the language.
