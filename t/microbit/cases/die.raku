# stop: 1000
# A runtime error on the board: a pixel off the edge of the display. The
# message and its line go to the serial port, and a simulation exits 1; a
# board shows a sad face and the line. The interpreter adds a backtrace the
# board has no room for, so the runner compares stderr without location lines.
use MicroBit;

my int $x = 3;
loop {
    display.set-pixel($x, 0, 9);
    $x++;
    pause 10;
}
