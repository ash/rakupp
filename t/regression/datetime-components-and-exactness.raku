# Regression: DateTime's `date =>` constructor argument, and keeping the clock
# EXACT rather than floating.
#   * `DateTime.new(date => Date.new(…), hour => 1)` ignored the date entirely, so
#     every field fell back to its default and the whole Dateish family — .year,
#     .month, .day, .day-of-week, .day-of-year, .earlier, .later — answered for
#     year 0.
#   * fractional seconds parse as a Rat: `:00.43` is 43/100, not a double. That
#     is what lets .day-fraction / .julian-date / .modified-julian-date stay
#     rational instead of trailing float noise.
#   * `.days-in-year`, `.offset-in-minutes`, `.offset-in-hours` and `.posix(:real)`
#     were missing; an unset `.formatter` is the Callable type object, not Any.
#   * a leap second (:60) only exists on the day it was inserted — arithmetic that
#     moves off that day clamps it to :59 instead of rolling over into midnight.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# the `date =>` constructor argument
my $dt = DateTime.new(date => Date.new('2015-12-24'), hour => 1);
check($dt.year,         '2015', 'date-arg-year');
check($dt.month,        '12',   'date-arg-month');
check($dt.day,          '24',   'date-arg-day');
check($dt.hour,         '1',    'date-arg-keeps-the-other-nameds');
check($dt.day-of-month, '24',   'date-arg-day-of-month');
check($dt.day-of-week,  '4',    'date-arg-day-of-week');
check($dt.Str,          '2015-12-24T01:00:00Z', 'date-arg-renders');
check(DateTime.new(date => Date.new('2015-03-24'), hour => 1).day-of-year, '83', 'date-arg-day-of-year');
# the plain constructors are unchanged
check(Date.new('2015-12-31').year,   '2015', 'date-year');
check(Date.new('2015-12-31').month,  '12',   'date-month');
check(Date.new('2015-12-31').day-of-week, '4', 'date-day-of-week');
check(DateTime.new(:2016year, :3month).month, '3', 'named-fields-still-work');

# .days-in-year
check(Date.new('2016-01-02').days-in-year,          '366', 'days-in-a-leap-year');
check(DateTime.new(:year<2100>, :month<2>).days-in-year, '365', 'days-in-a-century-year');
check(Date.new('2015-01-02').days-in-year,          '365', 'days-in-an-ordinary-year');

# exact fractional seconds
my $f = DateTime.new('2021-12-24T12:23:00.43Z');
check($f.second.^name,    'Rat',  'a-fractional-second-is-a-rat');
check($f.second.raku,     '0.43', 'and-keeps-its-exact-value');
check($f.day-fraction.^name, 'Rat', 'so-day-fraction-is-exact');
check($f.day-fraction,    '0.5159772',       'day-fraction');
check($f.julian-date,     '2459573.0159772', 'julian-date');
check($f.modified-julian-date, '59572.5159772', 'modified-julian-date');
check(DateTime.new('2015-12-24T12:23:00Z').day-fraction, '0.515972',   'day-fraction-of-a-whole-second');
check(DateTime.new('2015-12-24T12:23:00Z').julian-date,  '2457381.015972', 'julian-date-of-a-whole-second');

# offsets and posix
check(DateTime.new('2015-12-24T12:23:00+0200').offset-in-minutes, '120', 'offset-in-minutes');
check(DateTime.new('2015-12-24T12:23:00+0200').offset-in-hours,   '2',   'offset-in-hours');
check(DateTime.new('2015-12-24T12:23:00+0200').offset,            '7200', 'offset-is-still-seconds');
check(DateTime.new('2015-12-24T12:23:00Z').posix,            '1450959780',   'posix');
check(DateTime.new('2022-06-21T12:23:00.5Z').posix,          '1655814180',   'posix-truncates-by-default');
check(DateTime.new('2022-06-21T12:23:00.5Z').posix(:real),   '1655814180.5', 'posix-real-keeps-the-fraction');

# the formatter attribute
check(Date.new('2015-12-31').formatter.^name, 'Callable', 'an-unset-formatter-is-the-callable-type');
my $us-format = sub ($self) { sprintf "%02d/%02d/%04d", .month, .day, .year given $self };
check(Date.new('2015-12-31', formatter => $us-format).formatter.^name, 'Sub', 'a-set-formatter-is-a-sub');
check(Date.new('2015-12-31', formatter => $us-format).Str, '12/31/2015', 'and-it-is-used');

# earlier / later
check(Date.new('2015-02-27').earlier(month => 5).earlier(:2days).Str, '2014-09-25', 'date-earlier');
my $d2 = DateTime.new(date => Date.new('2015-02-27'));
check($d2.earlier(month => 1).earlier(:2days).Str, '2015-01-25T00:00:00Z', 'datetime-earlier');
check($d2.later((:1month, :2days)).Str,            '2015-03-29T00:00:00Z', 'datetime-later-with-a-unit-list');
check($d2.later(:1month).Str,                      '2015-03-27T00:00:00Z', 'datetime-later-a-month');
check($d2.later(:3days).Str,                       '2015-03-02T00:00:00Z', 'datetime-later-crossing-a-month');
# a leap second clamps when the day moves
check(DateTime.new('2008-12-31T23:59:60Z').Str, '2008-12-31T23:59:60Z', 'a-leap-second-survives-parsing');
check(DateTime.new('2008-12-31T23:59:60Z').earlier(:1day).Str,
      '2008-12-30T23:59:59Z', 'and-clamps-when-the-day-changes');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
