# Arduino: a Raku program as firmware

`rakupp --target=arduino` turns a Raku program into one Arduino sketch: C++
that names no runtime, which the Arduino core's own compiler builds into a
firmware image the board runs on its own. The same program runs without a
board, on a simulated one, under the interpreter or compiled for this computer.

> **Phase A0 of [ARDUINO-PLAN.md](../dev/plans/ARDUINO-PLAN.md), with A1's
> constants.** The sketch and both simulators are implemented and checked
> against each other byte for byte (`t/arduino/run.raku`). Every sketch in that
> gate compiles for an Arduino Due with its own core and compiler, warning-free
> under `-Wall -Werror`, except the LED-matrix one, which needs the Uno R4
> core; with arduino-cli installed, each first goes through the IDE's own
> `.ino` step. All of them, the LED-matrix one on the Uno R4 WiFi alone, also
> build with arduino-cli 1.5.1 for an Uno (AVR core 1.8.8) and an Uno R4 WiFi
> (Renesas core 1.6.0), tried by hand rather than by the gate. Not yet tried:
> a program running on real hardware. Building and flashing the firmware is
> still done with the Arduino IDE or arduino-cli.

```raku
use Arduino;

pinMode(LED_BUILTIN, OUTPUT);
say "blink";

loop {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
}
```

```bash
rakupp blink.raku                                        # on the simulated board, in real time
RAKUPP_ARDUINO_STOP=2200 rakupp blink.raku               # 2.2 s of board time, at once
rakupp --target=arduino blink.raku -o blink.ino          # the sketch, for the Arduino IDE
rakupp --target=arduino --sim blink.raku -o blink-sim    # the sketch, built for this computer
```

```
$ RAKUPP_ARDUINO_STOP=2200 rakupp blink.raku
[0 ms] pin 13 mode OUTPUT
blink
[0 ms] pin 13 HIGH
[500 ms] pin 13 LOW
[1000 ms] pin 13 HIGH
[1500 ms] pin 13 LOW
[2000 ms] pin 13 HIGH
[2200 ms] stop
```

## The shape of a program

An Arduino sketch is two functions, and a Raku program maps onto them without
naming either:

| Raku | the sketch |
|---|---|
| the mainline before the top-level `loop { }` | `setup()` |
| one iteration of the top-level `loop { }` | one call of `loop()`: the core does its own work between calls |
| no top-level loop | the whole program in `setup()`; it runs once and the board stops |
| `next` / `last` in the top-level loop | return from `loop()` / stop the board |
| a top-level `my` variable | a global |
| `state` | a `static` local |
| `use Arduino` | the core's API, by its own names |

A statement after the top-level loop is refused: the loop never ends, so the
statement would never run.

## Without a board

`use Arduino` is part of rakupp: the engine answers it before it looks at any
`-I` path or module store. Under the interpreter its routines are a simulated
board, and a `--sim` build links the sketch against a C++ copy of the same
board. Both write the same two streams:

- **stdout is the serial port.** `say` ends a line with LF, as it does
  everywhere in Raku; `Serial.println` ends it with CR LF, as a board does.
- **stderr is the board.** Each pin change is one line: `[1500 ms] pin 13
  HIGH`, `pin 9 pwm 128`, `pin 2 mode INPUT_PULLUP`. A write that changes
  nothing writes nothing, so the log shows what an LED would.

The clock is **real** by default: `delay(500)` waits half a second, and
Ctrl-C stops the board. With `RAKUPP_ARDUINO_STOP=MS` it is **virtual**:
`delay` and `delayMicroseconds` advance it at once, every other board call
(`pinMode`, `digitalWrite`, `millis`…) advances it one microsecond, so a loop
that polls `millis` still moves on, and when it passes MS milliseconds the
program prints `[MS ms] stop` and exits 0. A run of ten minutes of board time
takes milliseconds, and gives the same transcript every time.

Inputs are fixed from outside with `RAKUPP_ARDUINO_PINS`:

```bash
RAKUPP_ARDUINO_STOP=1000 RAKUPP_ARDUINO_PINS="2=0,A0=512" rakupp button.raku
```

`digitalRead` of a pin listed there answers HIGH for a non-zero value;
`analogRead` answers the value. An unlisted pin reads its own output level,
HIGH under `INPUT_PULLUP`, else LOW, and `analogRead` answers 0.

