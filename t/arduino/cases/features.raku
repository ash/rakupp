# stop: 100000
# The subset in one program: constants and a BEGIN value computed while
# building, native wrap-around, floored div and %, bit operators, shaped
# arrays, subs with results taken from their last statement, `for` over a
# range and over a table, num to integer, and next/last in the board's loop.
use Arduino;

constant N = 2 ** 4;
constant @primes = (2..50).grep(*.is-prime);
constant @wave = (^8).map: { (127.5 + 127.5 * sin(2 * pi * $_ / 8)).Int };
constant GREETING = "built at compile time: " ~ N;
my uint8 @hist[8];
my int16 $big = 32767;
my uint8 $small = 250;
my uint16 $boot;
BEGIN { $boot = 6 * 7 }

sub collatz(int $n is copy --> int) {
    my int $steps = 0;
    while $n != 1 {
        $n = $n %% 2 ?? $n div 2 !! 3 * $n + 1;
        $steps++;
    }
    $steps
}
sub sign(int $x --> int8) { if $x < 0 { -1 } elsif $x == 0 { 0 } else { 1 } }
sub blink(uint8 $pin, int $times) {
    for ^$times {
        digitalWrite($pin, HIGH); delay(100);
        digitalWrite($pin, LOW);  delay(100);
    }
}

$big++;
$small += 10;
say GREETING;
say "wrapped: $big $small boot=$boot";
say "N={N} primes={@primes.elems} last={@primes[@primes.end]}";
for @primes -> $p { @hist[$p % 8]++ }
print "hist:"; for ^8 -> $k { print " ", @hist[$k] }; say "";
print "wave:"; for @wave -> $w { print " ", $w }; say "";
say "collatz(27)=", collatz(27);
say "sign: ", sign(-5), sign(0), sign(7);
say "div/mod: ", -7 div 2, " ", -7 % 2, " ", 7 div -2;
say "bits: ", 1 +< 10, " ", -16 +> 2, " ", 0xF0 +& 0x3C, " ", +^0;
say "bool: ", 3 > 2, " ", so 0;
my num $x = 2e0;
say "int of num: ", ($x * 1.5).Int, " ", (-2.5e0).round, " ", sqrt(16e0).Int;
pinMode(LED_BUILTIN, OUTPUT);
blink(LED_BUILTIN, 2);
my int $n = 0;
loop {
    $n++;
    next if $n %% 2;
    say "loop $n at {millis()}";
    last if $n > 5;
    delay(250);
}
