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

# 7. phase 3 (grammar expansion) invariants — each was a real bug:
#    the literal tie-break must be PATH-dependent: bar's dead `aa` path must
#    not lend its literal count to bar's live `<foo>` path and steal the tie
#    from the earlier-declared foo (longest-alternative.t test 35)
check('dead literal path does not poison the tie-break',
      ltm-run('t8.raku', q{grammar Galt { token TOP { <foo> | <bar> }; token foo { \w\w }; token bar { aa | <foo> } }; say Galt.subparse("bb")<foo>.Str;}),
      "bb\n");

#    a composed char class (`<[...] +rule>`) is a one-char UNION: modeling it
#    as its first member under-matched `scheme` and pruned the whole <URI>
#    branch (longest-alternative.t test 41)
check('composed char class ranks as a union, not its first member',
      ltm-run('t9.raku', q{grammar U { token TOP { <URI> | <rel> }; token URI { <scheme> ':' <[a..z/.]>+ }; token scheme { <.alpha1> <[+.] +alpha1>* }; token alpha1 { <[a..z]> }; token rel { <.alpha1> ** 0 } }; say U.subparse("http://example.com").Str;}),
      "http://example.com\n");

#    proto dispatch with PLAIN token candidates ranks by declarative prefix
#    (Rakudo: plain `token t:sym<x>` candidates get LTM; `multi token` ones
#    fall back to declaration order — that variant is a known divergence)
check('proto with plain-token candidates: longest prefix wins',
      ltm-run('t10.raku', q{grammar G { token TOP { <t> }; proto token t {*}; token t:sym<s> { ab }; token t:sym<l> { abcd } }; say G.subparse("abcdef")<t>.Str;}),
      "abcd\n");

#    <?{...}>/<!{...}> assertions are zero-width and TRANSPARENT to LTM
#    (protoregex.t 23-24): the ranking treats them as ε, the commit engine
#    enforces them — so a passing assertion extends the prefix and a failing
#    one just fails its branch at commit
check('a passing code assertion does not terminate the prefix',
      ltm-run('t11.raku', q{say ("aaa" ~~ / a <?{ 1 }> .+ | aa /).Str;}),
      "aaa\n");
check('a failing code assertion fails its branch at commit',
      ltm-run('t12.raku', q{grammar G { token TOP { a <?{ 0 }> .+ | aa } }; say G.subparse("aaa").Str;}),
      "aa\n");
#    ... and the same must hold in a PLAIN regex: un-"wired" matches used to
#    skip the assertPass hook entirely, so a positive <?{ 0 }> silently
#    PASSED (and <!{ 0 }> anti-failed via the negated constant-true default)
check('a failing code assertion fails in a plain regex too',
      ltm-run('t13.raku', q{say ("aaa" ~~ / a <?{ 0 }> .+ | aa /).Str;}),
      "aa\n");

# 8. phase-3 tail: :m literals rank (base-codepoint compare, marks consumed),
#    and a lexical `my rule` expands with its sigspace <ws> modeled as \s*
check(':m literals participate in ranking',
      ltm-run('t14.raku', q{say ("noël!" ~~ m:m/ noel | no /).Str;}),
      "noël\n");
check('a lexical rule (sigspace) expands into the prefix',
      ltm-run('t15.raku', q{my rule r { foo bar }; say ("foo  bar" ~~ / <r> | foob /).Str;}),
      "foo  bar\n");

unlink $work.add($_) for <t1.raku t2.raku t3.raku t4.raku t5.raku t6.raku t7.raku t8.raku t9.raku t10.raku t11.raku t12.raku t13.raku t14.raku t15.raku>;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
