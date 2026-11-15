# A beating heart: two of the built-in pictures, and a third made while
# building from a picture you can read in the source — dim LEDs and all.
#
#   RAKUPP_MICROBIT_STOP=2000 rakupp heart.raku
use MicroBit;

# A brightness from 0 to 9 for each LED, row by row, as MicroPython writes
# pictures. `image` runs while building: the board gets 25 bytes.
constant GLOW = image q:to/END/;
    03030
    39993
    39993
    03930
    00300
    END

loop {
    display.show(Image.HEART);
    pause 300;
    display.show(Image.HEART-SMALL);
    pause 200;
    display.show(GLOW);
    pause 500;
}
