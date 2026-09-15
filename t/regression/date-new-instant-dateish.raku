# BROKE: #88 — Date.new(now) printed +1789452789-01-01. A Dateish or Instant
# argument was turned into posix seconds for BOTH constructors, but only the
# DateTime arm consumed them; for Date they fell through to the positional
# fields and became the year (S32-temporal/Date.t 'can create Date from
# Instant', and Date.new(DateTime.now) the same way).
# FIXED: Date.new(Instant) takes the UTC day of those seconds, as Rakudo's
# `self.new(DateTime.new($i))` does; Date.new(Dateish) copies the civil
# year/month/day, so a DateTime at 01:00 in +02:00 is that local date, not
# the UTC one its posix names.
my $i = Instant.from-posix: 1234567890;
die 'Date.new(Instant) broken' unless Date.new($i).Str eq '2009-02-13';
die 'Date.new(Instant, :formatter) broken'
    unless Date.new($i, :formatter({ "It is {.year}" })).Str eq 'It is 2009';
die 'Date.new(now) is not today' unless (Date.new(now) - Date.today).abs <= 1;
die 'Date.new(DateTime) must copy the civil day'
    unless Date.new(DateTime.new(2026, 9, 15, 1, 0, 0, :timezone(7200))).Str eq '2026-09-15';
die 'Date.new(DateTime.now) broken' unless (Date.new(DateTime.now) - Date.today).abs <= 1;
die 'Date.new(Date) broken' unless Date.new(Date.new('2016-09-28')).Str eq '2016-09-28';
say 'PASS';
