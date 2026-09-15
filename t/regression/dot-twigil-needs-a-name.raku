# `.` inside an interpolation is the PUBLIC-ATTRIBUTE twigil, so an attribute
# name has to follow it. Accepting a bare `.` made "%.{$n}g" — the way a printf
# precision is built — parse as a hash subscript on an unnamed attribute, and
# the format came back as just "g". Astro::Sunrise's convergence test then
# compared "g" with "g", succeeded on the first pass, and its :iter mode
# returned midnight for every location on Earth.
use Test;
plan 6;

my $n = 8;
is "%.{$n}g", '%.8g',   'a precision built with a block keeps its %.';
is "%.{4}g",  '%.4g',   '…with a literal too';
is "50%.{$n}", '50%.8', '…and mid-string';
is "%{$n}g",  '%8g',    'a width with no dot was already right';

class K { has %.h = (a => 1); has @.a = (7, 8); method probe { ("%.h<a>", "@.a[0]") } }
is K.new.probe[0], '1', '%.name<key> still interpolates the attribute';
is K.new.probe[1], '7', '@.name[i] still interpolates the attribute';
