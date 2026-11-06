# Plan: ARDUINO — a Raku program as firmware

*Written 2026-11-02, before any code: a design draft for review. It lists the
ways to program an Arduino in Raku with Raku++, picks one — a compile target
whose output is a firmware image the board runs on its own — and sets out how
that target treats the Raku that runs while building (`BEGIN`, `CHECK`,
`constant`), which is where most of an Arduino program's Raku will live.
Phase A0 landed the same day, with A1's constants and parts of A2 and A3; what
landed and where it departs from this draft is in "Where it stands".*

## Where it stands (2026-11-02)

`--target=arduino` emits a sketch; `--target=arduino --sim` builds it for the
host against a simulated board; `rakupp prog.raku` runs a `use Arduino`
program on the same simulated board. User guide:
[ARDUINO.md](../../guide/ARDUINO.md). Gate: `t/arduino/run.raku` (41 checks
with `--arduino15`, ~5 s), one check in `t/run.raku`.

Measured on the gate's eight programs: each one's transcript under the
interpreter equals its golden, and its `--sim` sketch writes the same bytes on
stdout and stderr and exits the same way. Every sketch but the LED-matrix one
compiles for an Arduino Due with the SAM core 1.6.12 and its
arm-none-eabi-gcc 4.8.3 under `-Wall -Werror`: blink is 102 bytes of code,
`features.raku` 1,830 bytes of code and 29 of RAM (`-Os`, the sketch's object,
the core not linked). Not yet tried: an AVR board (no avr-gcc on the
machine), the Uno R4 core, real hardware.

**The Uno R4 WiFi's LED matrix (2026-11-03).** `use Arduino::LEDMatrix`, a
second shadow module (rakulib/Arduino/LEDMatrix.rakumod), is the first C++
library object: `LEDMatrix.begin`, `.show(@frame)` and `.clear` become the
core's Arduino_LED_Matrix calls on one global `ArduinoLEDMatrix`, and the
sketch includes the header only when the program uses them. `frame` packs a
picture into the library's three words while building; Arduino's tutorial
heart packs to the tutorial's own `3184a444 44042081 100a0040`, which is what
pins the bit order without the library at hand. Both simulators draw a frame
that changes the picture on stderr. The sketch has not been compiled against
the real header: the Uno R4 core is not installed on the machine. A
compile-time error now stops an Arduino build at once
(`CtTable::failed`), where it used to fall through to a refusal of the
constant it left behind.

| phase | state |
|---|---|
| A0 | done |
| A1 | constants and top-level `BEGIN` values become literals and flash tables, `.elems` folds; not yet: `if` on a constant emitting one branch, board facts |
| A2 | sized natives in parameters, results, locals, globals and `state`; `die` → message and halt; not yet: the `int` and `num` policy per board, printing a num |
| A3 | shaped native arrays, global and local; not yet: `repr('CStruct')` classes, enums |
| A4 | `--sim` (a host build, not in the draft); not yet: `--firmware`, `--upload`, the size report |
| A5 | the first C++ library object, the Uno R4 WiFi's LED matrix (frames, not yet animations); not yet: interrupts, a general way to declare a library class |

Where the implementation departs from the draft below, and why:

- **A new emitter, not `nrEmit` driven without its boxed side.** `nrEmit`
  takes full-width `int` and `num` only, and its refusals are woven through
  hand-offs to the boxed code. `src/codegen/Arduino.cpp` is a focused walk over
  the AST (~1,300 lines) that keeps `nrEmit`'s arithmetic — 64-bit wrap through
  `uint64_t`, floored `div` and `%` — and adds the sized types and the board's
  refusals. Codegen.cpp gained nothing.
- **The simulated board is Raku.** `rakulib/Arduino.rakumod`, embedded as a
  shadow module (`cmake/EmbedTools.cmake`, `isShadowedModule`, `SHADOWED` in
  `tools/install.raku`): built into the binary and found ahead of any path or
  store, as the draft requires, but written in Raku rather than as C++
  builtins. The emitter knows the routines by name; a program's own sub of
  the same name is taken first.
- **`--sim`.** The draft's host gate compiled the sketch against a fake core
  in the repository. The core is now `src/codegen/ArduinoSim.h`, embedded, and
  `--sim` is a user-facing flag: a program and its sketch can be tried with no
  board, and compared.
- **Every constant is computed while building.** The #190 pass skipped a
  constant whose initializer is already literals (`2 ** 4`), which a binary
  computes cheaply itself; `computeCompileTimeValues` takes `everyConstant`,
  and the Arduino target sets it.
