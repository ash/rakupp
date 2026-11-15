# Plan: MICROBIT — a Raku program as micro:bit firmware

*Written 2026-11-09, before any code: a design draft for review, answering
#226 ("Micro:Bit support"). It describes the board, picks one way to program
it in Raku — `--target=microbit`, the Arduino target's emitter writing C++ for
the board's own runtime, CODAL — and lists what that target shares with
[ARDUINO-PLAN.md](ARDUINO-PLAN.md) and what it adds. M0 landed on
2026-11-15, with parts of M1; what landed and where it departs from this
draft is in "Where it stands".*

## Where it stands (2026-11-15)

`--target=microbit` emits one C++ file for CODAL; `--target=microbit --sim`
builds it for the host against a simulated CODAL
(`src/codegen/MicroBitSim.h`); `rakupp prog.raku` runs a `use MicroBit`
program on the same simulated board (`rakulib/MicroBit.rakumod`). User
guide: [MICROBIT.md](../../guide/MICROBIT.md), with
[a FAQ](../../guide/faq/microbit.md) and
[a walkthrough](../../../examples/microbit/README.md). Gate:
`t/microbit/run.raku` (45 checks, ~7 s), one check in `t/run.raku`.

Measured on the gate's ten programs: each one's transcript under the
interpreter equals its golden, and its `--sim` C++ writes the same bytes on
stdout and stderr and exits the same way; so do the eight examples. The
C++ names CODAL by its own headers and samples (`uBit.display.print`,
`uBit.buttonA.wasPressed`, `uBit.messageBus.listen`, `MicroBitImage(5, 5,
bitmap)`, `target_random` through `uBit.random`), and CODAL's own build
takes it: with `--codal=DIR` the gate builds each of its ten programs into a
`MICROBIT.hex` in a checkout of microbit-v2-samples (55 checks), and the
eight examples build too, each with no warning from the program and about
100 KB of flash, nearly all CODAL's. Not yet tried: real hardware.

| phase | state |
|---|---|
| M0 | done: the board seam (`src/codegen/BoardEmitter.h`, every Arduino sketch byte for byte what it was), `--target=microbit` and `--sim`, the display, buttons, accelerometer, temperature, pins, `running-time`, `pause`, `sleep`, pictures while building, `say`, `die`; every gate program and example builds with CODAL |
| M1 | the display's text joined at run time (literals, integers, Bools), `was-pressed` and `get-presses`, the compass, the light level, the touch logo, gestures (`is-gesture`, `was-gesture`), `randint`; not yet: `Str` variables, the speaker and microphone |
| M2 | not started: `react` / `whenever`, the radio |
| M3 | not started: `--firmware`, `--upload`, the size report |

Where the implementation departs from the draft below, and why:

- **The seam is a subclass.** The draft's "board description" is
  `board::Emitter` in `BoardEmitter.h`, with hooks (`boardName`, `boardCall`,
  `boardMethod`, `boardStmt`, `boardPrint`, `lit`, `outStream`,
  `loopIsFunction`, `assemble`); `Arduino.cpp` and `MicroBit.cpp` are its two
  subclasses. The runtime's arithmetic and bounds checks are shared pieces
  (`sharedPieces`); each board lists its own among ranges of them
  (`boardPieces`).
- **`Image.HEART`, not `Image::HEART`** (the first open question). It is how
  MicroPython writes it, which is where pupils come from: a method of the
  module's `Image` class under the interpreter, a table the emitter makes on
  first use on the board. The names are kebab-case (`Image.ARROW-NE`).
- **`sleep` keeps Raku's meaning, and `pause` is MakeCode's** (the second).
  The build does not warn about `sleep 100` yet.
- **No yield in the top-level loop** (the third). What needed one was
  `was-gesture` in a loop that never pauses: CODAL raises gestures as a
  sample is read, and queues a listener's call for the scheduler. The
  listener is registered with `MESSAGE_BUS_LISTENER_IMMEDIATE`, so it runs
  as the event is raised, and `rk_was_gesture` reads a sample first.
