# stop: 7000
# wifi: home:correct-horse
# time: 1798761597
# A clock on the Uno R4 WiFi's LED matrix, set from the network, and the minute
# on the serial port. The board joins the network with wifi.json's name and
# password, read while building, and takes the time from its WiFi module
# (WiFi.getTime: seconds since 1970, UTC, which the module fetches from a time
# server); it counts on with millis() and takes the time again every hour. The
# zone is an offset the build adds: this case's is 0, so its run is the same on
# every machine, and a clock for a desk takes the building computer's own,
# which holds until a daylight-saving change:
#
#   constant ZONE = DateTime.now.offset;
#
# The gate's network time is 2026-12-31 23:59:57 UTC, so the run crosses
# midnight. HH:MM in fib.raku's 3×5 font is 17 columns, wider than the
# matrix's 12, so each reading shows the hours, scrolls a column at a time to
# the minutes and holds; on the bottom row a lit LED moves every 5 seconds.
use Arduino;
use Arduino::LEDMatrix;
use Arduino::WiFi;
use JSON::Native;

constant SSID = from-json($?FILE.IO.sibling('wifi.json').slurp)<ssid>;
constant PASS = from-json($?FILE.IO.sibling('wifi.json').slurp)<password>;
constant ZONE = 0;          # seconds east of UTC
constant HOUR = 3_600_000;  # how often the clock takes the network's time again, in ms

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

my int    $base;            # the time of day when the clock last took the network's time, in seconds
my uint32 $synced;          # millis() then

# Take the network's time; False when the module has none, as for a moment
# after joining, and the clock keeps counting from the time it has.
sub sync(--> Bool) {
    my int $t = WiFi.getTime;
    return False if $t == 0;
    $base = ($t + ZONE) % 86400;
    $synced = millis();
    True
}
# The milliseconds since then, in 32 bits as millis() is, so the difference
# stays right when millis() wraps after 49.7 days.
sub since-sync(--> uint32) { millis() - $synced }
# The time of day, in seconds since midnight.
sub now-seconds(--> int) { ($base + since-sync() div 1000) % 86400 }

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
until WiFi.begin(SSID, PASS) == WL_CONNECTED { delay(5000) }
until sync() { delay(1000) }
my int $said = -1;          # the minute last said on the serial port
loop {
    sync() if since-sync() >= HOUR;
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
