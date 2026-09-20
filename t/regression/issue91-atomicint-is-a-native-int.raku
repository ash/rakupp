# Issue #91: `sub f(atomicint $x)` refused every value handed to it —
# "Type check failed in binding to parameter '$x'; expected atomicint but got
# Int (0)" — including the `my atomicint $n = 0` two lines above the call.
#
# `atomicint` IS a native integer type; it differs from `int` only in that the
# ⚛ operators may be used on it. The engine knew that in the places that were
# written for the ⚛ family (declaration, atomic assignment, attribute zeroing)
# and nowhere else: every list that enumerates the native-int family — the bind
# type check, the `~~` and `.does` answers for the type object, a native array's
# element default and its store check, the scalar and container declaration
# defaults, the bind-to-a-native refusal, the `.^find_method` probe — spelled
# out int/int8/…/uint64/byte and left atomicint off. So it fell through to "an
# unknown type name", which is why the bind failed and why `atomicint ~~ Int`
# answered False.
#
# Contract: exit 0 + last line PASS. Every case below is oracle-checked against
# Rakudo 2026.06 — run this file under `raku` too, output is byte-identical.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# the report itself: a declared atomicint, and a plain Int, both bind
sub write-atomic-int(atomicint $x) { "* atomic-int=$x" }
my atomicint $n = 0;
check(write-atomic-int($n), '* atomic-int=0', 'a declared atomicint binds to an atomicint parameter');
check(write-atomic-int(9),  '* atomic-int=9', 'a plain Int binds to an atomicint parameter');

sub doubled(atomicint $x --> atomicint) { $x * 2 }
check(doubled(21), 42, 'atomicint works as a return type too');

sub defaulted(atomicint $x = 11) { $x }
check(defaulted(), 11, 'an atomicint parameter takes its default');

# the type object sits under Int, exactly where `int` sits
check(atomicint.^name, 'atomicint', '.^name');
check((atomicint ~~ Int, atomicint ~~ Numeric, atomicint ~~ Real,
       atomicint ~~ Cool, atomicint ~~ Any), (True, True, True, True, True),
      'atomicint smartmatches the numeric ancestry');
check((atomicint.does(Real), atomicint.does(Numeric)), (True, True),
      'atomicint does Real and Numeric');

# an undeclared-value scalar is a concrete zero, not a type object
my atomicint $u;
check($u, 0, 'an unassigned atomicint scalar is 0');
check($u.WHAT.^name, 'Int', '…and answers as an Int');

# native arrays: element type, zero-filled gaps, and a store check
my atomicint @a;
check(@a.of.^name, 'atomicint', 'my atomicint @a carries its element type');
check(@a.elems, 0, '…and starts empty');
@a[2] = 5;
check(@a.raku, 'array[atomicint].new(0, 0, 5)', '…with gaps read as native zeroes');
my atomicint @b = 1, 2, 3;
check(@b.sum, 6, 'a list initialiser fills a native atomicint array');
my @c := array[atomicint].new(4, 5);
check(@c.raku, 'array[atomicint].new(4, 5)', 'array[atomicint] is a writable parameterization');
{
    # the exception TYPE differs by engine (Rakudo: X::TypeCheck, rakupp:
    # X::TypeCheck::Binding — as it already does for `my int @a`); what both
    # agree on is that a Str does not go into a native int array
    my $threw = False;
    try { @b.push('x'); CATCH { default { $threw = True } } }
    check($threw, True, 'a native atomicint array refuses a Str');
}

# attributes zero the same way
class Counter { has atomicint $.hits; has atomicint $.start = 7; }
my $c = Counter.new;
check(($c.hits, $c.start), (0, 7), 'an atomicint attribute zeroes, and takes its default');

# and the ⚛ family still works on all of it (t/regression/atomic-forms.raku
# covers the operators themselves; this is the declaration path)
my atomicint $m = 0;
$m ⚛= 3;
check(⚛$m, 3, '⚛= stores into a declared atomicint');
check($m⚛++, 3, '⚛++ returns the old value');
check(⚛$m, 4, '…and increments');

if @fail { note $_ for @fail; say 'FAIL'; exit 1 }
say 'PASS';
