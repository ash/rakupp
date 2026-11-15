# stop: 1000
# pins: 1=700,2=1
# buttons: 0@300-500
# The edge connector: digital and analog outputs, an analog and a digital
# input, and pin 0 touched for a while.
use MicroBit;

pin8.write-digital(1);
say "pin1 reads ", pin1.read-analog;
say "pin2 reads ", pin2.read-digital;
pin16.write-analog(pin1.read-analog div 2);
pin16.write-analog(5000);
loop {
    pin12.write-digital(pin0.is-touched);
    pause 100;
}
