# `my $ = EXPR` in a hot loop (rakudo#1416). An anonymous `my $` missed
# the fast declaration path a named one takes, and the loop kernel refused
# it: `for ^3_000_000 { my $ = 1 }` took 0.48 s against 0.003 s for
# `my $x = 1`. A ternary on the right, the Raku shape of the issue's
# `nqp::if`, took the full path too: `my $ = $n ?? 1 !! Nil` took 0.54 s
# here and 0.22 s in Rakudo, and now takes 0.21 s. These checks pin what the
# faster paths must still give.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my $n = 42; my $z = 0; my $s = "0"; my $e = "";
for ^2 {
    my $a = $n ?? 1 !! Nil;           check $a, 1, 'a ternary\'s then branch';
    my $b = $z ?? 1 !! Nil;           check $b.raku, 'Any', '…its Nil branch leaves the default';
    my $c = $s ?? "y" !! "n";         check $c, 'y', '"0" is true';
    my $d = $e ?? "y" !! "n";         check $d, 'n', '"" is false';
    my $f = $n > 40 ?? $n * 2 !! $n - 1;  check $f, 84, 'a comparison picks an arithmetic branch';
    my $g = $z < 1 ?? 2.5 !! 3;       check $g.^name, 'Rat', 'a Rat branch stays a Rat';
    my $i = $n ?? 123456789012345678901234567890 !! 3;  check $i, '123456789012345678901234567890', 'a big Int literal branch';
    my $j = $n ?? 1e0 !! 2.5;         check $j.^name, 'Num', 'a Num literal branch';
    my $h = $n == 42 ?? (1, 2) !! 3;  check $h.raku, '$(1, 2)', 'a List branch is itemized';
}

my $calls = 0;
sub f { $calls++; 5 }
for ^4 { my $x = $_ %% 2 ?? 1 !! f() }
check $calls, 2, 'the branch the lane cannot give runs once, and only when picked';
for ^3 { my $ = $_ > 0 ?? f() !! 0 }
check $calls, 4, '…in an anonymous declaration too';

check (for ^3 { my $ = $_ %% 2 ?? "even" !! "odd" }).join(','), 'even,odd,even', 'an anonymous declaration\'s value';
check (my $ = 7), 7, 'my $ = 7 is 7';
my ($, $second) = 1, 2;
check $second, 2, 'my ($, $b) skips the first';

my $t = 0;
for ^1000 { my $ = $_ * 2; $t += $_ }
check $t, 499500, 'a loop with an anonymous declaration (a kernel)';
$t = 0;
for ^1000 { my $ = $n ?? 1 !! 2; my $ = 3; $t += $_ }
check $t, 499500, 'two anonymous declarations in one loop are two variables';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
