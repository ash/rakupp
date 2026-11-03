# Arduino, the simulated board behind `use Arduino` (ARDUINO-PLAN.md). Built
# into rakupp as a shadow module (cmake/EmbedTools.cmake), so this copy is
# found before any -I path or store and an ecosystem `Arduino` never stands in
# for it. Under the interpreter these routines ARE the board; under
# `rakupp --target=arduino` the emitter (src/codegen/Arduino.cpp) knows each
# name and emits the Arduino core's own call, so nothing here reaches a board.
#
# The board writes what a real one shows. Serial is stdout. Every pin change
# is a line on stderr — `[1500 ms] pin 13 HIGH` — the LED a desk would see.
# The host core a `--sim` build links (src/codegen/ArduinoSim.h) writes the
# same bytes by the same rules, so a sketch and its source can be compared.
#
# The clock: real time by default, so `delay(500)` waits half a second. With
# RAKUPP_ARDUINO_STOP=MS it is virtual — `delay` advances it instantly, every
# other board call advances it one microsecond (so a loop polling `millis`
# moves on), and the program stops when it passes MS milliseconds. Inputs:
# RAKUPP_ARDUINO_PINS="2=1,A0=512" fixes what digitalRead and analogRead see.

unit module Arduino;

constant LOW          is export = 0;
constant HIGH         is export = 1;
constant INPUT        is export = 0;
constant OUTPUT       is export = 1;
constant INPUT_PULLUP is export = 2;
# an Uno's numbering: the emitter writes these names, and each core has its own
constant LED_BUILTIN  is export = 13;
constant A0 is export = 14;
constant A1 is export = 15;
constant A2 is export = 16;
constant A3 is export = 17;
constant A4 is export = 18;
constant A5 is export = 19;
constant A6 is export = 20;
constant A7 is export = 21;

my $stop-us = do with %*ENV<RAKUPP_ARDUINO_STOP> { .Int * 1000 if .chars };
my $virtual = $stop-us.defined;
my $now-us  = 0;     # the virtual clock
my $start   = now;   # …and the real one's origin
my %mode;
my %level;
my %pwm;
my %input = do with %*ENV<RAKUPP_ARDUINO_PINS> {
    .split(',', :skip-empty).map({
        my ($p, $v) = .split('=', 2);
        ($p ~~ /^ A (\d) $/ ?? 14 + $0 !! $p.Int) => $v.Int
    }).Hash
} // {};

sub clock-us() { $virtual ?? $now-us !! ((now - $start) * 1_000_000).floor }
# `our`, so Arduino::LEDMatrix and other parts of the board keep one clock
our sub stamp() { '[' ~ (clock-us() div 1000) ~ ' ms] ' }
our sub advance($us) {
    return unless $virtual;
    $now-us += $us;
    if $now-us > $stop-us {
        $*ERR.print: '[' ~ ($stop-us div 1000) ~ " ms] stop\n";
        exit 0;
    }
}
sub mode-name($m) { $m == OUTPUT ?? 'OUTPUT' !! $m == INPUT_PULLUP ?? 'INPUT_PULLUP' !! 'INPUT' }

sub pinMode($pin, $mode) is export {
    advance 1;
    %mode{+$pin} = +$mode;
    $*ERR.print: stamp() ~ "pin $pin mode {mode-name($mode)}\n";
}
sub digitalWrite($pin, $value) is export {
    advance 1;
    my $l = $value ?? HIGH !! LOW;
    return if (%level{+$pin} // LOW) == $l;
    %level{+$pin} = $l;
    $*ERR.print: stamp() ~ "pin $pin {$l ?? 'HIGH' !! 'LOW'}\n";
}
sub digitalRead($pin) is export {
    advance 1;
    with %input{+$pin} { return $_ ?? HIGH !! LOW }
    my $m = %mode{+$pin} // INPUT;
    $m == OUTPUT ?? (%level{+$pin} // LOW) !! $m == INPUT_PULLUP ?? HIGH !! LOW
}
sub analogRead($pin) is export {
    advance 1;
    %input{+$pin} // 0
}
sub analogWrite($pin, $value) is export {
    advance 1;
    my $v = ($value max 0) min 255;
    return if (%pwm{+$pin} // -1) == $v;
    %pwm{+$pin} = $v;
    $*ERR.print: stamp() ~ "pin $pin pwm $v\n";
}
sub delay($ms) is export {
    if $virtual { advance $ms * 1000 } else { sleep $ms / 1000 }
}
sub delayMicroseconds($us) is export {
    if $virtual { advance +$us } else { sleep $us / 1_000_000 }
}
sub millis() is export { advance 1; clock-us() div 1000 }
sub micros() is export { advance 1; clock-us() }

# Serial is this process's stdout. A board's println ends the line with
# CR LF, and so does this one; `say` is Raku's and ends it with LF alone.
class Serial is export {
    method begin(|) { }
    method print(*@v)   { print @v.join }
    method println(*@v) { print @v.join ~ "\r\n" }
}
