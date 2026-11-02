# stop: 700
# A table computed while building — `.map` and `sin` run on the computer, and
# the board gets 32 bytes in flash — driving a PWM pin.
use Arduino;

constant LED   = 9;                                                  # a PWM pin
constant @wave = (^32).map: { (127.5 + 127.5 * sin(2 * pi * $_ / 32)).Int };

pinMode(LED, OUTPUT);                  # the mainline before the loop: setup()
say "a wave of {@wave.elems} steps";

loop {                                 # the top-level loop: loop()
    state uint8 $i = 0;
    analogWrite(LED, @wave[$i++ % @wave.elems]);
    delay(20);
}
