# Blink — the Arduino's "hello, world". The mainline before the top-level
# loop becomes setup(), and one pass of the loop becomes loop(). Run it on
# the simulated board, then write the sketch for a real one:
#
#   rakupp blink.raku                          # in real time; Ctrl-C stops it
#   RAKUPP_ARDUINO_STOP=2200 rakupp blink.raku # 2.2 s of board time, at once
#   mkdir blink && rakupp --target=arduino blink.raku -o blink/blink.ino

use Arduino;

pinMode(LED_BUILTIN, OUTPUT);
say "blink";

loop {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
}