- **`die` on the display** (the fourth): after the message on the serial
  port, a board shows a sad face and scrolls `line N`, over and over.
- **MicroPython's brightness table** (the fifth): levels 0 to 9 are
  `0, 1, 2, 4, 8, 16, 32, 64, 128, 255`, and a level read back is a value's
  highest bit, as micropython-microbit-v2 maps them.
- **Text is logged as text.** A scroll or a multi-character show is one line
  with its text, and the clock moves by CODAL's own timing (`6 × (n + 1) ×
  120` ms, `(n + 2) × 400` ms); a single character is drawn from CODAL's own
  font, so `get-pixel` reads it.
- **The inputs are timelines.** `RAKUPP_MICROBIT_ACCEL`, `_GESTURES`,
  `_HEADING`, `_TEMP` and `_LIGHT` take `TIME:VALUE;…`; buttons, the logo and
  the touch pins are presses with a start and an end
  (`RAKUPP_MICROBIT_BUTTONS="A@1000,logo@2000-2400,0@3000"`); `randint` is
  xorshift32 in both simulations, seeded by `RAKUPP_MICROBIT_SEED`.
- **The stub dist** is `MicroBit` in ash/raku-modules (2026-11-15), the
  Arduino stub with the name changed; its test passes on Rakudo v2026.09, on
  a Raku++ without the name and on one with it. Not yet uploaded: with the
  release that ships the target, as for Arduino.

## The board

The BBC micro:bit is a teaching board. The BBC gave one to every Year 7 pupil
in the UK in 2016; the Micro:bit Educational Foundation runs it now, and
schools elsewhere use it as the step after block programming. Everything a
lesson needs is on the board:

| | v1 (2016–2020) | v2 (2020–) |
|---|---|---|
| CPU | nRF51822, Cortex-M0, 16 MHz | nRF52833, Cortex-M4F, 64 MHz |
| flash / RAM | 256 KB / 16 KB | 512 KB / 128 KB |
| `double` | software | software; `float` in hardware |
| runtime | microbit-dal | CODAL (`codal-microbit-v2`) |

Both have a 5×5 LED display with brightness levels, buttons A and B, an
accelerometer and a compass, a temperature sensor, light sensing through the
LEDs, Bluetooth LE, a simple radio of their own between micro:bits, and an
edge connector whose three large rings take crocodile clips. v2 adds a
microphone, a speaker and a touch-sensitive logo.

Plugged in over USB, the board is a drive called `MICROBIT`: a `.hex` file
copied onto it is flashed and runs. Schools program it in three ways:

- **MakeCode**: blocks and the TypeScript behind them, compiled in the
  browser and linked to a runtime built on CODAL.
- **MicroPython**: a prebuilt interpreter with the script stored in the hex.
  The course #226 links to (python-online.ch, Swiss, in German and English)
  is written in it:

  ```python
  from microbit import *

  while True:
      acc = accelerometer.get_x()
      print(acc)
      if acc > 0:
          display.show(Image.ARROW_E)
      else:
          display.show(Image.ARROW_W)
      sleep(100)
  ```

- **C++ on CODAL**, Lancaster University's runtime, which MakeCode itself sits
  on (`microbit-v2-samples`: `python build.py` gives `MICROBIT.hex`).

The Calliope mini, the German board that the same course also teaches, uses
the micro:bit v2's chip family in its version 3 and has its own CODAL target.

## The constraint

A v2 has more room than an Uno R4 (128 KB of RAM against 32 KB) and no more
OS than any Arduino. What ARDUINO-PLAN.md says about the runtime holds here
word for word: none of `librakupp_rt.a` fits, so what runs on the board is C++
that names no runtime. The Arduino target already is that: a walk over the
AST in [src/codegen/Arduino.cpp](../../../src/codegen/Arduino.cpp) that turns
the board subset (native scalars, shaped native arrays, subs with native
signatures, constants computed while building) into C++ and refuses the rest
with its line. The micro:bit target is that emitter with a different board.

## The options

### 1. Through the Arduino target, with a third-party core

The community arduino-nRF5 core lists the micro:bit v2, so today's
`--target=arduino` sketches (`pinMode`, `digitalWrite`) may compile for it.

**Not chosen.** The display, the buttons and the radio would each need an
outside library; the core's tracker has open v2 reports ("Only serial works –
micro:bit v2", Bluetooth); and it is not what the board's own tools use. One
probe is worth an hour: compile `t/arduino/cases` for it and record what
happens.

### 2. Its own target on CODAL

`rakupp --target=microbit` writes one `main.cpp` against CODAL's `MicroBit`
object; `microbit-v2-samples` builds it into `MICROBIT.hex`.

**Chosen.** CODAL is the runtime MakeCode stands on, so the display, the
buttons, the sensors, the radio and the event bus are the board's own,
maintained by the people who make it. The emitter is the Arduino one.

### 3. Raku on the computer, the board driven over USB

**Not chosen**, for the reasons ARDUINO-PLAN.md gives for Firmata: the board
does nothing on its own, and nothing is ever saved to it.

### 4. A browser-only chain

Edit, simulate, build and flash in a web page, as MakeCode and MicroPython
do: a prebuilt runtime hex, the program written into it as bytecode, flashed
over WebUSB.

**Set aside (2026-11-09).** It is the route a locked-down school computer
needs, but it is a second back end and a virtual machine on the board. The
target below comes first; its subset checks, `use MicroBit` and its simulated
board are what a browser chain would reuse.

### 5. Raku translated to MicroPython

**Not chosen.** The program would run on Python's runtime, with Python's
errors and line numbers, and rakupp would own a Python back end.

## What a program looks like

The course's program in Raku:

```raku
use MicroBit;

