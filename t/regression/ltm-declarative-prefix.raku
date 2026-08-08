# True LTM under RAKUPP_LTM=1 (v3 LTM pillar, phase 2) — the two
# oracle-verified divergences of the probe ranker, now fixed by the
# declarative-prefix NFA, plus the semantics that must not move. Programs
# run under $*EXECUTABLE with RAKUPP_LTM=1 in the child's environment, so
# this file passes under BOTH engines: Rakudo ignores the variable and has
# true LTM natively; rakupp with the variable ranks the same way.

my $work = $*TMPDIR.add("ltm-prefix-$*PID");
mkdir $work;

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eq $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got}] WANT [{$want}]";
    }
}

sub ltm-run(Str $name, Str $code) {
    my $f = $work.add($name);
    $f.spurt($code);
    my %e = %*ENV;
    %e<RAKUPP_LTM> = '1';
    my $p = run($*EXECUTABLE, $f.Str, :out, :!err, :env(%e)); # no :timeout — that adverb is a rakupp extension, and this file runs under Rakudo too
    $p.out.slurp(:close)
}

# 1. ranking is by DECLARATIVE PREFIX, not greedy full-match end: the first
#    alternative CAN match four chars, but its prefix ends at the {} (length
#    2); abc's prefix is 3 — abc wins (S05; Rakudo confirms)
check('[ ab {} cd ] | abc on "abcd" → abc',
      ltm-run('t1.raku', q{say ("abcd" ~~ / [ ab { } cd ] | abc /).Str;}),
      "abc\n");

# 2. ranking runs NO user code: the losing branch's {} must not fire
check('a losing branch\'s code block does not run during ranking',
      ltm-run('t2.raku', q{my $n = 0; "x" ~~ / [ { $n++ } y ] | x /; say $n;}),
      "0\n");

# 3. || is sequential: its later branches are NOT LTM candidates
check('|| stays sequential under the NFA ranker',
      ltm-run('t3.raku', q{say ("ab" ~~ / a || ab /).Str;}),
      "a\n");

# 4. plain longest-token behavior unchanged where both rankers agree
check('longest token still wins (a | ab on "ab")',
      ltm-run('t4.raku', q{say ("ab" ~~ / a | ab /).Str;}),
      "ab\n");
check('quantified atom still wins (a* | aa on "aaa")',
      ltm-run('t5.raku', q{say ("aaa" ~~ / a* | aa /).Str;}),
      "aaa\n");

# 5. the literal-prefix tie-break: same length, literal beats open class
check('literal beats char-class on equal prefix length',
      ltm-run('t6.raku', q{say ("ab" ~~ / <[a..c]> b | ab /).Str;}),
      "ab\n");

# 6. the hybrid keeps subrule alternations on the safe path until phase 3:
#    a token subrule in one branch must not be unfairly demoted
check('subrule branch is not demoted (hybrid fallback)',
      ltm-run('t7.raku', q{my token t { foo }; say ("foobar" ~~ / <t> bar | foob /).Str;}),
      "foobar\n");

unlink $work.add($_) for <t1.raku t2.raku t3.raku t4.raku t5.raku t6.raku t7.raku>;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
