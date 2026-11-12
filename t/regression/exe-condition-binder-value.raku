# --exe: an `if EXPR -> $x { … }` that is a routine's last statement is its
# return value. Compiled, the binder form was emitted as a plain statement and
# the routine answered Nil ("have 3" interpreted); and a SIGILLESS binder
# (`-> \n`) inside a routine was a C++ local the bare term `n` never found, so
# `"have {n}"` read an undefined `n`. The first now compiles with its value,
# the second bundles. Checked both ways by t/exe/parity.raku.
# Contract: exit 0 + last line PASS.

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

sub f($v) { if $v -> $n { "have $n" } else { 'none' } }
check f(3), 'have 3', 'if -> $n is the value of a sub';
check f(0), 'none', '…and so is its else';

sub u($v) { unless $v -> $n { "not $n" } }
check u(0), 'not 0', 'unless -> $n too';

sub g($v) { if $v -> \n { "have {n}" } else { 'none' } }
check g(4), 'have 4', 'a sigilless binder in a sub';

class Stack {
    has @!e;
    method elems { @!e.elems }
    method push-one { @!e.push(1); self }
    method report { if self.elems -> \elems { "have {elems}" } else { 'empty' } }
}
check Stack.new.report, 'empty', 'a sigilless binder in a method, false';
check Stack.new.push-one.report, 'have 1', '…and true';

my $r = '';
if 5 -> \v { $r = v * 2 }
check $r, 10, 'a sigilless binder at the top level';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
