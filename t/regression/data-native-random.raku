# DATA-PLAN P5: the `random` tag's engine primitives — crypt_random_buf,
# crypt_random, crypt_random_uniform.
#
# The only tag admitted on a missing-capability argument rather than a measured
# speed gap: core Raku has no CSPRNG API (`rand` is not one), and a tag that
# ships `hmac` invites "and where do I get a key".
#
# Randomness is the one family where a test cannot check the answer, only its
# SHAPE — so what is pinned here is: the range is respected, the distribution is
# flat, the bytes differ, and the failure modes are refusals rather than
# something predictable. The flatness check is not decoration: `draw % $upper`
# is the obvious implementation, it is biased whenever $upper does not divide
# the range, and section 3 is built to fail loudly if anyone ever writes it.

use Test;

my &crypt-buf     = &::('rakupp-crypt_random_buf');
my &crypt-int     = &::('rakupp-crypt_random');
my &crypt-uniform = &::('rakupp-crypt_random_uniform');

# ---- 1. shapes and sizes -------------------------------------------------

is crypt-buf(8).^name, 'Buf', 'crypt_random_buf answers a Buf';
is crypt-buf(8).elems, 8, 'of the length asked for';
is crypt-buf(0).elems, 0, 'and zero bytes is a legal ask, not an error';
is crypt-buf(1000).elems, 1000, 'a length past any single OS call still comes back whole';
ok crypt-int() ~~ Int, 'crypt_random answers an Int';
ok 0 <= crypt-int() < 2**32, 'of four bytes by default';
ok 0 <= crypt-int(1) < 256, '$size picks the width';
ok 0 <= crypt-int(8) < 2**64, '…including eight bytes, which does not fit a native int';
ok 0 <= crypt-int(32) < 2**256, '…and thirty-two, which is a big Int and must not overflow';

# A 32-byte draw is very unlikely to fit in 63 bits; if it always does, the
# value is being truncated somewhere rather than built as a big Int.
ok (^20).map({ crypt-int(32) }).grep(* >= 2**63), 'a wide draw really does exceed a native int';

# ---- 2. the range is respected -------------------------------------------

my $out-of-range = 0;
for 1, 2, 3, 7, 10, 256, 257, 1000 -> $u {
    for ^200 {
        my $v = crypt-uniform($u);
        $out-of-range++ unless 0 <= $v < $u;
    }
}
is $out-of-range, 0, 'crypt_random_uniform stays in [0, $upper) over 1,600 draws';
is crypt-uniform(1), 0, 'an upper bound of 1 can only be 0';

# Big bounds are where the reference gives up: `crypt_random_uniform(2**40)`
# hangs there, because it draws four bytes and rejection-samples for a value it
# can never produce. Sizing the draw to the bound is the documented difference.
ok 0 <= crypt-uniform(2**40) < 2**40, 'a bound above 2**32 terminates and is in range';
ok 0 <= crypt-uniform(2**200) < 2**200, '…and one far beyond any machine word';
ok 0 <= crypt-uniform(10, 8) < 10, 'an explicit $size is honoured';

# ---- 3. the distribution is flat -----------------------------------------
#
# 200 is deliberate. A byte holds 256 values, so `draw % 200` would make
# 0..55 twice as likely as 56..199 — a 2:1 split that no sampling noise can
# produce. This is the assertion that says "rejection sampling, not a modulus".

{
    my $n = 12_000;
    my @c = 0 xx 200;
    @c[crypt-uniform(200, 1)]++ for ^$n;
    my $low  = @c[^56].sum   / 56;      # the values a modulus would double
    my $high = @c[56..199].sum / 144;
    ok $high > 0, 'every bucket is reachable';
    my $ratio = $low / $high;
    ok 0.85 < $ratio < 1.15,
       "the low values are not favoured — a modulus would make this 2.0, it is {$ratio.fmt('%.3f')}";
}

# And a plain flatness check over a small bound, wide enough not to be flaky.
{
    my $n = 16_000;
    my @c = 0 xx 8;
    @c[crypt-uniform(8)]++ for ^$n;
    my $expect = $n / 8;
    my $worst = @c.map({ abs($_ - $expect) / $expect }).max;
    ok $worst < 0.15,
       "eight buckets over {$n} draws are within 15% of even (worst {($worst*100).fmt('%.1f')}%)";
}

