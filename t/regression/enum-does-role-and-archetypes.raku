# `enum E does R <a b>` parses, a role before the value list as after it, as in
# Rakudo (rakudo#3376): it was "Missing required term after infix", and
# `enum G does R2 (p => 1)` read R2 as a coercion of the pair list. And an
# enum's metaobject says it is composalizable, its archetypes 1 or 0 as
# Rakudo's native ints are.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

enum Norse-gods <Thor Odin Freija>;
check Norse-gods.HOW.archetypes.composalizable, '1', 'an enum is composalizable (rakudo#3376)';
enum All-gods does Norse-gods <Ammon>;
check Ammon.key, 'Ammon', 'a role before the word list';
role R2 { }
enum G does R2 (p => 1);
check p.value, '1', 'a role before a pair list';
enum H <x y> does role { method z { 1 } };
check y.key, 'y', 'a role after the values, as before';

my $a = Norse-gods.HOW.archetypes;
check <nominal augmentable inheritable composable parametric>.map({ $a."$_"() }).join(','), '1,1,0,0,0',
    'an enum\'s other archetypes';
role R { }
check R.HOW.archetypes.composable, '1', 'a role is composable';
class C { }
check C.HOW.archetypes.inheritable, '1', 'a class is inheritable';
check C.HOW.archetypes.composalizable, '0', '…and not composalizable';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
