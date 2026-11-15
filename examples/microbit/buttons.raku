# A counter you work with the buttons: press A to count up, press B to count
# down, and touch the gold logo on the front to start again from 0. The count
# is on the LEDs from the start, and on the serial port each time it changes.
# One button at a time is enough; there is nothing to press together.
#
# get-presses counts the presses that began since it was last asked, so two
# quick presses of A count two even while the board shows a number;
# was-pressed says whether there was at least one.
#
#   RAKUPP_MICROBIT_STOP=3000 RAKUPP_MICROBIT_BUTTONS="A@200,A@600,A@900,B@1500,logo@2500" rakupp buttons.raku
use MicroBit;

my int $count = 0;
display.show($count);
loop {
    my int $was = $count;
    $count += button-a.get-presses;
    if button-b.was-pressed && $count > 0 { $count-- }
    if pin-logo.is-touched { $count = 0 }
    if $count != $was {
        say "count: $count";
        display.show($count);
    }
    pause 50;
}
