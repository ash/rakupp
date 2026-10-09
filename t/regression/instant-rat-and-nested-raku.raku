# Regression, 2026-10-09: an Instant holds a Rat, whatever made it, and it
# renders as `Instant.from-posix(…)` nested as well as at the top.
# `Instant.from-posix(10)` and `DateTime.new(0).Instant` held Ints, so
# `from-posix(10) eqv from-posix(10.0)` was False and `.raku` said `(10)`;
# nested in a Pair, Array or Hash an Instant rendered as its bare number (a
# Rat-based one) or as `Instant:10`, and a Duration as its number. BSON::Simple
# compares decoded documents by `.raku`, so its datetime tests failed.
# Expected values are Rakudo 2026.09's.
use Test;
plan 11;

my $a = Instant.from-posix(10);
my $b = Instant.from-posix(10.0);
is $a.raku, 'Instant.from-posix(10.0)', 'from an Int, a Rat underneath';
is $a.tai.^name, 'Rat', '…as .tai says';
ok $a eqv $b, 'from-posix(10) eqv from-posix(10.0)';
is DateTime.new(0).Instant.raku, 'Instant.from-posix(0.0)', 'DateTime.Instant on a whole second';
ok DateTime.new(0).Instant eqv Instant.from-posix(0), '…eqv from-posix(0)';
is (a => Instant.from-posix(0)).raku, ':a(Instant.from-posix(0.0))', 'nested in a Pair';
is [Instant.from-posix(1), Duration.new(2)].raku, '[Instant.from-posix(1.0), Duration.new(2.0)]', 'nested in an Array';
is (d => Duration.new(5)).raku, ':d(Duration.new(5.0))', 'a Duration nested';
is ~Instant.from-posix(10), 'Instant:20', '.Str is unchanged';
is (Instant.from-posix(20) - Instant.from-posix(10)).raku, 'Duration.new(10.0)', 'Instant arithmetic';
my Mu $value; $value = Instant.from-posix(10000 / 1000);
is ('k' => $value).raku, ':k(Instant.from-posix(10.0))', 'from a $ variable into a Pair';
