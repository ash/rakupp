# Regression: `augment class X does Role { }` composes the role.
#
# An augment may name roles as well as declare methods, and a module that adds
# behaviour to a built-in type usually does it that way: Rat::Precise writes one
# `my role Precise { … }` and then `augment class Rat does Precise { }` plus the
# same for FatRat, with EMPTY augment bodies. This engine read only the body's
# methods, so both augments added nothing at all.
#
# The composition here is shallow — the role's own methods, by name. A role that
# brings attributes or composes further roles needs the class-declaration path.
#
# Contract: exit 0 + last line PASS.
use MONKEY-TYPING;
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my role Shouty  { method shout()  { "LOUD:" ~ self }
                  method tag()    { "TAG:" ~ self.^name } }
my role Counted { method twice()  { self + self } }

augment class Int does Shouty { }
check 5.shout, 'LOUD:5', 'a built-in type takes the role';

# The empty-body case is the one that found it, and the multi-role case beside it.
augment class Rat does Shouty does Counted { }
check (0.5).shout, 'LOUD:0.5', 'a second built-in type, first role';
check (0.5).twice, 1.0,        'and its second role';

# A user class augmented the same way.
class Plain { has $.n }
augment class Plain does Shouty { }
check Plain.new(n => 1).tag, 'TAG:Plain', 'a user class takes it too';

# …and the role membership is recorded, not just the methods.
check Plain.new(n => 1) ~~ Shouty, True, 'the augmented class does the role';

# What must not change: a body method still lands, and still wins over the role.
augment class Int does Shouty { method quiet() { "soft:" ~ self } }
check 5.quiet, 'soft:5', 'a method declared in the augment body still lands';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
