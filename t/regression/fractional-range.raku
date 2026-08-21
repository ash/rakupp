# Regression: a range with a fractional endpoint keeps that endpoint.
#
# `1.5 .. 2.5` parks its real endpoints in the value's n/im doubles and leaves
# the integer fields holding their FLOORS. Two consumers read the floors:
# membership, so `2.4 ~~ 0..^2.5` was False and `.grep(1.5..2.5)` over measured
# data silently cut at 2; and `.raku`, which printed `1.5..2.5` as `1..2`. gist
# was right the whole time, which is why nothing noticed.
#
# Contract: exit 0 + last line PASS. Passes under both engines, so it doubles as
# an oracle.
my @fail;

# 1. membership honours the real endpoints, in both directions and with the
#    exclusivity markers still meaning what they say
{
    @fail.push('frac top')      unless 2.4 ~~ 0 ..^ 2.5;
    @fail.push('Rat endpoints') unless 1.4 ~~ 1/2 .. 3/2;
    @fail.push('inside')        unless 2.2 ~~ 1.5 .. 2.5;
    @fail.push('above')         if     2.6 ~~ 1.5 .. 2.5;
    @fail.push('below')         if     1.4 ~~ 1.5 .. 2.5;
    @fail.push('excluded top')  if     2.5 ~~ 1.5 ..^ 2.5;
    @fail.push('excluded low')  if     1.5 ~~ 1.5 ^.. 2.5;
    @fail.push('included top')  unless 2.5 ~~ 1.5 .. 2.5;
    @fail.push('included low')  unless 1.5 ~~ 1.5 .. 2.5;
    # a Rat candidate takes the exact path: it must not be rounded to get there
    @fail.push('exact Rat')     unless 4.999999999999999999999999 ~~ 0 ..^ 5;
    # grep is the same test, over a list
    @fail.push('grep') unless (1.0, 1.6, 2.4, 2.6).grep(1.5 .. 2.5).elems == 2;
}

# 2. the integer ranges the same code path serves are untouched
{
    @fail.push('int inside')   unless 5 ~~ 1 .. 10;
    @fail.push('int excluded') if     5 ~~ 0 ..^ 5;
    @fail.push('int low excl') if     4 ~~ 4 ^.. 6;
    @fail.push('int frac cand')unless 1.4 ~~ 1 .. 2;   # containment, not membership
    @fail.push('Str range')    unless 'c' ~~ 'a' .. 'e';
}

# 3. .raku round-trips the endpoints it was given
{
    @fail.push('raku frac')  unless (1.5 .. 2.5).raku eq '1.5..2.5';
    @fail.push('raku exto')  unless (0 ..^ 2.5).raku eq '0..^2.5';
    @fail.push('raku Rat')   unless (1/2 .. 3/2).raku eq '0.5..1.5';
    # the forms that were already right stay right
    @fail.push('raku ^n')    unless (^5).raku      eq '^5';
    @fail.push('raku int')   unless (1 .. 10).raku eq '1..10';
    @fail.push('raku inf')   unless (1 .. Inf).raku eq '1..Inf';
    @fail.push('raku Str')   unless ('a' .. 'e').raku eq '"a".."e"';
}

die "FAILED: {@fail.join(', ')}" if @fail;
say 'PASS';
