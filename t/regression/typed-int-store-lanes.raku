# Regression: a `my Int $x` store is as fast as an untyped one, and checked as before.
#
# A loop over `my Int` variables never became a kernel ("a container a plain
# store would not honour"), and `$hi div= $base` declined one whatever the
# types; interpreted, every `my Int` declaration, `=` and `+=` walked the
# scopes for the name's where/smiley/coercion/default tables. Rakudo-checked
# answers for what the new lanes take (an Int from Ints) and for what they hand
# back to the full path (a Rat, a Str, Nil, an overloaded infix).
#
# Expectations checked against Rakudo 2026.09. Green on both. The second half
# is Raku++'s own: the typed loop and `div=` loop run as kernels, with the same
# answers as with RAKUPP_NO_KERNELS=1.
#
# Contract: exit 0 + last line PASS.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub err(&b) { try { b(); 'none' } // $!.^name }

# op= into `my Int`
{ my Int $a = 1; ck err({ $a += 0.5 }), 'X::TypeCheck::Assignment', '+= a Rat dies'; ck $a, 1, '…and keeps the Int' }
{ my Int $a = 1; ck err({ $a -= 1e0 }), 'X::TypeCheck::Assignment', '-= a Num dies'; ck $a, 1, '…and keeps the Int' }
{ my Int $a = 3; $a *= 2**70; ck $a, 3 * 2**70, '*= past a machine word' }
{ my Int $a = 9223372036854775807; $a += 1; ck $a, 9223372036854775808, '+= overflows into a big Int' }
{ my Int $a = 5; $a += True; ck $a, 6, '+= a Bool' }
{ my Int $a = 5; $a += "3"; ck $a, 8, '+= a numeric Str' }
{ my Int $a = 5; $a += IntStr.new(2, "2"); ck ($a, $a.^name), (7, 'Int'), '+= an IntStr' }
{ enum E <x y z>; my Int $a = 5; $a += z; ck ($a, $a.^name), (7, 'Int'), '+= an enum value' }
{ my Int $a = 5; ck err({ $a += Duration.new(2) }), 'X::TypeCheck::Assignment', '+= a Duration dies'; ck $a, 5, '…unchanged' }
{
    class P { has $.v }
    multi infix:<+>(Int $l, P $r) { $l + $r.v }
    multi infix:<*>(Int $l, P $r) { "nope" }
    my Int $a = 5; $a += P.new(v => 4); ck $a, 9, '+= through an overloaded infix';
    my Int $b = 5; ck err({ $b *= P.new(v => 4) }), 'X::TypeCheck::Assignment', 'an overload answering a Str dies';
    ck $b, 5, '…unchanged';
}
# `=` into `my Int`
{ my Int $a = 5; $a = $a + 3; $a = $a div 2; $a = $a * 9223372036854775807; ck $a, 4 * 9223372036854775807, '= of Int ops' }
{ my Int $a = 5; ck err({ $a = $a / 2 }), 'X::TypeCheck::Assignment', '= a Rat dies'; ck $a, 5, '…unchanged' }
{ my Int $a = 5; ck err({ $a = $a ** -1 }), 'X::TypeCheck::Assignment', '= ** -1 dies'; ck $a, 5, '…unchanged' }
{ my Int $a = 5; $a = Nil; ck $a.raku, 'Int', 'Nil resets to Int' }
{ my Int $a = 3; my $b := $a; $a += 1; ck $b, 4, 'a bound alias sees the store' }
# `my Int $x = …` declarations, again each time round
{ my @s; for ^3 { my Int $q = 2 + 3; @s.push: $q }; ck @s, [5, 5, 5], 'a declaration in a loop' }
{ my @s; for ^2 { my Int $r = 7; $r = Nil; @s.push: $r.raku }; ck @s, ['Int', 'Int'], '…whose Nil resets to Int' }
{ my $u = 2.5; ck err({ for ^1 { my Int $v = $u } }), 'X::TypeCheck::Assignment', 'declared from a Rat dies' }
{ my $u = "a"; ck err({ for ^1 { my Int $v = $u } }), 'X::TypeCheck::Assignment', 'declared from a Str dies' }
{ my @s; for ^2 { my Int $w = 4; $w += 1; $w = $w - 10; @s.push: $w }; ck @s, [-5, -5], 'declared, then stored' }
{ sub f(Int $x) { my Int $y = $x; $y += 1; $y }; ck f(41), 42, 'in a routine' }
{ my @s; for ^2 { my Int $z = 3; @s.push: err({ $z = "q" }), $z }; ck @s, ['X::TypeCheck::Assignment', 3, 'X::TypeCheck::Assignment', 3], 'a refused store, twice' }
# div= %= mod= (a kernel's now), negatives and a zero divisor
{ my Int $x = 0; for ^20 { $x += 7; $x div= 2; $x %= 5; $x mod= 4 }; ck $x, 0, 'div= %= mod= in a loop' }
{ my $y = -17; for ^1 { $y div= 5 }; ck $y, -4, 'div= floors' }
{ my $z = -17; for ^1 { $z %= 5 }; ck $z, 3, '%= takes the divisor\'s sign' }
{ my $q = 5; for ^1 { $q div= 0 }; ck $q.WHAT.raku, 'Failure', 'div= 0 stores a Failure'; ck $q.exception.^name, 'X::Numeric::DivideByZero', '…of a zero divisor' }
# the program that found it (a Kaprekar-number search)
{
    sub kaprekar(Int $n, Int :$base = 10) {
        my Int $hi = $n ** 2;
        my Int $lo = 0;
        loop (my Int $s = 1; $hi; $s *= $base) {
            $lo += ($hi % $base) * $s;
            $hi div= $base;
            return $hi, $lo if $lo + $hi == $n and $lo;
        }
        ();
    }
    ck (^10000).grep({ kaprekar($_) }).List, (1, 9, 45, 55, 99, 297, 703, 999, 2223, 2728, 4879, 4950, 5050, 5292, 7272, 7777, 9999), 'Kaprekar numbers below 10000';
}

# Raku++ only: the loops run as kernels, with the interpreter's answers
if $*VM.name ne 'moar' {
    my $prog = q:to/END/;
        my Int $w = 0; for ^200 { $w = $w + 3; $w = $w div 2 }; say $w;
        my Int $t = 0; for ^200 { $t += 7; $t div= 2; $t %= 1000; $t mod= 999 }; say $t;
        my $u = 10**15; for ^40 { $u div= 3 }; say $u;
        END
    my %env = %*ENV;
    %env<RAKUPP_KERNEL_TRACE> = 1;
    %env<RAKUPP_NO_KERNELS>:delete;
    my $with = run($*EXECUTABLE, '-e', $prog, :out, :err, :env(%env));
    my $out-with = $with.out.slurp(:close);
    my $trace = $with.err.slurp(:close);
    %env<RAKUPP_NO_KERNELS> = 1;
    my $without = run($*EXECUTABLE, '-e', $prog, :out, :err, :env(%env));
    my $out-without = $without.out.slurp(:close);
    $without.err.slurp(:close);
    ck $out-with.lines.elems, 3, 'the program ran';
    ck $out-with eq $out-without, True, 'the same answers with RAKUPP_NO_KERNELS=1';
    ck $trace.lines.grep(/'kernel: loop at line ' \d+ ' compiled'/).elems, 3, 'all three loops compiled';
    ck so($trace ~~ /'not entered' | 'declined'/), False, '…and none was turned away';
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
