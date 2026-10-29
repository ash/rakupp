# Regression: an enum's type object held in a variable or a container is
# called as its name is (issue #216).
#
# `MyEnum(2)` written by name is the coercion to the member CCC, but the same
# type object taken out of a Hash, an Array element or a Scalar and invoked —
# `$r<x>(2)`, `$t(2)` — died "No such method 'CALL-ME' for invocant of type
# 'MyEnum'": only the call by NAME knew the coercion, and invoking a value
# reached it for a class's type object and an enum MEMBER, never for an
# enum's type object. A Scalar holding the type object also stopped it being
# one for printing and type checks: `$t.raku` was the pair list behind it,
# `$[:AAA(0), …]`, `$t ~~ Int` was False, `$t.^mro` was (Any Mu) and
# `my Int $i = $t` died. Every answer below is Rakudo 2026.09's.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

enum MyEnum <AAA BBB CCC DDD>;
enum S (Strict => "Strict", Lax => "Lax");

my Hash $r = %(:x(MyEnum));
ck $r<x>(2), CCC, 'the issue: a type object in a Hash, called';
ck $r<x>.(2), CCC, '…and called with `.( )`';
my %h = x => MyEnum, s => S;
ck (%h<x>(1), %h<s>("Lax")), (BBB, Lax), '…an Int- and a Str-valued enum';
my $t = MyEnum;
ck ($t(0), $t(BBB), $t(3)), (AAA, BBB, DDD), 'in a Scalar: by value, and a member passes through';
my @a; @a[0] = MyEnum;
ck @a[0](3), DDD, 'in an Array element';
sub f($e) { $e(2) }
ck f(MyEnum), CCC, 'a parameter';
sub g(\e) { e.(1) }
ck g(MyEnum), BBB, 'a sigilless parameter';
class C { has $.e = MyEnum }
ck C.new.e.(3), DDD, 'an attribute';
ck { enum Inner <p q r>; Inner }()(2).key, 'r', 'an enum declared in a block, called outside it';

my $f = $t(9);
ck $f.^name, 'Failure', 'no member with that value: a Failure';
$f.so;
ck ($f.exception.^name, $f.exception.type.raku, $f.exception.value), ('X::Enum::NoValue', 'MyEnum', 9),
    '…X::Enum::NoValue naming the type object and the value';
ck $t(Less).exception.message, "No value 'Less' found in enum MyEnum", '…and a member of another enum';
ck (try $t()), Nil, 'called with no argument: dies';

my $c = CCC;
ck $c(3), DDD, 'a MEMBER in a variable still coerces as its type does';

ck ($t.raku, $t.gist, $r<x>.raku, $t.^name), ('MyEnum', '(MyEnum)', 'MyEnum', 'MyEnum'),
    'a type object in a Scalar renders as the type';
ck ($t ~~ MyEnum, $t ~~ Int, $t ~~ Enumeration, $t.defined), (True, True, True, False),
    '…and type-checks as the type';
ck $t.^mro.map(*.^name).List, <MyEnum Int Cool Any Mu>, '…and linearises as the enum';
my Int $i = $t;
ck $i.raku, 'MyEnum', '…and is assignable to its value type';

say 'PASS';
