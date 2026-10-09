# Regression: Nil is bound by a binding and resets a container (2026-10-09).
#
# `my \x = Nil` and `constant c = Nil` BIND, so the name is Nil itself; they
# answered Any, because the "Nil resets the container" rule of `$x = Nil` was
# applied to them. A list assignment did the opposite: it stored Nil as it
# came, so `my ($a, $b) = Nil, 5` left Nil in $a and `my Int ($a) = Nil` died
# with a type check. Each element of an `@` target is a container too, and a
# `%` target is stored into as `my %h = …` is (an odd count is an error).
#
# Expectations checked against Rakudo 2026.09 (/opt/homebrew/bin/rakudo).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# binding keeps Nil
{
    my \x = Nil;
    check x.raku, 'Nil', 'my \x = Nil';
    constant c = Nil;
    check c.raku, 'Nil', 'constant c = Nil';
    sub g { Nil }
    my \t = g();
    check t.raku, 'Nil', 'my \t = a call answering Nil';
    my \w = Any;
    check w.raku, 'Any', 'my \w = Any is still Any';
    my $k is default(7) = Nil;
    check $k, 7, 'a `$` container still resets to its default';
}

# a list assignment resets each container it hands Nil
{
    my ($p) = Nil;
    check $p.raku, 'Any', 'my ($p) = Nil';
    my ($a, $b) = Nil, 5;
    check ($a.raku, $b), ('Any', 5), 'my ($a, $b) = Nil, 5';
    my Int ($e, $f) = Nil, 3;
    check ($e.raku, $f), ('Int', 3), 'a typed one resets to its type object';
    my $m is default(4); my $n;
    ($m, $n) = Nil, 2;
    check $m, 4, '…and to an `is default`';
    my ($i, @j) = 1, Nil, 2;
    check @j.raku, '[Any, 2]', 'an @ target\'s elements reset';
    my ($r, Int @s) = 1, Nil, 2;
    check @s.raku, 'Array[Int].new(Int, 2)', '…a typed one to its type, and it stays typed';
    my ($w, %x) = 1, a => Nil;
    check %x.raku, '{:a(Any)}', 'a % target\'s values reset';
    check ((try { my ($w2, %x2) = 1, 2, 3, 4; %x2 }) // $!).^name, 'X::Hash::Store::OddNumber',
          '…and an odd count left for it is an error';
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