- **Any module may be `use`d.** A module's routines run while building, in a
  constant or `BEGIN`; a call into one from the board's code is refused at the
  call. `t/arduino/cases/pinmap.raku` reads a JSON file with `JSON::Native`.
- **Time and inputs.** Real time by default; `RAKUPP_ARDUINO_STOP=MS` makes it
  virtual (`delay` advances it, every other board call one microsecond, so a
  loop polling `millis` moves on). `RAKUPP_ARDUINO_PINS` fixes inputs.
- **One deliberate difference.** A subscript past the end of a constant list
  is Nil in Raku; the board has no Nil and stops with `Index out of range`.

## The constraint

| board | CPU | flash | RAM | `double` |
|---|---|---:|---:|---|
| Uno R3 | ATmega328P, 8-bit AVR, 16 MHz | 32 KB | 2 KB | 32-bit |
| Uno R4 Minima | RA4M1, Cortex-M4F, 48 MHz | 256 KB | 32 KB | 64-bit, software |
| Due | SAM3X8E, Cortex-M3, 84 MHz | 512 KB | 96 KB | 64-bit, software |
| ESP32 boards | Xtensa, 240 MHz | 4–16 MB | 320–520 KB | 64-bit, software |

`build/rakupp` is 26 MB (Release, arm64), and `say "Hello"` compiled with
`--exe` is 10.1 MB at the default `--slim=safe` ([CLI.md](../../guide/CLI.md)).
Every `--exe` program links `librakupp_rt.a`: boxed `Value`s, the
interpreter object, exceptions, threads, a file system. None of that fits on
any board above, and the boards that have room for part of it have no OS to
run it on. So whatever runs on the board is C++ that names no runtime.

## The options

### 1. Raku on the computer, Firmata on the board

The standard Firmata sketch is flashed once; rakupp drives the pins over USB
serial (a Firmata module in Raku, the port set up with `stty` or NativeCall
termios). All of Raku is available, the REPL and the Jupyter kernel can blink
an LED, and Firmata's pin reports map onto a `Supply`. It works today.

**Not chosen.** The board does nothing without the computer attached, timing
is the host's (milliseconds of jitter over USB), and nothing is ever saved to
the board. It is a demo, not a way to make a device.

### 2. A compile target: Raku → C++ → firmware image

rakupp translates a subset of Raku into one C++ sketch, the Arduino core's
compiler builds it into a `.hex`/`.bin`, and the image is flashed. The board
runs it alone, at C speed, in the board's own memory.

**Chosen.** It is the only option that gives a self-sufficient device on every
board, and rakupp already has most of the emitter it needs (below).

### 3. Full Raku while building, small Raku on the board

Not a separate route but the half of option 2 that makes it worth having. The
code of an Arduino program is two functions; most of its work is *data*:
lookup tables, pin maps, fonts, note frequencies, calibration curves, state
tables, strings. In C those are typed by hand or generated by a Python script
into a header. Here the program computes them itself, in full Raku, while
building — rationals, big integers, grammars, files, modules — and the board
gets only the numbers. Zig's `comptime` is the nearest relative; no C-based
Arduino toolchain has it.

**Chosen, as the main feature of option 2.** It has its own section below.

### 4. A board that runs Linux: rakupp itself on it

The Arduino UNO Q pairs a Qualcomm arm64 processor running Debian with an
STM32 microcontroller; the Portenta X8 is similar. rakupp runs on the Linux
side unchanged — linux-aarch64 is a release target — and could take the place
of the Python side, calling the microcontroller's sketch over Arduino's
Bridge RPC (not yet checked in detail).

**Not chosen.** It needs a particular, larger board, and the microcontroller
half — the part that touches the pins in real time — is still C++. With
option 2 in place it becomes interesting again: both halves in Raku.

### 5. A Raku VM on the board

A compact bytecode interpreter for the same subset, flashed once, with
programs sent over serial — MicroPython's model. Live reload without
reflashing is its one real gain.

**Not chosen.** It fits only the 32-bit boards, spends most of the flash and
RAM on the interpreter, pays an interpreter's cost on every operation, and
still cannot run full Raku. It would reuse option 2's subset, so it can come after.

### Rejected outright

- **`--target=js` on Espruino.** The JS runtime alone is 424 KB
  (`src/js-rt/`), and it uses language features Espruino does not have.
