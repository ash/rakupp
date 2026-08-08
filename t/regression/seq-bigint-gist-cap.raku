# Two divergences from one user snippet (`my @powers = 1, 2, 4 ... *`):
#   1. the geometric/arithmetic `...` stepper computed integer terms through a
#      double + llround, so from 2^63 the sequence flatlined at int64 max —
#      exact arithmetic (bigint promotion) now takes precedence;
#   2. a list's .gist showed EVERY element — Rakudo caps at 100 and appends
#      " ..." (`.Str`/`.raku` stay complete).
# Runs under both engines.

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eqv $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got.raku}] WANT [{$want.raku}]";
    }
}

my @powers = 1, 2, 4 ... *;
check('2^64 is exact in a geometric sequence', @powers[64], 18446744073709551616);
check('2^110 is exact in a geometric sequence', @powers[110], 1298074214633706907132624082305024);

my @walk = 2**62, 2**62 + 1 ... *;
check('an arithmetic walk crosses the double-safe zone exactly',
      @walk[2], 4611686018427387906);

check('Rat-ratio geometric stays exact', (1, 1/2, 1/4 ... *)[3], 1/8);

check('a 100-element list gist has no ellipsis',
      (1..100).list.gist.substr(*-10), '98 99 100)');
check('a 101-element array gist caps at 100 with an ellipsis',
      [1..101].gist.substr(*-11), '99 100 ...]');
check('.Str stays complete past 100 elements',
      (1..101).list.Str.substr(*-7), '100 101');

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
