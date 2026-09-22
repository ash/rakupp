# `X* % Y` under `|`: the first element takes no separator (issue #94).
#
# RAKUPP-ONLY, and deliberately so — this is the one place the LTM ranker is
# knowingly AHEAD of Rakudo rather than level with it, so unlike
# ltm-declarative-prefix.raku this file does not run under both engines. Every
# expectation below is what Rakudo *ought* to print; Rakudo 2026.08 prints
# `nil` where this file wants `ok` for the separated-list cases.
#
# The bug: `X* % Y` is `[ X [ Y X ]* ]?` — the FIRST element carries no leading
# separator — but the declarative-prefix NFA modelled the whole quantifier as
# `(Y X)*`, which says a non-empty list OPENS with a separator. The NFA then
# ruled the branch out the moment it saw an element in that position, and `|`
# PRUNED a branch that plainly matches:
#
#     token call { 'f(' <e>* % ',' ')' }
#     token alt  { <call> | <asg> }      # 'f(1,2)' did not match
#
# What makes it pruning rather than mis-ranking: `<call> | 'zzzz'` failed too,
# where the second branch cannot match anything at all. Adding an alternative
# that matches nothing removed a match — no reading of alternation semantics
# allows that. The only input that survived was the zero-element one, which is
# the single case the `(Y X)*` shape does model.
#
# Boundaries, all verified: it needs BOTH a separator AND a quantifier that can
# match zero times. `X+ % Y`, `X ** 2 % Y`, `X?` and a bare `X*` with no
# separator were all fine before and stay fine. Roast covers `%` with `**2`,
# `**3..3`, `**1..*`, `?` and `+` in S05-metasyntax/proto-token-ltm.t — every
# quantifier except this one, which is why it survived.

my $work = $*TMPDIR.add("ltm-sepq-$*PID");
mkdir $work;

my $fails = 0;
my $n = 0;
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

sub run-prog(Str $code) {
    my $f = $work.add('p' ~ $n++ ~ '.raku');
    $f.spurt($code);
    my $p = run($*EXECUTABLE, $f.Str, :out, :!err);
    $p.out.slurp(:close)
}

# --- the reported grammar, every arity ------------------------------------
my $G = q{grammar G {
    token e    { \d+ }
    token call { 'f(' <e>* % ',' ')' }
    token asg  { 'x=' \d+ }
    token alt  { <call> | <asg> }
    token solo { <call> | 'zzzz' }
}
};

for <f() f(1) f(1,2) f(1,2,3)> -> $in {
    check("<call> | <asg> matches $in",
          run-prog($G ~ qq{say G.parse('$in', :rule<alt>) ?? 'ok' !! 'nil';}),
          "ok\n");
}
# the decisive one: a second branch that cannot match must not remove a match
check('a branch that matches nothing does not remove the match',
      run-prog($G ~ q{say G.parse('f(1,2)', :rule<solo>) ?? 'ok' !! 'nil';}), "ok\n");
# and the alternation still picks the other branch when that is the one
check('the other branch still matches its own input',
      run-prog($G ~ q{say G.parse('x=42', :rule<alt>) ?? 'ok' !! 'nil';}), "ok\n");

# --- a declarative element, the case that used to prune -------------------
my $J = q{grammar G {
    token value  { <array> | <jx> }
    token array  { '[' <number>* % ',' ']' }
    token number { \d+ }
    token jx     { 'x' }
}
};
check('a declarative element is no longer pruned',
      run-prog($J ~ q{say G.parse('[1]', :rule<value>) ?? 'ok' !! 'nil';}), "ok\n");
check('...nor with several elements',
      run-prog($J ~ q{say G.parse('[1,2,3]', :rule<value>) ?? 'ok' !! 'nil';}), "ok\n");
check('the empty list still matches',
      run-prog($J ~ q{say G.parse('[]', :rule<value>) ?? 'ok' !! 'nil';}), "ok\n");
check('the other branch of that grammar still matches',
      run-prog($J ~ q{say G.parse('x', :rule<value>) ?? 'ok' !! 'nil';}), "ok\n");

# a RECURSIVE element already worked (its prefix ends at the loop entry); it
# must keep working, since the fix rebuilt the very path that made it reachable
my $R = q{grammar G { token value { <array> | <number> }; token array { '[' [ <value>* % [ ',' ] ] ']' }; token number { \d+ } }; };
check('a recursive element still is not pruned',
      run-prog($R ~ q{say G.parse('[1,2]', :rule<value>) ?? 'ok' !! 'nil';}), "ok\n");
check('...and nested one level down',
      run-prog($R ~ q{say G.parse('[1,[2,3]]', :rule<value>) ?? 'ok' !! 'nil';}), "ok\n");

# --- the bounded form has the same shape and the same fix -----------------
my $B = q{grammar G { token e { \d+ }; token c { 'f(' <e> ** 0..3 % ',' ')' }; token a { <c> | 'zzzz' } }; };
check('a bounded 0..n separated list matches one element',
      run-prog($B ~ q{say G.parse('f(1)', :rule<a>) ?? 'ok' !! 'nil';}), "ok\n");
check('...and three',
      run-prog($B ~ q{say G.parse('f(1,2,3)', :rule<a>) ?? 'ok' !! 'nil';}), "ok\n");
check('...and still refuses one past the bound',
      run-prog($B ~ q{say G.parse('f(1,2,3,4)', :rule<a>) ?? 'ok' !! 'nil';}), "nil\n");

# --- what must NOT have moved: the quantifiers that were always right -----
check('X+ % Y still matches',
      run-prog(q{grammar G { token e { \d+ }; token c { 'f(' <e>+ % ',' ')' }; token a { <c> | 'zzzz' } }; say G.parse('f(1,2)', :rule<a>) ?? 'ok' !! 'nil';}),
      "ok\n");
check('X+ % Y still refuses the empty list',
      run-prog(q{grammar G { token e { \d+ }; token c { 'f(' <e>+ % ',' ')' }; token a { <c> | 'zzzz' } }; say G.parse('f()', :rule<a>) ?? 'ok' !! 'nil';}),
      "nil\n");
check('a bare X* with no separator is unchanged',
      run-prog(q{grammar G { token e { \d+ }; token c { 'f(' <e>* ')' }; token a { <c> | 'zzzz' } }; say G.parse('f(11)', :rule<a>) ?? 'ok' !! 'nil';}),
      "ok\n");
check('LTM still RANKS: the longer declarative prefix wins',
      run-prog(q{grammar G { token e { \d+ }; token long { 'f(' <e>* % ',' ')!' }; token short { 'f(' }; token a { <short> | <long> } }; say G.subparse('f(1,2)!', :rule<a>).Str;}),
      "f(1,2)!\n");

for $work.dir { .unlink }
$work.rmdir if $work.e;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
