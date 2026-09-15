# Two halves of one gap. A standard X:: exception built by hand carries its
# message in its ATTRIBUTES — Rakudo renders them in a `method message` — so
# `X::OutOfRange.new(what => …, got => …, range => …)` had an undefined
# .message and an empty .gist here. And the core Date/DateTime constructors
# threw plain X::OutOfRange where Rakudo throws X::Temporal::OutOfRange, so a
# `CATCH { when X::Temporal::OutOfRange {…} }` never matched.
# Four Date::Calendar distributions throw the first one to report a bad month.
use Test;
plan 8;

my $e = X::OutOfRange.new(what => 'Year', got => 0, range => '1..Inf');
is $e.message, 'Year out of range. Is: 0, should be in 1..Inf', 'a hand-built X::OutOfRange renders its attributes';
is $e.gist.lines[0], 'Year out of range. Is: 0, should be in 1..Inf', '…and the gist says the same';

my $c = X::OutOfRange.new(what => 'Day', got => 32, range => '1..31', comment => 'not in February');
is $c.message, 'Day out of range. Is: 32, should be in 1..31; not in February', 'a comment is appended';

is X::NYI.new(feature => 'Frobnication').message, 'Frobnication not yet implemented. Sorry.', 'X::NYI renders too';
is X::OutOfRange.new(what => 'X', got => 1, range => '2..3', message => 'mine').message,
   'X out of range. Is: 1, should be in 2..3',
   'the rendered message wins over an explicit one, as Rakudo\'s method does';

my $r = try Date.new(2026, 13, 1);
is $!.^name, 'X::Temporal::OutOfRange', 'Date.new throws X::Temporal::OutOfRange';
is $!.message, 'Month out of range. Is: 13, should be in 1..12', '…with the right message';
ok $! ~~ X::OutOfRange, '…and it is still an X::OutOfRange';
