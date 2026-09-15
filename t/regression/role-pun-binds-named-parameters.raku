# `R[Type, :name<value>]` — a role parameterization carrying BOTH a positional
# and a named argument. The parser kept only the identifiers from inside the
# brackets, so the named argument's value was dropped and its KEY joined the
# positional list; the pun then bound neither. `BitEnum[MyBits, :prefix<BIT_>]`
# is that shape, and every lookup through it read an empty prefix. An ENUM's
# type object survives the trip too: it IS a tagged pair-list, so the argument
# list used to flatten it into its own pairs.
use Test;
plan 6;

my enum Bits (A => 0x01, B => 0x02);

role R[::E, Str:D :$prefix = 'def'] {
    method nm    { E.^name }
    method pfx   { $prefix }
    method names { E.enums.keys.sort.join(',') }
}

is R[Int, :prefix<P_>].new.pfx, 'P_',       'a named argument binds beside a positional';
is R[Int].new.pfx, 'def',                   '…and its default still applies';
is R[Bits, :prefix<Q_>].new.pfx, 'Q_',      '…with an enum in the positional slot';
is R[Bits].new.nm, 'Bits',                  'the enum type object survives the capture';
is R[Bits].new.names, 'A,B',                '…carrying its values with it';

class C does R[Bits, :prefix<R_>] { }
is C.new.pfx, 'R_',                         'the composed-into-class path is unchanged';
