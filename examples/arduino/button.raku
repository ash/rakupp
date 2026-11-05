# A button lights the LED, and each press is reported on the serial port.
# The button joins pin 2 to GND, so with the pin's pull-up it reads HIGH until
# it is pressed. On the simulated board, an input reads what
# RAKUPP_ARDUINO_PINS says — here the button is held down:
#
#   RAKUPP_ARDUINO_STOP=100 RAKUPP_ARDUINO_PINS="2=0" rakupp button.raku
#
# Every variable has a native type: the board has no untyped ones.

use Arduino;

constant BUTTON = 2;

pinMode(BUTTON, INPUT_PULLUP);
pinMode(LED_BUILTIN, OUTPUT);

loop {
    state Bool $was = False;
    my Bool $down = digitalRead(BUTTON) == LOW;
    digitalWrite(LED_BUILTIN, $down);
    if $down && !$was { say "pressed" }
    $was = $down;
    delay(10);
}
