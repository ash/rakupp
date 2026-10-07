# Regression guard for the fast paths Graph.diameter's speed work added (#47):
# each answers a common shape early and must answer exactly what the full path
# answers — or what Rakudo answers, where the full path was wrong.
#
#   - an attribute ACCESSOR answered right after the user-method lookup
#     (plainAccessorSlot), before the built-in ladder
#   - `$!x = EXPR` straight into the slot (attrAssignLane)
#   - `.new` of a class with `is required` / `:D` / `where` attributes through
#     the default constructor's shortcut (constructDefault)
#   - user-class and Mu parameters, and `--> Class` / `--> Bool:D` returns,
#     accepted from a per-routine cache
#   - `max`/`min` of two plain Ints
#   - a placeholder block without `@_` in its body binds no `@_`
#
# Answers are rakudo 2026.09's.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Mu $got, Mu $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
sub err(&code) { try { code(); return 'lived' }; $!.^name }
sub msg(&code) { try { code(); return 'lived' }; $!.message }

# accessors
class Base { has $.x = 1; has $.y = 2 }
class Over is Base { method x { 'method' } }
check Over.new.x, 'method', 'a method of the accessor\'s name wins';
check Over.new.y, 2, 'an inherited accessor';
class Del { has @.items handles <elems>; has $.elems-attr = 5 }
check Del.new(items => (1, 2, 3)).elems, 3, '`handles` delegation is not an accessor';
my class MyErr is X::AdHoc { }
check MyErr.new(payload => 'p').message, 'p', 'X::AdHoc subclass keeps its message';
class Gisty { has $.gist = 'attr' }
check Gisty.new.gist, 'attr', 'an accessor named like a Mu method';
check err({ Base.new.x(1) }), 'X::AdHoc', 'an accessor takes no positional';

# attribute assignment
class A {
    has Int $.i; has Int:D $.d = 1; has Mu $.mu = 5; has Cool $.c; has Int $.def is default(7);
    has $.u; has Int $.w where * > 0 = 1;
    method set-i($v) { $!i = $v }; method set-d($v) { $!d = $v }; method nil-mu { $!mu = Nil }
    method set-c($v) { $!c = $v }; method nil-def { $!def = Nil }; method set-u($v) { $!u = $v }
    method set-w($v) { $!w = $v }; method fail-i { $!i = 'x'.Int }
}
my $a = A.new;
$a.set-i(5); check $a.i, 5, 'typed store';
$a.set-i(Nil); check $a.i, Int, 'Nil resets to the type';
check msg({ A.new.set-i('s') }), q:to/M/.chomp, 'type refusal';
    Type check failed in assignment to $!i; expected Int but got Str ("s")
    M
check msg({ A.new.set-d('s') }), q:to/M/.chomp, ':D attribute names the smiley';
    Type check failed in assignment to $!d; expected Int:D but got Str ("s")
    M
check err({ A.new.set-d(Int) }), 'X::TypeCheck::Assignment', ':D refuses a type object';
$a.nil-mu; check $a.mu, Mu, 'Nil into a Mu attribute is Mu';
check err({ A.new.set-c(Any) }), 'X::TypeCheck::Assignment', 'Any is not Cool';
$a.set-c(42); check $a.c, 42, 'an Int is Cool';
$a.nil-def; check $a.def, 7, '`is default` on Nil';
$a.set-u([1, 2]); check $a.u.elems, 2, 'an Array into a $ attribute';
check err({ A.new.set-w(-1) }), 'X::TypeCheck::Assignment', '`where` still holds';
check err({ A.new.fail-i }), 'X::TypeCheck::Assignment', 'a Failure into a typed attribute';

# construction with checks
class R { has Int:D $.r is required; has $.n where * > 0 = 1 }
check R.new(r => 3).r, 3, 'required + :D';
check err({ R.new }), 'X::Attribute::Required', 'required is still required';
check err({ R.new(r => Int) }), 'X::TypeCheck::Assignment', ':D still refuses a type object';
check err({ R.new(r => 1, n => -1) }), 'X::TypeCheck::Assignment', '`where` at construction';

# parameters and returns
class P { }; class Q is P { }; class Z { }
sub takes(P $p) { 'ok' }
sub takes-d(P:D $p) { 'ok' }
sub takes-mu(Mu $m) { $m.^name }
check takes(Q.new), 'ok', 'a subclass binds';
check err({ takes(Z.new) }), 'X::TypeCheck::Binding::Parameter', 'another class does not';
check err({ takes-d(P) }), 'X::Parameter::InvalidConcreteness', ':D parameter';
check takes-mu(Mu), 'Mu', 'Mu takes Mu';
sub gives(--> P) { Q.new }
sub gives-bad(--> P) { Z.new }
sub gives-b(--> Bool:D) { True }
sub gives-b-bad(--> Bool:D) { Bool }
check gives().^name, 'Q', 'a subclass returns';
check err({ gives-bad() }), 'X::TypeCheck::Return', 'another class does not return';
check gives-b(), True, 'Bool:D';
check err({ gives-b-bad() }), 'X::TypeCheck::Return', 'Bool:D refuses the type object';

# max / min
check max(3, 7), 7, 'max';
check min(3, 7), 3, 'min';
check max(1, 2.5), 2.5, 'max mixed';
check max(10**30, 1), 10**30, 'max bignum';

# placeholders
check { $^a + $^b }(1, 2), 3, 'placeholders';
check { $^a; @_.elems }(1, 2, 3), 2, '@_ beside placeholders takes the rest';
check err({ { $^a }(1, 2) }), 'X::AdHoc', 'too many positionals';

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say "PASS";
