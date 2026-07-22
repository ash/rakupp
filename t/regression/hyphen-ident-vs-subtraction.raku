# Regression: a hyphen continues an identifier ONLY before a letter, never a
# digit. The nqp leg needed hyphenated qualified names (`M::from-json`), and the
# first fix let `consumeIdentChars` eat `-`+isIdentCont — which INCLUDES digits,
# so `$x.elems-1` became a method named `elems-1` instead of `elems - 1`. That
# was an 11,770-assertion Roast regression (gate mf3, 2026-07-23), caught before
# ship and fixed by requiring a LETTER after the hyphen (isIdentStart).

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { say "FAIL: $label — got {$got.raku}, want {$want.raku}"; $ok = False }
}

# hyphen-then-digit is subtraction
check([1, 2, 3].elems - 1, 2, 'elems - 1 spaced');
check([1, 2, 3].elems-1,   2, 'elems-1 unspaced (subtraction, not a method name)');
my $x = 10;
check($x-1, 9, '$x-1 is subtraction');
check(5-2,  3, '5-2 literal subtraction');

# hyphen-then-letter is one identifier
sub twice-it($n) { $n * 2 }
check(twice-it(21), 42, 'hyphenated sub name');
my %h = 'a-b' => 7;
check(%h<a-b>, 7, 'hyphenated hash key');

# hyphenated names survive package qualification (the nqp-leg motivation)
module Demo {
    our sub from-json($s) { $s.flip }
}
check(Demo::from-json('abc'), 'cba', 'hyphenated qualified sub call');

# apostrophe too (before a letter)
sub is'ok() { 99 }
check(is'ok(), 99, "apostrophe in identifier");

say 'PASS' if $ok;
