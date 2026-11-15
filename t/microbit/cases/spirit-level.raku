# stop: 700
# accel: 0:150,-150,-1000;300:-150,150,-1000;550:0,0,-1024
# The course's spirit level ("Wasserwaage-Libelle"): one lit LED that the
# tilt moves. Each turn clears the display and lights the LED again, so the
# LEDs go dark for a moment, as on the board.
use MicroBit;

my int $x = 1;
my int $y = 1;
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
