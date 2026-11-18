# Regression guard: the default construction of a plain class, planned once.
#
# `Point.new(x => 1, y => 2)` took runAttrDefaults' general walk — the ancestor
# chain, the named-argument table, the BUILD question per level, the store key
# and the checks a typed, defaulted or `@`/`%` attribute needs — about 2,000
# instructions an attribute, and two such constructions were 44% of
# tools/bench/objects-steady.raku. A class whose attributes are all plain `$`
# (no type, trait or `where`; no default, or an Int or Num literal) now has
# that worked out once (PlainFill.cpp), and a construction whose arguments are
# plain values walks the plan. What must stay true: every class or argument
# the plan does not cover is built exactly as before — inherited and shadowed
# attributes, a BUILD or TWEAK, `is required`, typed and `@` attributes, a
# private attribute's name given, `is built`, a role's attributes, a name
# given twice, list, hash, Nil and type-object arguments — and a class that
# changes after its first construction is planned again.

my @got;
sub o(**@a) { @got.push: @a.map(*.gist).join }
class A { has $.x; has $.y = 7; has $!p = 3; has $.z is rw; method p { $!p } }
class B is A { has $.w = 2.5e0; has $!q; method q { $!q } }
class C { has $.x; submethod BUILD(:$x) { $!x = ($x // 0) * 2 } }
class D is C { has $.d }
class E { has $.x; has @.l }
class F { has $.x is required }
class G { has Int $.x }
class H { has $.x; method x { 'own' } }
class I { has $.a; has $!a2 is built; method a2 { $!a2 } }
class J is A { has $.x }                     # shadows A's x
role R { has $.r = 11 }
class K does R { has $.k }
class L { has $.x; submethod TWEAK { $!x //= 99 } }
class M { has $.x; has $.s = 'lit'; has $.n = -4 }
class Empty { }
my $destroyed = 0;
class WithDestroy { has $.x; submethod DESTROY { $destroyed++ } }
for ^2 {
    my $a = A.new(x => 1, z => 's', p => 5, nope => 9);
    o $a.raku, ' ', $a.p, ' ', $a.y.WHAT.raku;
    my $b = B.new(x => 1, w => 3, q => 4, y => Nil);
    o $b.raku, ' ', $b.q.raku, ' ', $b.w.WHAT.raku;
    o B.new.raku;
    o C.new(x => 4).raku;  o D.new(x => 4, d => 5).raku;
    o E.new(x => 1, l => (1, 2)).raku;
    o (try F.new) // "required: {$!.^name}";  o F.new(x => 3).raku;
    o G.new(x => 3).raku;  o (try G.new(x => 's')) // "typed: {$!.^name}";
    o H.new(x => 3).x;  o I.new(a => 1, a2 => 2).a2;
    o J.new(x => 5).raku;  o K.new(k => 1).raku;  o K.new(k => 1, r => 2).raku;
    o L.new.raku;  o M.new.raku;  o M.new(n => 2, s => 3).raku;  o Empty.new.raku;  o WithDestroy.new(x => 1).x;
    o A.new(x => (1, 2)).x.raku;  o A.new(x => [1, 2]).x.raku;  o A.new(x => Nil).raku;  o A.new(x => Int).raku;
    my @arr = 1, 2;  o A.new(x => @arr).x.raku;  my %h = a => 1;  o A.new(x => %h).x.raku;
    o A.new(x => 1, x => 2).x;  o A.new(:x(5), :!z).raku;
    my $v := 42;  o A.new(x => $v).x;
    my $obj = A.new(x => 1);  $obj.z = 5;  o $obj.z;  o A.new(x => $obj).x.x;
    o (try A.new(3)) // "positional: {$!.^name}";
    constant CC = 5;  my $k = A.new(x => CC);  o $k.x;
    o A.new(x => 1e0).x.raku, A.new(x => 'str').x.raku, A.new(x => True).x.raku, A.new(x => 3/4).x.raku, A.new(x => <5>).x.raku;
    my int $nat = 9;  my $n = A.new(x => $nat);  $n.z = 300;  o $n.x, ' ', $n.x.WHAT.raku, ' ', $n.z;
    # each object its own attributes
    my $p1 = A.new(x => 1);  my $p2 = A.new(x => 2);  $p1.z = 'one';  o $p1.z, ' ', $p2.z.raku, ' ', $p1.x + $p2.x;
    # an attribute is writable from inside whatever name the value was read through
    my \ro = 10;  my $w = A.new(z => ro);  $w.z++;  o $w.z;
}
# the class changes: its next construction sees the change
class O { has $.x; has $.y }
o O.new(x => 1, y => 2).raku;
O.^add_method('sum', method { self.x + self.y });
o O.new(x => 1, y => 2).sum;
# many attributes, more than a first chunk of the common sizes
class Wide { has $.a; has $.b; has $.c; has $.d; has $.e; has $.f; has $.g; has $.h; has $.i; has $.j; has $.k; has $.l; has $.m; has $.n; has $.o; has $.p; has $.q; has $.r }
my $wide = Wide.new(a => 1, r => 18, j => 10);
o $wide.a + $wide.r + $wide.j, ' ', $wide.b.raku;

my @want = q:to/WANT-END/.lines;
A.new(x => 1, y => 7, z => "s") 3 Int
B.new(w => 3, x => 1, y => Any, z => Any) Any Int
B.new(w => 2.5e0, x => Any, y => 7, z => Any)
C.new(x => 8)
D.new(d => 5, x => 8)
E.new(x => 1, l => [1, 2])
required: X::Attribute::Required
F.new(x => 3)
G.new(x => 3)
typed: X::TypeCheck::Assignment
own
2
J.new(x => 5, x => 5, y => 7, z => Any)
K.new(k => 1, r => 11)
K.new(k => 1, r => 2)
L.new(x => 99)
M.new(x => Any, s => "lit", n => -4)
M.new(x => Any, s => 3, n => 2)
Empty.new
1
(1, 2)
[1, 2]
A.new(x => Any, y => 7, z => Any)
A.new(x => Int, y => 7, z => Any)
[1, 2]
{:a(1)}
2
A.new(x => 5, y => 7, z => Bool::False)
42
5
1
positional: X::Constructor::Positional
5
1e0"str"Bool::True0.75IntStr.new(5, "5")
9 Int 300
one Any 3
11
A.new(x => 1, y => 7, z => "s") 3 Int
B.new(w => 3, x => 1, y => Any, z => Any) Any Int
B.new(w => 2.5e0, x => Any, y => 7, z => Any)
C.new(x => 8)
D.new(d => 5, x => 8)
E.new(x => 1, l => [1, 2])
required: X::Attribute::Required
F.new(x => 3)
G.new(x => 3)
typed: X::TypeCheck::Assignment
own
2
J.new(x => 5, x => 5, y => 7, z => Any)
K.new(k => 1, r => 11)
K.new(k => 1, r => 2)
L.new(x => 99)
M.new(x => Any, s => "lit", n => -4)
M.new(x => Any, s => 3, n => 2)
Empty.new
1
(1, 2)
[1, 2]
A.new(x => Any, y => 7, z => Any)
A.new(x => Int, y => 7, z => Any)
[1, 2]
{:a(1)}
2
A.new(x => 5, y => 7, z => Bool::False)
42
5
1
positional: X::Constructor::Positional
5
1e0"str"Bool::True0.75IntStr.new(5, "5")
9 Int 300
one Any 3
11
O.new(x => 1, y => 2)
3
29 Any
WANT-END
for ^max(+@got, +@want) -> $i {
    next if (@got[$i] // '(nothing)') eq (@want[$i] // '(nothing)');
    say "FAIL: line {$i + 1} — got {(@got[$i] // '(nothing)').raku}, want {(@want[$i] // '(nothing)').raku}";
    exit 1;
}
say 'PASS';
