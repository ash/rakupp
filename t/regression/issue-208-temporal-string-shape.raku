# Issue #208: `DateTime.new('')` was a DateTime in year 0, where Rakudo throws
# X::Temporal::InvalidFormat, so a missing timestamp read from a file
# (`%h<at> // ''`) became 0000-01-01T00:00:00Z and an age came out at about
# 2026 years. The empty string was not alone: the string constructor refused
# only a string with no `-` after its first character, and read the rest with
# sscanf, so `2020-01-01 `, `2020-1-1`, `999-01-01`, `2020-01-01T10:00` (no
# seconds), `2020-01-01 10:00:00`, a bare `T`, `UTC`, `Z ` and `.5,5` were
# all dates, and Date.new took a full timestamp and kept its day. Every
# string now has to be the shape Rakudo's grammar takes before it is read:
# yyyy-mm-dd, a year of four digits or more, signed or not; for a DateTime,
# optionally Thh:mm:ss, a fraction of 1 to 12 digits after `.` or `,`, and Z
# or ±hh, ±hhmm, ±hh:mm. The exception's `.format` for a DateTime is
# Rakudo's text now, not "an ISO 8601 timestamp" alone.
#
# The coercions go the same way: `''.DateTime`, `''.Date`, `DateTime('')`.
# Every answer below is Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }
sub E(&c) { my $r = try c(); $r.defined ?? ~$r !! $!.^name }
sub EX(&c) { my $e; { c(); CATCH { default { $e = $_ } } }; $e }

# the issue's program
{
    my $d = try DateTime.new('');
    check "the issue's program", ($d.defined ?? "defined: {$d.year}" !! "failed: {$!.^name}"),
          'failed: X::Temporal::InvalidFormat';
}

my $bad = 'X::Temporal::InvalidFormat';
for '', ' ', 'junk', 'not-a-date', 'a-b', '2020', '20-01-01', '999-01-01', '2020-1-1', '2020-01-1',
    '2020-011-01', '2020/01/01', '20200101', ' 2020-01-01', '2020-01-01 ', "2020-01-01\n",
    '2020-01-01x', "20\x[308]20-01-01" -> $s {
    check "Date.new({$s.raku})", E({ Date.new($s) }), $bad;
    check "DateTime.new({$s.raku})", E({ DateTime.new($s) }), $bad;
}
# a time of day is no Date
for '2020-01-01T', '2020-01-01T10:00:00', '2020-01-01T10:00:00Z', '2020-01-01t10:00:00z',
    '2020-01-01T10:00:00+01:00' -> $s {
    check "Date.new({$s.raku})", E({ Date.new($s) }), $bad;
}
# and the time a DateTime may carry has one shape
for '2020-01-01T', '2020-01-01T10', '2020-01-01T10:00', '2020-01-01T1:00:00', '2020-01-01T10:00:0',
    '2020-01-01T10:00:000', '2020-01-01 10:00:00', '2020-01-01T10:00:00.', '2020-01-01T10:00:00.Z',
    '2020-01-01T10:00:00.5,5Z', '2020-01-01T10:00:00.1234567890123Z', '2020-01-01T10:00:00 Z',
    '2020-01-01T10:00:00Z ', '2020-01-01T10:00:00UTC', '2020-01-01T10:00:00Zjunk',
    '2020-01-01T10:00:00Z+01', '2020-01-01T10:00:00+0100Z', '2020-01-01T10:00:00+0',
    '2020-01-01T10:00:00+7', '2020-01-01T10:00:00+01:', '2020-01-01T10:00:00+013',
    '2020-01-01T10:00:00+01:30:00', '2020-01-01T10:00:00x' -> $s {
    check "DateTime.new({$s.raku})", E({ DateTime.new($s) }), $bad;
}

