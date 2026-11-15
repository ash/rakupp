# FAQ — writing Raku for a micro:bit

`rakupp --target=microbit` compiles a Raku program into C++ for the BBC
micro:bit V2's own runtime, CODAL. The board has no interpreter, so it takes
the same subset of Raku as an Arduino: the parts that become plain C++ with
no runtime behind them. This page answers what a MicroPython or MakeCode user
asks first. The full reference is [MICROBIT.md](../MICROBIT.md); a walk from
a scrolled greeting to a die you shake is in
[examples/microbit](https://github.com/ash/rakupp/tree/main/examples/microbit);
the subset itself is in [the Arduino FAQ](arduino.md), which holds for the
micro:bit too.

## The short answer

- Start with `use MicroBit`. The names are MicroPython's, spelt as Raku:
  `button_a.is_pressed()` is `button-a.is-pressed`.
- Give every variable a native type: `my int $count`, not `my $count`.
- `while True:` is `loop { }`, and `sleep(100)` is `pause 100`.
- Text is for the display and the serial port: put it together in the call,
  `display.scroll("T: {temperature}")`, and do not keep it in a variable.
- Make pictures while building: `constant SMILE = image '…'`, or one of
  `Image`'s.
- Everything the board cannot do is refused when you build, with the line,
  and never fails later on the board.
- `--target=microbit` writes C++, not the `.hex` the board takes: CODAL's
  build project turns the one into the other (below).

## What do I do with the `.cpp` file?

Make it into the `.hex` file the board takes, with CODAL's own build
project, microbit-v2-samples. The `.cpp` is your whole program; the `.hex`
is that program compiled together with CODAL, the board's runtime, for its
ARM processor. Once you have the project and an ARM compiler (set up once:
[the guide's steps](../MICROBIT.md#once-a-compiler-and-codals-build-project)),
each program is three commands:

```bash
rakupp --target=microbit prog.raku -o ~/microbit-v2-samples/source/main.cpp
cd ~/microbit-v2-samples && python3 build.py
cp ~/microbit-v2-samples/MICROBIT.hex /Volumes/MICROBIT/
```

The first puts your program in the project as its `source/main.cpp`, in
place of the demo that is there; you don't edit it. The second compiles it
into `MICROBIT.hex`, in the project's top folder. The third copies that onto
the board, which shows up as a drive called `MICROBIT` when plugged in over
USB (`/media/$USER/MICROBIT` on Linux; on Windows, drag it to the drive).
The board restarts and runs the program.

## I know MicroPython. What changes?

| MicroPython | Raku |
|---|---|
| `from microbit import *` | `use MicroBit;` |
| `while True:` | `loop { … }` |
| `sleep(100)` | `pause 100` (or Raku's own `sleep 0.1`, in seconds) |
| `x = accelerometer.get_x()` | `my int $x = accelerometer.x;` |
| `print(x)` | `say $x;` |
| `display.show(Image.ARROW_E)` | `display.show(Image.ARROW-E);` |
| `display.scroll("Hi " + str(n))` | `display.scroll("Hi $n");` |
| `if button_a.was_pressed():` | `if button-a.was-pressed { … }` |
| `random.randint(1, 6)` | `randint(1, 6)` |
| `Image("09090:99999:99999:09990:00900")` | `image '09090:99999:99999:09990:00900'`, in a constant |
| `pin0.write_digital(1)` | `pin0.write-digital(1);` |

A method with no arguments needs no parentheses: `accelerometer.x`,
`display.clear`. A variable is declared once, with `my` and a type. Blocks
are in braces, not indentation.

## Why `pause` and not `sleep`?

Raku has its own `sleep`, and it takes seconds: `sleep 2` waits two seconds,
`sleep 0.25` a quarter of one. On a micro:bit that still works, on the
board's clock. MicroPython's `sleep` takes milliseconds, so `sleep(100)` from
a Python program would wait 100 seconds here. `pause`, MakeCode's name for the
same block, takes milliseconds: `pause 100`.

## Which number type do I want?

`int` for counting is fine: it is 64 bits, as in Raku, and a micro:bit
computes it in two steps. For readings and pixels, `int16` or `int32`: the
accelerometer is about −2000 to 2000, a pixel 0 to 4, a brightness 0 to 9.
A native type wraps when a value does not fit: `my int8 $t = 127; $t++` is
−128, on the board and in the interpreter alike.

## How do I show a number, or text with a number in it?

```raku
display.show($count);                       # a number
display.scroll("Score: $score");            # text with a number in it
display.scroll("{accelerometer.x} mg");     # a reading, in braces
display.show($won ?? "Y" !! "N");           # one text or another
```

The text is put together as the board runs, from literals, integers and
`True`/`False`. What it cannot do is keep text in a variable (`my Str $name`)
or join texts with `~`: put the whole text in the call instead. A `num` is
refused on the display, as in `say`; show `.round` of it.

## How do I make my own picture?

With `image`, in a constant. It takes MicroPython's text form, a brightness 0
to 9 for each LED, five rows of five, by lines or separated by `:`; or `#`
for lit and `.` for dark:

```raku
constant BOAT = image q:to/END/;
    05050
    05050
    05050
    99999
    09990
    END
constant @CROSS = image '#...#:.#.#.:..#..:.#.#.:#...#';
display.show(BOAT);
```

`image` runs while building, on your computer: a picture with a mistake
stops the build and says which row. A picture the program works out as it
runs is a shaped array of 25: `my uint8 @p[25]`, filled with levels and
shown with `display.show(@p)`. One LED at a time is `display.set-pixel(X,
Y, BRIGHTNESS)`.

## How do I animate?

Keep the frames in one constant, and copy each into a picture as you show it:

```raku
constant @FRAMES = flat image('..#..:..#..:..#..:.....:.....'),
                        image('.....:.....:..###:.....:.....'),
                        image('.....:.....:..#..:..#..:..#..');
my uint8 @picture[25];
loop {
    for ^3 -> $f {
        for ^25 -> $i { @picture[$i] = @FRAMES[$f * 25 + $i] }
        display.show(@picture);
        pause 200;
    }
}
```

The three pictures are 75 bytes in flash; the copy is 25 at a time.

## Why did my button press go missing?

`button-a.is-pressed` is whether the button is down now. While the board
scrolls text, the program waits for the scroll, so a press that starts and
ends meanwhile is not down when it next looks. `button-a.was-pressed` (and
`get-presses`) counts presses as they begin, whatever the program is doing:
use it when a press must not be missed. The touch logo has only
`is-touched`, so a quick touch during a scroll is missed, on the board too.

## How do I react to a shake?

```raku
if accelerometer.was-gesture('shake') { … }
```

`was-gesture` is whether the gesture happened since you last asked about
it; `is-gesture` whether it is happening now. The board listens for
gestures from the start, so a shake is seen even in a loop that never
pauses.

## How do I check a program without a board?

Run it: `rakupp prog.raku` runs it on a simulated micro:bit, which writes the
serial port on stdout and the LEDs on stderr. `RAKUPP_MICROBIT_STOP=5000`
runs five seconds of board time at once, and the other `RAKUPP_MICROBIT_*`
variables press buttons, tilt the board and set the temperature
([the list](../MICROBIT.md#the-inputs)). `rakupp --target=microbit --sim
prog.raku -o prog` builds the C++ for this computer, against a simulated
CODAL, and writes the same.

## What is not there yet?

The radio between micro:bits, the speaker and microphone, `react` /
`whenever` over the board's events, and text kept in variables. They are in
[MICROBIT-PLAN.md](../../dev/plans/MICROBIT-PLAN.md), in order.

## Does it run on a micro:bit V1, or in the Python editor?

The target is the micro:bit V2 (2020 and later: it has a speaker and a touch
logo). The V1's runtime is an older one with less memory, and is not
supported yet. The Python editor and MakeCode run their own languages; a Raku
program goes onto the board as the `.hex` file CODAL's build makes, copied to
the `MICROBIT` drive.

## Does it run under Rakudo?

`use MicroBit` is built into rakupp. Rakudo has no such module and stops at
the `use` line with `Could not find MicroBit`.
