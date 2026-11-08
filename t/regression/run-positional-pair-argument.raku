# A Pair passed to `run` as a positional is an argument, its Str "a\t1", as
# on Rakudo: run() read every Pair as an adverb and dropped the ones it did
# not know. A named argument, or a hash slipped in as named ones, is still
# an adverb.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck run('echo', 'a' => 1, :out).out.slurp(:close), "a\t1\n", 'a positional Pair';
my %o = :out;
ck run('echo', 'x', |%o).out.slurp(:close), "x\n", 'a slipped hash is named';
my @c = 'echo', 'y';
ck run(@c, :out, :!chomp).out.slurp(:close), "y\n", 'an array of words (as before)';

say 'PASS';
