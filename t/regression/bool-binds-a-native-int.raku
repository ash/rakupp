# Raku's Bool is an enum over Int, so a Bool binds an `Int` parameter and the
# native `int` family — `f(False)` is 0. The binder's fast path admitted only
# VT::Int, so String::Color could not pass its own `$force` flag to its own
# helper.
use Test;
plan 4;

sub native(int $x) { $x + 1 }
is native(False), 1,        'False binds a native int as 0';
is native(True),  2,        '…and True as 1';

sub boxed(Int $x) { $x + 1 }
is boxed(False), 1,         'and an Int parameter takes it too';
is boxed(7),     8,         '…without disturbing an ordinary Int';
