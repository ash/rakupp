# Regression: a native container holds its own kind of number and converts
# nothing on the way in. A native int takes an Int (Bool and an Int enum are
# Ints), a native num takes a Num; anything else assigned or bound at run time
# dies with X::AdHoc, as it does in Rakudo:
#
#     my num $a = 1/2;   # rakupp stored 0.5
#     my int $c = 7/2;   # rakupp stored 3
#
# The literal spellings (`my num $b = 1`) were already refused at compile time;
# only the run-time store diverged. The same held for int8/num32/uint8 widths,
# a later `=`, compound ops (`$i /= 2`), list assignment, an `is rw` parameter
# written inside the routine, and a native parameter given a Rat (that died,
# but as X::TypeCheck::Binding::Parameter).
#
# Two things the check had to bring along:
#   - a native's tags travelled with its VALUE into a boxed container, so
#     `my $n = $an-int; $n = 1/2` stored 0 — and would now have died;
#   - `$i ** -1` on a native int is the native candidate (an Int, 0) — the
#     truncation had been hiding that rakupp made a Rat there.
#
# Every expectation checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo
# (NOT the bare name `raku`, which on this box is rakupp). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
# the class of what `&code` dies with, or 'lived'
sub dies-as(&code) {
    code();
    CATCH { default { return .^name } }
    'lived'
}

# ---- declarations and later assignments ----
ck dies-as({ my num $a = 1/2 }),            'X::AdHoc', 'my num = Rat';
ck dies-as({ my int $c = 7/2 }),            'X::AdHoc', 'my int = Rat';
ck dies-as({ my int8 $c = 7/2 }),           'X::AdHoc', 'my int8 = Rat';
ck dies-as({ my uint8 $c = 7/2 }),          'X::AdHoc', 'my uint8 = Rat';
ck dies-as({ my num32 $a = 1/2 }),          'X::AdHoc', 'my num32 = Rat';
ck dies-as({ my num $a = 0e0; $a = 1/2 }),  'X::AdHoc', 'num = Rat later';
ck dies-as({ my int $c = 0; $c = 7/2 }),    'X::AdHoc', 'int = Rat later';
ck dies-as({ my $r = 1/2; my num $a = $r }), 'X::AdHoc', 'num = Rat from a variable';
ck dies-as({ my $i = 3; my num $a = $i }),  'X::AdHoc', 'num = Int at run time';
ck dies-as({ my $n = 3.7e0; my int $c = $n }), 'X::AdHoc', 'int = Num at run time';
ck dies-as({ my $b = True; my num $a = $b }), 'X::AdHoc', 'num = Bool (an Int)';
ck dies-as({ my $f = FatRat.new(1, 2); my num $a = $f }), 'X::AdHoc', 'num = FatRat';
ck dies-as({ my $z = 1+2i; my num $a = $z }), 'X::AdHoc', 'num = Complex';
ck dies-as({ my num $y = 0e0; $y = now - now }), 'X::AdHoc', 'num = Duration';
ck dies-as({ my $r = <1/2>; my int $c = $r }), 'X::AdHoc', 'int = RatStr';
ck dies-as({ my int $c = 1/2 + 1/2 }),      'X::AdHoc', 'an integral Rat is still a Rat';

# a refused store leaves the old value
{
    my num $a = 2e0;
    my $r = 1/2;
    try { $a = $r };
    ck $!.^name, 'X::AdHoc', 'refused = throws…';
    ck $a, 2e0, '…and the num keeps what it held';
    my int $c = 5;
    try { $c /= 2 };
    ck $c, 5, 'a refused /= keeps the int';
}

# ---- compound ops and list assignment ----
ck dies-as({ my int $x = 4; $x /= 2 }),     'X::AdHoc', 'int /= makes a Rat';
ck dies-as({ my int $x = 4; $x += 1.5e0 }), 'X::AdHoc', 'int += Num';
ck dies-as({ my int $x = 0; $x += 7/2 }),   'X::AdHoc', 'int += Rat';
ck dies-as({ my int $x = 10; $x = $x / 2 }), 'X::AdHoc', 'int = int / 2';
ck dies-as({ my int $x = 3; $x = $x ** 0.5 }), 'X::AdHoc', 'int = int ** Rat';
ck dies-as({ my $b; my num $x = 0e0; ($x, $b) = (1/2, 2) }), 'X::AdHoc', 'list assignment';

