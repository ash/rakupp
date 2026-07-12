# The Parrot Mandelbrot, revived

`examples/mandel.raku` is a running descendant of one of the oldest surviving
Perl 6 example programs: `languages/perl6/examples/mandel.p6`, which shipped
inside the [Parrot](https://github.com/parrot/parrot) virtual machine around
2004, back when the Perl 6 compiler (the Perl 5 "P6C" front end) lived as a
subdirectory of Parrot — before Pugs, before Rakudo.

It was translated from a compact C Mandelbrot printer by Glenn Rhoads and
ported to Perl 6 by Leopold Tötsch. The original source (recovered from the
`RELEASE_0_1_1` tag of the Parrot repository):

```perl6
#!perl6
sub main() {
    my ($x, $y, $k);
    # no substr now
    my @b = (' ', '.', ':', ',', ';', '!', '/', '>', ')', '|', '&', 'I', 'H', '%', '*', '#');

    my ($r, $i, $z, $Z, $t, $c, $C);
    loop ($y=30; $C = $y*0.1 - 1.5, $y--;) {
	loop ($x=0; $c = $x*0.04 - 2.0, $z=0.0, $Z=0.0, $x++ < 75;) {
	    loop ($r=$c, $i=$C, $k=0; $t = $z*$z - $Z*$Z + $r, $Z = 2.0*$z*$Z + $i, $z=$t, $k<112; $k++) {
	        last if ($z*$z + $Z*$Z > 10.0);
	    }
	    print (@b[$k%16]);
        }
	print "\n";
    }
}
```

## The original no longer compiles — and that is not Raku++'s fault

The first thing worth stating plainly: this file is rejected by **present-day
Rakudo**, not just by Raku++. Both implementations fail at the *same line* for
the *same reason*.

| Implementation | Result on the original file |
| --- | --- |
| Rakudo (system `raku`) | `===SORRY!=== Malformed loop spec` at line 17 |
| Raku++ (system `rakupp`) | `===SORRY!=== Parse error at line 17: expected ; (got '10.0')` |

The wording differs; the diagnosis is identical. Because the reference
implementation breaks in lock-step with Raku++, the breakage is a matter of
**Raku syntax evolving since 2004**, not an implementation bug.

Two independent language changes are responsible.

### 1. Whitespace is now required around `<` and `>`

The innermost loop condition contains `$k<112` with no spaces. In modern Raku,
`$k<...>` is angle-bracket **subscript / word-quote** syntax, so the parser
opens a `<…>` construct at `$k<` and consumes everything until it finds the
next `>` — which happens to be the `> 10.0` on the following line. That is why
both implementations report the error one line *below* the real offender.

In isolation, Rakudo names it directly:

```
Whitespace required before < operator
```

The fix is simply `$k < 112`.

### 2. A comma expression is no longer "the value of its last element"

The loop conditions lean on C's comma operator, where `a, b, c` evaluates all
three and yields `c`. For example the middle loop's condition is:

```
$c = $x*0.04 - 2.0, $z=0.0, $Z=0.0, $x++ < 75
```

The intent (C semantics) is "do the three assignments, then test
`$x++ < 75`". In modern Raku, `a, b, c` builds a **`List`**, and a non-empty
list is always true — so the condition never becomes false and the loop runs
forever. Verified against Rakudo directly:

```raku
my $y; loop ($y = 3; $y > 0, say $y; $y--) { }   # prints 3 2 1 0 -1 -2 ... forever
```

So even after fixing the whitespace, the program would hang rather than draw
anything.

## The modernisation

`examples/mandel.raku` applies the two forced changes, plus one cosmetic tidy:

* whitespace added around every `<` / `>`;
* the per-iteration side effects that used to hide inside the comma-conditions
  moved into the loop init / body / step, leaving each condition a plain
  boolean;
* the entry point is now `sub MAIN`, Raku's auto-invoked program entry, in
  place of the original `sub main()` followed by an explicit `main()` call.
  This isn't required — the explicit call worked fine — but it honours the
  original intent of "start the program from this function" the idiomatic way,
  and it lets us drop the trailing call. Both Rakudo and Raku++ run `MAIN`
  automatically.

The algorithm, the escape threshold, the 16-character ramp
`" .:,;!/>)|&IH%*#"`, and the 75×30 grid are untouched. The inner-loop
restructuring is output-equivalent: the only behavioural difference is one
extra (discarded) iteration step for points inside the set, which never
changes the printed glyph.

## Result

Under both the reference implementation and Raku++ the modernised program
prints the same picture, and the two outputs are **byte-for-byte identical**
(`diff` reports no differences):

```
................::::::::::::::::::::::::::::::::::::::::::::...............
...........::::::::::::::::::::::::::::::::::::::::::::::::::::::..........
........::::::::::::::::::::::::::::::::::,,,,,,,:::::::::::::::::::.......
.....:::::::::::::::::::::::::::::,,,,,,,,,,,,,,,,,,,,,,:::::::::::::::....
...::::::::::::::::::::::::::,,,,,,,,,,,,;;;!:H!!;;;,,,,,,,,:::::::::::::..
:::::::::::::::::::::::::,,,,,,,,,,,,,;;;;!!/>&*|& !;;;,,,,,,,:::::::::::::
::::::::::::::::::::::,,,,,,,,,,,,,;;;;;!!//)|.*#|>/!;;;;;,,,,,,:::::::::::
::::::::::::::::::,,,,,,,,,,,,;;;;;;!!!!//>|:    !:|//!!;;;;;,,,,,:::::::::
:::::::::::::::,,,,,,,,,,;;;;;;;!!/>>I>>)||I#     H&))>////*!;;,,,,::::::::
::::::::::,,,,,,,,,,;;;;;;;;;!!!!/>H:  #|              IH&*I#/;;,,,,:::::::
::::::,,,,,,,,,;;;;;!!!!!!!!!!//>|.H:                     #I>!!;;,,,,::::::
:::,,,,,,,,,;;;;!/||>///>>///>>)|H                         %|&/;;,,,,,:::::
:,,,,,,,,;;;;;!!//)& :;I*,H#&||&/                           *)/!;;,,,,,::::
,,,,,,;;;;;!!!//>)IH:,        ##                            #&!!;;,,,,,::::
,;;;;!!!!!///>)H%.**           *                            )/!;;;,,,,,::::
                                                          &)/!!;;;,,,,,::::
,;;;;!!!!!///>)H%.**           *                            )/!;;;,,,,,::::
,,,,,,;;;;;!!!//>)IH:,        ##                            #&!!;;,,,,,::::
:,,,,,,,,;;;;;!!//)& :;I*,H#&||&/                           *)/!;;,,,,,::::
:::,,,,,,,,,;;;;!/||>///>>///>>)|H                         %|&/;;,,,,,:::::
::::::,,,,,,,,,;;;;;!!!!!!!!!!//>|.H:                     #I>!!;;,,,,::::::
::::::::::,,,,,,,,,,;;;;;;;;;!!!!/>H:  #|              IH&*I#/;;,,,,:::::::
:::::::::::::::,,,,,,,,,,;;;;;;;!!/>>I>>)||I#     H&))>////*!;;,,,,::::::::
::::::::::::::::::,,,,,,,,,,,,;;;;;;!!!!//>|:    !:|//!!;;;;;,,,,,:::::::::
::::::::::::::::::::::,,,,,,,,,,,,,;;;;;!!//)|.*#|>/!;;;;;,,,,,,:::::::::::
:::::::::::::::::::::::::,,,,,,,,,,,,,;;;;!!/>&*|& !;;;,,,,,,,:::::::::::::
...::::::::::::::::::::::::::,,,,,,,,,,,,;;;!:H!!;;;,,,,,,,,:::::::::::::..
.....:::::::::::::::::::::::::::::,,,,,,,,,,,,,,,,,,,,,,:::::::::::::::....
........::::::::::::::::::::::::::::::::::,,,,,,,:::::::::::::::::::.......
...........::::::::::::::::::::::::::::::::::::::::::::::::::::::..........
```

## Reproduce

```sh
# modern Raku (reference)
raku examples/mandel.raku

# Raku++
build/rakupp examples/mandel.raku

# they match exactly
diff <(raku examples/mandel.raku) <(build/rakupp examples/mandel.raku)
```
