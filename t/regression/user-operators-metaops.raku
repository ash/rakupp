# Regression: the metaoperators over a program's own infix — what the native
# and JavaScript backends REFUSE (the runtime's metaops apply the built-in),
# so these answers come from the interpreter whichever way the program is
# compiled: `--exe` bundles it, `--target=js` says which construct. The rest
# of the cases are in user-operators-compiled.raku, which compiles natively.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged; every
# expectation was checked against Rakudo 2026.09.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok — $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub infix:<->($a, $b) { "minus($a,$b)" }
ck(([-] 10, 3, 1), "minus(minus(10,3),1)", 'a reduction over a plain sub');
ck(((1, 2) Z- (3, 4)).List, ("minus(1,3)", "minus(2,4)"), 'Z-');
ck((1 R- 2), "minus(2,1)", 'R-');
ck(((1, 2) X- (3,)).List, ("minus(1,3)", "minus(2,3)"), 'X-');
ck(((1, 2) >>-<< (3, 4)).List, ("minus(1,3)", "minus(2,4)"), 'the hyper');

# a multi over the program's own class: the hyper takes its candidate
class P { has $.v }
multi infix:<+>(P $a, P $b) { P.new(v => $a.v + $b.v) }
ck(((P.new(v => 1), P.new(v => 2)) >>+<< (P.new(v => 3), P.new(v => 4))).map(*.v).List, (4, 6),
   'a hyper over a user multi');
ck(((1, 2) >>+<< (3, 4)).List, (4, 6), 'and core operands keep the built-in');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
