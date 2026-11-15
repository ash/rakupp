# stop: 20000
# The subset on a micro:bit: a sub with a native signature, a native that
# wraps, Raku's own sleep in seconds on the board's clock, a Bool as text, and
# `last` leaving the top-level loop, which ends the program.
use MicroBit;

sub square(int16 $x --> int16) { $x * $x }

my int8 $w = 127;
$w++;
say "int8 wraps to $w";
say "square 12 is {square(12)}";
sleep 0.25;
say "after sleep: {running-time} ms";
sleep 1;
say "after a second: {running-time} ms";
display.scroll(button-a.is-pressed);
display.show(-12);
my int $n = 0;
loop {
    $n++;
    say "turn $n";
    last if $n == 3;
    pause 100;
}
