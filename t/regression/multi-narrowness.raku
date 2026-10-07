# Regression: two multi candidates that both match and score the same are
# ranked by TYPE, as Rakudo sorts candidates, whichever is declared first.
# A nominal type scored the same 8 whether it was `Animal` or `Dog`,
# `Numeric` or `Real`, so the earlier declaration kept the call:
# `multi f(Animal)` beat `multi f(Dog)` for a Puppy, `Cool` beat `Numeric`
# for 5, `Positional` beat `List` for [1], `Code` beat `Routine` for a sub.
# Unrelated types (Positional and Iterable for an Array) are ambiguous on
# Rakudo and are not asserted here. Also: `sub f(Allomorph $a)` takes <5>.
# Every expectation is Rakudo's (2026.09).
# Contract: exit 0 + last line PASS.
my @fail;
sub ck(Bool() $ok, $desc) { @fail.push($desc) unless $ok }
sub is-r($got, $want, $desc) { ck(($got // '') eq $want, "$desc: got {$got // '(died)'}, want $want") }

my class Animal { }
my class Dog is Animal { }
my class Puppy is Dog { }
my role R { }
my role R2 does R { }
my class C does R { }
my class D is C { }
my class E does R2 { }

# Each pair is declared in both orders: the narrower must win both times.
multi a1(Animal $) { 'Animal' }; multi a1(Dog $) { 'Dog' }
multi a2(Dog $) { 'Dog' };       multi a2(Animal $) { 'Animal' }
is-r (try a1(Puppy.new)), 'Dog', 'Animal/Dog for a Puppy';
is-r (try a2(Puppy.new)), 'Dog', 'Dog/Animal for a Puppy';

multi r1(R $) { 'R' }; multi r1(C $) { 'C' }
multi r2(C $) { 'C' }; multi r2(R $) { 'R' }
is-r (try r1(D.new)), 'C', 'R/C for a D';
is-r (try r2(D.new)), 'C', 'C/R for a D';

multi q1(R $) { 'R' };   multi q1(R2 $) { 'R2' }
multi q2(R2 $) { 'R2' }; multi q2(R $) { 'R' }
is-r (try q1(E.new)), 'R2', 'R/R2 for an E';
is-r (try q2(E.new)), 'R2', 'R2/R for an E';

multi n1(Numeric $) { 'Numeric' }; multi n1(Real $) { 'Real' }
multi n2(Real $) { 'Real' };       multi n2(Numeric $) { 'Numeric' }
is-r (try n1(0.5)), 'Real', 'Numeric/Real for 0.5';
is-r (try n2(0.5)), 'Real', 'Real/Numeric for 0.5';
is-r (try n1(Int)), 'Real', 'Numeric/Real for the Int type object';

multi c1(Cool $) { 'Cool' };       multi c1(Numeric $) { 'Numeric' }
multi c2(Numeric $) { 'Numeric' }; multi c2(Cool $) { 'Cool' }
is-r (try c1(5)), 'Numeric', 'Cool/Numeric for 5';
is-r (try c2(5)), 'Numeric', 'Numeric/Cool for 5';

multi o1(Real $) { 'Real' };         multi o1(Rational $) { 'Rational' }
multi o2(Rational $) { 'Rational' }; multi o2(Real $) { 'Real' }
is-r (try o1(0.5)), 'Rational', 'Real/Rational for 0.5';
is-r (try o2(0.5)), 'Rational', 'Rational/Real for 0.5';

multi s1(Cool $) { 'Cool' };       multi s1(Stringy $) { 'Stringy' }
multi s2(Stringy $) { 'Stringy' }; multi s2(Cool $) { 'Cool' }
is-r (try s1('x')), 'Stringy', 'Cool/Stringy for "x" (a role outranks Cool)';
is-r (try s2('x')), 'Stringy', 'Stringy/Cool for "x"';

multi p1(Positional $) { 'Positional' }; multi p1(List $) { 'List' }
multi p2(List $) { 'List' };             multi p2(Positional $) { 'Positional' }
is-r (try p1([1])), 'List', 'Positional/List for [1]';
is-r (try p2([1])), 'List', 'List/Positional for [1]';

multi i1(Iterable $) { 'Iterable' }; multi i1(List $) { 'List' }
is-r (try i1([1])), 'List', 'Iterable/List for [1]';

multi m1(Associative $) { 'Associative' }; multi m1(Map $) { 'Map' }
is-r (try m1(%(a => 1))), 'Map', 'Associative/Map for a Hash';

multi k1(Callable $) { 'Callable' }; multi k1(Code $) { 'Code' }
multi k2(Code $) { 'Code' };         multi k2(Routine $) { 'Routine' }
multi k3(Block $) { 'Block' };       multi k3(Routine $) { 'Routine' }
is-r (try k1(sub {})), 'Code', 'Callable/Code for a sub';
is-r (try k2(sub {})), 'Routine', 'Code/Routine for a sub';
is-r (try k3(sub {})), 'Routine', 'Block/Routine for a sub';

multi h1(Associative $) { 'Associative' }; multi h1(QuantHash $) { 'QuantHash' }
multi h2(QuantHash $) { 'QuantHash' };     multi h2(Setty $) { 'Setty' }
is-r (try h1(set(1))), 'QuantHash', 'Associative/QuantHash for a Set';
is-r (try h2(set(1))), 'Setty', 'QuantHash/Setty for a Set';

multi l1(Cool $) { 'Cool' }; multi l1(Str $) { 'Str' }
multi l2(Cool $) { 'Cool' }; multi l2(Int $) { 'Int' }
is-r (try l1(<5>)), 'Str', 'Cool/Str for an IntStr';
is-r (try l2(<5>)), 'Int', 'Cool/Int for an IntStr';

# several positions, methods, `is default`, callsame order, smileys
multi t1(Animal $, Int $) { 'A' }; multi t1(Dog $, Int $) { 'D' }
is-r (try t1(Puppy.new, 5)), 'D', 'narrower in one position, equal in the other';
my class K { multi method m(Animal $) { 'A' }; multi method m(Dog $) { 'D' } }
is-r (try K.new.m(Puppy.new)), 'D', 'a multi method';
multi u1(Animal $) is default { 'A' }; multi u1(Dog $) { 'D' }
multi u2(Dog $) { 'D' };               multi u2(Animal $) is default { 'A' }
is-r (try u1(Puppy.new)), 'D', '`is default` does not beat a narrower candidate';
is-r (try u2(Puppy.new)), 'D', '…declared after it either';
multi w1(Animal $x) { 'A' }; multi w1(Dog $x) { 'D>' ~ callsame }
is-r (try w1(Puppy.new)), 'D>A', 'callsame walks from the narrower down';
multi x1(Animal:D $) { 'A' }; multi x1(Dog:D $) { 'D' }
is-r (try x1(Puppy.new)), 'D', 'with :D on both';
multi y1(Any $) { 'Any' }; multi y1(Animal $) { 'A' }; multi y1(Dog $) { 'D' }
is-r (try (Dog.new, Animal.new, Puppy.new, 1).map({ y1($_) }).join(',')), 'D,A,D,Any',
     'repeated calls through the dispatch cache';
multi z1(Cool $) { 'Cool' }; multi z1(Int $) { 'Int' }; multi z1(Numeric $) { 'Numeric' }
is-r (try (5, 0.5, 'a').map({ z1($_) }).join(',')), 'Int,Numeric,Cool', 'three candidates';

# crossing narrowness stays ambiguous
multi v1(Animal $, Dog $) { 'AD' }; multi v1(Dog $, Animal $) { 'DA' }
ck (try { v1(Puppy.new, Puppy.new); 1 }) === Nil && $! ~~ X::Multi::Ambiguous, 'crossing positions are ambiguous';

# an allomorph binds Allomorph
sub al(Allomorph $a) { 'ok' }
is-r (try al(<5>)), 'ok', 'IntStr binds Allomorph';
is-r (try al(<1.5>)), 'ok', 'RatStr binds Allomorph';
ck !(try { al('5'); 1 }), 'a plain Str is no Allomorph';

if @fail { say "FAIL: $_" for @fail; say 'FAIL' } else { say 'PASS' }
