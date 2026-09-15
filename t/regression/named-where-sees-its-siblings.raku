# A `where` on a NAMED parameter can read the candidate's other parameters, the
# way one on a positional always could. In multi dispatch it could not, so
# `multi method get(UInt:D() :$line!, UInt:D() :$col! where * <= $line)` could
# not be scored at all and NO candidate matched — Math::PascalTriangle's whole
# API is that one signature.
use Test;
plan 5;

multi pick-one(Int :$line!, Int :$col! where * <= $line) { "$line/$col" }
is pick-one(line => 4, col => 2), '4/2',    'a named where reads a sibling named';
dies-ok { pick-one(line => 2, col => 9) },  '…and refuses when it does not hold';

class P {
    proto method get(UInt:D() :$line!, UInt:D() :$col!) {{*}}
    multi method get(UInt:D() :$line!, UInt:D() :$col! where * <= $line) { $line * 10 + $col }
}
is P.get(line => 4, col => 2), 42,          'through a proto, with coercion types';

multi mixed($base, Int :$off! where * < $base) { $base + $off }
is mixed(10, off => 3), 13,                 'a named where reads a positional too';

sub plain(Int :$line!, Int :$col! where * <= $line) { "$line:$col" }
is plain(line => 3, col => 1), '3:1',       'a plain sub is unchanged';
