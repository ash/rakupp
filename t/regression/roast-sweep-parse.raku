# Parse-time rules, from the 2026-09-28 partial-file sweep (roast
# S06-operator-overloading/circumfix.t, S06-advanced/return.t,
# S06-traits/precedence.t, S03-metaops/reverse.t, S04-statements/repeat.t,
# S10-packages/require-and-use.t, S12-class/attributes.t,
# S14-traits/routines.t). Every line answers the same under Rakudo 2026.08.

use MONKEY-SEE-NO-EVAL;
my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eqv $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got.raku}] WANT [{$want.raku}]";
    }
}
sub error-of(Str $code) { (try { EVAL $code; 'lived' }) // $!.^name }

# a circumfix takes two parts; a bare `@` is one of them
check('three parts from a constant', error-of('my constant $x = "@ µ ."; sub circumfix:<<$x>>($) { 42 }'),
      'X::Syntax::AddCategorical::TooManyParts');

# `has T method` is a method with that return type
check('has Int method', EVAL('my class HM1 { has Int method foo() { 42 } }; HM1.foo'), 42);
check('…whose return type is checked', error-of('my class HM2 { has Int method foo() { "hi" } }; HM2.foo'),
      'X::TypeCheck::Return');
check('…and may not be said twice', error-of('my class HM3 { has Str method foo(--> Int) { "hi" } }'),
      'X::Redeclaration');

# a postfix declared looser than a prefix applies to the prefix's result
sub prefix:<'foo1'> (Int $x) { 2 * $x }
sub postfix:<'bar1'> (Int $x) is looser(&prefix:<'foo1'>) { 1 + $x }
check('postfix looser than prefix', 'foo1'3'bar1', 7);

# the reverse metaop refuses the assignments
check('R:= is CannotMeta', error-of('5 R:= my $x'), 'X::Syntax::CannotMeta');
check('R[and]= is CannotMeta', error-of('my $a; $a R[and]= 42'), 'X::Syntax::CannotMeta');

# a placeholder in a while/repeat body takes the condition's value
my $b = 1;
my $tracker;
repeat while $b < 10 { $tracker = $^a; $b++ }
check('placeholder in repeat while', $tracker, True);
my $c = 1;
my @seen;
while $c < 3 { @seen.push: $^x; $c++ }
check('placeholder in while', @seen, [True, True]);

# `use lib ''` names no repository
check('use lib with an empty string', error-of('use lib ""'), 'X::LibEmpty');

# a class-body sub has no `self`
check('$.attr in a class-body sub', error-of('class NoSelfC { has $.b; sub bomb { "a $.b" } }'),
      'X::Syntax::NoSelf');

# `has @.a of int` keeps its native element type
class TypedOf { has @.a of int }
my $to = TypedOf.new;
check('has @.a of int refuses a Str', (try { $to.a = 1, "b"; 'lived' }) // 'died', 'died');

# a trait nothing declares is an error
check('unknown routine trait', error-of('sub yulia is krassivaya { }'), 'X::Comp::Trait::Unknown');

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
