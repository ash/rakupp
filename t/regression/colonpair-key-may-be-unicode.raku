# A colonpair key is an identifier, and a Raku identifier may be any Unicode
# letter. The word-quote reader demanded ASCII, so `<<:måndag(1) tisdag>>` read
# the whole token `:måndag(1)` as a literal enum KEY at ordinal 0 and shifted
# every later name down by one. Swedish::TextDates_sv builds its weekday and
# day-of-month tables that way: it answered onsdag for Thursday and trettonde
# for twelve, silently, and only under rakupp — its ASCII month table was the
# one part that survived, which is what made it hard to see.
use Test;
plan 6;

my enum Accent «:måndag(1) tisdag onsdag»;
is Accent::måndag.Int, 1, 'a non-ASCII colonpair key seeds the enum';
is Accent::tisdag.Int, 2, '…and the name after it follows on';
is Accent::onsdag.Int, 3, '…and so does the one after that';

my enum Ascii «:mon(1) tue wed»;
is Ascii::wed.Int, 3,     'the ASCII spelling is unchanged';

my @pairs = «:año(7) :Ω(9)»;
is @pairs[0].value, 7, 'a bare word list takes one too';
is @pairs[1].key,  'Ω', '…and keeps the key it was given';
