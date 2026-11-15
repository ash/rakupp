# A die: shake the board, and it shows a number from 1 to 6. The board
# remembers a shake until the program asks about it.
#
#   RAKUPP_MICROBIT_STOP=3000 RAKUPP_MICROBIT_GESTURES="800:shake;820:face up;2000:shake" rakupp dice.raku
use MicroBit;

display.show(Image.SQUARE-SMALL);
loop {
    if accelerometer.was-gesture('shake') {
        my int $n = randint(1, 6);
        say "rolled $n";
        display.show($n);
    }
    pause 50;
}
