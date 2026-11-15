# micro:bit: a Raku program on a BBC micro:bit

`rakupp --target=microbit` turns a Raku program into one C++ file for the BBC
micro:bit V2, written against CODAL, the board's own runtime (Lancaster
University's, which MakeCode runs on). CODAL's build turns it into
`MICROBIT.hex`, and copying that onto the board's `MICROBIT` drive flashes
it. The same program runs without a board, on a simulated micro:bit, under
the interpreter or compiled for this computer.

New to it? [examples/microbit](../../examples/microbit/README.md) walks from a
scrolled greeting to a die you shake, with a program for each step. Writing
your own? [The micro:bit FAQ](faq/microbit.md) answers what a MicroPython or
MakeCode user asks first.

> **Phase M0 of [MICROBIT-PLAN.md](../dev/plans/MICROBIT-PLAN.md), with parts
> of M1.** The emitter and both simulated boards are implemented and checked
> against each other byte for byte (`t/microbit/run.raku`). The C++ calls
> CODAL by the names in its own headers and samples. Not yet tried: a build
> with CODAL's toolchain (`t/microbit/run.raku --codal=DIR` does it, given a
> checkout), and a program on real hardware. Building and flashing is done
> with CODAL's `build.py` and a file copy; rakupp does not drive them yet.

```raku
use MicroBit;

loop {
    my int16 $acc = accelerometer.x;
    say $acc;
    display.show: $acc > 0 ?? Image.ARROW-E !! Image.ARROW-W;
    pause 100;
}
```

```bash
rakupp tilt.raku                                          # on the simulated board, in real time
RAKUPP_MICROBIT_STOP=600 rakupp tilt.raku                 # 0.6 s of board time, at once
rakupp --target=microbit tilt.raku -o source/main.cpp     # the C++, for CODAL's build
rakupp --target=microbit --sim tilt.raku -o tilt          # the C++, built for this computer
```

```
$ RAKUPP_MICROBIT_STOP=600 RAKUPP_MICROBIT_ACCEL="0:-200,0,-1000;300:250,0,-950" rakupp tilt.raku
-200
[0 ms] display 00900:09000:99999:09000:00900
-200
-200
250
[300 ms] display 00900:00090:99999:00090:00900
250
250
[600 ms] stop
```

## The shape of a program

CODAL's program is `main()`, and a Raku program maps onto it without naming
it:

| Raku | the C++ |
|---|---|
| `use MicroBit` | the board, as CODAL's global `MicroBit uBit`, started by `uBit.init()` at the top of `main()` |
| the mainline before the top-level `loop { }` | the start of `main()` |
| the top-level `loop { }` | `for (;;) { … }` in `main()` |
| no top-level loop | the whole program once; then the board idles, its display as the program left it |
| `next` / `last` in the top-level loop | `continue` / leave the loop, which ends the program |
| a top-level `my` variable | a global |
| `state` | a `static` local |
| `say`, `print` | the serial port over USB, at CODAL's 115200 baud |

A statement after the top-level loop is refused: the loop never ends, so the
statement would never run.

## Without a board

`use MicroBit` is part of rakupp: the engine answers it before it looks at
any `-I` path or module store. Under the interpreter its objects and
routines are a simulated board, and a `--sim` build links the C++ against a
C++ copy of the same board, a stand-in for CODAL. Both write the same two
streams:

- **stdout is the serial port.** `say` ends a line with LF, as it does
  everywhere in Raku.
- **stderr is the board.** A change of what the LEDs show is one line, the
  picture in MicroPython's own format — five rows of five brightnesses, 0 to
  9, separated by `:` — or `display clear` when they go dark:
  `[1200 ms] display 09090:99999:99999:09990:00900`. Text is one line when it
  starts, `[0 ms] scroll "Hello"` or `[0 ms] show "42"`, and the clock then
  moves on by the time CODAL takes to show it. A pin written is one line:
  `pin 8 HIGH`, `pin 16 analog 512`. A write that changes nothing writes
  nothing, so the log shows what the board would.

