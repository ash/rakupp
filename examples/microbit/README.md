# Raku on a BBC micro:bit

Raku++ turns a Raku program into a program for the BBC micro:bit V2: C++ for
CODAL, the board's own runtime (the one MakeCode runs on), which CODAL's build
turns into the `.hex` file you copy onto the board. You need no board to
start. The same program runs on a simulated micro:bit, which shows on your
screen what the board would do: the serial port, every picture on the LEDs,
every text it scrolls.

The names are MicroPython's, spelt as Raku: `display.scroll`,
`button-a.is-pressed`, `accelerometer.x`, `Image.HEART`. A class that has
used MicroPython on the micro:bit finds its way around at once.

This is a walkthrough, with a program for each step in this directory. The
full reference is [docs/guide/MICROBIT.md](../../docs/guide/MICROBIT.md).

| Program | What it shows |
|---|---|
| [`hello.raku`](hello.raku) | The first program: text and a picture, on a simulated board |
| [`heart.raku`](heart.raku) | Pictures: the built-in ones, and one made while building, with dim LEDs |
| [`buttons.raku`](buttons.raku) | Buttons and the touch logo, and pressing them on the simulated board |
| [`tilt.raku`](tilt.raku) | The accelerometer: a MicroPython course's first program, in Raku |
| [`spirit-level.raku`](spirit-level.raku) | Single LEDs, moved by the tilt |
| [`dice.raku`](dice.raku) | Gestures and random numbers: shake for a number |
| [`thermometer.raku`](thermometer.raku) | Text put together as the board runs, from a sensor's reading |

## What you need

