# `.absolute`, `.cleanup` and `.canonpath` answered for ANY invocant, so
# `7.absolute` returned "$*CWD/7" and `Int.^can('absolute')` said True. Rakudo
# has these on IO::Path only. A silent wrong answer: a typo on a number came
# back as a plausible-looking path instead of an error.
use Test;
plan 6;

dies-ok { 7.absolute },     'Int has no .absolute';
dies-ok { 'x'.absolute },   'Str has no .absolute';
dies-ok { 7.cleanup },      'Int has no .cleanup';
nok Int.^can('absolute'),   'Int.^can("absolute") is False';

# …and an IO::Path still has them
ok 'x'.IO.absolute.ends-with('/x'), 'IO::Path.absolute still works';
is 'a/./b'.IO.cleanup.Str, 'a/b',   'IO::Path.cleanup still works';
