# stop: 60000
# Every picture: MicroPython's 64 built-in ones in turn, then two made while
# building with `image`, one the program fills as it runs, and single LEDs.
# The board shows the same pictures from the emitter's own copy of the list.
# No pauses: on a real board the whole show is over in a flicker, ending
# dark. examples/microbit/pictures.raku shows each picture for half a second.
use MicroBit;

constant SMILE = image q:to/END/;
    00000
    09090
    00000
    90009
    09990
    END
constant @CROSS = image '#...#:.#.#.:..#..:.#.#.:#...#';

display.show(Image.HEART);
display.show(Image.HEART-SMALL);
display.show(Image.HAPPY);
display.show(Image.SMILE);
display.show(Image.SAD);
display.show(Image.CONFUSED);
display.show(Image.ANGRY);
display.show(Image.ASLEEP);
display.show(Image.SURPRISED);
display.show(Image.SILLY);
display.show(Image.FABULOUS);
display.show(Image.MEH);
display.show(Image.YES);
display.show(Image.NO);
display.show(Image.CLOCK12);
display.show(Image.CLOCK1);
display.show(Image.CLOCK2);
display.show(Image.CLOCK3);
display.show(Image.CLOCK4);
display.show(Image.CLOCK5);
display.show(Image.CLOCK6);
display.show(Image.CLOCK7);
display.show(Image.CLOCK8);
display.show(Image.CLOCK9);
display.show(Image.CLOCK10);
display.show(Image.CLOCK11);
display.show(Image.ARROW-N);
display.show(Image.ARROW-NE);
display.show(Image.ARROW-E);
display.show(Image.ARROW-SE);
display.show(Image.ARROW-S);
display.show(Image.ARROW-SW);
display.show(Image.ARROW-W);
display.show(Image.ARROW-NW);
display.show(Image.TRIANGLE);
display.show(Image.TRIANGLE-LEFT);
display.show(Image.CHESSBOARD);
display.show(Image.DIAMOND);
display.show(Image.DIAMOND-SMALL);
display.show(Image.SQUARE);
display.show(Image.SQUARE-SMALL);
display.show(Image.RABBIT);
display.show(Image.COW);
display.show(Image.MUSIC-CROTCHET);
display.show(Image.MUSIC-QUAVER);
display.show(Image.MUSIC-QUAVERS);
display.show(Image.PITCHFORK);
display.show(Image.XMAS);
display.show(Image.PACMAN);
display.show(Image.TARGET);
display.show(Image.TSHIRT);
display.show(Image.ROLLERSKATE);
display.show(Image.DUCK);
display.show(Image.HOUSE);
display.show(Image.TORTOISE);
display.show(Image.BUTTERFLY);
display.show(Image.STICKFIGURE);
display.show(Image.GHOST);
display.show(Image.SWORD);
display.show(Image.GIRAFFE);
display.show(Image.SKULL);
display.show(Image.UMBRELLA);
display.show(Image.SNAKE);
display.show(Image.SCISSORS);
display.show(SMILE);
display.show(@CROSS);
my uint8 @fade[25];
for ^25 -> $i { @fade[$i] = $i % 10 }
display.show(@fade);
say "fade: ", display.get-pixel(4, 0), " ", display.get-pixel(0, 1);
display.set-pixel(0, 0, 9);
say "corner: ", display.get-pixel(0, 0);
display.show(7);
say "the 7 lights its corner: ", display.get-pixel(0, 0);
display.clear;