loop {
    my int16 $acc = accelerometer.x;
    say $acc;
    display.show: $acc > 0 ?? Image::ARROW-E !! Image::ARROW-W;
    pause 100;
}
```

and roughly the `main.cpp` it should become (the image literal's form is
CODAL's, to be checked against its headers):

```cpp
// Generated by `rakupp --target=microbit` from tilt.raku
#include "MicroBit.h"
#include <stdint.h>

MicroBit uBit;
static const uint8_t c_ARROW_E[] __attribute__((aligned(4))) = {0xff, 0xff, 5, 0, 5, 0, /* 25 levels */};
static const uint8_t c_ARROW_W[] __attribute__((aligned(4))) = {0xff, 0xff, 5, 0, 5, 0, /* 25 levels */};

int main() {
    uBit.init();
    for (;;) {
        int16_t nv_acc = (int16_t)uBit.accelerometer.getX();
        rk_say(nv_acc);
        uBit.display.print(nv_acc > 0 ? rk_image(c_ARROW_E) : rk_image(c_ARROW_W));
        uBit.sleep(100);
    }
}
```

**The mapping.**

| Raku | on the board |
|---|---|
| the mainline before the top-level `loop { }` | `main()`, after `uBit.init()` |
| the top-level `loop { }` | `for (;;) { … }` in `main()` (see the open question on yielding) |
| a mainline with no top-level loop | `main()`, ending with `release_fiber()`, so event handlers keep running |
| `next` / `last` in the top-level loop | `continue` / leave the loop and `release_fiber()` |
| a top-level `my` variable, `state`, `constant`, a sub with a native signature | as on Arduino |
| `say`, `print`, `note` | `uBit.serial` at CODAL's 115,200 baud, MicroPython's too; `say` ends the line with LF |
| `use MicroBit` | the `MicroBit` object's members, by the table below |
| `die`, a zero divisor, a subscript out of range | the message and line over serial, then a halt (open question: also on the display) |

## `use MicroBit`

Answered by the engine before any path or store, as `use Arduino` is: a shadow
module, `rakulib/MicroBit.rakumod`, embedded by `cmake/EmbedTools.cmake` and
listed in `isShadowedModule` and in `SHADOWED` in `tools/install.raku`. Under
the interpreter it is the simulated board; under `--target=microbit` the
emitter knows each name and writes CODAL's call. A program's own sub of the
same name is taken first, as with Arduino.

**The names are MicroPython's, spelt as Raku.** The Arduino module uses the
core's own names because Arduino programmers know them. micro:bit pupils know
MicroPython's (or MakeCode's), not CODAL's, so the module follows the
language they come from:

| Raku | CODAL | MicroPython |
|---|---|---|
| `display.show(IMG)`, `display.show(INT)` | `uBit.display.print(…)` | `display.show` |
| `display.scroll(…)` | `uBit.display.scroll(…)` | `display.scroll` |
| `display.set-pixel(X, Y, B)`, `display.get-pixel(X, Y)` | `uBit.display.image.setPixelValue` / `getPixelValue` | `display.set_pixel` / `get_pixel` |
| `display.clear` | `uBit.display.clear()` | `display.clear` |
| `button-a.is-pressed`, `button-b.is-pressed` | `uBit.buttonA.isPressed()` | `button_a.is_pressed()` |
| `button-a.was-pressed`, `.get-presses` | a click listener and a counter the emitter adds | `button_a.was_pressed()` |
| `accelerometer.x`, `.y`, `.z` | `uBit.accelerometer.getX()` … | `accelerometer.get_x()` … |
| `compass.heading` | `uBit.compass.heading()` | `compass.heading()` |
| `temperature` | `uBit.thermometer.getTemperature()` | `temperature()` |
| `pin0.read-digital`, `.write-digital(V)`, `.read-analog`, `.write-analog(V)` | `uBit.io.P0.getDigitalValue()` … | `pin0.read_digital()` … |
| `running-time` | `uBit.systemTime()` | `running_time()` |
| `pause(MS)` | `uBit.sleep(MS)` | `sleep(MS)` |
| `sleep(SECONDS)`, Raku's own | `uBit.sleep(SECONDS × 1000)` | — |

**Pictures are computed while building**, as `frame` packs one for the Uno R4
WiFi's matrix. `image` takes MicroPython's form, a brightness from 0 to 9 per
LED, row by row:

```raku
constant SMILE = image q:to/END/;
    09090
    00000
    90009
    09990
    00000
    END
