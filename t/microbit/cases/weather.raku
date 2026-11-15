# stop: 30000
# temp: 0:19;7000:23
# heading: 0:90;8000:200
# light: 0:12;8000:180
# The room: the temperature scrolled with its unit, the compass heading as an
# arrow, night or day from the light level: text joined as the board runs.
use MicroBit;

sub direction(int $h --> int) { (($h + 45) div 90) % 4 }

for ^2 {
    my int $t = temperature;
    display.scroll("{$t}C");
    say "temperature $t, heading {compass.heading}, light {display.read-light-level}";
    my int $d = direction(compass.heading);
    display.show($d == 0 ?? Image.ARROW-N !! $d == 1 ?? Image.ARROW-E !! $d == 2 ?? Image.ARROW-S !! Image.ARROW-W);
    pause 1000;
    display.show(display.read-light-level < 50 ?? "N" !! "D");
    pause 4000;
}
