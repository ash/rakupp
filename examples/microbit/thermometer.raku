# A thermometer: the temperature every five seconds, scrolled with its unit,
# and an arrow for whether it went up or down. The text is put together as
# the board runs: "21 C" is a literal and an integer, joined there.
#
#   RAKUPP_MICROBIT_STOP=13000 RAKUPP_MICROBIT_TEMP="0:21;6000:24" rakupp thermometer.raku
use MicroBit;

my int $last = temperature;
loop {
    my int $t = temperature;
    say "temperature: $t";
    display.scroll("$t C");
    if    $t > $last { display.show(Image.ARROW-N) }
    elsif $t < $last { display.show(Image.ARROW-S) }
    else             { display.clear }
    $last = $t;
    pause 5000;
}
