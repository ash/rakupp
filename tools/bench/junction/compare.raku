# Before/after comparison for the junction collapse, run as ONE program so the
# configurations interleave: every cell is measured at every point in the
# session, which is the discipline PARALLEL-SPEEDUP.md sets out (a batched sweep
# lets the machine's own drift into the ratio). Prints a checksum per case —
# if the two binaries disagree on it, the timing means nothing.
#
#   ./build/rakupp tools/bench/junction/compare.raku
# and run the same file under the other binary / under Rakudo to compare.

my $ROUNDS = 5;

my @cases;
sub case($name, $reps, &body) { @cases.push: %(:$name, :$reps, :&body) }

# --- the collapse, by where the match falls --------------------------------
for (200, 2000) -> $w {
    my $j = any(1 .. $w);
    case "any w=$w  hit first",  ($w == 200 ?? 20000 !! 2000), { 1     ~~ $j }
    # `div`, not `/`: $w/2 is a Rat, and a Rat-against-Int comparison is a
    # slower path than Int-against-Int — it would inflate this row against
    # every other row in the table for a reason that has nothing to do with
    # junctions.
    my $mid = $w div 2;
    case "any w=$w  hit middle", ($w == 200 ?? 2000  !! 200),  { $mid  ~~ $j }
    case "any w=$w  hit last",   ($w == 200 ?? 2000  !! 200),  { $w    ~~ $j }
    case "any w=$w  no hit",     ($w == 200 ?? 2000  !! 200),  { $w+1  ~~ $j }
}
{
    my $w = 2000;
    my $all  = all(1 .. $w);          # fails at the FIRST eigenstate
    my $none = none(1 .. $w);         # hits at the FIRST eigenstate
    my $one  = one(1 .. $w);          # one match, must scan all
    my $one2 = one(5, 5, |(10 .. $w)); # two matches early -> settles at the 2nd
    case "all w=$w fails first",  200,  { 1 ~~ $all };
    case "none w=$w hit first",   2000, { 1 ~~ $none };
    case "one w=$w  single hit",  200,  { 7 ~~ $one };
    case "one w=$w  two hits",    2000, { 5 ~~ $one2 };
}

# --- the shapes real code actually writes (width 2-4) ----------------------
case 'literal 5 ~~ 1|3|5',    200000, { 5 ~~ 1 | 3 | 5 };
case 'literal 1 ~~ 1|3|5',    200000, { 1 ~~ 1 | 3 | 5 };
case 'literal 9 ~~ 1|3|5',    200000, { 9 ~~ 1 | 3 | 5 };
{
    my $j = any(1, 3, 5);
    case 'prebuilt any(1,3,5)', 200000, { 5 ~~ $j };
}
case 'str any 3-wide',        200000, { 'c' ~~ any(<a b c>) };
case 'str any hit first',     200000, { 'a' ~~ any(<a b c>) };

# --- boolification (Value::truthy / boolify) -------------------------------
{
    my @t = True xx 2000;
    my @f = (False xx 2000);
    my $jt = any(@t);      # True at the first
    my $jf = all(@f);      # False at the first
    case 'bool any(2000 True)',  2000, { ?$jt };
    case 'bool all(2000 False)', 2000, { ?$jf };
}

# --- grep/first with a junction matcher (matcherAccepts) -------------------
{
    my @xs = 1 .. 200;
    case 'grep any(2,4,6)', 500, { @xs.grep(any(2,4,6)).elems };
    case 'first any(3,5)',  2000, { @xs.first(any(3,5)) };
}

# --- interleaved measurement ----------------------------------------------
my %best;
for ^$ROUNDS {
    for @cases -> %c {
        my $t0 = now;
        my $sum = 0;
        for ^%c<reps> { $sum = $sum + (%c<body>() ?? 1 !! 0) }
        my $dt = (now - $t0) / %c<reps>;
        %best{%c<name>} = ($dt, $sum) if !%best{%c<name>} || $dt < %best{%c<name>}[0];
    }
}

say "case\tus_per_op\tchecksum";   # TAB-separated: case names contain commas and pipes
for @cases -> %c {
    my ($dt, $sum) = %best{%c<name>};
    say sprintf("%s\t%.4f\t%d", %c<name>, $dt * 1e6, $sum);
}
