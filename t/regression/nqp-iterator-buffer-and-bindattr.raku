# Regression: three seams the nqp-level modules under App::Rak lean on.
#
# `.iterator.push-all(my $ib := IterationBuffer.new)` filled nothing — the
# push target had to be an Array (highlighter drains every needle's columns
# this way). An attribute bound with `nqp::bindattr` from a PARAMETER kept the
# parameter's readonly flag, so a later `$!iterator := nqp::null` died
# "Cannot assign to a readonly variable" (String::Utils' Paragraphs). And a
# native `str` array refused a Str with a role mixed in (highlighter's
# `"bar" but Type<words>` needle).
#
# Every expectation was checked against Rakudo.

use nqp;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

(1, 2, 3).iterator.push-all(my $ib := IterationBuffer.new);
ck($ib.elems, 3, 'push-all fills an IterationBuffer');
ck($ib.List, (1, 2, 3), 'with the items');
(4, 5).map(* + 1).iterator.push-all(my $jb := IterationBuffer.new);
ck($jb.List, (5, 6), 'from a lazy Seq too');

my class P does Iterator {
    has $!iterator;
    method new($iterator) {
        my $self := nqp::create(self);
        nqp::bindattr($self, P, '$!iterator', $iterator);
        $self
    }
    method pull-one() {
        return IterationEnd if nqp::isnull($!iterator);
        my $v = $!iterator;
        $!iterator := nqp::null;
        $v
    }
}
ck(Seq.new(P.new(42)).List, (42,), 'a bindattr-bound attribute can be rebound to null');

my role Tag { has $.type }
my str @parts;
@parts.push("plain");
@parts.push("bar" but Tag("words"));
ck(@parts.join(","), "plain,bar", 'a str array takes a Str with a role mixed in');

my class R {
    has $!rf;
    method !set($r) { $!rf := $r; self }
    method new($r) { nqp::create(self)!set($r) }
    method same($n) { nqp::iseq_i($!rf, $n) }
}
ck(R.new(1).same(1), 1, 'an nqp int op reads through a bound attribute');
ck(R.new(1).same(2), 0, '…and still compares');
sub none-here() { Empty }
ck((none-here() =:= Empty), True, 'a routine answering Empty answers THE Empty');
ck(nqp::x("ab", 3), "ababab", 'nqp::x repeats');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
