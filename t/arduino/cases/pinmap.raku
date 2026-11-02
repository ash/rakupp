# stop: 400
# A pin map read from a JSON file while building, with a module: the board
# gets three bytes and a string, and neither the file nor the parser.
use Arduino;
use JSON::Native;

constant @LEDS  = from-json($?FILE.IO.sibling('pinmap.json').slurp)<leds>.list;
constant BOARD  = from-json($?FILE.IO.sibling('pinmap.json').slurp)<board>;

say "board: {BOARD}, {@LEDS.elems} LEDs";
for @LEDS -> $p { pinMode($p, OUTPUT) }
loop {
    for @LEDS -> $p {
        digitalWrite($p, HIGH);
        delay(50);
        digitalWrite($p, LOW);
    }
}
