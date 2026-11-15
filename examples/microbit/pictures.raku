# Every built-in picture, one at a time: its name on the serial port, the
# picture on the LEDs for half a second, all 64 in turn, and round again.
# Watch it on a board with the serial port open (screen /dev/cu.usbmodem*
# 115200 on a Mac), to see which name goes with which picture.
#
# The pictures are MicroPython's, by its names spelt as Raku: Image.ARROW_NE
# is Image.ARROW-NE. Each one the program shows is 25 bytes in flash.
#
#   RAKUPP_MICROBIT_STOP=2000 rakupp pictures.raku
use MicroBit;

loop {
    # faces and feelings
    say "HEART";          display.show(Image.HEART);          pause 500;
    say "HEART-SMALL";    display.show(Image.HEART-SMALL);    pause 500;
    say "HAPPY";          display.show(Image.HAPPY);          pause 500;
    say "SMILE";          display.show(Image.SMILE);          pause 500;
    say "SAD";            display.show(Image.SAD);            pause 500;
    say "CONFUSED";       display.show(Image.CONFUSED);       pause 500;
    say "ANGRY";          display.show(Image.ANGRY);          pause 500;
    say "ASLEEP";         display.show(Image.ASLEEP);         pause 500;
    say "SURPRISED";      display.show(Image.SURPRISED);      pause 500;
    say "SILLY";          display.show(Image.SILLY);          pause 500;
    say "FABULOUS";       display.show(Image.FABULOUS);       pause 500;
    say "MEH";            display.show(Image.MEH);            pause 500;
    say "YES";            display.show(Image.YES);            pause 500;
    say "NO";             display.show(Image.NO);             pause 500;

    # the clock, an hour at a time
    say "CLOCK12";        display.show(Image.CLOCK12);        pause 500;
    say "CLOCK1";         display.show(Image.CLOCK1);         pause 500;
    say "CLOCK2";         display.show(Image.CLOCK2);         pause 500;
    say "CLOCK3";         display.show(Image.CLOCK3);         pause 500;
    say "CLOCK4";         display.show(Image.CLOCK4);         pause 500;
    say "CLOCK5";         display.show(Image.CLOCK5);         pause 500;
    say "CLOCK6";         display.show(Image.CLOCK6);         pause 500;
    say "CLOCK7";         display.show(Image.CLOCK7);         pause 500;
    say "CLOCK8";         display.show(Image.CLOCK8);         pause 500;
    say "CLOCK9";         display.show(Image.CLOCK9);         pause 500;
    say "CLOCK10";        display.show(Image.CLOCK10);        pause 500;
    say "CLOCK11";        display.show(Image.CLOCK11);        pause 500;

    # the arrows, around the compass
    say "ARROW-N";        display.show(Image.ARROW-N);        pause 500;
    say "ARROW-NE";       display.show(Image.ARROW-NE);       pause 500;
    say "ARROW-E";        display.show(Image.ARROW-E);        pause 500;
    say "ARROW-SE";       display.show(Image.ARROW-SE);       pause 500;
    say "ARROW-S";        display.show(Image.ARROW-S);        pause 500;
    say "ARROW-SW";       display.show(Image.ARROW-SW);       pause 500;
    say "ARROW-W";        display.show(Image.ARROW-W);        pause 500;
    say "ARROW-NW";       display.show(Image.ARROW-NW);       pause 500;

    # shapes
    say "TRIANGLE";       display.show(Image.TRIANGLE);       pause 500;
    say "TRIANGLE-LEFT";  display.show(Image.TRIANGLE-LEFT);  pause 500;
    say "CHESSBOARD";     display.show(Image.CHESSBOARD);     pause 500;
    say "DIAMOND";        display.show(Image.DIAMOND);        pause 500;
    say "DIAMOND-SMALL";  display.show(Image.DIAMOND-SMALL);  pause 500;
    say "SQUARE";         display.show(Image.SQUARE);         pause 500;
    say "SQUARE-SMALL";   display.show(Image.SQUARE-SMALL);   pause 500;

    # animals, music and things
    say "RABBIT";         display.show(Image.RABBIT);         pause 500;
    say "COW";            display.show(Image.COW);            pause 500;
    say "MUSIC-CROTCHET"; display.show(Image.MUSIC-CROTCHET); pause 500;
    say "MUSIC-QUAVER";   display.show(Image.MUSIC-QUAVER);   pause 500;
    say "MUSIC-QUAVERS";  display.show(Image.MUSIC-QUAVERS);  pause 500;
    say "PITCHFORK";      display.show(Image.PITCHFORK);      pause 500;
    say "XMAS";           display.show(Image.XMAS);           pause 500;
    say "PACMAN";         display.show(Image.PACMAN);         pause 500;
    say "TARGET";         display.show(Image.TARGET);         pause 500;
    say "TSHIRT";         display.show(Image.TSHIRT);         pause 500;
    say "ROLLERSKATE";    display.show(Image.ROLLERSKATE);    pause 500;
    say "DUCK";           display.show(Image.DUCK);           pause 500;
    say "HOUSE";          display.show(Image.HOUSE);          pause 500;
    say "TORTOISE";       display.show(Image.TORTOISE);       pause 500;
    say "BUTTERFLY";      display.show(Image.BUTTERFLY);      pause 500;
    say "STICKFIGURE";    display.show(Image.STICKFIGURE);    pause 500;
    say "GHOST";          display.show(Image.GHOST);          pause 500;
    say "SWORD";          display.show(Image.SWORD);          pause 500;
    say "GIRAFFE";        display.show(Image.GIRAFFE);        pause 500;
    say "SKULL";          display.show(Image.SKULL);          pause 500;
    say "UMBRELLA";       display.show(Image.UMBRELLA);       pause 500;
    say "SNAKE";          display.show(Image.SNAKE);          pause 500;
    say "SCISSORS";       display.show(Image.SCISSORS);       pause 500;
}