The simulated board numbers its pins as an Uno does (`LED_BUILTIN` is 13,
`A0` is 14). A sketch uses the core's names, so on a real board they mean that
board's pins.

**None of this costs a program that does not ask for it.** The emitter, the
`--sim` core and the board modules live in the `rakupp` command alone: an
`--exe` binary carries the `Arduino` module only when its program `use`s it,
and at `--slim`'s default level one whose program does not has no trace of
the backend at all. `t/run.raku` checks both.

**The two simulators agree byte for byte.** `rakupp t/arduino/run.raku` runs
each case under the interpreter, holds it to its golden, builds its sketch with
`--sim`, and holds the sketch to the interpreter: stdout, stderr and exit
status. That is how the C++ the backend writes is tested without a board.

## On a board

The Arduino IDE wants a sketch in a folder of its own name:

```bash
mkdir blink && rakupp --target=arduino blink.raku -o blink/blink.ino
arduino-cli compile --fqbn arduino:sam:arduino_due_x blink
arduino-cli upload  --fqbn arduino:sam:arduino_due_x -p /dev/cu.usbmodem1101 blink
```

The sketch is C++11 with the core's own headers, so any core that compiles
C++11 can take it. It needs no library. Above `setup()` it carries the helpers
its program calls, and only those: Raku's arithmetic where C's differs (`div`
and `%` floor, 64-bit integers wrap rather than overflow), array indexes
checked, and the message and line a runtime error prints before the board
halts. Blink calls none, so its sketch is the includes, `setup()` and
`loop()`, as the IDE's own Blink is. Measured with the Due core 1.6.12 and
its arm-none-eabi-gcc 4.8.3 (`-Os`, the sketch's object alone, the core not
linked): blink is 102 bytes of code, and `t/arduino/cases/features.raku`, its
tables included, is 1,830 bytes of code and 29 of RAM.

## Raku while building

The code of an Arduino program is two functions; most of its work is data —
tables, pin maps, curves, strings. Here the program computes them itself, in
full Raku, and the board gets only the numbers. **Every `constant` and every
top-level `BEGIN` block runs while building**, in the interpreter:

```raku
constant LED   = 9;
constant @wave = (^32).map: { (127.5 + 127.5 * sin(2 * pi * $_ / 32)).Int };
constant @primes = (2..50).grep(*.is-prime);
constant GREETING = "built at compile time: " ~ 2 ** 4;
my uint16 $boot;
BEGIN { $boot = 6 * 7 }
```

become

```cpp
static const uint8_t t_wave[32] RK_FLASH = {
    127, 152, 176, 198, 217, 233, 245, 252, 255, 252, 245, 233,
    ...
};
static const uint8_t t_primes[15] RK_FLASH = { 2, 3, 5, ... 47 };
```

with `LED`, `GREETING` and `$boot`'s 42 written where they are used. A list of
numbers is a table of the narrowest C type that holds every element, in flash
(`PROGMEM` on AVR); a string is a literal in flash; `.elems` of a table is a
number. A file read, a grammar or a module used in a constant runs on the
computer and never reaches the board —
[t/arduino/cases/pinmap.raku](../../t/arduino/cases/pinmap.raku) reads its
pins from a JSON file with `use JSON::Native`, and its sketch holds three
bytes. A `use` of any module is allowed for that reason; a call into a module
from the board's own code is refused where it is made. A constant whose value
is not a number, a string or a list of numbers is refused, and so is a `BEGIN`
block whose effect the build could not compute.

## The LED matrix (Uno R4 WiFi)

The Uno R4 WiFi has a 12×8 matrix of red LEDs. `use Arduino::LEDMatrix`,
built into rakupp as `use Arduino` is, names it:

```raku
use Arduino;
use Arduino::LEDMatrix;

constant @HEART = frame q:to/END/;
    ..##...##...
    .#..#.#..#..
    .#...#...#..
    .#.......#..
    ..#.....#...
    ...#...#....
    ....#.#.....
    .....#......
    END

LEDMatrix.begin;
loop {
    LEDMatrix.show(@HEART);  delay(500);
    LEDMatrix.clear;         delay(500);
}
```

`frame` runs while building: it turns eight rows of twelve — `#` (or `*`,
`X`, `O`, `1`) lit, `.` dark — into the three 32-bit words the core's
Arduino_LED_Matrix library takes, row by row from the top left, the most
significant bit first. This heart is the one in Arduino's own LED-matrix
tutorial, and it packs to the tutorial's words, `3184a444 44042081 100a0040`.
On the board the constant is twelve bytes in flash; `frame` never gets there.
A picture with a mistake in it stops the build with what is wrong.

