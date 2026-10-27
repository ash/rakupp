# Regression: a DateTime against a plain NUMBER was numified to 0, or moved.
#
# `DateTime.new(0) + 60` answered a DateTime a minute on, `$d * 2` was 0, and
# `$d == 10` False. In Rakudo a DateTime is its Instant to arithmetic: `$d + 60`
# is Instant:70 (TAI, ten leap seconds in by 1970), `$d * 2` the Num 20, and
# only a Duration moves one (dateTimeArith). `div` and `mod` are left alone:
# Rakudo refuses the one and answers an Instant for the other.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

my $d = DateTime.new(0);
ok ($d + 60).^name eq 'Instant' && ~($d + 60) eq 'Instant:70', '$d + 60 is Instant:70';
ok ~($d - 60) eq 'Instant:-50' && ~(60 + $d) eq 'Instant:70', '- and the other order';
ok ~(DateTime.new(0, :timezone(3600)) + 60) eq 'Instant:70', 'the offset does not enter it';
ok ($d * 2).^name eq 'Num' && $d * 2 == 20, '$d * 2';
ok (60 - $d).^name eq 'Num' && 60 - $d == 50, '60 - $d';
ok $d / 4 == 2.5 && $d % 3 == 1 && $d ** 2 == 100, '/, % and **';
ok $d == 10 && $d < 11 && 9 < $d && ($d <=> 10) == Same, 'comparisons ask the Instant';
ok ($d ~ 5) eq '1970-01-01T00:00:00Z5', '~ still concatenates';
# …while a Duration moves it, and Dates count days
ok ($d + Duration.new(60)).^name eq 'DateTime' && ~($d + Duration.new(60)) eq '1970-01-01T00:01:00Z',
   'a Duration moves it';
ok ~(DateTime.new(0, :timezone(3600)) + Duration.new(60)) eq '1970-01-01T01:01:00+01:00',
   '…keeping its offset';
ok ($d - DateTime.new(10)).^name eq 'Duration' && $d - DateTime.new(10) == -10, 'a difference is a Duration';
ok ~(Date.new(2020, 1, 1) + 1) eq '2020-01-02', 'Date + 1 is the next day';

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
