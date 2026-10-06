# Regression: the neighbours of `$s ~= $int` stay linear too (issue #130, the
# reporter's second round). Each of these copied the whole string per append:
#   - a Num, Rat or Bool on the right went through `~` and built a new string;
#   - a string that grew by appending stayed inline in its Value however long it
#     got, so every copy of it (a closure's or a method's result, an argument)
#     copied the text — and an append to a body nothing else held copied it too;
#   - an `is rw` parameter compared the caller's whole string with its own copy
#     on every call, and the argument list's copy kept the body shared, so the
#     append inside copied it again;
#   - `$s = $s ~ X`, compiled, and interpreted in a loop no kernel takes.
# Building by prepending, `$k = $s` before each append and repeated `.substr`
# still copy: a flat string has no other way, where Rakudo's strands do.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub linear(&build, $n, $desc) {
    my $t = now; my $len = build($n); my $ms = (now - $t) * 1000;
    ck($ms < 1500, True, "$desc: $n appends in well under 1.5 s");
    $len
}

# the text each right-hand side appends is what `~` gives it
{ my $s = 'a'; $s ~= 1.5e0; $s ~= True; $s ~= 1/3; $s ~= False; $s ~= 2e-5;
  ck($s, 'a1.5True0.333333False2e-05', 'a Num, a Bool, a Rat') }
enum Colour <red green>;
{ my $s = 'c:'; $s ~= green; ck($s, 'c:green', 'an enum value is its key') }
{ my $s = 'x'; $s ~= 1.5e0; $s ~= 'e'; $s ~= "\x[301]"; ck(($s.chars, $s.codes), (5, 5), 'renormalized after a Num') }

# a long string written in place is not written through its copies
{ my $s = 'x' x 40; my $c = $s; $s ~= 1.5e0; ck(($c.chars, $s.chars), (40, 43), 'a copy before a Num append') }
{ my $s = ''; $s ~= 'abcde' for ^10; my @a = $s, $s; $s ~= '!';
  ck((@a[0].chars, @a[1].chars, $s.chars), (50, 50, 51), 'array elements keep their text') }
{ my $s = ''; $s ~= 'abcde' for ^10; my %h = k => $s; $s ~= '!'; ck(%h<k>.chars, 50, 'a hash value keeps its text') }
{ my $s = ''; my &f = { $s ~= 'abcde' }; my $r = f() for ^10; my $r2 = f(); $s ~= '?';
  ck(($r2.chars, $s.chars), (55, 56), "a closure's result keeps its text") }

# an `is rw` parameter writes the caller's variable, and only it
sub add-rw($x is rw, $y) { $x ~= $y }
{ my $s = 'x' x 30; my $k = $s; add-rw($s, 'yz') for ^3;
  ck(($s.chars, $k.chars), (36, 30), 'is rw: the caller grows, its copy does not') }
{ my $s = 'x' x 30; my $r = add-rw($s, 'q'); $s ~= 'w'; ck(($r.chars, $s.chars), (31, 32), 'is rw: the result is a value') }
{ my @a = 'x' x 30; add-rw(@a[0], 'y'); ck(@a[0].chars, 31, 'is rw: an array element') }
class Acc { has Str $.b is rw = ''; method add($x) { $!b ~= $x } }
{ my $o = Acc.new; $o.add($_) for ^12; my $b = $o.b; $o.add('z'); ck(($b, $o.b), ('01234567891011', '01234567891011z'), '$!attr') }

# `$s = $s ~ X` is `$s ~= X` only for a Str in $s
{ my $s = 'a'; $s = $s ~ 'b'; $s = $s ~ 3; my $t = 'xy'; $s = $s ~ $t; $s = $s ~ $s; ck($s, 'ab3xyab3xy', '$s = $s ~ X') }
{ my $v = 5; $v = $v ~ 'z'; ck($v, '5z', 'an Int in $v becomes the Str') }
{ my str $n = 'q'; $n = $n ~ 'r'; ck($n, 'qr', 'a native str') }
{ my $w = 'e'; $w = $w ~ "\x[301]"; ck($w.chars, 1, 'renormalized') }
{ my $k = 'x' x 30; my $c = $k; $k = $k ~ 'q'; ck(($c.chars, $k.chars), (30, 31), 'a copy keeps its text') }
{ my $t = 'x'; my $u = 5; for ^2 { $t = $t ~ $u; $t = $t ~ $t; $t.say if False }; ck($t, 'x5x55x5x55', 'outside a loop kernel, X = $s itself') }

# linear: 250,000 of each in about 0.1 s at most; while they copied, the
# closure and `$!attr` took ~2.5 s, a Num ~17 s (Apple M-series, 2026-10-06)
ck(linear(-> $n { my $s = ''; $s ~= 1.5e0 for ^$n; $s.chars }, 250_000, 'Num'), 750_000, 'Num: the length');
ck(linear(-> $n { my $s = ''; $s ~= True for ^$n; $s.chars }, 250_000, 'Bool'), 1_000_000, 'Bool: the length');
ck(linear(-> $n { my $s = ''; my &f = { $s ~= 'abcde' }; f() for ^$n; $s.chars }, 250_000, 'a closure'),
   1_250_000, 'a closure: the length');
ck(linear(-> $n { my $o = Acc.new; $o.add('abcde') for ^$n; $o.b.chars }, 250_000, '$!attr'),
   1_250_000, '$!attr: the length');
ck(linear(-> $n { my $s = ''; $s = $s ~ 'abcde' for ^$n; $s.chars }, 250_000, '$s = $s ~ X'),
   1_250_000, '$s = $s ~ X: the length');
# …and in a loop no kernel takes (the `.say`): the interpreter's own `=` lane
ck(linear(-> $n { my $s = ''; for ^$n { $s = $s ~ 'abcde'; $s.say if False }; $s.chars }, 250_000,
          '$s = $s ~ X outside a kernel'), 1_250_000, '$s = $s ~ X outside a kernel: the length');
# (an `is rw` parameter is linear interpreted, but compiled it still passes a
# copy in and out, so no time bound here: 100,000 calls take ~0.6 s compiled)
{ my $s = ''; add-rw($s, 'abcde') for ^20_000; ck($s.chars, 100_000, 'is rw: 20,000 appends') }

say $fails ?? "FAILED $fails" !! "PASS";
