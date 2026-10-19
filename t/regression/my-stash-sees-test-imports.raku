# Regression: what `use Test` imports was missing from the importing scope's
# stash: `MY::.keys` had no `&ok`, and `MY::<&ok>:exists` was False, though
# calling `ok` worked. Test's subs are engine built-ins, visible with or
# without the `use`, so the frame never held them; it now holds a marker the
# stash views expand (MY::, UNIT::, LEXICAL::, OUTER::, `:exists`, `:p`, the
# value). rakush lists a session's commands with `MY::.keys` (the C API side
# is in tools/embed/embed-host.c). A real module's exports were already
# there. Also fixed on the way: `OUTER::<&g>:exists` dropped its adverb and
# answered the lookup's value.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged; every
# expectation was checked against Rakudo 2026.08 on 2026-10-19.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck(MY::<&ok>:exists, False, 'before the use, no &ok');
{
    use Test;
    ck(MY::.keys.grep(* eq any <&ok &is &plan &is-deeply &throws-like>).sort.List,
       ('&is', '&is-deeply', '&ok', '&plan', '&throws-like'), 'MY::.keys lists what Test exports');
    ck(MY::<&ok>:exists, True, 'MY::<&ok>:exists');
    ck(MY::<&nope>:exists, False, 'and nothing it does not export');
    ck(MY::<&is>.name, 'is', 'MY::<&is> is the routine');
    ck((MY::<&ok>:p).key, '&ok', ':p gives the pair');
    ck(LEXICAL::<&ok>:exists, True, 'LEXICAL:: sees it');
    sub inner { OUTER::<&cmp-ok>:exists }
    ck(inner(), True, 'OUTER:: from a sub declared in that scope');
    ck(MY::.keys.grep(*.starts-with("\x01")).elems, 0, 'no internal name shows');
}
ck(MY::<&ok>:exists, False, 'and the block\'s import stays in the block');

sub g { }
sub f { (OUTER::<&g>:exists, OUTER::<&nope>:exists) }
ck(f(), (True, False), 'OUTER::<&g>:exists answers the question');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
