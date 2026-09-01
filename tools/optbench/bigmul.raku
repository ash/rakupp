# A running product on a bignum accumulator: `$f *= $_`, the compound-assign
# lane. Past the twentieth step the accumulator no longer fits a machine word,
# and every step from there is a magnitude times one limb — which the runtime
# writes back OVER the accumulator when the box provably owns it alone
# (BigInt::mulLimbInPlace via applyArithInto), instead of copying the magnitude
# in, building a new one, and copying that back.
#
# This kernel exists because nothing else in this directory leaves int64:
# `powmod` tops out at 1e18. The compound-assign lane the codegen emits for `*=`
# therefore had no four-lane agreement check at all, which is what this gate is
# for — `-O` reaches it by a DIFFERENT route than plain `--exe`, and a bignum
# accumulator is the one shape where those routes could disagree.
#
# There is no `-O` win to expect here: the time is inside the runtime's multiply,
# not in the loop around it, so this row reads like `nummath` — near 1.0×. That
# is the point of measuring it.
my $f = 1;
$f *= $_ for 1 .. 10000;
say $f.chars;
say $f % 1000000007;
say $f.Str.substr(0, 24);
