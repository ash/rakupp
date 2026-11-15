# stop: 10000
# The first program: a greeting on the display and on the serial port. No
# top-level loop: it runs once, and the board keeps showing the face.
use MicroBit;

say "Hello from Raku";
display.scroll("Hello");
display.show(Image.HAPPY);
