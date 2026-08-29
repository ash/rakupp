# Regression: a `@` PARAMETER binds its argument, and binding never itemises —
# so a List argument stays a List, whose slots are BARE and therefore spread
# under `.flat`. rakupp coerced every bound `@` param to an Array, whose slots
# never spread, and that cost Digest::RIPEMD its constant table: the dist builds
# 80 entries as `[&a,$b,@c,$d]` destructuring followed by `flat @c »xx» 16`, got
# 5, and returned a wrong digest for EVERY input with nothing warning. Shipped
# in v3.21.0; found by the 59-dist battery, which was the only gate that saw it.
#
# The rule is about the SLOT, not the element:
#   * a List argument binds AS a List          — `@c.flat` spreads
#   * an Array argument binds AS that Array    — it does not
#   * `is copy` copies into a fresh Array      — it does not
#   * a reified Seq binds as the List it is    — Rakudo says List, not Seq
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# --- what a bound @ param IS ---------------------------------------------
sub what(@c) { @c.WHAT.^name }
sub whatc(@c is copy) { @c.WHAT.^name }
my @arr = 1, 2, 3;
check(what((1,2,3)),        'List',  'a List argument binds as a List');
check(what(@arr),           'Array', 'an Array argument binds as that Array');
check(what([1,2,3]),        'Array', 'an Array literal too');
check(what((1,2,3).Seq),    'List',  'a reified Seq binds as a List, not a Seq');
check(whatc((1,2,3)),       'Array', '`is copy` copies into a fresh Array');
sub slurp1(*@c) { @c.WHAT.^name }
check(slurp1(1,2,3),        'Array', 'a slurpy collects into an Array');
class C { method m(@c) { @c.WHAT.^name } }
check(C.m((1,2,3)),         'List',  'a method parameter binds the same way');
check((-> @c { @c.WHAT.^name })((1,2,3)), 'List', 'and so does a pointy block');
sub sub_sig([$b, @c]) { @c.WHAT.^name }
check(sub_sig([1, (7,8,9)]), 'List',  'a destructured List slot is a List');
check(sub_sig([1, [7,8,9]]), 'Array', 'a destructured Array slot is an Array');

# --- what that means for .flat -------------------------------------------
sub flatten(@c) { @c.flat.elems }
sub flattenc(@c is copy) { @c.flat.elems }
my @pairs = (1,2), (3,4);
check(flatten(((1,2),(3,4))), '4', "a bound List's slots are bare, so they spread");
check(flatten(@pairs),        '2', "an Array's slots are itemised, so they do not");
check(flattenc(((1,2),(3,4))),'2', '`is copy` itemises on the way in');
sub flatslurp(*@c) { @c.flat.elems }
check(flatslurp((1,2),(3,4)), '4', 'a slurpy does not itemise what it collects');

# --- the RIPEMD shape, and the shape that always worked ------------------
sub ripemd_shape([&a, $b, @c, $d]) { (flat @c »xx» 2).elems }
check(ripemd_shape([{;}, 1, (7,8,9), 2]), '6', 'the destructured hyper-xx that RIPEMD builds its table with');
my @c = 1, 2, 3;
check((flat @c »xx» 4).elems, '3', 'the same shape over an ASSIGNED array is still 3');

# --- binding still shares the caller's buffer ----------------------------
sub pushit(@c) { @c.push(99) }
my @shared = 1, 2;
pushit(@shared);
check(@shared.gist, '[1 2 99]', 'binding an Array still shares the caller buffer');
sub copyit(@c is copy) { @c.push(99); @c.elems }
my @kept = 1, 2;
copyit(@kept);
check(@kept.gist, '[1 2]', '`is copy` does not write back');

# --- a bound List is still a fully working Positional ---------------------
sub probe(@c) { (@c.elems, @c[1], @c.map(*+1).gist, @c.sort.gist, @c.join('-'),
                 @c ~~ Positional, @c ~~ Iterable).join('|') }
check(probe((3,1,2)), '3|1|(4 2 3)|(1 2 3)|3-1-2|True|True', 'a bound List answers the Positional interface');
sub twice(@c) { @c.elems ~ ':' ~ @c.elems }
check(twice((1,2,3).Seq), '3:3', 'a bound reified Seq can be walked more than once');

say @fail ?? @fail.join("\n") ~ "\nFAIL" !! 'PASS';
exit @fail ?? 1 !! 0;
