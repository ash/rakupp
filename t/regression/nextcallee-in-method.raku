# Regression: the callable `nextcallee` hands out inside a METHOD is the next
# method, called with the invocant first — `my &n = nextcallee; n(self, $x)`.
# rakupp's callable passed every argument to the frame's continuation, which
# binds the invocant itself: the next multi candidate saw `self` as its `$x`
# and the call answered Nil (a multi method), or died with too many
# positionals (a parent's method).
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

class D {
    multi method k(Int $x) { my &n = nextcallee; "D:" ~ n(self, $x) }
    multi method k(Any $x) { "any$x" }
}
ck D.new.k(2), 'D:any2', 'the next multi candidate, called with self';

class E {
    multi method k(Int $x) { my &n = nextcallee; "E:" ~ n(self, $x) }
    multi method k(Any $x) { "any$x:" ~ self.^name }
}
ck E.new.k(3), 'E:any3:E', '…which runs as a method of the invocant';

class B { method k($x) { "B$x" } }
class C is B { method k($x) { my &n = nextcallee; "C:" ~ n(self, $x) } }
ck C.new.k(4), 'C:B4', 'the parent class method, called with self';

class W { method m($x) { "m$x" } }
W.^find_method('m').wrap(method (|c) { my &orig = nextcallee; "w:" ~ orig(self, |c) });
ck W.new.m(5), 'w:m5', 'a method wrapper still passes self through';

sub f($x) { "f$x" }
&f.wrap(-> $y { my &n = nextcallee; "w" ~ n($y) });
ck f(6), 'wf6', 'a sub wrapper is unchanged';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