The clock is **real** by default: `pause(500)` waits half a second, and
Ctrl-C stops the board. With `RAKUPP_MICROBIT_STOP=MS` it is **virtual**:
`pause`, `sleep` and the display's animations advance it at once, every
other board call advances it one microsecond, so a loop that polls a sensor
still moves on, and when it passes MS milliseconds the program prints
`[MS ms] stop` and exits 0. Ten minutes of board time take milliseconds, and
give the same transcript every time.

### The inputs

| variable | what it gives | example |
|---|---|---|
| `RAKUPP_MICROBIT_BUTTONS` | presses: `A`, `B`, `logo`, and touches of the pins `0`, `1`, `2`, at a time in ms, for 100 ms or to an end | `A@1000,B@2000-2400,logo@3000` |
| `RAKUPP_MICROBIT_ACCEL` | the accelerometer's x, y, z in milli-g; flat, face up, `0,0,-1024`, unless it says | `0:-200,0,-1000;1500:300,0,-950` |
| `RAKUPP_MICROBIT_GESTURES` | gestures as they begin; each holds until the next | `1000:shake;1020:face up` |
| `RAKUPP_MICROBIT_HEADING` | the compass heading, degrees; 0 unless it says | `0:90;5000:180` |
| `RAKUPP_MICROBIT_TEMP` | the temperature, °C; 21 unless it says | `0:19;8000:23` |
| `RAKUPP_MICROBIT_LIGHT` | the light level, 0 to 255; 0 unless it says | `0:12;5000:180` |
| `RAKUPP_MICROBIT_PINS` | what a pin reads: non-zero is 1 for `read-digital`; the value for `read-analog` | `1=700,2=1` |
| `RAKUPP_MICROBIT_SEED` | `randint`'s seed: 1 on the virtual clock unless it says, the time on the real one | `7` |

A timeline is `TIME:VALUE` entries separated by `;`, each holding from its
time on; a value alone holds from the start. A pin that is not listed reads
what the program last wrote to it, else 0.

**None of this costs a program that does not ask for it.** The emitter, the
`--sim` core and the board module live in the `rakupp` command alone: an
`--exe` binary carries the `MicroBit` module only when its program `use`s it,
and never the C++ of either board.

## `use MicroBit`

The names are MicroPython's, spelt as Raku: `button_a.is_pressed()` is
`button-a.is-pressed`, `accelerometer.get_x()` is `accelerometer.x`,
`Image.ARROW_E` is `Image.ARROW-E`. A method with no arguments needs no
parentheses.

