# `for … -> Int $i`: a typed loop variable that binds a machine Int as itself
# takes the counted path and tiers up as `-> $i` does (countedForParam,
# FORTRAN-LESSONS-2026-10-15 item 1a). Unlike a native `int`, it does not
# wrap: a sum past int64 leaves the kernel and the interpreter finishes it as
# a BigInt. The native counter is t/cnp/cases/typed-counted-for.raku (only
# copy-and-patch binds a native cell).
my $s = 0;
for 1 .. 500 -> Int $i { $s = $s + $i * $i }
say "squares $s";
my $b = 9223372036854775000;
for ^2000 -> Int $i { $b = $b + $i }
say "past int64 $b";
my $c = 0;
for 1 .. 300 -> Int $i { next if $i %% 3; last if $i > 250; $c = $c + $i }
say "control $c";
