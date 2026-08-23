# Two divergences found by Sparrow6's check engine, which verifies task output
# with `$data.comb(/<mymatch=$pattern>/,:match)>>.<mymatch>`:
#
# 1. `<alias=$var>` — the ALIASED assertion form. The value must compile as a
#    regex capturing under `alias`. It used to interpolate literally, leaving
#    `<alias=value>` — an unknown subrule, matched as a zero-width no-op —
#    so the pattern matched ANY line and every `regexp:` check passed.
# 2. `.comb($rx, :match)` must answer Match objects (captures included), not
#    strings; a capture lookup on a Str element quietly gave Any.

use Test;
plan 12;

my $pattern = 'ok';

# 1: the aliased assertion form
ok !('fail' ~~ /<mymatch=$pattern>/), '<alias=$var> does not match a line without the value';
ok  ('raku-ok' ~~ /<mymatch=$pattern>/), '<alias=$var> matches a line with it';
is  ('raku-ok' ~~ /<mymatch=$pattern>/)<mymatch>.Str, 'ok', '...and captures under the alias';
my $rxv = rx/o./;
is ('a-okay' ~~ /<m=$rxv>/)<m>.Str, 'ok', 'a Regex-valued variable aliases too';

# 2: comb :match
my @c = 'a-ok b-ok'.comb(/<mymatch=$pattern>/, :match);
is @c.elems, 2, 'comb :match finds each occurrence';
isa-ok @c[0], Match, 'comb :match answers Match objects';
is @c>>.<mymatch>>>.Str.join(','), 'ok,ok', '...whose named captures are readable';
is 'a1b2c3'.comb(/\d/, :match, 2).elems, 2, 'the positional limit truncates';
is 'abc'.comb(/./, :match).elems, 3, 'plain pattern, all occurrences';
is 'abc'.comb(/x?/, :match).elems, 4, 'zero-width matches advance (Rakudo: 4)';
is 'no'.comb(/z/, :match).elems, 0, 'no match, empty list';
is 'ok!'.comb(/<[a..z]>+/).join, 'ok', 'comb WITHOUT :match still answers strings';