| Raku | CODAL | MicroPython |
|---|---|---|
| `display.show(PICTURE)` | `uBit.display.print(MicroBitImage(…))` | `display.show(image)` |
| `display.show(TEXT)`, `display.show(INT)` | `uBit.display.print(ManagedString(…))` | `display.show(value)` |
| `display.scroll(TEXT)`, `display.scroll(INT)` | `uBit.display.scroll(…)` | `display.scroll(value)` |
| `display.clear` | `uBit.display.clear()` | `display.clear()` |
| `display.set-pixel(X, Y, B)` | `uBit.display.image.setPixelValue(…)` | `display.set_pixel(x, y, b)` |
| `display.get-pixel(X, Y)` | `uBit.display.image.getPixelValue(…)` | `display.get_pixel(x, y)` |
| `display.read-light-level` | `uBit.display.readLightLevel()` | `display.read_light_level()` |
| `button-a.is-pressed`, `button-b.…` | `uBit.buttonA.isPressed()` | `button_a.is_pressed()` |
| `button-a.was-pressed` | `uBit.buttonA.wasPressed() > 0` | `button_a.was_pressed()` |
| `button-a.get-presses` | `uBit.buttonA.wasPressed()` | `button_a.get_presses()` |
| `pin-logo.is-touched` | `uBit.logo.isPressed()` | `pin_logo.is_touched()` |
| `accelerometer.x`, `.y`, `.z` | `uBit.accelerometer.getX()` … | `accelerometer.get_x()` … |
| `accelerometer.is-gesture(NAME)` | `uBit.accelerometer.getGesture() == …` | `accelerometer.is_gesture(name)` |
| `accelerometer.was-gesture(NAME)` | a listener on CODAL's gesture events | `accelerometer.was_gesture(name)` |
| `compass.heading` | `uBit.compass.heading()` | `compass.heading()` |
| `compass.calibrate` | `uBit.compass.calibrate()` | `compass.calibrate()` |
| `temperature` | `uBit.thermometer.getTemperature()` | `temperature()` |
| `pin0.read-digital` … `pin20` | `uBit.io.P0.getDigitalValue()` | `pin0.read_digital()` |
| `pinN.write-digital(V)` | `uBit.io.PN.setDigitalValue(…)` | `pinN.write_digital(v)` |
| `pinN.read-analog` | `uBit.io.PN.getAnalogValue()` | `pinN.read_analog()` |
| `pinN.write-analog(V)` | `uBit.io.PN.setAnalogValue(…)`, 0 to 1023 | `pinN.write_analog(v)` |
| `pin0.is-touched`, `pin1`, `pin2` | `uBit.io.P0.isTouched()` | `pin0.is_touched()` |
| `running-time` | `uBit.systemTime()` | `running_time()` |
| `pause(MS)` | `uBit.sleep(MS)` | `sleep(ms)` |
| `sleep(SECONDS)`, Raku's | `uBit.sleep(SECONDS × 1000)`, to the nearest ms | — |
| `randint(A, B)` | `A + uBit.random(B − A + 1)` | `random.randint(a, b)` |
| `Image.HEART` … | a table of 25 levels in flash | `Image.HEART` |
| `image(TEXT)`, while building | a table of 25 levels in flash | `Image(text)` |

The pins are `pin0` to `pin16`, `pin19` and `pin20`, the edge connector's;
the three big rings, `pin0`, `pin1` and `pin2`, are the touch pins. A
gesture's name is a string the build knows, a literal or a constant: `'up'`,
`'down'`, `'left'`, `'right'`, `'face up'`, `'face down'`, `'freefall'`,
`'3g'`, `'6g'`, `'8g'`, `'shake'`.

**A press counts as it begins**, as CODAL counts it: `was-pressed` and
`get-presses` read the same count and empty it, so a program asks one of
them. **A gesture is heard as it happens**: `was-gesture` has the board
listen for the accelerometer's events from the start of `main()`, as each is
raised, so a loop that never pauses still sees a shake.

## Pictures

A picture is 25 brightness levels, 0 to 9, row by row from the top left. One
is shown with `display.show`:

- one of `Image`'s 64, MicroPython's own: `Image.HEART`, `Image.SMILE`,
  `Image.ARROW-N` to `Image.ARROW-NW`, `Image.CLOCK1` to `Image.CLOCK12`,
  `Image.MUSIC-QUAVER`, `Image.DUCK`, …;
- a constant made with `image`, from five rows of five digits, or `#` (9) and
  `.` (0), by lines or separated by `:`. It runs while building, and the
  board gets 25 bytes;
- a shaped integer array of 25 the program fills as it runs, `my uint8
  @p[25]`;
- either of two pictures: `$x > 0 ?? Image.ARROW-E !! Image.ARROW-W`.

```raku
constant GLOW = image q:to/END/;
    03030
    39993
    39993
    03930
    00300
    END
constant @CROSS = image '#...#:.#.#.:..#..:.#.#.:#...#';
```

CODAL keeps a brightness from 0 to 255; the ten levels map onto it as
MicroPython's do, `0, 1, 2, 4, 8, 16, 32, 64, 128, 255`: steps the eye tells
apart. `get-pixel` reads a level back. A level outside 0 to 9 is refused while
building in a constant, and stops the program in an array.

