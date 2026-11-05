# stop: 25000
# Fibonacci numbers on the Uno R4 WiFi's LED matrix, one a second, and on
# the serial port. The digits are a 3×5 font drawn as a picture, which the
# build turns into column bitmasks; the board composes each frame as it runs
# (my uint32 @frame[3]). Three digits fit the 12 columns, centred; a longer
# number shows its start, scrolls a column at a time to its end, and holds.
# The run ends at F(92), the last Fibonacci number an int holds.
use Arduino;
use Arduino::LEDMatrix;

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
constant TOP = 1;      # the digits' first row of the eight

my uint8  @digits[19]; # the number's digits, most significant first
my uint32 @frame[3];   # 96 LEDs, row by row from the top left, high bit first

# Draw the first $n digits with the leftmost column at $x0, which is
# negative while a long number scrolls.
sub draw(int $x0, int $n) {
    for ^3 -> $k { @frame[$k] = 0 }
    for ^12 -> $x {
        my int $col = $x - $x0;
        if $col >= 0 && $col < 4 * $n - 1 && $col % 4 != 3 {
            my int $bits = @FONT[3 * @digits[$col div 4] + $col % 4];
            for ^5 -> $r {
                if $bits +& (1 +< $r) != 0 {
                    my int $i = (TOP + $r) * 12 + $x;
                    @frame[$i div 32] = @frame[$i div 32] +| (1 +< (31 - $i % 32));
                }
            }
        }
    }
    LEDMatrix.show(@frame);
}

my int $a = 0;
my int $b = 1;
my int $index = 0;
LEDMatrix.begin;

loop {
    say $a;
    my int $n = 0;
    my int $v = $a;
    repeat { $n++; $v = $v div 10 } while $v > 0;
    $v = $a;
    for ^$n -> $k { @digits[$n - 1 - $k] = $v % 10; $v = $v div 10 }

    my int $width = 4 * $n - 1;
    if $width <= 12 {
        draw((12 - $width) div 2, $n);
        delay(1000);
    }
    else {
        for 0 .. $width - 12 -> $s {
            draw(-$s, $n);
            if $s == 0 { delay(600) } else { delay(120) }
        }
        delay(900);
    }

    last if $index == 92;
    my int $next = $a + $b;
    $a = $b;
    $b = $next;
    $index++;
}
