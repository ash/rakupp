# Regression: assigning what a call returns reads a Proxy once (#281).
#
# `my $a = $obj.m` where `method m() is rw` returns a Proxy stored the Proxy
# itself, so every later read of $a ran FETCH again. Rakudo FETCHes once, at
# the assignment. With a FETCH that builds a fresh CStruct and fills it, the
# first copy turned into whatever the latest fetch made: Cairo's
# `my $prev = $ctx.matrix; $ctx.rotate(pi/2)` left $prev rotated.
#
# Contract: exit 0 + last line PASS.
use NativeCall;
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
my $n = 0;
class S is repr('CStruct') { has int32 $.v is rw }
class H {
    method count() is rw { Proxy.new: FETCH => { ++$n }, STORE => -> \c, \v { } }
    method snap() is rw {
        Proxy.new: FETCH => { my $s = S.new; $s.v = ++$n; $s }, STORE => -> \c, \v { }
    }
}
sub counted() is rw { Proxy.new: FETCH => { ++$n }, STORE => -> \c, \v { } }
my $h = H.new;

my $a = $h.count;            check $n, 1, 'my $a = method: one FETCH';
my $b = $h.count;            check $n, 2, '…another';
check ($a, $b, $a), (1, 2, 1), 'reads do not FETCH again';
check $n, 2, '…the count stays';
my $c; $c = $h.count;        check $n, 3, '$c = method: one FETCH';
my $d = counted();           check $n, 4, 'my $d = sub';
my $e; $e = counted();       check $n, 5, '$e = sub';
check ($c, $d, $e), (3, 4, 5), 'each holds its own value';

my $s1 = $h.snap;
my $s2 = $h.snap;
check ($s1.v, $s2.v, $s1.v), (6, 7, 6), 'a CStruct made in FETCH is a snapshot';

my $p := counted();
my $f = $p;                  check $f, $n, 'a bound Proxy still reads through';
my $was = $f; $n = 100;
check $f, $was, '…once';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
