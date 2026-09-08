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

# 9. `X* % Y` under `|`: the FIRST element takes no leading separator, so a
#    recursive element's prefix ends (accept) at the loop ENTRY — the branch
#    must stay a viable candidate (a JSON grammar's `<value>* % ','` was
#    pruned outright: the sep-first loop hid the accept behind the comma).
check('sep-quantifier with a recursive element is not pruned',
      ltm-run('t16.raku', q{grammar G { token value { <array> | <number> }; token array { '[' [ <value>* % [ ',' ] ] ']' }; token number { \d+ } }; say G.parse('[1,2]', :rule<value>) ?? 'ok' !! 'nil';}),
      "ok\n");
#    ... while a fully DECLARATIVE element keeps Rakudo's own behavior: the
#    modeled sep-loop cannot reach a first element, so a non-empty list dies
#    in ranking and the branch is pruned (oracle-verified on 2026.07; if
#    Rakudo ever fixes this, this expectation flips WITH it)
check('sep-quantifier with a declarative element prunes like Rakudo',
      ltm-run('t17.raku', q{grammar G { token value { <array> | <jx> }; token array { '[' <number>* % ',' ']' }; token number { \d+ }; token jx { 'x' } }; say G.parse('[1]', :rule<value>) ?? 'ok' !! 'nil';}),
      "nil\n");
check('sep-quantifier: empty list still matches',
      ltm-run('t18.raku', q{grammar G { token value { <array> | <jx> }; token array { '[' <number>* % ',' ']' }; token number { \d+ }; token jx { 'x' } }; say G.parse('[]', :rule<value>) ?? 'ok' !! 'nil';}),
      "ok\n");

# 10. a LOOKAROUND ends the declarative prefix. It does NOT continue it the way
#     a zero-width `<?{…}>` does, and it does not disable ranking either: here
#     the first branch's prefix stops at the assertion (4 chars) while the
#     second's runs the whole 10, so the SECOND branch wins even though both
#     match the full string and the first is declared earlier. Modelling the
#     lookaround as a gap instead demoted the whole alternation to the
#     greedy full-match ranker, which ties at 10 and takes the first
#     (LaTeX::Grammar, issue #61).
check('<?before> ends the prefix, so the rival branch outranks it',
      ltm-run('t19.raku', q{grammar G { token TOP { <p> | <q> }; token p { 'r1ab' <?before 'c'> 'cdefgh' }; token q { 'r1ab' 'cdefgh' } }; my $m = G.parse('r1abcdefgh'); say $m ?? $m.hash.keys.grep(*.chars).sort.join(',') !! 'FAIL';}),
      "q\n");
check('<!before> ends the prefix the same way',
      ltm-run('t20.raku', q{grammar G { token TOP { <p> | <q> }; token p { 'r5ab' <!before 'z'> 'cdefgh' }; token q { 'r5ab' 'cdefgh' } }; my $m = G.parse('r5abcdefgh'); say $m ?? $m.hash.keys.grep(*.chars).sort.join(',') !! 'FAIL';}),
      "q\n");
#     …demoted, never pruned: with the rival unable to match, a branch whose
#     prefix ends at a LEADING lookaround still wins
check('a lookaround-led branch is still a candidate',
      ltm-run('t21.raku', q{grammar G { token TOP { <p> | <q> }; token p { <?before 'r3'> 'r3xy' }; token q { 'r3xyZZZ' } }; my $m = G.parse('r3xy'); say $m ?? $m.hash.keys.grep(*.chars).sort.join(',') !! 'FAIL';}),
      "p\n");

# 11. the NFA build budget is PER BRANCH. `bushy` alone overruns it; when the
#     budget was shared and cumulative, it left nothing for `plain`, whose
#     'zzzz' prefix (4) is the reason `plain` should win over bushy's 'zz' (2).
#     Both branches match the whole input, so a ranker that gives up here ties
#     them and takes the earlier-declared `bushy`.
my $bushy-alts = (^200).map({ "'zq{$_}xxxxxxxxxxxxxxxxxxxxxxxx'" }).join(' | ');
check('an oversized branch does not starve the branches after it',
      ltm-run('t22.raku',
              'grammar B { token TOP { <bushy> | <plain> }; token bushy { '
              ~ $bushy-alts ~ ' | ' ~ "'zz' \\w+" ~ ' }; token plain { '
              ~ "'zzzz' \\w+" ~ ' } }' ~ "\n"
              ~ 'my $m = B.parse("zzzzABC"); say $m ?? $m.hash.keys.grep(*.chars).sort.join(",") !! "FAIL";'),
      "plain\n");

unlink $work.add($_) for <t1.raku t2.raku t3.raku t4.raku t5.raku t6.raku t7.raku t8.raku t9.raku t10.raku t11.raku t12.raku t13.raku t14.raku t15.raku t16.raku t17.raku t18.raku t19.raku t20.raku t21.raku t22.raku>;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
