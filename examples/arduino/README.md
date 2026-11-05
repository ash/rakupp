# Raku on an Arduino

Raku++ turns a Raku program into an Arduino sketch: plain C++ that the
Arduino IDE builds and uploads like any other. You need no board to start.
The same program runs on a simulated board, which shows on your screen what
the board would do: the serial port, and every pin as it changes.

This is a walkthrough, from a blinking LED to the Uno R4 WiFi's LED matrix,
with a program for each step in this directory. The full reference is
[docs/guide/ARDUINO.md](../../docs/guide/ARDUINO.md).

| Program | What it shows |
|---|---|
| [`blink.raku`](blink.raku) | The shape of a program: `setup()` and `loop()` without naming either |
| [`button.raku`](button.raku) | Inputs, and fixing them on the simulated board |
| [`fade.raku`](fade.raku) | A table computed in full Raku while building; the board gets only the numbers |
| [`raku-icon.raku`](raku-icon.raku) | A picture on the Uno R4 WiFi's 12×8 LED matrix |

## What you need

- **Raku++.** The [install page](https://raku.online/install/) has every way
  to get `rakupp`. Nothing else is needed for the simulated board.
- **For a real board:** the [Arduino IDE](https://www.arduino.cc/en/software)
  2, or [arduino-cli](https://arduino.github.io/arduino-cli/), with your
  board's core installed from the Boards Manager:

  | Board | Core in the Boards Manager | FQBN, for arduino-cli |
  |---|---|---|
  | Arduino Uno | Arduino AVR Boards | `arduino:avr:uno` |
  | Arduino Uno R4 WiFi | Arduino UNO R4 Boards | `arduino:renesas_uno:unor4wifi` |
  | Arduino Due | Arduino SAM Boards | `arduino:sam:arduino_due_x` |

Every program here builds for these three boards with their own cores (the
matrix one for the Uno R4 WiFi only), and runs the same on both simulated
boards: rakupp's interpreter, and the sketch compiled for your computer.

## 1. Blink, without a board

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

An Arduino sketch is two functions, and the program maps onto them by its
shape. The code before the top-level `loop { }` is `setup()`, and one pass
of the loop is one call of `loop()`. `use Arduino` gives you the core's own
names, so the program reads like Arduino code.

```bash
rakupp blink.raku
```

The board's clock is real: the LED changes every half second until you press
Ctrl-C. With `RAKUPP_ARDUINO_STOP` the clock is virtual instead, and that many
milliseconds of board time pass at once:

```
$ RAKUPP_ARDUINO_STOP=2200 rakupp blink.raku
blink
[0 ms] pin 13 mode OUTPUT
[0 ms] pin 13 HIGH
[500 ms] pin 13 LOW
[1000 ms] pin 13 HIGH
[1500 ms] pin 13 LOW
[2000 ms] pin 13 HIGH
[2200 ms] stop
```

**stdout is the serial port** (`blink`), and **stderr is the board**: one line
for each pin change. The simulated board numbers its pins as an Uno does, so
`LED_BUILTIN` is 13. In the sketch, the names are your board's own.

## 2. Inputs

[`button.raku`](button.raku) lights the LED while a button on pin 2 is held,
and reports each press on the serial port. On the simulated board, an input
reads what `RAKUPP_ARDUINO_PINS` says. `2=0` holds the button down:

```
$ RAKUPP_ARDUINO_STOP=100 RAKUPP_ARDUINO_PINS="2=0" rakupp button.raku
pressed
[0 ms] pin 2 mode INPUT_PULLUP
[0 ms] pin 13 mode OUTPUT
[0 ms] pin 13 HIGH
[100 ms] stop
```

`analogRead` reads a value the same way: `RAKUPP_ARDUINO_PINS="A0=512"`. A pin
that isn't listed reads its own output level: HIGH under `INPUT_PULLUP`, LOW
otherwise.

Every variable in a board program has a native type: `my Bool $down`,
`state uint8 $i`, `my int16 $x`. A board has no untyped variables, and
rakupp tells you where it found one, before anything is built:

```
$ rakupp --target=arduino -e 'my $x = 5; say $x'
rakupp --target=arduino: -e, line 1: the untyped variable $x — give it a native type, as in `my int16 $x`
```

## 3. Onto a board

The Arduino IDE wants each sketch in a folder of its own name:

```bash
mkdir blink
rakupp --target=arduino blink.raku -o blink/blink.ino
```

Open `blink/blink.ino` in the Arduino IDE, choose your board and port, and
press Upload. You can also paste the sketch into a new IDE sketch, in place
of the empty `setup()` and `loop()` it starts with. With arduino-cli:

```bash
arduino-cli compile --fqbn arduino:avr:uno blink
arduino-cli upload  --fqbn arduino:avr:uno -p /dev/cu.usbmodem1101 blink
```

The port is the one `arduino-cli board list` shows. The sketch says `say` but
never `Serial.begin`, so the sketch's `setup()` starts with
`Serial.begin(9600)`. Open the IDE's Serial Monitor at 9600 baud to see
`blink`.

> **With rakupp 5.6.2,** the IDE stops with `'T' does not name a type`. The
> IDE adds a declaration for each function of a `.ino` file, and gets the
> helper templates of that version's sketches wrong. The next release
> writes sketches it accepts. Until then, put the sketch in a `.cpp` file
> beside an empty `.ino`. The IDE compiles a `.cpp` file as it is:
>
> ```bash
> mkdir blink
> rakupp --target=arduino blink.raku -o blink/raku.cpp
> touch blink/blink.ino
> ```
>
> When pasting into the IDE instead, add a tab named `raku.cpp` (New Tab, in
> the menu at the right end of the tab bar), paste the sketch there, and
> empty the `.ino` tab.

## 4. Raku while building

```raku
constant LED   = 9;   # a PWM pin
constant @wave = (^32).map: { (127.5 + 127.5 * sin(2 * pi * $_ / 32)).Int };
```

Every `constant` is computed while the program is built, in full Raku, on
your computer: `sin`, `pi`, `.map`, a file read, a grammar, a module. The
board gets only the result. In [`fade.raku`](fade.raku) that is 32 bytes of
sine wave in flash:

```
$ rakupp --target=arduino fade.raku | grep -A3 t_wave
static const uint8_t t_wave[32] RK_FLASH = {
    127, 152, 176, 198, 217, 233, 245, 252, 255, 252, 245, 233,
    217, 198, 176, 152, 127, 102, 78, 56, 37, 21, 9, 2,
    0, 2, 9, 21, 37, 56, 78, 102
```

A list of numbers becomes a table of the narrowest C type that holds them
all; on an Uno it lives in flash (`PROGMEM`), not in the 2 KB of RAM.

## 5. The LED matrix (Uno R4 WiFi)

```raku
use Arduino;
use Arduino::LEDMatrix;

constant @RAKU = frame q:to/END/;
    ####....###.
    #...#..#...#
    ####...#####
    #...#..#...#
    ............
    #..#...#...#
    ###....#...#
    #..#....###.
    END

LEDMatrix.begin;
LEDMatrix.show(@RAKU);
```

`frame` turns a picture of 8 rows of 12 into the three 32-bit words the
matrix library takes. It runs while building too, so the board gets twelve
bytes. On the simulated board, each new picture is drawn on stderr:

```
$ RAKUPP_ARDUINO_STOP=10 rakupp raku-icon.raku
[0 ms] matrix
  ####....###.
  #...#..#...#
  ####...#####
  #...#..#...#
  ............
  #..#...#...#
  ###....#...#
  #..#....###.
```

`LEDMatrix.show` also takes a picture the board computes as it runs, a
`my uint32 @frame[3]`, and `LEDMatrix.clear` darkens the matrix.

## What a board program can use

- **Native types:** `int8` … `int64`, `uint8` … `uint64`, `byte`, `num`,
  `num32`, `Bool`, and shaped arrays of them, such as `my uint8 @buf[16]`.
  Every subscript is checked.
- **Arithmetic with Raku's meaning:** integers are computed in 64 bits,
  `div` and `%` round down, and a value is cut to its type's width when it
  is stored.
- **Control flow:** `if`, `unless`, `while`, `until`, `repeat`, `loop`,
  `for ^N`, `for A..B`, `for @table`, `next`, `last`.
- **Subs with native signatures:** `sub f(int16 $x --> int16) { … }`.
- **The Arduino core's functions:** `pinMode`, `digitalWrite`,
  `digitalRead`, `analogRead`, `analogWrite`, `delay`, `delayMicroseconds`,
  `millis`, `micros`, `Serial`. Also `say`, `print` and `note` of literals,
  integers and Bools.

Hashes, strings built at run time, classes, regexes and closures stop the
build with the line they are on. Most of these are planned: the guide's
[Not yet](../../docs/guide/ARDUINO.md#not-yet) section lists them.

**Choose types that fit.** A plain `int` is 64 bits, as in Raku, which no
Arduino does natively; `int16` or `uint8` is what a small board wants. A
native type wraps: `my int8 $d = 700` stores −68, and `delay(-68)` asks for
4,294,967,228 ms, about 50 days. For delays, `uint16` holds up to 65,535.

## Under Rakudo

`use Arduino` and `use Arduino::LEDMatrix` are built into rakupp:
`rakupp install` skips both names and says so. Other Raku implementations
can install stub modules of the same names:

```bash
zef install Arduino Arduino::LEDMatrix
```

They stop the program at the `use` line and say what it needs:

```
$ rakudo blink.raku
===SORRY!=== Error while compiling blink.raku
This program needs Raku++: `use Arduino` is built into rakupp
(https://github.com/ash/rakupp), which runs it against a simulated board
and compiles it for a real one:
    rakupp blink.raku
    rakupp --target=arduino blink.raku

at blink.raku:9
------> <BOL><HERE>use Arduino;
```

Without the stubs, Rakudo says only `Could not find Arduino`.

## More

- [docs/guide/ARDUINO.md](../../docs/guide/ARDUINO.md): everything above in
  full, with the subset as a table and where a board differs from the
  interpreter.
- [raku.online/arduino](https://raku.online/arduino/): this walkthrough's
  page on the web.
- [ARDUINO-PLAN.md](../../docs/dev/plans/ARDUINO-PLAN.md): what comes next,
  in order.
