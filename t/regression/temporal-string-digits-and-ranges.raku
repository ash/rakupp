# Date and DateTime strings take any decimal digit, as Rakudo's `\d` and
# `.Int` read one: `Date.new("٢٠١٠-٠١-٠٢")` was X::Temporal::InvalidFormat,
# where Rakudo makes 2010-01-02. X::Temporal::OutOfRange carries Rakudo's
# attributes, `.got` the Int, `.range` its Str and `.comment` (unset), where
# `.got` was a Str and `.comment` no method; and a timezone offset's minutes
# out of range are "minutes of timezone out of range. Is: 99, should be in
# 0..^60", `.got` a Rat, where they were the minute field's.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub caught(&c) { try c(); $! }

ck Date.new("٢٠١٠-٠١-٠٢").Str, '2010-01-02', 'Arabic-Indic digits';
ck DateTime.new("٢٠١٢-١٢-٢٢T07:02:00Z").Str, '2012-12-22T07:02:00Z', 'a DateTime';
ck caught({ Date.new("٢٠١٠-٠١-x") }).message, "Invalid Date string '٢٠١٠-٠١-x'; use yyyy-mm-dd instead",
   'still refused when malformed, as written';
my $e = caught({ Date.new(2015, 13, 1) });
ck ($e.^name, $e.what, $e.got, $e.got.^name, $e.range, $e.comment, $e.message),
   ('X::Temporal::OutOfRange', 'Month', 13, 'Int', '1..12', Any, 'Month out of range. Is: 13, should be in 1..12'), 'a month';
ck caught({ Date.new(2015, 2, 30) }).&{ (.got, .range, .message) }, (30, '1..28', 'Day out of range. Is: 30, should be in 1..28'), 'a day';
ck caught({ DateTime.new(2015, 2, 3, 25, 0, 0) }).&{ (.got, .range) }, (25, '0..23'), 'an hour';
ck caught({ DateTime.new("2012-13-22T07:02:00Z") }).got.^name, 'Int', 'from a string';
my $z = caught({ DateTime.new("2012-12-22T07:02:00+00:99") });
ck ($z.^name, $z.what, $z.range, $z.got, $z.message),
   ('X::OutOfRange', 'minutes of timezone', '0..^60', 99.0, 'minutes of timezone out of range. Is: 99, should be in 0..^60'),
   "a timezone's minutes";
ck caught({ DateTime.new("2012-12-22T07:02:00+0099") }).message,
   'minutes of timezone out of range. Is: 99, should be in 0..^60', 'without the colon';
ck ("٤٢".Int, "1٢".Int), (42, 12), '.Int, as before';

say 'PASS';
