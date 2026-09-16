# Regression: a destructuring sub-signature after a trait, and destructuring
# the DEFAULT when no argument came.
#
#     method update-local-package-list(@metas is copy [$, *@] = $.package-list)
#
# App::ecogen writes that, and it needed two things this engine did not do. The
# sub-signature check ran BEFORE the trait ladder, so the trait-first spelling
# died at "expected ) (got '['"; and once it parsed, a parameter with both a
# sub-signature and a default bound the inner signature to the empty list
# instead of to the default, leaving every inner name undefined even though the
# parameter had a value to unpack.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# the trait-first spelling parses and destructures
sub after-trait(@a is copy [$x, *@r]) { "$x|{@r.join(',')}" }
check after-trait((1, 2, 3)), '1|2,3', 'a sub-signature after `is copy`';

# …and with a default, the DEFAULT is what gets unpacked
sub with-default(@a is copy [$x, *@r] = (7, 8, 9)) { "$x|{@r.join(',')}" }
check with-default(),            '7|8,9', 'no argument: the default is destructured';
check with-default((1, 2)),      '1|2',   'an argument still wins';

# the plain default case, no trait
sub plain(@a [$x] = (5,)) { $x }
check plain(), 5, 'a sub-signature default with no trait';

# the spelling that always worked must keep working
sub no-trait(@a [$x, *@r]) { "$x|{@r.join(',')}" }
check no-trait((4, 5)), '4|5', 'a sub-signature with no trait at all';

# `is copy` still means a writable copy — the trait is not just parsed and lost
sub copies(@a is copy) { @a.push(9); @a.elems }
my @orig = 1, 2;
check copies(@orig), 3, '`is copy` still copies';
check @orig.elems,   2, '…and the caller\'s array is untouched';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
