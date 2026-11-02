# stop: 2200
# The first program on every board: the LED on for half a second, off for half.
use Arduino;

pinMode(LED_BUILTIN, OUTPUT);
Serial.begin(9600);
say "blink";

loop {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
}