```

or `#` and `.` for 9 and 0, as `frame` takes them. On the board the constant
is 25 bytes in flash, at CODAL's 0–255 scale; `image` never gets there.
`Image::HEART`, `Image::HAPPY`, `Image::ARROW-N` … are MicroPython's built-in
pictures, made the same way.

**The first strings at run time.** `display.scroll` is the micro:bit's "hello
world", and a teaching program scrolls what it measured:
`display.scroll("T: {temperature}")`. The Arduino subset has only literals.
CODAL has `ManagedString`, reference-counted, with `+` and a constructor from
an integer, and a v2 has the RAM for it. So an interpolation of literals and
native integers, passed straight to `display.scroll`, `say` or `radio.send`,
becomes a `ManagedString` concatenation. `Str` variables come after that,
with `ManagedString` as their type.

**Events, through `react`.** MakeCode teaches "on button A pressed", and
CODAL already has the event bus and the fibers to run handlers. The Raku form
is `react` / `whenever`, which also runs under the interpreter:

```raku
use MicroBit;

react {
    whenever button-a.pressed { display.show: Image::HAPPY }
    whenever button-b.pressed { display.show: Image::SAD }
}
```

Each `whenever` becomes a handler and
`uBit.messageBus.listen(DEVICE_ID_BUTTON_A, DEVICE_BUTTON_EVT_CLICK, handler)`;
the end of `react` becomes `release_fiber()`. A handler reads and writes
top-level variables; it closes over nothing else. Under the interpreter,
`button-a.pressed` is a `Supply` that the simulated presses feed. On Arduino
this is the hard last phase (A5); on CODAL the scheduler is already there.
The radio is the same shape: `radio.on`, `radio.send(…)`,
`whenever radio.received -> $msg { … }`.

