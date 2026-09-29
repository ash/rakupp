# Regression: a store between NATIVES converts, a boxed number is refused.
# `my num $n = $an-int-native` holds its Num; a native num into a native int
# truncates toward zero, saturates, and makes NaN 0. A boxed Int or Num (a
# plain or typed variable, a Num literal beside a native int) is still
# refused with X::AdHoc. A mixed native operation is native (`$int * $num`),
# a literal is native only beside a native of its own kind, and `/` is never
# native. Oracle-checked against Rakudo 2026.09; before this, every store of
# one native kind into the other died.
# Contract: exit 0 + last line PASS.
my @fail;
sub is($got, $want, $label) { @fail.push("$label: got {$got.raku}, want {$want.raku}") unless $got eqv $want }
sub dies(&code, $label) {
    my $died = False;
    try { code(); CATCH { default { $died = $_ ~~ X::AdHoc } } };
    @fail.push("$label: should die with X::AdHoc") unless $died;
}

my int $i = 5; my Int $I = 6; my $u = 7;
{ my num $n = 0e0; $n = $i;     is $n, 5e0, 'native int into num' }
{ my num $n = 0e0; $n = $i + 1; is $n, 6e0, 'native int result into num' }
{ my num $m = $i;               is $m, 5e0, 'declared from a native int' }
dies { my num $n = 0e0; $n = $I }, 'a typed Int variable into num';
dies { my num $n = 0e0; $n = $u }, 'an untyped Int into num';

my num $q = 3.7e0; my num $r = -3.7e0;
{ my int $j = 0; $j = $q; is $j, 3, 'native num into int truncates' }
{ my int $j = 0; $j = $r; is $j, -3, '…toward zero' }
{ my num $big = 1e30; my int $j = 0; $j = $big; is $j, 9223372036854775807, '…and saturates' }
{ my num $nan = NaN; my int $j = 0; $j = $nan; is $j, 0, 'NaN into int is 0' }

# which operations are native
{ my num $p = 2.5e0; my int $t = 0; $t = $p + 1e0; is $t, 3, 'num + Num literal is native' }
{ my num $p = 2.5e0; my int $t = 0; my int $k = 3; $t = $p * $k; is $t, 7, 'num * int is native' }
{ my num $p = 2.5e0; my int $t = 0; $t = -$p; is $t, -2, 'negated native num is native' }
dies { my num $p = 2.5e0; my int $t = 0; $t = $p + 1 }, 'num + Int literal is boxed';
dies { my num $p = 2.5e0; my int $t = 0; $t = $p / 2e0 }, '/ is never native';
dies { my num $p = 2.5e0; my int $t = 0; $t = $p ** 2 }, 'num ** Int literal is boxed';
dies { my int $t = 0; $t = $i / 2 }, 'int / int is a Rat';

# compound assignment is `$n = $n op V`
{ my int $m = 5; $m += $q; is $m, 8, 'int += native num converts' }
{ my int $k = 7; my num $n = 0e0; $n += $k; is $n, 7e0, 'num += native int' }
dies { my int $m = 5; $m += 1.5e0 }, 'int += Num literal is refused';
dies { my int $m = 5; my $b = 1.5e0; $m += $b }, 'int += boxed Num is refused';
dies { my int $m = 5; $m += 0.5 }, 'int += Rat literal is refused';
{ my int $m = 5; try { $m += 0.5 }; is $m, 5, 'a refused compound store keeps the old value' }

say @fail ?? "FAIL: {@fail.join('; ')}" !! 'PASS';
exit @fail ?? 1 !! 0;
