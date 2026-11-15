# stop: 1000
# accel: 0:-200,0,-1000;450:300,0,-950
# The course's first accelerometer program (python-online.ch, "Sensorwerte
# abfragen") in Raku: the tilt to the left or right, as an arrow.
use MicroBit;

loop {
    my int16 $acc = accelerometer.x;
    say $acc;
    display.show: $acc > 0 ?? Image.ARROW-E !! Image.ARROW-W;
    pause 100;
}