**The stub.** As with `Arduino`: an ecosystem `MicroBit` dist whose whole body
dies with "This program needs Raku++", so other implementations stop at the
`use` with a reason. It is uploaded by hand, never by a build.

## Numbers

As on the Due, with one difference. `int` keeps 64 bits, two words per
operation on a 32-bit core; `int32` and `int16` are what the board wants, and
the guide says so. `num` is a 64-bit `double` in software. The Cortex-M4F has
a single-precision FPU, so `num32` is in hardware, and is the type the guide
recommends for sensor arithmetic.

## The simulated board

The same rules as Arduino's: `rakulib/MicroBit.rakumod` under the interpreter,
and a host core, `src/codegen/MicroBitSim.h`, a fake `MicroBit.h` with the
same names, that a `--sim` build links. Both write the same two streams:

- **stdout is the serial port.**
- **stderr is the board.** A change to the display is one line in
  MicroPython's image format: `[1200 ms] display 09090:00000:90009:09990:00000`.
  A scroll is one line with its text; the clock then advances by the time
  CODAL takes to scroll it (120 ms per column, `DISPLAY_DEFAULT_SCROLL_SPEED`
  in codal-core's `AnimatedDisplay.h`; the exact column count is to be read
  from its `updateScrollText`).

The clock is Arduino's: real by default, virtual with
`RAKUPP_MICROBIT_STOP=MS`. Inputs come from the environment:

```bash
RAKUPP_MICROBIT_STOP=3000 RAKUPP_MICROBIT_BUTTONS="A@1000,B@2000-2400" \
  RAKUPP_MICROBIT_ACCEL="0:0,0,-1024;1500:300,0,-950" RAKUPP_MICROBIT_TEMP=21 rakupp tilt.raku
```

Button A pressed at 1000 ms for the default 100 ms, B held from 2000 to
2400 ms, the accelerometer flat until 1500 ms and then tilted, the room at
21 °C. `RAKUPP_MICROBIT_PINS` is `RAKUPP_ARDUINO_PINS` with the edge
connector's names (`0=1,1=512`). The clock code may move to a module both
boards `need`.

## Where it lives

- **The board seam first.** Arduino.cpp has its board inline: `apis()`,
  `isBoardConst`, `Serial`/`LEDMatrix`/`WiFi` recognized by name, `setup()`
  and `loop()` in `Emitter::run`, `rk_halt` and `PROGMEM` in the runtime
  pieces. They move into a board description the emitter is given (the
  `use`d modules, the API table, the program's shape, the print stream, the
  halt, the includes). The first commit is that move alone, with
  `t/arduino/run.raku` unchanged byte for byte.
- **`src/codegen/MicroBit.cpp`**: the micro:bit's board description, the
  `use MicroBit` calls, pictures, strings and handlers.
- **`src/main.cpp`**: `--target=microbit` beside `arduino`; `compileArduino`
  becomes one function over the board, and `--sim` applies to both.
- **`rakulib/MicroBit.rakumod`** and **`src/codegen/MicroBitSim.h`**,
  embedded as Arduino's are.
- **Docs**: `docs/guide/MICROBIT.md`, a FAQ beside `faq/arduino.md`, and
  `examples/microbit/`, starting with the course's own programs in Raku.

## Building and flashing

M0 stops at the source, as `--target=arduino` does:

```bash
rakupp --target=microbit tilt.raku -o microbit-v2-samples/source/main.cpp
cd microbit-v2-samples && python3 build.py      # MICROBIT.hex
cp MICROBIT.hex /Volumes/MICROBIT/
```

