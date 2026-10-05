# Regression: `++` and `--` ARE .succ and .pred, compiled as interpreted. The
# native backend (`--exe`) stepped every value by adding 1, so `$s++` on the
# Str "aa" died ("Cannot convert string to number") where the interpreter and
# Rakudo give "ab", a Bool did not saturate, and a class's own succ was never
# called. The interpreter's rules are one function now (stepValue), which the
# compiled code calls for anything but a plain Int.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

class Ctr { has $.n = 0; method succ { Ctr.new(n => $!n + 10) }; method pred { Ctr.new(n => $!n - 1) } }

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{ my $s = "aa"; $s++; ck($s, 'ab', 'a Str steps by succ') }
{ my $s = "Az"; $s++; ck($s, 'Ba', 'with a carry') }
{ my $s = "a9"; $s++; ck($s, 'b0', 'letters and digits') }
{ my $s = "b"; $s--; ck($s, 'a', 'a Str steps back by pred') }
{ my $s = "Zz"; my $o = $s++; ck(($o, $s), ('Zz', 'AAa'), 'postfix yields the old Str') }
{ my $s = "42"; $s++; ck($s, '43', 'a numeric-looking Str stays a Str') }
{ my $b = True; $b++; ck($b, True, 'True++ stays True') }
{ my $b = True; $b--; ck($b, False, 'True-- is False') }
{ my Bool $b; my $o = $b++; ck(($o, $b), (False, True), 'an undefined Bool: postfix yields False') }
{ my Num $n; $n++; ck($n, 1e0, 'an undefined Num steps from 0e0') }
{ my $u; $u++; ck($u, 1, 'an undefined scalar steps from 0') }
{ my $c = Ctr.new; $c++; $c++; $c--; ck($c.n, 19, 'a class with succ and pred') }
{ my %h; for <a b a c a> { %h{$_}++ }; ck(%h<a>, 3, 'a hash element counts') }
{ my @a = 1, 2; @a[1]++; @a[3]--; ck(@a, [1, 3, Any, -1], 'array elements, and one past the end') }
{ my $x = 9223372036854775807; $x++; ck($x, 9223372036854775808, 'past int64 grows') }
{ my $x = 1; my $y = $x++; my $z = ++$x; ck(($x, $y, $z), (3, 1, 3), 'postfix and prefix values') }

say $fails ?? "FAILED $fails" !! "PASS";
