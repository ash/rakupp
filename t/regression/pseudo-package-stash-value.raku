# Regression: a bare pseudo-package (`MY::`, `UNIT::`) as a value is the
# scope's symbol table.
#
# String::Utils exports everything `&`-named with
# `UNIT::.grep: { .key.starts-with('&') && .key ne '&EXPORT' }`. `UNIT::` as a
# term was an EMPTY Stash — `UNIT::{"&x"}` looked symbols up fine, but
# enumerating found nothing, so nothing was exported, and `after(…)` in the
# importer fell to the infix of the same name (answering True).
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my sub after($s, $n) { "user-after" }
my sub other($s) { "user-other" }
my $subs = MY::.grep({ .key.starts-with('&') }).map(*.key).sort.list;
ck($subs, ('&after', '&ck', '&other'), "MY:: enumerates the scope's routines");
ck(MY::<&after>("a", "b"), 'user-after', 'and hands out the user sub, not the infix');
{
    my $inner = 1;
    ck(MY::.keys.grep('$inner').elems, 1, 'an inner scope sees its own');
    ck(MY::.keys.grep('&after').elems, 0, 'and not the outer one');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
