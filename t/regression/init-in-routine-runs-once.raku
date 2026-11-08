# An INIT in a routine runs once, at program start, as Rakudo's does, though
# it reads a mainline variable with an initializer: `my $n = 0; sub g { INIT
# { $n++ }; 1 }; g(); g(); say $n` said 2, the INIT left to run on each call,
# where Rakudo says 0 (the INIT ran first, and `my $n = 0` after it). Such an
# INIT now runs ahead of the mainline as one reading a bare `my $n;` did.
# (Each case runs in a program of its own: the order is the program's.)
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub ran($code) { my $p = run $*EXECUTABLE, '-e', $code, :out, :err; $p.out.slurp(:close).lines.join('|') }

ck ran('my $n = 0; sub g { INIT { $n++ }; 1 }; g(); g(); say $n'), '0', 'an initialized variable';
ck ran('my $x = 10; sub g { INIT { $x++ }; $x }; say g(); say g()'), '10|10', 'and read after';
ck ran('my @log = 1; sub g { INIT { @log.push(2) }; 1 }; g(); say @log'), '[1]', 'an array';
ck ran('sub h { INIT { say "init" }; 1 }; h(); h(); say "end"'), 'init|end', 'one that reads nothing';
ck ran('my $n; sub g { INIT { $n = 5 }; 1 }; say $n'), '5', 'a bare variable, as before';
ck ran('my $n = 0; INIT { $n++ }; say $n'), '0', 'at the top level, as before';

say 'PASS';
