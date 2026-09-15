# A role's VALUE parameter is visible inside a `sub` declared in the role body.
# The bindings were injected per call into a role METHOD's frame, from the
# invocant's class — a plain sub has no invocant to carry them, so it ran with
# the name simply not declared. BitEnum looks every bit name up in
# `sub lookup($str) { EnumBits::{"$prefix$str"} }`, where `$prefix` is the role's
# own named parameter, and each of its five consumers died on the first lookup.
use Test;
plan 4;

enum Bits (BIT_a => 1, BIT_b => 2);

role R[::EnumBits, Str:D :$prefix = ''] {
    has Int $.value is rw = 0;
    sub lookup(Str:D $s) { EnumBits::{"$prefix$s"} // die "Bad value: $s" }
    method set(*@bits) { $!value +|= lookup($_).value for @bits }
    method pfx { $prefix }
    method via-sub($s) { lookup($s).value }
}

class C does R[Bits, :prefix<BIT_>] { }

my $c = C.new;
$c.set('a');
is $c.value, 1,             'a sub in the role body reads the role parameter';
$c.set('b');
is $c.value, 3,             '…on every call';
is C.new.via-sub('b'), 2,   '…and through a method that calls it';
is C.new.pfx, 'BIT_',       'a method still sees the parameter too';
