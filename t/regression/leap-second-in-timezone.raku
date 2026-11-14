# A leap second stays one when moved to a timezone that is not a whole
# number of minutes, as in Rakudo (rakudo#2097): `in-timezone(514)` of
# 23:59:60Z gave 00:08:34, the leap lost; Rakudo shifts the minute and keeps
# the second at 60.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my $leap = DateTime.new('2016-12-31T23:59:60Z');
check $leap.in-timezone(Int(3600/7)).Str, '2017-01-01T00:08:60+00:08', 'the issue (rakudo#2097)';
check $leap.in-timezone(-514).Str, '2016-12-31T23:51:60-00:08', 'a negative offset';
check $leap.in-timezone(30).Str, '2017-01-01T00:00:60+00:00', 'half a minute';
check $leap.in-timezone(3600).Str, '2017-01-01T00:59:60+01:00', 'a whole hour, as before';
check $leap.in-timezone(514).second, '60', 'the second is 60';
check $leap.in-timezone(514).utc.Str, '2017-01-01T00:00:60Z', 'and back to UTC';
check DateTime.new('2016-12-31T23:59:60.5Z').in-timezone(514).second, '60.5', 'a fraction stays';
check DateTime.new('2016-12-31T23:59:59Z').in-timezone(514).Str, '2017-01-01T00:08:33+00:08', 'an ordinary second, as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
