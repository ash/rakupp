# stop: 1000
# `die` on the board: the message and its line on the serial port, exit 1 in
# a simulation. The interpreter adds a backtrace the board has no room for, so
# the runner compares stderr without location lines.
use Arduino;

my int $d = 0;
my uint8 @buf[4];
say "before";
loop {
    @buf[$d] = 7;
    $d++;
    delay(10);
    if $d == 3 { die "stopped at $d" }
}
