# Regression: `.round($scale)` answers in the SCALE's type, exactly.
#
# Rakudo's last step is `(self / $scale + 1/2).floor * $scale`, and the type of
# that multiply is the type of the answer: an Int scale gives an Int, a Rat
# scale gives an exact Rat, and only a Num scale gives a Num. This engine did
# the whole calculation in doubles for a Num invocant and handed back a Num, so
# `178.14159e0.round(0.1)` came out as 178.10000000000002 where Rakudo says
# 178.1 — which is what a program formatting a measurement prints.
#
# Contract: exit 0 + last line PASS. Passes under both engines, so it doubles as
# an oracle.
my @fail;

# 1. the case that started it: a Num rounded by a Rat is an exact Rat
{
    @fail.push('Num by Rat value') unless 178.14159e0.round(0.1) == 178.1;
    @fail.push('Num by Rat type')  unless 178.14159e0.round(0.1) ~~ Rat:D;
    @fail.push('Num by Rat str')   unless 178.14159e0.round(0.1).Str eq '178.1';
    # and the same for a negative one, which floors the other way
    @fail.push('negative')         unless (-178.14159e0).round(0.1).Str eq '-178.1';
}

# 2. the type table, over every combination of invocant and scale. The rule is
#    the scale's: Int scale => Int, Rat scale => Rat, Num scale => Num.
{
    my @invocants = 178.14159e0, 178.14159, 1000/7, 178, -2.5e0, -2.5, 0e0;
    for @invocants -> $x {
        @fail.push("Int scale on {$x.WHAT.^name}") unless $x.round(5)    ~~ Int:D;
        @fail.push("Rat scale on {$x.WHAT.^name}") unless $x.round(0.1)  ~~ Rat:D;
        @fail.push("Num scale on {$x.WHAT.^name}") unless $x.round(0.1e0) ~~ Num:D;
        # no argument at all is an Int, whatever the invocant was
        @fail.push("no arg on {$x.WHAT.^name}")    unless $x.round       ~~ Int:D;
    }
}

# 3. exactness, where a double would show its seams
{
    @fail.push('23.01 scale') unless 1000.round(23.01).Str eq '989.43';
    @fail.push('Num by 23.01') unless 178.14159e0.round(23.01).Str eq '184.08';
    @fail.push('0.001 scale')  unless 178.14159e0.round(0.001).Str eq '178.142';
    # a Rat scale that is not a decimal fraction stays a Rat, and stays exact
    @fail.push('third scale')  unless (-2.5e0).round(1/3) == -7/3;
    # a negative scale rounds the same way it does under Rakudo
    @fail.push('negative scale') unless 178.4e0.round(-0.1).Str eq '178.4';
}

# 4. halves round toward +Inf, as they always did — the arm this touches is the
#    same one that decides that
{
    @fail.push('half up')       unless 2.5e0.round     == 3;
    @fail.push('negative half') unless (-2.5e0).round  == -2;
    @fail.push('half by scale') unless 0.25e0.round(0.1).Str eq '0.3';
}

# 5. what must NOT have changed: the non-finite invocants stay Nums, a Rat
#    invocant with a Rat scale keeps the exact path it already had, and the
#    string forms of both still work
{
    @fail.push('Inf') unless Inf.round(0.1) === Inf;
    @fail.push('NaN') unless NaN.round(0.1).isNaN;
    @fail.push('Rat by Rat') unless (1000/7).round(0.01).Str eq '142.86';
    @fail.push('Int by Int') unless 178.round(5) == 180 && 178.round(5) ~~ Int:D;
    @fail.push('allomorph')  unless "3.7".round(0.1) == 3.7;
}

die "FAILED: {@fail.join(', ')}" if @fail;
say 'PASS';
