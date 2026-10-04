# Regression: a compound assignment over core operands consults a user
# operator candidate, as the operator itself does. `$t += 200` beside
# `multi infix:<+>(Int $a, Int $b where * > 100)` is that candidate's answer in
# Rakudo; here only an OBJECT operand ever looked, and a plain scalar's
# `+=`/`-=`/`*=`/`~=` took a fast lane straight to the built-in.
#
# Every expectation below was checked against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{
    multi infix:<+>(Int $a, Int $b where * > 100) { "big" }
    my $t = 1; $t += 200;
    ck($t, "big", 'a scalar += reaches the narrower user candidate');
    my $u = 1; $u += 2;
    ck($u, 3, 'and leaves the calls it refuses to the built-in');
    my @a = 1, 2; @a[0] += 500;
    ck(@a[0], "big", 'an array element +=');
    my %h = a => 1; %h<a> += 300;
    ck(%h<a>, "big", 'a hash element +=');
}
{
    multi infix:<~>(Str $a, Str $b where "Z") { "cat" }
    my $s = "a"; $s ~= "Z";
    ck($s, "cat", 'a scalar ~=');
    my $q = "a"; $q ~= "b";
    ck($q, "ab", 'and a refused ~= concatenates');
}
{
    multi infix:<*>(Int $a, Int $b where 0) { "zero" }
    my $m = 5; $m *= 0;
    ck($m, "zero", 'a scalar *=');
    multi infix:<->(Int $a, Int $b where 1) { "minus one" }
    my $d = 5; $d -= 1;
    ck($d, "minus one", 'a scalar -=');
}
my $plain = 1; $plain += 200;
ck($plain, 201, 'outside the blocks += is the built-in');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
