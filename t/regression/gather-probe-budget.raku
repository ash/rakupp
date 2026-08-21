# Regression: declaring a `gather` costs a bounded amount, and a gather that
# outgrows its probe still renders and compares by all of its elements.
#
# A gather block is run at construction, collecting up to 64 takes, so that a
# block that turns out to be finite can be handed back as a plain eager Seq.
# 64 takes is a COUNT, though, and what matters is the cost: Math::SpecialFunctions
# parks an infinite Bernoulli-number gather at module scope where each take
# costs more than the one before it, so probing 64 of them made `use
# Math::SpecialFunctions` — and every program that loads Statistics::Distributions
# with it — take 3.4 seconds to compute numbers nothing would ever read. The
# probe is bounded by time as well now, and a probe that runs out of budget
# leaves the gather LAZY, which is what a gather of more than 64 takes already is.
#
# That lazy form was itself half-blind: it rendered and compared by the prefix
# it happened to have collected, so a 100-take gather printed 64 elements and
# `is-deeply` against its own contents failed. Rendering and comparison fill it
# now.
#
# Contract: exit 0 + last line PASS. Passes under both engines, so it doubles as
# an oracle. (Under Rakudo the timing assertion is trivially true — nothing runs
# at declaration there at all.)
my @fail;

# 1. an expensive generator is cheap to DECLARE. This is the shape
#    Math::SpecialFunctions ships, and it used to cost seconds.
{
    my $t0 = now;
    my $bernoulli = gather {
        my @a;
        for 0 ..* -> $m {
            @a = FatRat.new(1, $m + 1),
                 -> $prev { my $j = @a.elems; $j * (@a.shift - $prev) } ... { not @a.elems }
            take $m => @a[*- 1] if @a[*- 1];
        }
    }
    my $spent = now - $t0;
    # The budget is 20ms; a whole second is a hundred times over it and still
    # fifty times under what this cost before, so a loaded machine cannot make
    # this flap.
    @fail.push("declaring cost {$spent.round(0.01)}s") if $spent > 1;
    # …and it still WORKS: the numbers are there when they are asked for
    @fail.push('bernoulli values') unless $bernoulli.head(3).map(*.key).join(',') eq '0,1,2';
}

# 2. a gather bigger than the probe renders and compares by ALL of it, not by
#    the prefix that happened to be collected
{
    my $hundred = (^100).Seq;
    @fail.push('elems')     unless (gather { take $_ for ^100 }).elems == 100;
    @fail.push('Str')       unless (gather { take $_ for ^100 }).Str.words.elems == 100;
    @fail.push('gist')      unless (gather { take $_ for ^100 }).gist.contains(' 99)');
    @fail.push('raku')      unless (gather { take $_ for ^100 }).raku.contains('99).Seq');
    @fail.push('eqv')       unless (gather { take $_ for ^100 }) eqv $hundred;
    @fail.push('interp')    unless "{gather { take $_ for ^100 }}".words.elems == 100;
    # stored in an array it is an ARRAY of all hundred, not a Seq of sixty-four
    my @a = gather { take $_ for ^100 };
    @fail.push('stored elems') unless @a.elems == 100;
    @fail.push('stored eqv')   unless @a eqv [^100];
    @fail.push('sum')          unless (gather { take $_ for ^100 }).sum == 4950;
}

# 3. what a small gather answers is unchanged — the eager path is still the
#    path, and these are the answers roast pins down
{
    @fail.push('small gist')  unless (gather { take 1; take 2 }).gist eq '(1 2)';
    @fail.push('small raku')  unless (gather { take 1; take 2 }).raku eq '(1, 2).Seq';
    @fail.push('small elems') unless (gather { take 1; take 2 }).elems == 2;
    @fail.push('is-lazy')     if     (gather { take 1; take 2 }).is-lazy;
    @fail.push('WHAT')        unless (gather { take 1 }).WHAT.^name eq 'Seq';
    @fail.push('sort')        unless (gather { take 3; take 1; take 2 }).sort.join(',') eq '1,2,3';
    @fail.push('in sub')      unless do { sub f { gather { take $_ for 1..3 } }; f().join(',') } eq '1,2,3';
    # the loop-control forms: `last` inside the block, and a label from outside
    my $res = gather loop { take 42; last };
    @fail.push('last in loop') unless $res eqv (42,).Seq;
    my @l = gather L: for 1..3 -> $x { for 1..2 { take $x; next L } }
    @fail.push('labelled') unless @l eqv [1, 2, 3];
}

# 4. an unbounded gather is still consumable from the front, and asking for its
#    first elements does not try to reach its end
{
    my $inf = gather { my $i = 0; loop { take $i++ } };
    @fail.push('inf head')  unless $inf.head(3).join(',') eq '0,1,2';
    my $inf2 = gather { my $i = 0; loop { take $i++ } };
    @fail.push('inf slice') unless $inf2[^5].join(',') eq '0,1,2,3,4';
    # (`.is-lazy` on an unbounded gather is deliberately NOT asserted: this
    # engine says True and Rakudo says False, which is a divergence of its own
    # and not one this file is about.)
}

die "FAILED: {@fail.join(', ')}" if @fail;
say 'PASS';
