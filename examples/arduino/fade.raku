# An LED that breathes, on a PWM pin. The sine wave is computed while the
# program is built — `sin`, `pi` and `.map` run on your computer — and the
# board gets only the 32 numbers, as a table in flash:
#
#   RAKUPP_ARDUINO_STOP=100 rakupp fade.raku
#   rakupp --target=arduino fade.raku | grep -A3 t_wave

use Arduino;

constant LED   = 9;   # a PWM pin
constant @wave = (^32).map: { (127.5 + 127.5 * sin(2 * pi * $_ / 32)).Int };

pinMode(LED, OUTPUT);
say "a wave of {@wave.elems} steps";

loop {
    state uint8 $i = 0;
    analogWrite(LED, @wave[$i++ % @wave.elems]);
    delay(20);
}
