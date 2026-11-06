# stop: 7000
# A clock on the Uno R4 WiFi's LED matrix, and the minute on the serial port.
# HH:MM in fib.raku's 3×5 font is 17 columns, wider than the matrix's 12, so
# each reading shows the hours, scrolls a column at a time to the minutes and
# holds; on the bottom row a lit LED moves one column every 5 seconds. A board
# has no clock of the day (the network's time needs UDP, which the board does
# not have yet), so the clock is set while building and counts with millis().
# This case sets it to 23:59:57, so the run crosses midnight; a clock for a
# desk would set it to the time of the build, right until the board resets:
#
#   constant START = with DateTime.now { .hour * 3600 + .minute * 60 + .second };
use Arduino;
use Arduino::LEDMatrix;

constant START = do { my ($h, $m, $s) = '23:59:57'.split(':')».Int; $h * 3600 + $m * 60 + $s };

# The ten digits, three columns each with one between; in @FONT a digit is
# three numbers, one a column, whose bit r is row r from the top.
constant @FONT = do {
    my @rows = q:to/END/.lines;
        ###..#..###.###.#.#.###.###.###.###.###
        #.#.##....#...#.#.#.#...#.....#.#.#.#.#
        #.#..#..###.###.###.###.###...#.###.###
        #.#..#..#.....#...#...#.#.#...#.#.#...#
        ###.###.###.###...#.###.###...#.###.###
        END
    (^10).map(-> $d { (^3).map(-> $c {
        [+] (^5).map(-> $r { @rows[$r].substr(4 * $d + $c, 1) eq '#' ?? 1 +< $r !! 0 })
    }) }).flat
};
constant COLON = 0b01010;   # rows 1 and 3 of the digits
constant TOP   = 1;         # the digits' first row of the eight
constant WIDTH = 17;        # HH:MM: four digits and the colon, a column between each

my uint8  @cols[17];        # HH:MM's columns, as @FONT has them
my uint32 @frame[3];        # 96 LEDs, row by row from the top left, high bit first

# The time of day, in seconds since midnight.
sub now-seconds(--> int) { (START + millis() div 1000) % 86400 }

sub put-digit(int $at, int $d) {
    for ^3 -> $c { @cols[$at + $c] = @FONT[3 * $d + $c] }
}
sub compose(int $t) {
    for ^WIDTH -> $c { @cols[$c] = 0 }
    put-digit(0, $t div 36000);
    put-digit(4, $t div 3600 % 10);
    @cols[8] = COLON;
    put-digit(10, $t div 600 % 6);
    put-digit(14, $t div 60 % 10);
}

sub light(int $x, int $y) {
    my int $i = $y * 12 + $x;
    @frame[$i div 32] = @frame[$i div 32] +| (1 +< (31 - $i % 32));
}
# HH:MM from its column $s on, and the seconds' LED on the bottom row.
sub draw(int $s, int $t) {
    for ^3 -> $k { @frame[$k] = 0 }
    for ^12 -> $x {
        my int $bits = @cols[$x + $s];
        for ^5 -> $r {
            light($x, TOP + $r) if $bits +& (1 +< $r) != 0;
        }
    }
    light($t % 60 div 5, 7);
    LEDMatrix.show(@frame);
}

sub two(int $n) {
    print 0 if $n < 10;
    print $n;
}

LEDMatrix.begin;
my int $said = -1;          # the minute last said on the serial port
loop {
    my int $t = now-seconds();
    compose($t);
    if $t div 60 != $said {
        $said = $t div 60;
        two($t div 3600); print ":"; two($t div 60 % 60); say "";
    }
    draw(0, $t);
    delay(1500);
    for 1 .. WIDTH - 12 -> $s {
        draw($s, now-seconds());
        delay(100);
    }
    delay(1400);
}
