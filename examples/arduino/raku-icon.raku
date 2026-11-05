# RAKU on the Uno R4 WiFi's 12×8 LED matrix: RA over KU, as on the Learn
# Raku app's icon. `frame` runs while the program is built and packs the
# picture into the three 32-bit words the matrix library takes. Eight rows
# cannot hold two lines of four with a dark row between them, so KU is three
# rows tall. The simulated board draws the picture on stderr:
#
#   RAKUPP_ARDUINO_STOP=10 rakupp raku-icon.raku
#
# There is no top-level loop, so the program runs once, in setup(); the
# library keeps the picture lit.

use Arduino;
use Arduino::LEDMatrix;

constant @RAKU = frame q:to/END/;
    ####....###.
    #...#..#...#
    ####...#####
    #...#..#...#
    ............
    #..#...#...#
    ###....#...#
    #..#....###.
    END

LEDMatrix.begin;
LEDMatrix.show(@RAKU);