- **A `--slim` rakupp runtime on an ESP32.** `--slim` removes subsystems, not
  the assumption of an OS, threads and a heap of megabytes.
- **WebAssembly through wasm3.** rakupp has no wasm target, and an
  interpreter on the board is option 5 with an extra layer.

## What exists already (probed 2026-11-02)

**The native-routine emitter** (`planNative` / `nrEmit`,
[Codegen.cpp:7007](../../../src/Codegen.cpp)). A top-level sub whose
parameters and result are full-width `int` or `num` also compiles to
`n_<name>`, a plain C++ function over `int64_t` and `double`, with machine
arithmetic and direct calls between such functions. For

```raku
sub step(int $x --> int) { my int $y = $x * 3 + 1; $y % 256 }
sub run(int $n --> int) {
    my int $acc = 0;
    loop (my int $i = 0; $i < $n; $i++) { $acc = step($acc) }
    $acc
}
```

`rakupp --cpp -O` emits `n_step` and `n_run` with no reference to the
runtime. Copied out with the one helper they use (`__nMod`) and compiled with
the Due core's own compiler,

```
arm-none-eabi-g++ (4.8.3-2014q1) -mcpu=cortex-m3 -mthumb -std=gnu++11 -Os -fno-exceptions -fno-rtti
```

they come to **112 bytes of code, no data**. That compiler stops at C++11; the
emitted code already stays inside it.

**A top-level loop emitted natively** (`nrMainline`,
[Codegen.cpp:6964](../../../src/Codegen.cpp)), when it uses native arrays or
calls native routines.

