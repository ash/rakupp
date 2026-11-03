# stop: 2100
# The Uno R4 WiFi's LED matrix (use Arduino::LEDMatrix): frames drawn as
# pictures and packed into three 32-bit words while building, a frame the
# board computes as it runs, and clear. The heart is the one in Arduino's
# LED-matrix tutorial, whose words are 3184a444 44042081 100a0040: printing
# them pins the bit order to the library's.
use Arduino;
use Arduino::LEDMatrix;

constant @HEART = frame q:to/END/;
    ..##...##...
    .#..#.#..#..
    .#...#...#..
    .#.......#..
    ..#.....#...
    ...#...#....
    ....#.#.....
    .....#......
    END
constant @SMILE = frame q:to/END/;
    ............
    ...##..##...
    ...##..##...
    ............
    ..#......#..
    ...#....#...
    ....####....
    ............
    END
constant HEX = @HEART.map(*.base(16).lc).join(' ');

say "heart: {HEX}";
LEDMatrix.begin;
my uint32 @dot[3];
loop {
    state uint8 $n = 0;
    LEDMatrix.show(@HEART);  delay(300);
    LEDMatrix.show(@SMILE);  delay(300);
    LEDMatrix.show(@SMILE);  delay(100);    # the same picture: nothing to draw
    @dot[2] = 1 +< ($n++ % 12);              # one LED walking along the bottom row
    LEDMatrix.show(@dot);    delay(200);
    LEDMatrix.clear;         delay(100);
}
