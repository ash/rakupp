# Regression: `run`/`shell` took `:env` only as a bare Hash. Every other
# spelling fell past the branch that reads it and was dropped in SILENCE — the
# child then inherited this process's environment, which is what NO :env at all
# means. So `:env(PROBE => 'M')`, which in Rakudo is a clean environment holding
# one variable, handed the child the parent's entire environment: a child meant
# to run isolated was not isolated, and nothing said so. `shell` was worse — it
# did not parse `:env` at all, at any shape.
#
# Rakudo takes `.hash` of whatever `:env` holds, so a Hash, a Pair, a list of
# pairs, and a hash followed by pairs are all legal; the last is how one
# variable is added to the parent's set, and is the shape that surfaced this.
#
# Every expectation below IS Rakudo v2026.08's output, checked row by row.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eq $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku}, want {$want.raku}" }
}

# The child reports the probe variable, and whether it inherited anything else.
# PATH is the witness: every shape that builds a FRESH environment must lose it.
# Concatenated, not `print (a, "/", b)`: the space before the paren makes that
# ONE List argument, and a List stringifies with spaces between its elements.
my $report = 'print (%*ENV<PROBE> // "-") ~ "/" ~ (%*ENV<PATH>:exists ?? "inherited" !! "clean")';

sub child(|c) {
    my $p = run($*EXECUTABLE, '-e', $report, :out, :!err, |c);
    my $t = $p.out.slurp(:close);
    $t
}

ck(child(),                                  '-/inherited', 'no :env inherits, as always');
ck(child(:env(%(PROBE => 'M'))),             'M/clean',     ':env(%h) is the whole environment');
ck(child(:env(%*ENV, PROBE => 'M')),         'M/inherited', ':env(%*ENV, k => v) is the parent set plus one');
ck(child(:env(PROBE => 'M')),                'M/clean',     ':env(k => v) is a clean environment of one');
ck(child(:env(('PROBE', 'M'))),              'M/clean',     ':env(list) pairs elements up, as .hash does');
ck(child(:env((PROBE => 'x', PROBE => 'M'))), 'M/clean',    'later keys win');

# The DEFERRED path — `:in` spawns when stdin is written, and the environment
# has to travel with the Proc rather than being read at spawn time.
{
    my $p = run($*EXECUTABLE, '-e', 'print (%*ENV<PROBE> // "-"), $*IN.slurp.chomp',
                :in, :out, :env(%*ENV, PROBE => 'M'));
    $p.in.print('+stdin');
    $p.in.close;
    ck($p.out.slurp(:close), 'M+stdin', ':env survives the deferred :in spawn');
}

# shell(), which did not read :env at all
{
    my $sh = q{printf %s "${PROBE:-none}"};
    ck(shell($sh, :out, :env(%(PROBE => 'M'))).out.slurp(:close),      'M', 'shell :env(%h)');
    ck(shell($sh, :out, :env(%*ENV, PROBE => 'M')).out.slurp(:close),  'M', 'shell :env(%*ENV, k => v)');
    ck(shell($sh, :out).out.slurp(:close),                          'none', 'shell without :env is untouched');
}

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
