# DATA-PLAN P5, the interface cross-check: the `random` primitives answer the
# same SHAPE as `Crypt::Random`, which is the converged interface they stand in
# for.
#
# Values cannot be compared — that is the whole point of the family — so what is
# checked is everything else: the return types, the defaults, the widths, and
# the range. Separate from data-native-random.raku because it `#?requires` that
# module.
#
# The ONE deliberate divergence is here too, and it is the reason this file
# cannot simply run both sides on the same inputs: `crypt_random_uniform` there
# defaults $size to 4 and rejection-samples, so an upper bound above 2**32 never
# terminates. Probed 2026-09-05: `crypt_random_uniform(2**40)` hangs. Ours sizes
# the draw to the bound. The assertion below runs OURS on that input; running
# the reference on it would hang this suite, which is the finding.

#?requires Crypt::Random

use Test;

my &p-buf     = &::('rakupp-crypt_random_buf');
my &p-int     = &::('rakupp-crypt_random');
my &p-uniform = &::('rakupp-crypt_random_uniform');

my (&r-buf, &r-int, &r-uniform);
{
    use Crypt::Random;
    &r-buf     = &crypt_random_buf;
    &r-int     = &crypt_random;
    &r-uniform = &crypt_random_uniform;
}

# ---- the return types ----------------------------------------------------

is p-buf(8).^name, r-buf(8).^name, 'crypt_random_buf answers what the reference answers';
is p-buf(8).elems, r-buf(8).elems, 'and the same number of bytes';
is p-buf(0).elems, r-buf(0).elems, 'including for a length of zero';
ok p-int() ~~ Int && r-int() ~~ Int, 'crypt_random answers an Int on both';

# ---- the defaults --------------------------------------------------------
#
# $size defaults to 4, which is only observable statistically: 40 four-byte
# draws that all fit in three bytes would be a 1-in-2**40 coincidence.

ok (^40).map({ p-int() }).max >= 2**24, 'crypt_random defaults to four bytes';
ok (^40).map({ r-int() }).max >= 2**24, '…as the reference does';
ok (^40).map({ p-int(1) }).max < 256, 'and $size narrows it';
ok (^40).map({ r-int(1) }).max < 256, '…there too';

# ---- the range -----------------------------------------------------------

my ($pbad, $rbad) = 0, 0;
for 1, 2, 7, 10, 255, 256, 257 -> $u {
    for ^100 {
        $pbad++ unless 0 <= p-uniform($u) < $u;
        $rbad++ unless 0 <= r-uniform($u) < $u;
    }
}
is $pbad, 0, 'crypt_random_uniform stays in range over 700 draws';
is $rbad, 0, '…and so does the reference, on the bounds it can do';
is p-uniform(1), r-uniform(1), 'an upper bound of 1 is 0 on both';

# ---- the divergence, written down ----------------------------------------

ok 0 <= p-uniform(2**40) < 2**40,
   'a bound above 2**32 terminates here; the reference hangs on it, which is why this file does not ask it';

done-testing;
