# The first program of python-online.ch's accelerometer chapter, in Raku: the
# tilt to the left or right, printed on the serial port and shown as an arrow.
# The MicroPython it comes from:
#
#   from microbit import *
#   while True:
#       acc = accelerometer.get_x()
#       print (acc)
#       if acc > 0:
#           display.show(Image.ARROW_E)
#       else:
#           display.show(Image.ARROW_W)
#       sleep(100)
#
#   RAKUPP_MICROBIT_STOP=600 RAKUPP_MICROBIT_ACCEL="0:-200,0,-1000;300:250,0,-950" rakupp tilt.raku
use MicroBit;

loop {
    my int16 $acc = accelerometer.x;
    say $acc;
    if $acc > 0 {
        display.show(Image.ARROW-E);
    }
    else {
        display.show(Image.ARROW-W);
    }
    pause 100;
}
