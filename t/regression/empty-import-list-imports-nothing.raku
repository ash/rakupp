# `use Mod ()` and `need Mod` load a module and import NOTHING. rakupp parsed
# the empty parens as an expression argument for sub EXPORT, left the default
# import running, and then published the module's exported routines to GLOBAL
# under their bare names on top of that — so `use P5index ()` installed &index
# over the built-in one and `index("foobar","zzz")` answered -1 where it has to
# answer Nil.
#
# P5index is not part of this repo, so the case declares what it needs and the
# runner skips it where the module is absent — CI has never installed it, and
# without the marker the file failed there while testing nothing at all.

#?requires P5index

use Test;
plan 4;

{
    use P5index ();
    ok !index("foobar", "zzz").defined, '`use Mod ()` leaves the built-in index alone';
    is index("foobar", "bar"), 3,       '…which still answers for a hit';
}
{
    need P5index;
    ok !index("foobar", "zzz").defined, '`need Mod` imports nothing either';
}
{
    use P5index;
    is index("foobar", "zzz"), -1,      'a plain `use` does import it';
}