CODAL's build needs the GNU Arm Embedded toolchain, CMake, Python 3 and git,
or the `microbit-v2-samples` Dockerfile. Later, `--firmware --codal=DIR` runs
`build.py` in a checkout the user names, and `--upload` copies the hex onto
the `MICROBIT` drive: `/Volumes/MICROBIT` on macOS, `/media/$USER/MICROBIT` on
Linux, the drive labelled `MICROBIT` on Windows. Flashing needs no serial
protocol and no extra tool. rakupp never clones CODAL or installs a
toolchain: without them it prints the commands.

**This Mac (2026-11-09).** No `arm-none-eabi-gcc` on PATH and no Docker;
`~/Library/Arduino15` has `arm-none-eabi-gcc` 4.8.3-2014q1 (Due) and
7-2017q4 (Renesas). CMake and Python 3 are installed. No micro:bit is
attached. **2026-11-15:** CODAL builds with 7-2017q4 (gcc 7.2.1) on PATH:
its first build fetched 95 MB of libraries and took 28 s, and a program
after that takes a few seconds.

## How it is tested

- **The oracle:** the interpreter with the simulated board.
- **The host gate**, `t/microbit/run.raku` (CI, no toolchain): each case's
  `--sim` build writes the oracle's bytes on stdout and stderr and exits the
  same way, as `t/arduino/run.raku` checks. One check in `t/run.raku`.
- **The cross gate**, `--codal=DIR` (skipped without it, as `--arduino15`
  is): every case through `build.py` to a hex, warning-free from the
  generated file, with its size recorded.
- **On hardware**, by hand: a v2 board; its serial transcript against the
  oracle's.

## Phases

| phase | what | done when |
|---|---|---|
| **M0** | the board seam in the emitter; `--target=microbit` and `--sim`; `use MicroBit` with the display (`show`, `scroll` of literals and integers, `set-pixel`, `clear`), `is-pressed`, the accelerometer, `temperature`, the pins, `running-time`, `pause`, `sleep`; pictures while building; `say`; `die` | the Arduino gate unchanged; five cases (a blinking pixel, a counter, the course's tilt arrows, a spirit level, a button counter) agree byte for byte, and one builds to a `MICROBIT.hex` |
| **M1** | strings at run time for `scroll`, `say` and the radio; `was-pressed` and `get-presses`; the compass, light level, the v2's speaker and microphone level | the display, buttons, accelerometer and compass chapters of the course written in Raku |
| **M2** | `react` / `whenever` over buttons, gestures and the radio; `radio.send` / `received` | two boards, or two simulated ones, talk |
| **M3** | `--firmware --codal=DIR`, `--upload`, the size report | a board flashed from one rakupp command |
| later | v1 boards (microbit-dal, 16 KB of RAM); the Calliope mini 3; the browser-only chain (option 4) | |

## Open questions

- **MicroPython's names or CODAL's.** The draft takes MicroPython's, in Raku
  spelling, for the pupils. `Image::HEART` or `Image.HEART`: the second reads
  like Python, the first is a constant the emitter already folds.
- **`sleep`.** Raku's takes seconds; MicroPython's takes milliseconds, so a
  pupil's `sleep 100` waits 100 seconds. The draft keeps Raku's meaning, adds
  MakeCode's `pause(ms)`, and has the build warn when `sleep` is given an
  integer constant of 60 or more.
- **Yielding in the top-level loop.** A `for (;;)` that never sleeps starves
  the event handlers. Either each iteration ends with a yield
  (`schedule()`), or only a loop whose body calls no CODAL wait gets one.
  To be measured.
- **`die` with no serial console.** Most pupils never open one. Also scroll
  the message, or a short "line N", on the display before halting?
- **Brightness.** MicroPython's 0–9 onto CODAL's 0–255: a linear scale, or
  MicroPython's own table.
- **The toolchain.** Answered: the Arduino IDE's `arm-none-eabi-gcc`
  7-2017q4 builds CODAL v2 and every program of the gate.
