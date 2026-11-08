# A FIRST or INIT phaser written last gives the block or routine its value,
# as Rakudo keeps the value of the one run and answers it on every pass:
# `(1, 2, 3).map({ $_; FIRST { 7 } })` was (1 2 3) and `sub g { 1; INIT { 5 } }`
# answered 1, where Rakudo answers (7 7 7) and 5. A FIRST that runs before an
# ENTER does so in a called block too: `.map` ran ENTER first. ENTER written
# last is its own value, as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck (1, 2, 3).map({ $_ if 0; FIRST { 7 } }).List, (7, 7, 7), 'FIRST last in a map';
sub g { 1 if 0; INIT { 5 } }
ck (g(), g()), (5, 5), 'INIT last in a sub';
ck (1, 2).map({ $_ if 0; INIT { 6 } }).List, (6, 6), 'INIT last in a map';
my &b = { 1 if 0; FIRST { 9 } };
ck (b(), b()), (9, 9), 'FIRST last in a called block, each call';
my @r = do for 1..3 { $_ if 0; FIRST { "f" } };
ck @r, ['f', 'f', 'f'], 'FIRST last in a for loop';
sub h { FIRST { 3 } }
ck (h(), h()), (3, 3), 'a sub that is a FIRST';
my $c = 0; sub k { $c++; FIRST { $c } }
ck (k(), k()), (0, 0), 'the value of the one run';
sub e { ENTER { 11 } }
ck e(), 11, 'ENTER last';
ck (1, 2).map({ $_ if 0; ENTER { $_ * 10 } }).List, (10, 20), 'ENTER last in a map, each pass';
my @o; for 1..2 { ENTER { @o.push("E$_") }; FIRST { @o.push("F") } }
ck @o, ['F', 'E1', 'E2'], 'FIRST before ENTER in a loop';
my @p; (1, 2).map({ ENTER { @p.push("E") }; FIRST { @p.push("F") }; $_ }).eager;
ck @p, ['F', 'E', 'E'], 'and in a map';
sub n { 4; LEAVE { } }
ck n(), Nil, 'another phaser written last is still Nil';

say 'PASS';
