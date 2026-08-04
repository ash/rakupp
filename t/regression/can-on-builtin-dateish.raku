# Regression: a built-in value answers `.can`, 2026-08-04. Found taking
# Date::Calendar::Strftime through its own suite: it gates its %u and %V
# specifiers on `$date.can('day-of-week')` / `.can('week-number')`, and with .can
# answering False for every built-in method it emitted the format specifier
# instead of the value. The sets below were enumerated from Rakudo's own answers.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

my $d = Date.new(2001, 2, 1);
my $t = DateTime.new(:2001year, :2month, :1day);

# the two the module actually gates on
ck(?$d.can('day-of-week'), True, "Date.can('day-of-week')");
ck(?$d.can('week-number'), True, "Date.can('week-number')");

# and they really do work, which was never in doubt — that was the point
ck(($d.day-of-week, $d.week-number), (4, 5), 'the methods themselves answer');

# a spread of the rest
ck(?$d.can('year') && ?$d.can('is-leap-year') && ?$d.can('truncated-to'), True,
   'other Dateish methods');
ck(?$d.can('nonesuch'), False, 'and an unknown name is still False');

# the split between the two types is real: succ/pred are Date's, the time
# accessors are DateTime's
ck((?$d.can('succ'), ?$t.can('succ')),   (True, False), 'succ is Date-only');
ck((?$d.can('hour'), ?$t.can('hour')),   (False, True), 'hour is DateTime-only');
ck((?$d.can('utc'),  ?$t.can('utc')),    (False, True), 'so is utc');

# .can hands back something callable, not merely a truthy flag
{
    my $m = $d.can('day-of-week');
    ck($m.elems >= 1, True, '.can returns a list of methods');
}

say $ok ?? 'PASS' !! 'FAIL';
