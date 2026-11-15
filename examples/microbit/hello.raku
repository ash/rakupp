# The first program on a micro:bit: a greeting scrolled across the LEDs, then
# a smile that stays. It also says hello on the serial port.
#
#   RAKUPP_MICROBIT_STOP=6000 rakupp hello.raku                 # on the simulated board
#   rakupp --target=microbit hello.raku -o source/main.cpp      # for the board, with CODAL
use MicroBit;

say "Hello from Raku";
display.scroll("Hello");
display.show(Image.HAPPY);
