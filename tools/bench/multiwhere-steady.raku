# multiwhere.raku at 7.5 times the work — 3M calls into a `where`-constrained
# multi candidate.
#
# V6-PLAN P0. At multiwhere.raku's size Rakudo's startup and warm-up are a
# large part of its total, so the row reads rakupp far closer than it is: 2.5×
# there, 4.8× at this size on 2026-09-26. This row is the steady-state figure
# V6's first number is judged by. Keep the body identical to multiwhere.raku;
# only the count differs.
class K {
    proto method m(|) {*}
    multi method m(Int $x where * > 0) { $x }
    multi method m(Int $x)             { 0 }
}
my $k = K.new;
my $t = 0;
my int $n = 0;
while $n < 3_000_000 { $t = $t + $k.m($n % 3); $n = $n + 1 }
say $t;