# what IS a Date or a DateTime still reads as one
check 'Date', E({ Date.new('2020-01-02') }), '2020-01-02';
check 'Date with a signed long year', E({ Date.new('+12345-01-01') }), '+12345-01-01';
check 'Date with a negative year', E({ Date.new('-1234-12-24') }), '-1234-12-24';
check 'Date year 0', E({ Date.new('0000-01-01') }), '0000-01-01';
check 'DateTime, date only', E({ DateTime.new('2023-03-04') }), '2023-03-04T00:00:00Z';
check 'DateTime, date only, :timezone', E({ DateTime.new('2023-03-04', :timezone(3600)) }), '2023-03-04T00:00:00+01:00';
check 'DateTime, no offset', E({ DateTime.new('2009-12-31T22:33:44') }), '2009-12-31T22:33:44Z';
check 'DateTime, Z', E({ DateTime.new('2009-12-31T22:33:44Z') }), '2009-12-31T22:33:44Z';
check 'DateTime, t and z', E({ DateTime.new('2015-08-23t02:27:33z') }), '2015-08-23T02:27:33Z';
check 'DateTime, +hh', E({ DateTime.new('2012-12-22T07:02:00+12') }), '2012-12-22T07:02:00+12:00';
check 'DateTime, +hhmm', E({ DateTime.new('2012-12-22T07:02:00+0530') }), '2012-12-22T07:02:00+05:30';
check 'DateTime, -hh:mm', E({ DateTime.new('2012-12-22T07:02:00-12:45') }), '2012-12-22T07:02:00-12:45';
check 'DateTime, a fraction', E({ DateTime.new('2015-12-11T20:41:10.5Z') }), '2015-12-11T20:41:10.500000Z';
check 'DateTime, a comma fraction', DateTime.new('2015-12-11T20:41:10,5Z').second, 10.5;
check 'DateTime, twelve fraction digits', DateTime.new('2015-12-11T20:41:10.123456789012Z').second, 10.123456789012;
check 'DateTime, a long year', E({ DateTime.new('+9992000-01-01T00:00:00') }), '+9992000-01-01T00:00:00Z';
check 'DateTime, a negative year', E({ DateTime.new('-4004-10-23T00:00:00') }), '-4004-10-23T00:00:00Z';
check 'y/m/d as strings', E({ Date.new('2018', '1', '4') }), '2018-01-04';
check 'an IntStr is an epoch', E({ DateTime.new(<1234>) }), '1970-01-01T00:20:34Z';
check 'a well-shaped bad month is out of range', E({ DateTime.new('2012-13-22T07:02:00Z') }), 'X::Temporal::OutOfRange';
check 'offset minutes past 59', E({ DateTime.new('2012-12-22T07:02:00+00:60') }), 'X::OutOfRange';
check 'an offset with :timezone', E({ DateTime.new('2012-12-22T07:02:00+01:00', :timezone(0)) }), 'X::DateTime::TimezoneClash';

# the coercions are the constructor
check "''.Date", E({ ''.Date }), $bad;
check "''.DateTime", E({ ''.DateTime }), $bad;
check "Date('')", E({ Date('') }), $bad;
check "DateTime('')", E({ DateTime('') }), $bad;
check "'2020-01-01 '.Date", E({ '2020-01-01 '.Date }), $bad;
check "'2020-01-01T10:00'.DateTime", E({ '2020-01-01T10:00'.DateTime }), $bad;
check "'2020-01-02'.Date", E({ '2020-01-02'.Date }), '2020-01-02';
check "'2020-01-02T03:04:05Z'.DateTime", E({ '2020-01-02T03:04:05Z'.DateTime }), '2020-01-02T03:04:05Z';

# the exception says what it refused and what it wants
{
    my $e = EX({ DateTime.new('') });
    check 'DateTime .invalid-str', $e.invalid-str, '';
    check 'DateTime .target', $e.target, 'DateTime';
    check 'DateTime .format', $e.format, 'an ISO 8601 timestamp (yyyy-mm-ddThh:mm:ssZ or yyyy-mm-ddThh:mm:ss+01:00)';
    check 'DateTime .message', $e.message,
          "Invalid DateTime string ''; use an ISO 8601 timestamp (yyyy-mm-ddThh:mm:ssZ or yyyy-mm-ddThh:mm:ss+01:00) instead";
    check 'X::Temporal', $e ~~ X::Temporal, True;
}
{
    my $e = EX({ Date.new('2020-1-1') });
    check 'Date .invalid-str', $e.invalid-str, '2020-1-1';
    check 'Date .target', $e.target, 'Date';
    check 'Date .format', $e.format, 'yyyy-mm-dd';
    check 'Date .message', $e.message, "Invalid Date string '2020-1-1'; use yyyy-mm-dd instead";
}

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
