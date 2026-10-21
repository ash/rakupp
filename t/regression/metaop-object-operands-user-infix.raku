# Regression: the zip, cross, reverse and reduce metaops over OBJECT operands
# (2026-10-21). `V + V` consulted a user `multi infix:<+>` and numified an
# object through its `.Numeric`, but `Z+`, `X+`, `R+` and `[+]` apply their
# inner operator through applyBinOp, which did neither: they answered the
# built-in over the object taken as 0, so `(V,) Z+ (V,)` was (0) where Rakudo
# says (plus). The hyper `»+»` already found the multi. When a multi has no
# candidate for the operands (`V + 3`), the built-in it falls back to now goes
# through `.Numeric` too, and so does a `callsame` past the last candidate.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

class V { has $.x; method Numeric { $!x } }
multi sub infix:<+>(V $a, V $b) { "plus" }
my ($p, $q) = V.new(x => 1), V.new(x => 2);

ck(($p,) »+» ($q,), ("plus",), '`»+»` finds the multi (it always did)');
ck((($p,) Z+ ($q,)).List, ("plus",), '`Z+` finds the multi');
ck((($p,) X+ ($q,)).List, ("plus",), '`X+` finds the multi');
ck($p R+ $q, "plus", '`R+` finds the multi');
ck(([+] $p, $q), "plus", '`[+]` finds the multi');
ck(([R+] $p, $q), "plus", '`[R+]` finds the multi');
ck(([\+] $p, $q)[1], "plus", '`[\+]` finds the multi');

# no candidate for (V, Int): the built-in answers through V.Numeric
ck(V.new(x => 5) + 3, 8, '`V + 3` is V.Numeric + 3 when a multi exists');
ck(V.new(x => 5) R+ 3, 8, '…and so is `V R+ 3`');
ck((($p, 2) Z+ (3, 4)).List, (4, 6), '…and each step of `Z+`');
ck((($p, 2) Z+ ($q, 4)).List, ("plus", 6), 'a zip takes the multi where it fits');
ck(($p,) »+» (3,), (4,), '…as the hyper does');

# no multi at all: the metaops numify an object as `-` does
class W { has $.x; method Numeric { $!x } }
ck(((W.new(x => 1),) Z+ (W.new(x => 1),)).List, (2,), '`Z+` over objects without a multi');
ck(W.new(x => 2) R- W.new(x => 5), 3, '`R-` over objects without a multi');
ck(([+] W.new(x => 1), W.new(x => 2), W.new(x => 3)), 6, '`[+]` over three objects');
ck(([**] W.new(x => 2), W.new(x => 3)), 8, '`[**]` over objects');
ck(((W.new(x => 10),) Z/ (W.new(x => 4),)).List, (2.5,), '`Z/` over objects');
ck(((W.new(x => 10),) Z!= (W.new(x => 9),)).List, (True,), '`Z!=` over objects');
ck(((W.new(x => 10),) X* (2, 3)).List, (20, 30), '`X*` over an object and Ints');

# a string operator's multi, which met the object's .Str first
multi sub infix:<eq>(V $a, V $b) { "eq!" }
ck((($p,) Zeq ($q,)).List, ("eq!",), '`Zeq` finds the multi');
ck($p Req $q, "eq!", '`Req` finds the multi');

# a candidate that dies is not "no candidate": the error propagates
multi sub infix:<->(V $a, V $b) { die "boom" }
ck((try $p R- $q) // $!.message, "boom", 'a dying candidate under `R-` propagates');

# an ENUM operand takes the same arm
multi sub infix:<+>(Order $a, Order $b) { "ord" }
ck(((Less,) Z+ (More,)).List, ("ord",), '`Z+` over Order values finds the multi');
ck((Less) R+ More, "ord", '`R+` over Order values finds the multi');
ck(([+] Less, More), "ord", '`[+]` over Order values finds the multi');

# a lexical multi is seen from the scope that applies the metaop
{
    multi sub infix:<*>(V $a, V $b) { "times" }
    ck(([*] V.new(x => 2), V.new(x => 3)), "times", '`[*]` sees a multi in its own scope');
}
ck(([*] V.new(x => 2), V.new(x => 3)), 6, '…and not one in a closed scope');

# `callsame` past the last user candidate reaches the built-in through .Numeric
class U { has $.x; method Numeric { $!x } }
multi sub infix:<*>(U $a, U $b) { callsame() + 100 }
ck(U.new(x => 2) * U.new(x => 3), 106, '`callsame` in a multi over objects');
ck(([*] U.new(x => 2), U.new(x => 3)), 106, '…reached from `[*]`');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
