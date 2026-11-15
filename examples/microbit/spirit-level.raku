# A spirit level, from the same chapter: one lit LED that the tilt moves, which
# is in the middle when the board lies flat.
#
#   RAKUPP_MICROBIT_STOP=600 RAKUPP_MICROBIT_ACCEL="0:150,0,-1000;300:0,-150,-1000" rakupp spirit-level.raku
use MicroBit;

my int $x = 2;
my int $y = 2;
loop {
    my int $ax = accelerometer.x;
    my int $ay = accelerometer.y;
    if    $ax > 100  && $x < 4 { $x++ }
    elsif $ax < -100 && $x > 0 { $x-- }
    elsif $ay > 100  && $y < 4 { $y++ }
    elsif $ay < -100 && $y > 0 { $y-- }
    display.clear;
    display.set-pixel($x, $y, 9);
    pause 100;
}
