# Regression: `$range.rand` — a Num drawn uniformly from a numeric range.
#
# The generic `.rand` arm numified its invocant and multiplied by a random
# fraction, and a Range numifies to 0 there, so EVERY numeric range answered a
# constant 0. Nothing in roast asks, and the module that leans on it hardest —
# Statistics::Distributions, whose Uniform variates are `($min .. $max).rand` —
# checks counts and types in its own suite, so it stayed green while generating
# a column of zeros.
#
# Contract: exit 0 + last line PASS. Passes under both engines, so it doubles as
# an oracle.
my @fail;

# 1. the value lands inside the range, and is a Num
{
    my @s = (5..6).rand xx 200;
    @fail.push('type')   unless @s.all ~~ Num:D;
    @fail.push('bounds') unless [&&] @s.map({ 5 <= $_ <= 6 });
    # not the constant the broken arm handed back, and not one repeated draw
    @fail.push('constant') unless @s.unique.elems > 100;

    # (compared with operators rather than `~~ $range`, so that what this file
    # asserts about `.rand` does not also depend on range MEMBERSHIP — see
    # t/regression/fractional-range.raku for that)
    sub within(@v, $lo, $hi) { [&&] @v.map({ $lo <= $_ <= $hi }) }
    @fail.push('negative range') unless within((-5 .. -1).rand xx 50, -5, -1);
    @fail.push('fractional')     unless within((1.5 .. 2.5).rand xx 50, 1.5, 2.5);
    @fail.push('Rat endpoints')  unless within((1/2 .. 3/2).rand xx 50, 1/2, 3/2);
    @fail.push('^n form')        unless within((^5).rand xx 50, 0, 5);
    # exclusivity does not move the endpoints: 5^..^6 still draws between 5 and 6
    @fail.push('exclusive')      unless within((5^..^6).rand xx 50, 5, 6);
}

# 2. it is uniform over the range, not bunched at one end. Ten buckets over
#    0..10 with 4,000 draws: each expects 400, and a bucket below 250 or above
#    550 is a distribution problem rather than luck (p is vanishing).
{
    my @bucket = 0 xx 10;
    @bucket[(0..10).rand.Int min 9]++ for ^4000;
    @fail.push("uniformity: {@bucket.join(' ')}") unless @bucket.all ~~ 250..550;
}

# 3. the four refusals, each with Rakudo's own type and first line
{
    sub refusal(&c) { try &c(); $! ?? ($!.^name, $!.message.lines[0]).join(' | ') !! 'NO THROW' }

    my $ENDPOINTS = 'X::Range::Rand::InvalidEndpoints';
    @fail.push('descending')  unless refusal({ (10..1).rand })
        eq "$ENDPOINTS | Impossible to get a random number from range containing no values.";
    @fail.push('equal')       unless refusal({ (1..1).rand })
        eq "$ENDPOINTS | Impossible to generate random numbers for a range where endpoints are equal";
    @fail.push('endless')     unless refusal({ (1..Inf).rand })
        eq "$ENDPOINTS | Impossible to get a random number from an infinite range";
    @fail.push('open bottom') unless refusal({ (-Inf..0).rand })
        eq "$ENDPOINTS | Impossible to get a random number from an infinite range";
    @fail.push('Str range')   unless refusal({ ('a'..'z').rand })
        eq 'X::AdHoc | Can only get a random value on Real values, did you mean .pick?';

    # the descending message carries its hint, naming the endpoints both ways
    my $hint = do { try { (10..1).rand }; $!.message };
    @fail.push('descending hint') unless $hint.contains('(1..10).rand is')
                                      && $hint.contains('meant by (10..1).rand');
}

# 4. the numbers `.rand` answers off a NON-range invocant are untouched
{
    @fail.push('Int.rand')  unless [&&] (10.rand  xx 50).map({ 0 <= $_ < 10  });
    @fail.push('Num.rand')  unless [&&] (2.5.rand xx 50).map({ 0 <= $_ < 2.5 });
    @fail.push('bare rand') unless [&&] (rand     xx 50).map({ 0 <= $_ < 1   });
}

die "FAILED: {@fail.join(', ')}" if @fail;
say 'PASS';
