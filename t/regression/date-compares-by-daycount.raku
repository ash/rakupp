# Regression: a Date compares and numifies by its DAYCOUNT, not by whatever
# fields its hash happens to carry.
#
# `Date.today` used to keep the clock reading it was built from (hour, minute,
# second, posix), while `Date.new(y,m,d)` recorded midnight. Comparison ran on
# the stored posix, so two Dates naming the SAME DAY compared unequal whenever
# one came from the clock — `DateTime.now.Date == Date.today` was False. And a
# Date reaching generic numification counted its hash FIELDS, so `+$date` was 3
# or 7 (the field count) rather than the daycount.
#
# Rakudo gives Dateish a Numeric: a Date IS its daycount, a DateTime its
# Instant. Every check below is verified against Rakudo.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $today  = Date.today;
my $viaDT  = DateTime.now.Date;
my $built  = Date.new($today.year, $today.month, $today.day);
my $day    = Date.new(2026, 8, 26);
my $next   = Date.new(2026, 8, 27);

# --- the reported bug: same day, built three ways ---------------------------
check $today == $built,  True, 'Date.today == the same day built by hand';
check $viaDT == $built,  True, 'DateTime.now.Date == the same day built by hand';
check $today == $viaDT,  True, 'Date.today == DateTime.now.Date';
check $today eqv $built, True, 'a clock-built Date is eqv to a hand-built one';

# --- the full comparison surface --------------------------------------------
check $day == Date.new(2026,8,26), True,  '== on equal Dates';
check $day == $next,               False, '== on different Dates';
check $day != $next,               True,  '!= on different Dates';
check $day != Date.new(2026,8,26), False, '!= on equal Dates';
check $day <  $next,               True,  '<  orders by day';
check $next <  $day,               False, '<  the other way';
check $day <= Date.new(2026,8,26), True,  '<= on equal Dates';
check $next >  $day,               True,  '>  orders by day';
check $day >= Date.new(2026,8,26), True,  '>= on equal Dates';
check ($day cmp Date.new(2026,8,26)), Same, 'cmp on equal Dates';
check ($day cmp $next),              Less, 'cmp on an earlier Date';
check ($next cmp $day),              More, 'cmp on a later Date';
check $day before $next,           True,  'before';
check $next after $day,            True,  'after';

# `<=>` yields an Order, like every other numeric comparison — this arm used
# to hand back a bare Int, so `$a <=> $b` was -1 where Rakudo says Less.
check ($day <=> $next),              Less, '<=> is an Order, not an Int';
check ($next <=> $day),              More, '<=> the other way';
check ($day <=> Date.new(2026,8,26)), Same, '<=> on equal Dates';

# a clock-built Date orders against its own neighbours
check $today <  $today.succ, True, 'today is before tomorrow';
check $today >= Date.today,  True, 'today is not before itself';
check $day.succ == $next,    True, '.succ lands on the next day';
check $next.pred == $day,    True, '.pred lands on the previous day';

# --- numification ------------------------------------------------------------
check +$day,          61278,  '+Date is its daycount';
check $day.Numeric,   61278,  'Date.Numeric is its daycount';
check $day.Int,       61278,  'Date.Int is its daycount';
check $day.daycount,  61278,  '.daycount agrees';
check $day == $day.daycount, True, 'a Date equals its own daycount';
check $day.Numeric.^name, 'Int', 'Date.Numeric is an Int';

# --- a Date carries no clock fields, however it was built --------------------
check $today.raku, $built.raku, 'a clock-built Date has the same fields as a hand-built one';
check $day.raku, 'Date.new(2026,8,26)', 'Date.raku is the Rakudo form';
check $today.Str, $built.Str, 'a clock-built Date stringifies as the plain day';

# --- identity: same day means one key, one element --------------------------
check ($today, $built).Set.elems, 1, 'two spellings of one day are one Set element';
check ($today, $built).unique.elems, 1, '...and one unique element';
check $today.WHICH eq $built.WHICH, True, '...with the same WHICH';

# --- DateTime still compares by its instant ---------------------------------
my $z1 = DateTime.new(:2026year, :8month, :26day, :10hour, :timezone(7200));
my $z2 = DateTime.new(:2026year, :8month, :26day, :9hour,  :timezone(0));
check $z1 == $z2, False, 'DateTimes an hour apart are not equal';
check ($z1 cmp $z2), More, 'a later DateTime sorts after';
check $z2 == DateTime.new(:2026year, :8month, :26day, :9hour, :timezone(0)), True,
      'equal DateTimes compare equal';
check $z2 < $z2.later(:1hour), True, 'a DateTime is before an hour later';
check $z2.Numeric.^name, 'Instant', 'DateTime.Numeric is an Instant';

# a DateTime keeps its clock fields — only Date lost them
check $z1.hour, 10, 'DateTime keeps its hour';
check $z1.timezone, 7200, 'DateTime keeps its timezone';
check $z1.Date == Date.new(2026,8,26), True, 'DateTime.Date is that civil day';

if @fail { .say for @fail; say "FAIL" }
else     { say "PASS" }