## Text on the display

`display.show` and `display.scroll` take text made of literals, integers and
Bools, put together as the board runs: `display.scroll("T: {temperature}C")`.
An integer shows all its 64 bits; a Bool shows `True` or `False`. Either of
two texts is a text too: `display.show($ok ?? "Y" !! "N")`.

The display takes the time CODAL takes, on both simulated boards as on the
board:

- `scroll`: 120 ms a column, six columns a character (five and a space), and
  six more to clear the last: `6 × (n + 1) × 120` ms. The display is dark
  after.
- `show` of one character: at once, and it stays.
- `show` of more: each character for 400 ms after a first 400, then a blank:
  `(n + 2) × 400` ms. The display is dark after.

While text moves the program waits, as on the board: a button pressed and
let go meanwhile is still counted by `get-presses`, but `is-pressed` misses
it.

## The subset

The rest is the Arduino target's subset, from the same emitter: native types
(`int8` … `uint64`, `num`, `num32`, `Bool`), shaped native arrays, constant
tables, subs with native signatures, `if`/`while`/`loop`/`for`, and
arithmetic with Raku's meaning. [ARDUINO.md](ARDUINO.md#the-subset) has it
as a table, and [its FAQ](faq/arduino.md) what to write instead of what it
refuses.

**Numbers on a micro:bit.** Its processor is a 32-bit Cortex-M4F with a
single-precision FPU: `int32` and `int16` are what it computes natively, a
plain `int` (64 bits, as in Raku) takes two words, `num32` is in hardware and
`num` (a 64-bit `double`) in software.

A runtime error — a zero divisor, a subscript or a pixel out of range, a
brightness out of range, `die` — prints its message and line on the serial
port. A simulation then exits 1; a board shows a sad face and scrolls the
line, over and over, until it is reset.

## What the board refuses

Everything outside the subset stops the build, with exit 5 and the line:

```
$ rakupp --target=microbit -e 'use MicroBit; display.scroll(Image.HEART)'
rakupp --target=microbit: -e, line 1: display.scroll of a picture — it scrolls text; show a picture with display.show
```

Among them: untyped and boxed variables, hashes, strings kept in variables,
classes, closures, regexes, a num on the display, `image` called as the board
runs, a gesture's name the build does not know, `is-touched` on a pin that is
not a touch pin, and a name of `use MicroBit` without the `use`.

## Where it differs from the interpreter

- **`die`** prints the message and `at line N`; the interpreter's backtrace
  does not fit on a board.
- **Integer expressions** are 64-bit on the board, where an untyped Raku
  expression is an unbounded `Int`.
- **A board at rest.** When the program ends, a board keeps its display as
  it was; a simulation exits.
- **Timing.** CODAL's display runs on a scheduler tick of a few milliseconds,
  so its 120 and 400 ms steps are a little longer on a board than in a
  simulation.

## Building and flashing

```bash
git clone https://github.com/lancaster-university/microbit-v2-samples
rakupp --target=microbit tilt.raku -o microbit-v2-samples/source/main.cpp
cd microbit-v2-samples && python3 build.py
cp MICROBIT.hex /Volumes/MICROBIT/
```

CODAL's build needs the GNU Arm Embedded toolchain, CMake, Python 3 and git,
or the repository's Dockerfile; its first run fetches CODAL. The `MICROBIT`
drive is `/Volumes/MICROBIT` on a Mac, `/media/$USER/MICROBIT` on Linux, a
drive letter on Windows. The board starts the program as soon as the copy
ends.

## Not yet

The radio between micro:bits, the speaker and the microphone, `react` /
`whenever` over the board's events, strings kept in variables, the micro:bit
V1, the Calliope mini, and `rakupp` building and flashing the board itself
(`--firmware`, `--upload`). The plan has them in order:
[MICROBIT-PLAN.md](../dev/plans/MICROBIT-PLAN.md).
