# Regression: a NativeCall Pointer is a Real whose value is its address, an Int.
# `abs $p` and `$p.Real` came out Num (4096e0); Rakudo's `abs $p` is the Int.
#
# DELIBERATE DIVERGENCE: Rakudo's Pointer has .Numeric and .Int but no .Real,
# so there `$p < $end`, `$p <=> $q`, `$p % 8`, `$p mod 8` and `$p %% 8` die
# (X::Multi::NoMatch, "Cannot resolve caller Real") while `$p + 1`, `$p == $q`
# and `$p cmp $q` all answer. Raku++ orders addresses as C does. Those rows are
# checked only under Raku++; the rest run unchanged under Rakudo.
#
# Contract: exit 0 + last line PASS.
use NativeCall;
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

my $p = Pointer.new(4096);
ck abs($p),               4096, 'abs $p is the Int address';
ck abs(Pointer.new(-5)),  5,    'abs of a negative address is an Int';

if $*VM.name ne 'moar' {
    my $end = Pointer.new(4096 + 64);
    ck $p.Real,           4096,         '$p.Real is the Int address';
    ck $p < $end,         True,         '$p < $end orders addresses';
    ck $end <= $p,        False,        '$end <= $p';
    ck $p <=> $end,       Order::Less,  '$p <=> $end';
    ck $p <=> 4096,       Order::Same,  '$p <=> 4096';
    ck $p % 64,           0,            '$p % 64 tests alignment';
    ck $p mod 7,          1,            '$p mod 7';
    ck $p %% 16,          True,         '$p %% 16';
}

if @fail { .say for @fail; say 'FAIL' }
else     { say 'PASS' }