**The compile-time pass** (#190, [CompileTime.h](../../../src/CompileTime.h)).
`--exe`, `--aot` and `--bundle` run the program's `BEGIN`, `CHECK`, `use`,
declarations and constant initializers in an interpreter while building, and
substitute each value for the code that computed it, written as Raku
(`.raku`). Probed: `BEGIN { note "BEGIN ran" }` prints during `--exe`, not
when the binary runs, and `constant @sine = (^8).map({ … })` reaches the
binary as its value. `--cpp` does not run the pass (only the compile modes
call `buildGate` in `main.cpp`), so the new target must.

**Sized natives in the interpreter.** `my uint8 $x = 250; $x += 10` gives 4,
`my int16 @a[4]; @a[1] = 40000` gives −25536, and a `repr('CStruct')` class
with `int16` and `uint8` attributes constructs. The interpreter computes what
the board's C types compute, so it can be the oracle.

**`is native` declarations** with no library parse:
`sub digitalWrite(uint8, uint8) is native {*}`.

**What the emitter refuses today** (each probe compiled with `--cpp -O`; no
`n_` function came out):

| construct | example |
|---|---|
| a native routine that reads a `constant` | `sub pin(int $x --> int) { $x + LED }` (with `13` in place of `LED` it compiles) |
| a native routine that reads a constant array | `sub level(int $i --> int) { @sine[$i % 4] }` |
| a native array local | `my int @t = 1, 2, 3, 4; @t[$i % 4]` |
| a sized native parameter or result | `sub small(uint8 $x --> uint8) { $x + 1 }` |
| the mainline's native variables | `my int $acc` is a boxed `static Value` |

Every one of these is a statement the emitter hands to the boxed code today.
On the board there is no boxed code to hand it to.

## What a program looks like

```raku
use Arduino;

constant LED   = 9;                                                  # a PWM pin
constant @wave = (^32).map: { (127.5 + 127.5 * sin(2 * pi * $_ / 32)).Int };   # computed while building

pinMode(LED, OUTPUT);                  # the mainline before the loop → setup()
say "a wave of {@wave.elems} steps";

loop {                                 # the top-level loop → loop()
    state uint8 $i = 0;                # → a static local
    analogWrite(LED, @wave[$i++ % @wave.elems]);
    delay(20);
}
```

and the sketch it should become:

```cpp
// Generated by `rakupp --target=arduino` from fade.raku
#include <Arduino.h>
static const uint8_t c_wave[32] PROGMEM = {127, 152, 176, 198, 217, 233, 245, 252,
    255, 252, 245, 233, 217, 198, 176, 152, 127, 102, 78, 56, 37, 21, 9, 2,
    0, 2, 9, 21, 37, 56, 78, 102};
void setup() {
    Serial.begin(9600);
    pinMode(9, OUTPUT);
    Serial.println(F("a wave of 32 steps"));
}
void loop() {
    static uint8_t nv_i = 0;
    analogWrite(9, pgm_read_byte(&c_wave[(uint8_t)(nv_i++) % 32]));
    delay(20);
}
```

The same file runs under the interpreter, `rakupp fade.raku`, against a
simulated board (below).

**The mapping.**

| Raku | on the board |
|---|---|
| the mainline before the top-level `loop { }` | `setup()` |
| the top-level `loop { }`, one iteration | one call of `loop()` — the core does its housekeeping between calls (serial events on AVR, Wi-Fi on ESP32) |
| a mainline with no top-level loop | `setup()`, and an empty `loop()` |
| a top-level `my` native variable | a global; `const` (flash, not RAM) when nothing writes it |
| `state` | a `static` local |
| a `constant` | folded into each use, or a `const` table (below) |
| a sub with a native signature | a C++ function |
| `say`, `print` of literals and natives | `Serial.print` calls, literal strings kept in flash |
| a builtin `use Arduino` brings in | a direct call to the core function of that name |
| `if` on a constant | only the branch taken is emitted |
| `die`, an uncaught failure | the message and line over serial, then a halt |

**What never goes onto the board.** `Int` past a native width, `Rat`,
`FatRat`, `Complex`, strings built at run time, `Hash`, lazy lists and
`gather`, closures that escape, multi dispatch, `CATCH` and phasers, regexes
and grammars, junctions, `EVAL`, threads and promises. The build names the
line and the construct, and says when the fix is a declaration (`declare it
uint16`). There is no fallback: `--exe` bundles what it cannot compile, but on
the board there is nothing to bundle it with. **All of these are allowed while
building.**

## Raku while building

The rule: **the computer runs the Raku, the board gets the numbers.**

1. **Everything that runs at compile time runs in the full interpreter:**
   `BEGIN`, `CHECK`, constant initializers, `use` of ordinary modules from the
   store, files read relative to `$?FILE`, grammars. The #190 pass does this
   now; the target calls it as `--exe` does.

2. **Values become C data, not `.raku` text.** #190 writes each value back as
   Raku source for the binary to read when it starts. The target lowers it
   instead:

   | value | C |
   |---|---|
   | `Int`, `Num`, `Bool`, an enum | a literal at each use |
   | `Str` | a string in flash (`F()`, `PROGMEM`) |
   | a list of `Int` | `const T name[N]` |
   | a list of `Str` | a table of flash strings |
   | a rectangular list of lists | a 2-D array |
   | a `Hash` | refused at first; later a sorted key table, or a perfect hash computed while building |
   | an object, a closure | a build error naming the line |

   `T` is the explicit type when there is one (`array[int16].new(…)`,
   `blob8`, `buf16`), otherwise the narrowest C type that holds every
   element, and the build report says which it picked. On AVR, constant data
   goes in `PROGMEM` and is read with `pgm_read_*`; on ARM and ESP32 a
   `const` array already lives in flash and is read directly.

   #190 leaves a value it cannot write as Raku to be computed when the binary
   starts. The target has no start-up interpreter, so the same case is an
   error.

3. **What `BEGIN` leaves in a top-level variable** becomes that global's
   initial value; when nothing on the board writes the variable it is `const`
   and costs flash instead of RAM, which on a 2 KB board is the difference
   that matters.

4. **Folding.** An expression made of constants is computed while building:
   `@wave.elems` is 32, `"a wave of {@wave.elems} steps"` is one literal
   string, and `if HAS-DISPLAY { … }` emits one branch or none. A sub that is
   only called while building is not emitted.

5. **The board is known while building.** `--board` names it (an Arduino
   FQBN). The `Arduino` module exports its facts as constants — CPU clock,
   flash and RAM sizes, FPU or not, the built-in LED pin — so that

   ```raku
   constant TICKS = BOARD.f-cpu div 1000;
   if BOARD.has-fpu { … } else { … }      # folded: one branch reaches the board
   ```

   The interpreter gets the same facts from the same flag.

6. **`CHECK` is a build guard.**
   `CHECK { die "the font needs {@font.elems} bytes" if @font.elems > 1024 }`
   fails the build with that message. `BEGIN say …` prints on the computer
   during the build, as it does for `--exe`.

What should prove it (the gate programs for phase A1):

- a sine or gamma table, computed (the example above);
- a melody written as text (`"C4 E4 G4 …"`), turned into
  `const uint16_t` frequencies in equal temperament;
- a 5×7 font parsed by a small grammar from an ASCII-art file into
  `const uint8_t font[96][5]`;
- Morse code read from a text file into a table indexed by letter;
- a pin map read from a JSON file.

None of those programs' Raku reaches the board; all of their data does.

## Numbers

- **Sized natives** (`int8` … `uint64`, `num32`, `num64`) mean exactly the C
  type, on the board and in the interpreter.
- **`int`** keeps Raku's meaning, 64 bits with wrap-around. That is native on
  no Arduino: every operation is a multi-word sequence on a 32-bit board and
  a long one on an 8-bit board. The build says how many such operations each
  routine has on an 8-bit board and suggests a sized type.
- **`Int` and untyped variables** are refused on the board unless every value
  they take is known while building (then they fold). Inferring a native type
  for them is a later possibility, not a first step. The fold is not built
  yet (2026-11-06: `my $x = 42;` is refused, `constant x = 42` folds). Both
  are in [INFER-PLAN.md](INFER-PLAN.md), with the other engines that ask the
  same question.
- **`num`** is a 64-bit `double` on ARM and ESP32 (in software on all the
  boards above), but the AVR toolchain the Arduino core ships has a 32-bit
  `double`. On AVR, `num32` maps to `float`, and `num` is refused rather than
  silently narrowed (open question below).
- **`/` on two integers** is a `Rat` in Raku, so it is refused on the board;
  `div` and `%` keep Raku's floored meaning, which the emitter already
  implements (`__nDiv`, `__nMod`).
- **A zero divisor**, which today sends the native code back to the boxed
  path, becomes the `die` rule: message and line over serial, then a halt.

## The hardware API

`use Arduino;` is answered by the engine, as `use JS` and `use Data::Native`
already are (`dataNativeUse`, DATA-PLAN P6): no file is looked for, so no
`-I` path, store or ecosystem dist can stand in for it. It brings in the core
API as engine builtins (`pinMode`, `digitalWrite`, `digitalRead`,
`analogRead`, `analogWrite`, `delay`, `delayMicroseconds`, `millis`,
`micros`, `tone`, `noTone`, `pulseIn`, `shiftOut`, the `Serial` class) and
the constants `HIGH`, `LOW`, `INPUT`, `OUTPUT`, `INPUT_PULLUP`,
`LED_BUILTIN`, `A0`… They mean two things:

- **Under `--target=arduino`**, each builtin is a direct call to the core's C
  or C++ function of that name, and each constant is emitted as the core's
  own macro name, since `LED_BUILTIN` and the analog pin numbers differ
  between boards. The emitter knows them by identity, as the builtins this
  `use` imported, so a program's own `sub digitalWrite` is never taken for
  the core's.
- **Under the interpreter**, the same builtins are a simulated board,
  implemented in the engine: pin states in an array, a virtual clock that
  `delay` advances (so a program that runs for ten minutes is tested in
  milliseconds), `Serial` on stdout, and every pin change logged
  (`t=500ms pin 13 HIGH`).

`say` and `print` write to `Serial`. When the program prints and never calls
`Serial.begin` itself, `setup()` starts with `Serial.begin(9600)`, or the
`--baud` given.

C++ library classes (Servo, Wire, NeoPixel) are a later phase: a Raku class
whose trait names the C++ class, its methods mapped to member calls.

**The name in the ecosystem.** A module name cannot be reserved there: a
distribution is its name together with its `auth`, and anyone may upload the
same name under their own. In the fez index of 2026-10-15, 269 of 9,701 module
names come from two or more auths (`App::MoarVM::Debug` from
`zef:raku-community-modules` and from `zef:slid1amo2n3e4`); in REA, 4,876 of
13,763. Today none is named Arduino or mentions Firmata. So the guarantee is
the engine's, not the ecosystem's: rakupp answers `use Arduino` before it
looks anywhere, and whatever is uploaded under the name never reaches a
rakupp program.

What the ecosystem can hold is a **stub** `Arduino` dist, for every other
implementation. Its whole body says the program needs Raku++:

```raku
unit module Arduino;
die "This program needs Raku++: `use Arduino` is built into rakupp, which compiles\n"
  ~ "it for an Arduino board (https://github.com/ash/rakupp).\n"
  ~ "Run it as:  rakupp --target=arduino {$*PROGRAM-NAME}\n";
```

so `use Arduino` under Rakudo stops at the `use` with that message instead of
a missing-module error. `rakupp install` skips the stub, as it skips the
`NativeHelpers::*` dists rakupp shadows (`SHADOWED` in `tools/install.raku`,
which then lists `Arduino` too). Uploading the stub claims the raku.land page
for the name, but it cannot stop a second `Arduino` under another auth; that
one is harmless to rakupp and only confuses a reader. The upload is done by
hand from the maintainer's fez account, never by a build. A Firmata client
(option 1), if ever written, would be an ordinary dist under its own name.

## The command line

```
rakupp --target=arduino fade.raku                       # print the sketch, as --target=cpp prints C++
rakupp --target=arduino fade.raku -o fade/fade.ino      # write it
rakupp --firmware --board=arduino:sam:arduino_due_x fade.raku -o fade.bin
rakupp --firmware --board=arduino:sam:arduino_due_x --upload=/dev/cu.usbmodem1101 fade.raku
```

`--firmware` builds through `arduino-cli compile --fqbn …`, which uses the
cores already installed in `~/Library/Arduino15` (shared with the Arduino
IDE); PlatformIO can be a second driver later. Without `arduino-cli` the build
says so and prints the command; rakupp never installs it. Only `--upload`
writes to a device. Every build ends with a size report: flash and RAM used
against the board's limits, and the largest tables by name.

## Where it lives

A new emitter, `src/codegen/Arduino.cpp` beside `Js.cpp`, that drives the
native-routine emitter in a mode with no boxed side: every place `nrEmit`
hands a statement to the boxed code, or refuses a routine, becomes an error
with the line and the reason. Codegen.cpp is 10,083 lines, so the Arduino
parts go in the new file, and Codegen.cpp gains only the seam. The engine's
half — `use Arduino` answered beside `use JS` in `InterpreterModules.cpp`,
and the simulated board's builtins — goes in a source file of its own.

## How it is tested

- **The oracle:** the interpreter with the simulated board — the serial
  output and the pin log, on the virtual clock.
- **The host gate** (CI; no hardware, no Arduino toolchain): the emitted
  sketch compiled with the system `c++` against a small fake core in the
  repository (`Arduino.h` with the same API, the same log format and the same
  virtual clock). Its output must equal the oracle's byte for byte, as an
  `--exe` binary's output must equal the interpreter's.
- **The cross gate** (when the toolchain is present; skipped otherwise, as
  the JS section is without node): every gate program compiles for one AVR
  and one ARM board with the core's own compiler and flags, and its size is
  recorded.
- **On hardware**, by hand: the Due, whose core and compiler are installed on
  the development Mac; its serial transcript against the oracle's.
- simavr for AVR boards later. Wokwi needs a network account, so it is never
  a gate.

## Phases

| phase | what | done when |
|---|---|---|
| **A0** | the target: the whole program through `nrEmit` and `nrMainline` with no boxed side; mainline → `setup()`, top-level loop → `loop()`; `int`, `num`; `use Arduino` with `pinMode`, `digitalWrite`, `digitalRead`, `delay`, `millis`, `say`/`print`; every refusal an error with its line | blink and a serial counter pass the host gate and compile for the Due |
| **A1** | Raku while building: the target runs `buildGate`, lowers #190's values to C data, lets native routines read constants and constant tables, folds `if` / `.elems` / interpolation of constants | the five gate programs above pass the host gate |
| **A2** | sized natives everywhere (parameters, results, locals, globals, `state`); the `int` and `num` policy per board; `die` → message and halt | an 8-bit AVR build of every gate program |
| **A3** | data: shaped native arrays, global and local; `repr('CStruct')` classes → structs with methods as member functions; enums | a state-machine program with a struct table |
| **A4** | `--firmware`, `--upload`, `--board` facts while building, the size report | a Due flashed from one rakupp command |
| **A5** | interrupts (a trait on a sub, `volatile` globals); C++ library classes; then `react` / `whenever` over timers compiled to a `millis()` scheduler | a servo sweep driven by a pin interrupt |

## Open questions

- **`int` on 8-bit boards:** keep 64 bits and warn (Raku's meaning, slow), or
  let a flag reinterpret plain `int` as 16 or 32 bits (fast, a different
  language).
- **`num` on AVR:** refuse it and allow only `num32`, or accept a 32-bit
  `double` with a note.
- **Flag names:** `--target=arduino` and `--firmware`, or `--exe --board=…`.
- **C++ library classes:** declared in Raku by hand, with a trait naming the
  C++ class, or read from the library's headers.
- **Strings at run time:** literals only (the first phases), or fixed-size
  buffers later.
- **`Serial.begin` inserted for the program:** the right default baud, and
  whether to wait for the port on boards with native USB.