# ---- parameters ----
ck dies-as({ sub f(num $x) { }; f(1/2) }),   'X::AdHoc', 'num parameter given a Rat';
ck dies-as({ sub g(int $x) { }; g(7/2) }),   'X::AdHoc', 'int parameter given a Rat';
ck dies-as({ sub g(int8 $x) { }; g(7/2) }),  'X::AdHoc', 'int8 parameter';
ck dies-as({ sub g(uint8 $x) { }; g(7/2) }), 'X::AdHoc', 'uint8 parameter';
ck dies-as({ sub f(num32 $x) { }; f(1/2) }), 'X::AdHoc', 'num32 parameter';
ck dies-as({ -> int $x { }(7/2) }),          'X::AdHoc', 'pointy block parameter';
ck dies-as({ my $i = 3; sub f(num $x) { }; f($i) }), 'X::AdHoc', 'num parameter given an Int';
ck dies-as({ my $n = 1.5e0; sub g(int $x) { }; g($n) }), 'X::AdHoc', 'int parameter given a Num';
ck dies-as({ sub h(num $x is rw) { $x = 1/2 }; my num $n = 0e0; h($n) }), 'X::AdHoc', 'is rw num written a Rat';
ck dies-as({ sub k(int $x is rw) { $x = 7/2 }; my int $n = 0; k($n) }), 'X::AdHoc', 'is rw int written a Rat';
# …and a multi passes such a candidate over rather than dying in it
{
    multi m(num $x) { 'num' }
    multi m($x)     { 'any' }
    ck m(1/2), 'any', 'a multi falls through to the Any candidate';
}

# ---- what a native DOES take ----
{
    my int $a = True;                          ck $a, 1,  'int takes a Bool';
    my int $b = Less;                          ck $b, -1, 'int takes an Int enum';
    my int $c = <3>;                           ck $c, 3,  'int takes an IntStr';
    my num $d = <1.5e0>;                       ck $d, 1.5e0, 'num takes a NumStr';
    my $i = 3; my int32 $e = $i;               ck $e, 3,  'int32 takes a boxed Int';
    my num $f = 1e0; $f += 1;                  ck $f, 2e0, 'num += Int is a Num';
    my num $g = 1e0; $g *= 1/2;                ck $g, 0.5e0, 'num *= Rat is a Num';
    my num $h = 1e0; $h = 1/3 + $h;            ck $h, 1/3 + 1e0, 'Rat + num is a Num';
    my int $j = 7; $j = floor($j / 2);         ck $j, 3,  'floor of a Rat is an Int';
    sub s(int $x is rw) { $x = 5 }; my int $k = 0; s($k); ck $k, 5, 'is rw int takes an Int';
}

# ---- native ** with a negative int literal (or native) exponent is an Int 0 ----
{
    my int $x = 10;
    ck $x ** -1, 0, 'int ** -1';
    my int $m = -2;
    ck $x ** $m, 0, 'int ** native negative int';
    my $n = -1;
    ck $x ** $n, 0.1, '…a BOXED exponent stays boxed: a Rat';
    my int $y = 10; $y = $y ** -2;             ck $y, 0, 'int = int ** -2';
    my int $z = 10; $z **= -1;                 ck $z, 0, 'int **= -1';
    my $X = 10;
    ck $X ** -1, 0.1, 'a boxed Int ** -1 is a Rat';
}

# ---- a boxed container holding a native's value is not native ----
{
    my int $m = 3;
    my $n = $m; $n = 1/2;                      ck $n, 0.5, 'my $n = $int; $n = Rat';
    my num $f = 1e0;
    my $g = $f; $g = 1;                        ck $g, 1, 'my $g = $num; $g = Int';
    my $h = 0; $h = $m; $h /= 2;               ck $h, 1.5, 'a boxed /= after holding an int';
    my ($p, $q) = $m, $f; $p = 1/2; $q = 1;    ck ($p, $q), (0.5, 1), 'list-assigned copies';
    sub c($x is copy) { $x = 1/2; $x };        ck c($m), 0.5, 'an `is copy` parameter';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
