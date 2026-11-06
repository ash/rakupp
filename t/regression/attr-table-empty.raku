# OBJECT-SLOTS-PLAN batch 2 (2026-11-06): an object's attributes are an
# AttrTable, whose hash is allocated on the first store, so an object nothing
# is stored in has no table at all. Every walk over an object's attributes —
# .raku, .gist, eqv, clone, a mixin, .Capture, the cycle collector — meets
# that table here, and answers as it did over an empty hash. The answers are
# Rakudo's.

my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

class E { }
class E2 is E { }
class F { has $.a }
role R { has $.r = 4 }

my $e = E.new;
check($e.raku, 'E.new', '.raku');
check($e.gist, 'E.new', '.gist');
check(E.new eqv E.new, True, 'eqv: two empty objects');
check(E.new eqv E2.new, False, 'eqv: another class');
check($e.clone.raku, 'E.new', '.clone');
check(($e but R).r, 4, '`but` a role with an attribute');
my $m = E.new; $m does R;
check($m.r, 4, '`does` a role with an attribute');
check(E.new.Capture.raku, '\()', '.Capture');
check(E.^attributes.elems, 0, '.^attributes');
check(F.new.raku, 'F.new(a => Any)', 'a declared attribute never passed');
check(F.new(a => 1) eqv F.new(a => 1), True, 'eqv: one attribute each');
check(F.new eqv F.new(a => 1), False, 'eqv: one attribute unset');
check((E.new xx 3).elems, 3, 'objects copied');
my $cyc = F.new(a => []); $cyc.a.push($cyc);
check($cyc.a.elems, 1, 'a cycle through an attribute');
check((E.new, E.new).unique(:with(&[eqv])).elems, 1, 'unique by eqv');

if @fail { note $_ for @fail; say "FAIL" } else { say "PASS" }
exit @fail ?? 1 !! 0;
