# FAQ — writing Raku for an Arduino

`rakupp --target=arduino` compiles a Raku program into an Arduino sketch. The
board has no interpreter, so it takes a subset of Raku: the parts that become
plain C++ with no runtime behind them. This page is about writing inside that
subset: what to reach for, what the build refuses and what to write instead,
and where the board behaves differently from the Raku you know. The full
reference is [ARDUINO.md](../ARDUINO.md); a walk from a blinking LED to the
Uno R4 WiFi's network is in
[examples/arduino](https://github.com/ash/rakupp/tree/main/examples/arduino).

## The short answer

- Give every variable, parameter and result a native type: `my int16 $x`,
  not `my $x`.
- Give every array a fixed size: `my uint8 @buf[16]`.
- Strings are for printing. Interpolate into `say`; do not keep, join or
  compare one.
- Do the heavy work while building. A `constant` or a `BEGIN` block runs on
  your computer, in full Raku, and the board gets the result.
- There are no hashes, classes, closures or regexes on the board. Use constant
  tables, top-level variables, plain subs and `if`/`elsif` instead.
- Everything the board cannot do is refused when you build, with the line, and
  never fails later on the board.

## Why does `my $x` stop the build?

```
$ rakupp --target=arduino -e 'use Arduino; my $x = 5; say $x'
rakupp --target=arduino: -e, line 1: the untyped variable $x — give it a native type, as in `my int16 $x`
```

An untyped `$x` holds an `Int` object, which can grow without limit. A board
has no room for objects, so every variable becomes a C variable of a fixed
size, and you choose the size. What usually fits:

| what it holds | type |
|---|---|
| a pin number, a byte, a PWM level (0–255) | `uint8` |
| an `analogRead` (0–1023), a temperature | `int16` |
| a `millis()` timestamp | `uint32` |
| a fraction | `num32` (`float`) or `num` (`double`) |
| true or false | `Bool` |

A plain `int` works, but it is 64 bits, as it is in Raku. On an Uno, with
2 KB of RAM, that is 8 bytes for every `int` variable.

The same goes for subs: `sub scale(int16 $x --> int16) { $x * 4 }`. A sub
with no `-->` returns nothing, and one whose result is used must declare it.

## What happens when a value does not fit?

It wraps, as a Raku native does. Arithmetic is done in 64 bits and cut down
to the variable's size when it is stored, so a result overflows where you
store it and never in the middle of the expression:

```raku
my int8  $d = 700;          # -68
my uint8 $c = 255; $c++;    # 0
my int16 $a = 300;
my int16 $p = $a * $a;      # 24464: 90000 does not fit in 16 bits
my int32 $q = $a * $a;      # 90000
```

The interpreter, a sketch and Rakudo all give these numbers. `div` and `%`
floor, as Raku's do and C's do not: `-7 div 2` is −4 and `-7 % 2` is 1.

## How do I make an array?

Give it a type and a size:

```raku
my uint8 @buf[16];             # sixteen zeros
my uint8 @pins[4] = 2, 3, 4;   # the last one is 0
```

The size must be one positive integer known while building: a literal or a
`constant`. `my uint8 @a[W * H]` is refused, but `constant CELLS = W * H;
my uint8 @a[CELLS]` is accepted. There is no `push`, no array without a
shape, no 2-D shape and no array of `Bool`. For a grid, use one dimension and
compute the index. For flags, use `uint8`.

You cannot pass an array to a sub, because a sub's parameters are native
scalars. A top-level `my` is a global in the sketch, so a sub can use the
array directly:

```raku
use Arduino;

constant W = 4;
constant H = 3;
constant CELLS = W * H;
my uint8 @grid[CELLS];
my uint16 $lit = 0;

sub light(uint8 $row, uint8 $col) {
    @grid[$row * W + $col] = 1;
    $lit++;
}

light(1, 2);
say "cell 6: ", @grid[6], ", lit: $lit";    # cell 6: 1, lit: 1
```

`.elems` and `for @buf -> $x { … }` work. Every subscript is checked. An
index past the end prints a message and the line on the serial port, and the
board stops. That is better than writing over whatever comes after the array
in RAM:

```
Index out of range. Is: 4, should be in 0..3
  at line 5
```

## Does string interpolation work?

Yes, in what you print. This:

```raku
say "temp: $t C, twice {$t * 2}";
```

becomes one `Serial.print` for each piece, and the literal parts stay in flash
memory:

```cpp
Serial.print(F("temp: "));
rk_print_int(Serial, ((int64_t)v_t));
Serial.print(F(" C, twice "));
rk_print_int(Serial, rk_mul(((int64_t)v_t), ((int64_t)2LL)));
Serial.print(F("\n"));
```

`say "t=", $t, " ok"` and `print` work the same way, and so does an element
of a constant table, as in `"@NOTE[$i] Hz"`.

What the board cannot do is **keep** a string at run time, because it has no
string type. Each of these is refused:

```raku
my $s = "t=$t";                # an interpolated string used as a value
say "t=" ~ $t;                 # `~` makes a string
say sprintf("%03d", $t);       # and so does .fmt
if $name eq "on" { … }         # a string comparison
say $on ?? "on" !! "off";      # a string chosen at run time
```

To choose a string, choose the statement that prints it:

```raku
if $on { say "LED is on" } else { say "LED is off" }
```

**Strings known while building are fine anywhere.** A `constant` runs on your
computer, so `~`, `sprintf` and modules all work there, and the board gets a
literal:

```raku
constant VERSION = "1.2";
constant BUILT   = DateTime.now.yyyy-mm-dd;
say "v{VERSION} built {BUILT}";        # v1.2 built 2026-11-07
```

`say` ends a line with LF, as it does in Raku. `Serial.println` ends it with
CR LF, as a board does. If a program prints but never calls `Serial.begin`,
the sketch starts with `Serial.begin(9600)`.

## Can I print a decimal number?

Not yet. Printing a `num` is refused, because the board cannot yet write one
the way Raku does. Scale it to an integer and print the two halves:

```raku
use Arduino;

sub print-hundredths(int32 $c is copy) {
    if $c < 0 { print "-"; $c = -$c }
    print $c div 100, ".";
    print "0" if $c % 100 < 10;
    say $c % 100;
}

my num $v = 3.05e0;
print-hundredths(($v * 100).round);         # 3.05
print-hundredths((-2.5e0 * 100).round);     # -2.50
```

The sign is handled first because `div` and `%` floor: `-305 div 100` is −4
and `-305 % 100` is 95.

Two other things about fractions. `/` of two integers is a `Rat` in Raku,
which the board does not have, so it is refused. Use `div`, or make one side
a num: `$raw * 5e0 / 1023`. Also, `my num $v = 3.3` is refused by both
Raku++ and Rakudo, because `3.3` is a `Rat` literal. Write `3.3e0`.

## How do I make a lookup table, or a hash?

There are no hashes on the board. Use a constant list indexed by a number,
and compute it in Raku while building:

```raku
constant @NOTE = 262, 294, 330, 349, 392, 440, 494;    # C4 to B4, in Hz
constant @wave = (^16).map: { (127.5 + 127.5 * sin(2 * pi * $_ / 16)).Int };
```

Each list becomes a table in flash (`PROGMEM` on AVR), in the narrowest C
type that holds every element:

```cpp
static const uint16_t t_NOTE[7] RK_FLASH = { … };
static const uint8_t t_wave[16] RK_FLASH = { … };
```

`sin`, `pi` and the `.map` never reach the board, and neither does a file you
read or a module you `use` in a constant. A table must hold numbers, so a
list of names is refused. Use `if`/`elsif` to print a name. For the same
reason, write an `enum` as constants and `given`/`when` as `if`/`elsif`.

## How do I wait without blocking?

`delay` stops everything. To do two things at once, check the clock:

```raku
use Arduino;

my uint32 $last = 0;
loop {
    if (millis() - $last) +& 0xFFFF_FFFF >= 500 {
        $last = millis();
        # every half second
    }
    # the rest of the loop keeps running
}
```

The `+& 0xFFFF_FFFF` matters, and it is where Raku differs from C. On a
board, `millis()` wraps to 0 after 2³² ms, about 49.7 days. C subtracts two
`unsigned long`s and the wrap cancels out. Here the subtraction is done in
64 bits, so just after the wrap the difference is negative and the check
never passes again:

```raku
my uint32 $last = 4_294_967_000;     # just before the wrap
my uint32 $now  = 200;               # just after it
say $now - $last;                    # -4294966800
say ($now - $last) +& 0xFFFF_FFFF;   # 496
```

Storing the difference in a `uint32` also wraps it, on Raku++ and on the
board. Rakudo refuses that store ("Cannot unbox negative bigint into native
unsigned integer"), so the mask is the form that means the same everywhere.
Neither simulator wraps `millis()` at 2³², so test the arithmetic as shown
here; waiting for the wrap will not show it.

A `my` inside the top-level `loop` starts again on every pass, as it does in
Raku. Use a top-level `my`, or a `state` inside a sub, for anything that must
last between passes.

## What else is refused, and what do I write instead?

| refused on the board | write instead |
|---|---|
| a class | top-level variables and plain subs |
| a hash | a constant list, indexed by a number |
| `multi`, `proto`, a method | subs with different names |
| a closure, a block as a value | a named sub |
| a regex, a grammar, a module's routine | run it in a `constant` or `BEGIN` |
| `given`/`when` | `if`/`elsif` |
| a phaser, `CATCH`, a labelled loop | plain control flow |

Each refusal stops the build with exit 5 and the line, so nothing on this
list gets as far as the board.

## How do I check a program without a board?

Run it. Under the interpreter, `use Arduino` is a simulated board: the serial
port is stdout, and every pin change is a line on stderr.

```bash
RAKUPP_ARDUINO_STOP=2000 rakupp blink.raku                   # 2 s of board time, at once
RAKUPP_ARDUINO_STOP=1000 RAKUPP_ARDUINO_PINS="2=0,A0=512" \
    rakupp button.raku                                       # fix what the inputs read
rakupp --target=arduino blink.raku -o blink.ino              # does the board take it?
rakupp --target=arduino --sim blink.raku -o blink-sim        # the sketch itself, run here
```

The interpreter and a `--sim` build print the same output. They differ when
something goes wrong. A runtime error on the board is a short message and
the line, with no backtrace. A subscript past the end of a constant list
gives `(Any)` in the interpreter and stops the board.

## Does it run under Rakudo?

Rakudo has no Arduino target. With the stub modules installed
(`zef install Arduino`), it stops at `use Arduino` and names the `rakupp`
command to use instead. Everything on this page that does not need the board
works the same in Rakudo, with one exception noted above: Rakudo refuses to
store a negative value into an unsigned native. That covers native
wrap-around, floored `div` and `%`, shaped arrays and their range check, and
the refused `Rat` literal.