| Raku | on the board |
|---|---|
| `LEDMatrix.begin` | `matrix.begin()` |
| `LEDMatrix.show(@frame)` | `matrix.loadFrame(…)`: `@frame` is a constant of three words, or a shaped integer array of three the board computes as it runs (`my uint32 @f[3]`) |
| `LEDMatrix.clear` | a dark frame |

The sketch includes `Arduino_LED_Matrix.h`, which comes with the Uno R4
core, so it builds for an Uno R4 WiFi and not for a board without the
matrix. The simulated board draws each frame that changes the picture on
stderr, with `#` and `.`, and an all-dark one as `matrix clear`:

```
[300 ms] matrix
  ............
  ...##..##...
  ...##..##...
  ............
  ..#......#..
  ...#....#...
  ....####....
  ............
```

Not yet: animations (`loadSequence`, `play`) and text. Both would be frames
computed while building, from a font table in Raku.

## The subset

| Raku | on the board |
|---|---|
| `int`, `int8` … `int64`, `uint8` … `uint64`, `byte` | that C integer type |
| `num`, `num64`, `num32` | `double`, `float` |
| `Bool` | `bool` |
| `my uint8 @buf[16]` | a C array, every subscript bounds-checked |
| `constant`, top-level `BEGIN` | values computed while building |
| `sub f(int16 $x --> int16) { … }` | a C++ function; the last statement is its result, as in Raku |
| `if`/`elsif`/`else`/`unless`, `while`/`until`, `repeat`, `loop (;;)` | the same in C++ |
| `for ^N`, `for A..B`, `for @table` | a counted C loop |
| `+ - * div % %% **`, `+& +| +^ +< +>`, `min max`, comparisons, `&& \|\| !` | the same, with Raku's meaning |
| `say`, `print`, `note`, `Serial.print`/`println` of literals, integers and Bools | serial writes |
| `abs sqrt sin cos tan exp log atan2`, `.Int .floor .ceiling .round` | `<math.h>` |
| `pinMode digitalWrite digitalRead analogRead analogWrite delay delayMicroseconds millis micros`, `Serial.begin` | the core's own |

**Integers are computed in 64 bits** with Raku's native wrap-around, and
stored by truncation, as the interpreter stores into a native of that size:
`my int8 $t = 127; $t += 1` is −128 on both. `div` and `%` floor, as Raku's
do. `/` of two integers is a `Rat` in Raku, which the board does not have, so
it is refused; make one side a num, or use `div`. A plain `int` is 64 bits, as
in Raku, which no Arduino does natively; `int16` or `uint8` is what a small
board wants.

A runtime error — a zero divisor, a subscript out of range, `die` — prints its
message and line on the serial port and stops the board. A simulation exits 1.

When the program prints and never calls `Serial.begin`, `setup()` starts with
`Serial.begin(9600)`.

## What the board refuses

Everything else stops the build, with exit 5 and the line:

```
$ rakupp --target=arduino -e 'my $x = 5; say $x'
rakupp --target=arduino: -e, line 1: the untyped variable $x — give it a native type, as in `my int16 $x`
```

Among them: untyped and boxed variables (`Int`, `Str`), hashes, strings built
at run time, classes, closures, multi subs, phasers, regexes, calling a
module's routines, and printing a num. There is no fallback: `--exe`
bundles what it cannot compile with the interpreter, and a board has no
interpreter to bundle.

## Where it differs from the interpreter

- **A subscript past the end of a constant list** is Nil in Raku, with a
  warning; a board has no Nil, so it stops with `Index out of range`.
- **`die`** prints the message and `at line N`; the interpreter's backtrace
  does not fit on a board.
- **Integer expressions** are 64-bit on the board, where an untyped Raku
  expression is an unbounded `Int`: a product that passes 2⁶³ wraps.
- **`millis()`** wraps at 2³² milliseconds (49.7 days) on a board, as the core
  defines it.

## Not yet

Printing a num, strings at run time, hashes, `repr('CStruct')` classes,
interrupts, `Serial` input, C++ libraries beyond the LED matrix (Servo,
Wire), the board's facts as constants (`--board`), and `rakupp` building and
flashing the firmware itself.
The plan has them in order: [ARDUINO-PLAN.md](../dev/plans/ARDUINO-PLAN.md).