- **Raku++.** The [install page](https://raku.online/install/) has every way
  to get `rakupp`. Nothing else is needed for the simulated board.
- **For a real board:** a micro:bit V2 (the one with a speaker and a touch
  logo), and CODAL's build project,
  [microbit-v2-samples](https://github.com/lancaster-university/microbit-v2-samples),
  which turns the C++ rakupp writes into the `.hex` file the board takes. It
  needs an ARM compiler, CMake, Python 3 and git. Step 8 has the commands.

## 1. Hello, without a board

```raku
use MicroBit;

say "Hello from Raku";
display.scroll("Hello");
display.show(Image.HAPPY);
```

`use MicroBit` gives you the board: `display`, the buttons, the sensors and
the pins, as MicroPython names them. There is no top-level loop, so the
program runs once, and the board keeps showing the face.

```bash
rakupp hello.raku
```

The simulated board's clock is real: the scroll takes four seconds, as on the
board. With `RAKUPP_MICROBIT_STOP` the clock is virtual instead, and that many
milliseconds of board time pass at once:

```
$ RAKUPP_MICROBIT_STOP=6000 rakupp hello.raku
Hello from Raku
[0 ms] scroll "Hello"
[4320 ms] display 00000:09090:00000:90009:09990
```

**stdout is the serial port**, and **stderr is the board**: a line for each
text it scrolls or shows, and for each change of the LEDs. A picture is
written as MicroPython writes it: five rows of five LEDs, each a brightness
from 0 (off) to 9. `00000:09090:00000:90009:09990` is the smile: two eyes on
the second row, a mouth on the last two.

## 2. Pictures

```raku
constant GLOW = image q:to/END/;
    03030
    39993
    39993
    03930
    00300
    END

loop {
    display.show(Image.HEART);
    pause 300;
    display.show(Image.HEART-SMALL);
    pause 200;
    display.show(GLOW);
    pause 500;
}
```

`Image` has MicroPython's 64 pictures, by their names spelt as Raku:
`Image.HEART`, `Image.ARROW-NE`, `Image.MUSIC-QUAVER`. `image` makes your
own from the same five rows of digits, or from `#` (lit) and `.` (dark). It
runs **while building**, on your computer, so the board gets 25 bytes and
never the text of the picture. [`heart.raku`](heart.raku) beats:

```
$ RAKUPP_MICROBIT_STOP=2000 rakupp heart.raku
[0 ms] display 09090:99999:99999:09990:00900
[300 ms] display 00000:09090:09990:00900:00000
[500 ms] display 03030:39993:39993:03930:00300
[1000 ms] display 09090:99999:99999:09990:00900
[1300 ms] display 00000:09090:09990:00900:00000
[1500 ms] display 03030:39993:39993:03930:00300
[2000 ms] stop
```

`pause` takes milliseconds, as MicroPython's `sleep` does and as MakeCode's
"pause" block does. Raku's own `sleep` takes seconds, and works too:
`sleep 0.5` is `pause 500`.

## 3. Buttons and the logo

[`buttons.raku`](buttons.raku) counts the presses of button A, scrolls the
count when B is pressed, and starts again from 0 when the logo is touched:

```raku
my int $count = 0;
loop {
    $count += button-a.get-presses;
    if button-b.was-pressed {
        say "count: $count";
        display.scroll($count);
    }
    if pin-logo.is-touched {
        $count = 0;
        display.show(Image.NO);
    }
    pause 50;
}
```

`is-pressed` is whether a button is down now; `was-pressed` and
`get-presses` are about the presses that began since you last asked, so a
quick press between two looks is not lost. On the simulated board you press
them with `RAKUPP_MICROBIT_BUTTONS`: a name, `@`, and the time in
milliseconds, held for a tenth of a second unless you give an end (`B@1500-2400`):

```
$ RAKUPP_MICROBIT_STOP=3500 RAKUPP_MICROBIT_BUTTONS="A@200,A@600,A@900,B@1500,logo@3100" rakupp buttons.raku
count: 3
[1500 ms] scroll "3"
[3140 ms] display 90009:09090:00900:09090:90009
[3500 ms] stop
```

The logo was touched at 3100 ms, and the program saw it at 3140: until then
it was busy scrolling the count. A touch while the board scrolls is missed,
on the board as here.

Every variable in a board program has a native type: `my int $count`, not
`my $count`. A board has no untyped variables, and rakupp tells you where it
found one before anything is built:

```
$ rakupp --target=microbit -e 'use MicroBit; my $x = 5; say $x'
rakupp --target=microbit: -e, line 1: the untyped variable $x — give it a native type, as in `my int16 $x`
```

## 4. The accelerometer

The first program of the accelerometer chapter at
[python-online.ch](https://python-online.ch/robotik/mb/sensor/), and the same
program in Raku:

```python
from microbit import *

while True:
    acc = accelerometer.get_x()
    print (acc)
    if acc > 0:
        display.show(Image.ARROW_E)
    else:
        display.show(Image.ARROW_W)
    sleep(100)
```

```raku
use MicroBit;

loop {
    my int16 $acc = accelerometer.x;
    say $acc;
    if $acc > 0 {
        display.show(Image.ARROW-E);
    }
    else {
        display.show(Image.ARROW-W);
    }
    pause 100;
}
```

`accelerometer.x`, `.y` and `.z` are in milli-g: about −1000 to 1000 as you
tilt the board, and −1024 for `z` when it lies flat. On the simulated board
the tilt is a timeline, `RAKUPP_MICROBIT_ACCEL`: from each time on (`300:`),
the three values. Here the board leans left, then right:

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

The arrow is drawn when it changes, not on every turn of the loop: the
board's LEDs only change when the picture does.

## 5. Single LEDs

[`spirit-level.raku`](spirit-level.raku) moves one lit LED with the tilt:

```raku
display.clear;
display.set-pixel($x, $y, 9);
```

`set-pixel` takes a column and a row, 0 to 4 from the top left, and a
brightness; `get-pixel` reads one back. A pixel off the display stops the
program, with the line, as an index out of range does in Raku.

## 6. Shake for a number

```raku
display.show(Image.SQUARE-SMALL);
loop {
    if accelerometer.was-gesture('shake') {
        my int $n = randint(1, 6);
        say "rolled $n";
        display.show($n);
    }
    pause 50;
}
```

The board works out gestures from the accelerometer: `'up'`, `'down'`,
`'left'`, `'right'`, `'face up'`, `'face down'`, `'freefall'`, `'3g'`, `'6g'`,
`'8g'` and `'shake'`. `is-gesture` asks about now, `was-gesture` whether one
happened since you last asked. `randint(1, 6)` is a whole number from 1 to 6,
both included, as in MicroPython's `random.randint`. On the simulated board
gestures are a timeline too, and the random numbers start from the same
seed each time, so a run repeats (`RAKUPP_MICROBIT_SEED` picks another):

```
$ RAKUPP_MICROBIT_STOP=3000 RAKUPP_MICROBIT_GESTURES="800:shake;820:face up;2000:shake" rakupp dice.raku
[0 ms] display 00000:09990:09090:09990:00000
rolled 4
[800 ms] show "4"
rolled 2
[2000 ms] show "2"
[3000 ms] stop
```

## 7. Text from a sensor

```raku
my int $last = temperature;
loop {
    my int $t = temperature;
    say "temperature: $t";
    display.scroll("$t C");
    if    $t > $last { display.show(Image.ARROW-N) }
    elsif $t < $last { display.show(Image.ARROW-S) }
    else             { display.clear }
    $last = $t;
    pause 5000;
}
```

`"$t C"` is put together as the board runs: a number it has just measured,
and a literal. `display.scroll` and `display.show` take text made of
literals, integers and `True`/`False`. `temperature` is in degrees Celsius,
from the processor's own sensor; on the simulated board it is 21 unless
`RAKUPP_MICROBIT_TEMP` says otherwise:

```
$ RAKUPP_MICROBIT_STOP=13000 RAKUPP_MICROBIT_TEMP="0:21;6000:24" rakupp thermometer.raku
temperature: 21
[0 ms] scroll "21 C"
temperature: 24
[8600 ms] scroll "24 C"
[12200 ms] display 00900:09990:90909:00900:00900
[13000 ms] stop
```

## 8. Onto a board

`--target=microbit` writes your program as one C++ file, not yet as
something the board takes. A micro:bit takes a `.hex` file: the program
compiled together with CODAL, the board's runtime. CODAL's own build
project, microbit-v2-samples, makes it: the C++ file goes in as its
`source/main.cpp`, its `build.py` makes `MICROBIT.hex`, and that file is
copied onto the board.

**Once:** an ARM compiler, CMake, Python 3 and git. With the Arduino IDE and
its Uno R4 core on a Mac, the compiler is already there:

```bash
export PATH=~/Library/Arduino15/packages/arduino/tools/arm-none-eabi-gcc/7-2017q4/bin:$PATH
```

(On Debian or Ubuntu: `sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi cmake git python3`;
the guide has the other ways.) Check that `arm-none-eabi-gcc --version` works
in the terminal you build in: a first build without it picks the computer's
own compiler and keeps it, and stops with `clang: error: unsupported argument
'cortex-m4'` (`rm -rf build` and building again, with the compiler found,
mends it). Then fetch the build project and build it once, which downloads
CODAL (95 MB) and takes under a minute:

```bash
cd ~
git clone https://github.com/lancaster-university/microbit-v2-samples
cd microbit-v2-samples
python3 build.py
```

**Each program:** write it over the project's `source/main.cpp`, build, and
copy the `.hex` onto the board:

```bash
rakupp --target=microbit hello.raku -o ~/microbit-v2-samples/source/main.cpp
cd ~/microbit-v2-samples
python3 build.py
cp MICROBIT.hex /Volumes/MICROBIT/
```

The generated `main.cpp` is a whole program: there is nothing to add or
edit. `build.py` takes seconds now, and leaves `MICROBIT.hex` beside itself
(not `MICROBIT.bin`, which is no use to the board). A micro:bit plugged in
with a USB cable is a drive called `MICROBIT`, like a memory stick:
`/Volumes/MICROBIT` on a Mac, `/media/$USER/MICROBIT` on Linux, a drive
letter on Windows, or drag the file there. The light on the back flashes,
the board restarts, and the program runs; the drive comes back without the
file, as it should.

`say` writes to the serial port over the same USB cable, at 115200 baud:
`screen /dev/cu.usbmodem* 115200` on a Mac, or the serial console of the
micro:bit Python editor or MakeCode.

The guide's [From the C++ file to the board](../../docs/guide/MICROBIT.md#from-the-c-file-to-the-board)
has each step in full, with Docker and Windows, and what to do when one goes
wrong.

> Every program here builds with CODAL into a `MICROBIT.hex`, with no
> warning from the program. Not yet tried: running one on a board. The C++
> is checked against a simulated CODAL on every change, and CODAL's own
> compiler accepts it; the first run on hardware will say what that missed.

## 9. The C++, without a board

`--sim` builds the same C++ for your computer instead, against a simulated
CODAL that writes what the interpreter's board writes:

```bash
rakupp --target=microbit --sim tilt.raku -o tilt
RAKUPP_MICROBIT_STOP=600 RAKUPP_MICROBIT_ACCEL="0:-200,0,-1000;300:250,0,-950" ./tilt
```

That is how rakupp checks itself: every program in `t/microbit` runs both
ways, and the two must agree byte for byte.

## What a board program can use

- **Native types:** `int8` … `int64`, `uint8` … `uint64`, `num`, `num32`,
  `Bool`, and shaped arrays of them, such as `my uint8 @picture[25]`.
- **Arithmetic with Raku's meaning,** control flow (`if`, `while`, `loop`,
  `for ^N`, `for @table`, `next`, `last`), and subs with native signatures:
  the same subset as on an Arduino ([its FAQ](../../docs/guide/faq/arduino.md)
  has the details).
- **The display:** `show` (a picture, a number, text), `scroll` (text),
  `clear`, `set-pixel`, `get-pixel`, `read-light-level`.
- **Buttons and touch:** `button-a`, `button-b` (`is-pressed`, `was-pressed`,
  `get-presses`), `pin-logo.is-touched`.
- **Sensors:** `accelerometer.x/.y/.z`, `is-gesture`, `was-gesture`,
  `compass.heading`, `compass.calibrate`, `temperature`.
- **Pins:** `pin0` to `pin16`, `pin19`, `pin20`: `read-digital`,
  `write-digital`, `read-analog`, `write-analog`, and `is-touched` on the
  three big rings `pin0`, `pin1`, `pin2`.
- **Time and chance:** `pause`, `sleep`, `running-time`, `randint`.

Hashes, strings kept in variables, classes, regexes and closures stop the
build with the line they are on. The guide's
[Not yet](../../docs/guide/MICROBIT.md#not-yet) section lists what is
planned, the radio and the speaker first.

## Under Rakudo

`use MicroBit` is built into rakupp, and `rakupp install` skips the name.
Other Raku implementations have no such module: Rakudo stops at the `use`
line with `Could not find MicroBit`.

## More

- [docs/guide/MICROBIT.md](../../docs/guide/MICROBIT.md): everything above in
  full, with every name of `use MicroBit` and where a board differs from the
  interpreter.
- [docs/guide/faq/microbit.md](../../docs/guide/faq/microbit.md): the
  questions a MicroPython or MakeCode user asks first.
- [MICROBIT-PLAN.md](../../docs/dev/plans/MICROBIT-PLAN.md): what comes next,
  in order.
