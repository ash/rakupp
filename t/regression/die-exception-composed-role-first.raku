# Regression: `die` of an Exception whose class names a ROLE before `is Exception`.
#
# `class D does Iterable is Exception` puts the role in the class's first
# parent slot and Exception among the extra parents. dieError walked `parent`
# alone, never met Exception, and so wrapped the object in an X::AdHoc that
# carried it: a CATCH saw X::AdHoc, and `.error` on it was "No such method".
# Text::CSV's own CSV::Diag (`does Iterable does Positional is Exception`) is
# thrown with `die self.error_diag`, so its t/12_acc.t died at line 198 and
# `rakupp install Text::CSV` refused to install it. `.throw` and `fail` were
# right all along; the trait order `is Exception does R` was too.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

role R { }
class Diag does Iterable does Positional is Exception {
    has Int $.error = 1008;
    method message  { "INI - SEP undefined" }
    method iterator { (1008, "x").iterator }
}
class OwnRole  does R is Exception { method message { "own role" } }
class IterOnly does Iterable is Exception {
    method message  { "iterable" }
    method iterator { ().iterator }
}
class PosOnly  does Positional is Exception { method message { "positional" } }
class RoleLast is Exception does Iterable {
    method message  { "role last" }
    method iterator { ().iterator }
}

sub caught(&thrower) {
    thrower();
    CATCH { default { return .^name => .message } }
    "nothing thrown"
}

check caught({ die Diag.new }),     ('Diag' => 'INI - SEP undefined'), 'die: Iterable and Positional, then is Exception';
check caught({ die OwnRole.new }),  ('OwnRole' => 'own role'),         'die: a program role first';
check caught({ die IterOnly.new }), ('IterOnly' => 'iterable'),        'die: Iterable first';
check caught({ die PosOnly.new }),  ('PosOnly' => 'positional'),       'die: Positional first';
check caught({ die RoleLast.new }), ('RoleLast' => 'role last'),       'die: is Exception first (always worked)';
check caught({ Diag.new.throw }),   ('Diag' => 'INI - SEP undefined'), '.throw (always worked)';

# what Text::CSV's test reads off the caught object
{
    my Int $e = 0;
    { die Diag.new; CATCH { default { $e = .error } } }
    check $e, 1008, 'the caught object is the one thrown';
}

# the type object still wants an instance
check caught({ Diag.throw }).key, 'X::Parameter::InvalidConcreteness', 'Diag.throw on the type object';

# .raku of such an exception lists no `message` (it is a method in Rakudo)
check OwnRole.new.raku, 'OwnRole.new', '.raku';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
