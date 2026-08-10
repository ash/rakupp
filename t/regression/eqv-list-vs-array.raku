# Regression: `eqv` did not distinguish a List from an Array.
#
# `eqv` is type-aware — that is the whole of what separates it from `==` and
# `eq` — so two containers of different types are never eqv, however equal
# their elements. Raku++ represents an Array, a List, a Seq and a Slip all as
# VT::Array, distinguished by the `isList` flag and `s`, and valueEqv compared
# only sizes and elements. So `(1,2) eqv [1,2]` was True where Rakudo says
# False.
#
# Found writing t/regression/ast-cache-publication.raku: a check spelled
# `@got eqv (99, 198, 297, 396)` passed here and failed under Rakudo with the
# VALUES identical — the dangerous shape, agreeing on the numbers and
# disagreeing on the verdict. `is-deeply` is built on eqv, so this decided
# test outcomes.
#
# Fixed by comparing typeName() in valueEqv's VT::Array arm, which is exactly
# the rule: it derives Array/List/Seq/Slip from isList and s, and a
# Junction/Capture/Uni from their own tags.
#
# EVERY CASE BELOW WAS CHECKED AGAINST RAKUDO, and this file is meant to pass
# under both engines — run `raku t/regression/eqv-list-vs-array.raku` as the
# oracle. That is how the bug was caught in the first place.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my @a = 1, 2;
my @b = 1, 2;

# --- different types are NEVER eqv, however equal the elements ---------------
check (@a eqv (1, 2)),            False, 'an Array is not eqv a List';
check ((1, 2) eqv [1, 2]),        False, 'a List is not eqv an Array';
check ((1, 2) eqv (1, 2).Seq),    False, 'a List is not eqv a Seq';
check ((1, 2).Seq eqv [1, 2]),    False, 'a Seq is not eqv an Array';
check ((1, 2).Slip eqv (1, 2)),   False, 'a Slip is not eqv a List';
check (([] ) eqv ()),             False, 'an empty Array is not eqv an empty List';
check ((1..2) eqv (1, 2)),        False, 'a Range is not eqv a List';

# --- same type IS eqv --------------------------------------------------------
check ([1, 2] eqv [1, 2]),        True,  'Array eqv Array';
check (@a eqv @b),                True,  'two @arrays';
check (@a eqv [1, 2]),            True,  'an @array and an Array literal';
check ((1, 2) eqv (1, 2)),        True,  'List eqv List';
check ((1, 2).Seq eqv (1, 2).Seq), True, 'Seq eqv Seq';
check ((1..2) eqv (1..2)),        True,  'Range eqv Range';

# --- and a coercion makes them the same type, so they compare again ----------
check ((1, 2).List eqv (1, 2)),   True,  '.List of a List is still a List';
check ([1, 2].List eqv (1, 2)),   True,  'an Array coerced to List';
check ((1, 2).Array eqv [1, 2]),  True,  'a List coerced to Array';

# --- ITEMISATION is not a type difference ------------------------------------
# The tempting over-fix is to compare `itemized` too. Rakudo does not: these
# are all True there, and a fix that broke them would be wrong in a way the
# rows above would not catch.
check ([1, 2] eqv $[1, 2]),       True,  'an Array and an itemized Array';
check ($[1, 2] eqv $[1, 2]),      True,  'two itemized Arrays';
check ($(1, 2) eqv (1, 2)),       True,  'an itemized List and a List';

# --- nesting: the rule applies all the way down ------------------------------
check ([[1], [2]] eqv [[1], [2]]),   True,  'nested Arrays';
check ([(1, 2),] eqv [[1, 2],]),     False, 'an inner List is not an inner Array';
check ([[1, 2],] eqv [[1, 2],]),     True,  'inner Arrays match';

# --- hashes were never affected, and must stay that way ----------------------
my %h = a => 1;
check ({a => 1} eqv {a => 1}),    True,  'Hash eqv Hash';
check (%h eqv {a => 1}),          True,  'a %hash and a Hash literal';
check ({a => 1} eqv {a => 2}),    False, 'different values still differ';

# --- is-deeply is built on eqv, so it must agree -----------------------------
# (Checked as values rather than via Test, so this file needs no plan.)
check ([1, 2] eqv [1, 2]),        True,  'is-deeply-shaped Array comparison';
check ((^3).map(* + 1).List eqv (1, 2, 3)), True, 'a map result coerced to List';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