# ---- 4. it is not returning the same thing twice -------------------------

{
    my %seen;
    %seen{crypt-buf(16).list.join(',')}++ for ^500;
    is %seen.elems, 500, '500 sixteen-byte buffers are 500 distinct buffers';
}
{
    # The weaker but more pointed check: consecutive calls must not agree, and
    # a buffer must not be all one byte.
    my $a = crypt-buf(64);
    my $b = crypt-buf(64);
    isnt $a.list.join(','), $b.list.join(','), 'two calls do not agree';
    ok $a.list.unique > 20, 'and a 64-byte buffer is not one value repeated';
}
# Bytes must be spread over the whole 0..255 range, not just ASCII — a
# /dev/urandom read decoded as text somewhere would show up exactly here.
{
    my $b = crypt-buf(4000);
    ok $b.list.grep(* > 127) > 1500, 'the high half of the byte range is used';
    ok $b.list.grep(* == 0) > 0, 'and NUL is a byte like any other';
}

# ---- 5. what the primitives refuse ---------------------------------------

throws-like { crypt-buf(-1) }, X::AdHoc, message => /'must not be negative'/,
    'a negative length is refused';
throws-like { crypt-buf(100_000_000) }, X::AdHoc, message => /'16 MB'/,
    'and an absurd one, rather than trying to allocate it';
throws-like { crypt-buf() }, X::AdHoc, message => /'exactly one argument'/,
    'crypt_random_buf needs its length';
throws-like { crypt-int(0) }, X::AdHoc, message => /'at least one byte'/,
    'a zero-byte integer is not a thing';
throws-like { crypt-uniform(0) }, X::AdHoc, message => /'must be positive'/,
    'an upper bound of zero has no value below it';
throws-like { crypt-uniform(-5) }, X::AdHoc, message => /'must be positive'/,
    'nor a negative one';
throws-like { crypt-uniform(1000, 1) }, X::AdHoc, message => /'never be accepted'/,
    'a $size too small for the bound is refused — which is the reference\'s hang, said out loud';
throws-like { crypt-buf(8, :nope) }, X::AdHoc, message => /'no such adverb'/,
    'and an adverb none of these takes';

# ---- 6. the backend ------------------------------------------------------

is &::('rakupp-random-backend')(), 'core',
   "random-backend says 'core' — the engine asked the OS, no module did";

# ---- 7. the compiler answering a `use` must not pollute anything ----------

my $exe = $*EXECUTABLE.absolute;
sub compiles(Str $code) { run($exe, '-e', $code, :out, :err).exitcode == 0 }

nok compiles('crypt_random_buf(4)'), 'crypt_random_buf is undeclared without a `use`';
nok compiles('crypt_random()'),      'and crypt_random';
nok compiles('random-backend()'),    'and random-backend';
ok compiles('&::("rakupp-crypt_random")()'),
   'the rakupp- primitive is reachable, which is the adoption mechanism';
ok compiles('{ use Data::Native <random>; crypt_random_buf(4) }'),
   'a `use` inside a block works there';
nok compiles('{ use Data::Native <random>; crypt_random_buf(4) }; crypt_random_buf(4)'),
    'and does not escape the block';
nok compiles('use Data::Native <random>; crc32("a")'),
    'an unclaimed tag contributes no names';
ok compiles('use Data::Native <random>;
             die "not claimed" unless (PROCESS::<%DATA-NATIVE-CLAIMED> // {})<random>;'),
   'the compiler writes the claim registry';

# ---- 8. all five tags at once --------------------------------------------
#
# P1 to P5 are done, so a bare `use Data::Native` now claims every tag the plan
# names. That is worth one assertion of its own: it is the line the synopsis
# promises and the first time it has been true.

ok compiles('use Data::Native;
             my %c = PROCESS::<%DATA-NATIVE-CLAIMED>;
             my @missing = <json csv digest zlib random>.grep({ !%c{$_} });
             die "unclaimed: @missing[]" if @missing;
             to-json([1]); from-csv("a"); md5("a"); crc32("a"); crypt_random(4);'),
   'a bare `use Data::Native` claims all five tags and exports all of them';

done-testing;
