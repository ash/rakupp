# Regression: a `for` used as an EXPRESSION returned a different type depending
# on how it was spelled.
#
#     my \a = ($_ for ^2);      # was Seq,  Rakudo says List
#     my $b = (for ^5 { $_ });  # was List, Rakudo says List  <- already right
#
# Raku++ disagreeing with ITSELF is what marked this as a bug rather than a
# deliberate laziness choice. The cause was not in the loop at all: the parser
# desugars `(EXPR for LIST)` to `map({ EXPR }, LIST)`, and `map` correctly gives
# a Seq — while the block spelling goes through ForStmt's asExpr path, which
# builds a List. The desugaring now ends in `.List`.
#
# Nothing was lost by making it a List: the desugaring is eager either way
# (`is-lazy` was already False on both spellings and both engines), so the Seq
# tag was only ever a name. Raku++ also caps an infinite source at 10,000
# iterations where Rakudo hangs — a separate, pre-existing difference this does
# not touch.
#
# It surfaced on 2026-08-10 when `eqv` learned to tell a List from a Seq
# (t/regression/eqv-list-vs-array.raku): S04-statements/for.t's two
# "assigning list comprehension to sigilless works" assertions started failing,
# having passed only because eqv ignored the container type.
#
# Checked against Rakudo, and this file is meant to pass under both — run
# `raku t/regression/for-expression-is-a-list.raku` as the oracle.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- the two spellings must agree, and both must be List ---------------------
check ($_ for ^2).WHAT.^name,       'List', 'a statement-modifier for is a List';
check (for ^2 { $_ }).WHAT.^name,   'List', 'a block for is a List';
check ($_ * 2 for 1..3).WHAT.^name, 'List', 'a modifier for with an expression body';

# --- and the values are right, which is what Roast asserts -------------------
check (($_ for ^1) eqv (0,)),       True, 'a one-element comprehension eqv (0,)';
check (($_ for ^2) eqv (0, 1)),     True, 'a two-element comprehension eqv (0, 1)';
check (($_ * 2 for 1..3) eqv (2, 4, 6)), True, 'a doubling comprehension';
check ((for ^3 { $_ }) eqv (0, 1, 2)),   True, 'the block spelling eqv a List too';

# --- neither spelling is lazy, which is why the tag was safe to change -------
check ($_ for ^2).is-lazy,     False, 'a modifier for is eager';
check (for ^2 { $_ }).is-lazy, False, 'a block for is eager';

# --- the source type does not change the answer ------------------------------
my @src = 1, 2;
check ($_ for @src).WHAT.^name,      'List', 'over an Array';
check ($_ for (^2).Seq).WHAT.^name,  'List', 'over a Seq';
check ($_ for 1..2).WHAT.^name,      'List', 'over a Range';

# --- SIDE EFFECTS and topicalisation must be untouched by the retag ----------
{
    my @out;
    (@out.push($_) for ^3);
    check @out.List, (0, 1, 2), 'the body still runs, in order';
}
{
    my $n = 0;
    ($n++ for ^4);
    check $n, 4, 'the body still runs the right number of times';
}
{
    # $_ is restored afterwards
    $_ = 'outer';
    my $ignored = ($_ for ^2);   # bound, not sunk: Rakudo warns about `$_` in sink context
    check $_, 'outer', 'the topic is restored after the loop';
    check $ignored.elems, 2, '…and the loop still produced its values';
}

# --- the contextualiser forms that ride on this ------------------------------
check @($_ * 2 for 1..3).Array, [2, 4, 6], 'an @(...) contextualiser over a modifier for';
check (($_ for ^2),).elems,     1,          'a comprehension nests as one item';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
