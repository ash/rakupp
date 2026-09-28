# Dispatch and binding, from the 2026-09-28 partial-file sweep (roast
# S06-multi/*, S06-currying/positional.t, S06-signature/named-parameters.t,
# S06-traits/misc.t, S06-operator-overloading/workout.t, S12-coercion,
# S03-binding/arrays.t, S06-multi/subsignature.t, integration/weird-errors.t,
# S06-advanced/{wrap,dispatching}.t).
# Every line answers the same under Rakudo 2026.08.

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
sub dies(&code) { (try { code(); True }) // False ?? False !! True }

# nothing before a `;;` tells two candidates apart: ambiguous
multi a(;; Any $b) { "one" }
multi a(;; Int $a) { "two" }
check('all-;; candidates tie', (try { a(42); 'lived' }) // $!.^name, 'X::Multi::Ambiguous');

# an invocant type capture is in scope for a `where`
my role Foo[\] { }
my class Bar {
    proto method baz($ --> Bool:D) {*}
    multi method baz(::T: $ where Foo[T] --> True) { }
    multi method baz($ --> False) { }
}
check('::T: invocant capture', Bar.baz(Foo[Bar]), True);

# a sub-signature is a constraint: tried before the bare @
multi sub u(@a) { 1 }
multi sub u([]) { 2 }
check('sub-signature before bare @', u([]), 2);

# a positional Pair is a positional: one too many is refused
sub quoted(:$x = 5) { $x }
check('a quoted-key pair is positional', dies({ quoted("x" => 5) }), True);

# .assuming checks a coercion parameter against both its types (Rakudo mixes
# the Failure into the Code it answers; rakupp throws it — neither primes)
my $primed = try sub (Int(Str) $a) { }.assuming(1.1);
check('.assuming refuses what the coercion cannot take',
      $primed.defined && !$primed.can('Failure'), False);

# `is rw` wants a Scalar container
sub wants-rw($x is rw) { $x }
check('is rw refuses an itemized array', dies({ wants-rw($[1, 2]) }), True);
check('…and a composer', dies({ wants-rw([1, 2]) }), True);
my $v = 1;
check('…but takes a variable', wants-rw($v), 1);

# a user `multi method new` none of whose candidates match: Mu.new refuses positionals
class Vector { has @.c; multi method new(*@x where { @x.elems == 3 }) { self.bless(c => @x) } }
check('positionals reaching Mu.new die', dies({ Vector.new(1, 2, 3, 4) }), True);
check('…the matching candidate still builds', Vector.new(1, 2, 3).c.elems, 3);

# an @-name binds only a Positional
check('my @a := 1 is refused', (try { my @r := 1; 'lived' }) // $!.^name, 'X::TypeCheck::Binding');
{
    sub failer { fail }
    my $caught = 'lived';
    { my @f := failer; CATCH { default { $caught = .^name } } }
    check('…and so is a Failure', $caught, 'X::TypeCheck::Binding');
}

# the PROTO's signature bounds a call before any candidate is tried (Rakudo
# refuses literal calls like these at compile time, hence the EVALs)
sub fails-as(Str $code, $type) { (try { EVAL $code; False }) // ($! ~~ $type) }
check('arity outside the proto',
      fails-as('proto sub pa($x) {*}; multi sub pa($a, $b) { 2 }; pa(1, 2)', X::TypeCheck::Argument), True);
check('a proto without parens is ()',
      fails-as('proto sub pb {*}; multi sub pb($x) { 1 }; pb("baz")', X::TypeCheck::Argument), True);
check('a capture sub-signature types the call',
      fails-as('proto sub pc(|c(Int $x)) {*}; multi sub pc(|c($)) { 1 }; pc("foo")', X::TypeCheck::Binding::Parameter), True);
check('…and counts it',
      fails-as('proto sub pd(|c($x)) {*}; multi sub pd(|c($x, $y)) { 1 }; pd(1, 2)', X::AdHoc), True);
{
    proto sub pe(|c($x)) {*}
    multi sub pe(|c(Int $x)) { 'int' }
    multi sub pe(|c($x)) { 'any' }
    check('…and still dispatches what fits', pe(3) ~ pe('s'), 'intany');
}

# a method of B run on a plain A reads a `$!x` that object does not have
class PvA { }
class PvB is PvA { has $!x = 5; our method rd(PvA:) { $!x } }
check('a private attribute of another class', (try { &PvB::rd(PvA.new); 'lived' }) // 'died', 'died');
check('…the class itself reads it', &PvB::rd(PvB.new), 5);

# a method group lists its parents' multis first; a wrapper's callwith
# re-feeds the multi and method chains below it
class CandA { multi method bar(Str $s) { 'A' } }
class CandB is CandA { multi method bar(Int $i) { 'B' } }
check('inherited candidates come first',
      CandB.^find_method('bar', :no_fallback).candidates.map({ .signature.params.grep(*.type === Str).elems }).List,
      (1, 0));
class CwA { method m($x) { "A$x" } }
class CwB is CwA {
    multi method m(Any $x) { "any$x " ~ callsame() }
    multi method m(Int $x) { "int$x " ~ callsame() }
}
CwB.^lookup('m').candidates[1].wrap: -> \s, $x { "w$x " ~ callwith(s, $x + 1) }
check('callwith in a wrapper re-feeds the chain', CwB.m(1), 'w1 int2 any2 A2');

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
