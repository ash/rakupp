# Regression: once a program declared `multi sub infix:<OP>`, the
# one-argument form every built-in infix has was gone: `&infix:<->(10)`
# died with X::Multi::NoMatch instead of answering 10. The user's
# candidates own the name, so the dispatcher ran first and only fell back
# to the built-in for TWO arguments. The one-argument rule now has the same
# fallback; an operator the language does not have stays NoMatch.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged; every
# expectation was checked against Rakudo 2026.08 on 2026-10-19.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

class M { has $.c }
multi sub infix:<->(M $a, M $b) { M.new(c => $a.c - $b.c) }
multi sub infix:<(|)>(M $a, M $b) { "u" }
multi sub infix:<~>(M $a, M $b) { "cat" }
multi sub infix:<==>(M $a, M $b) { False }
multi sub infix:<qq>(Int $a, Int $b) { $a + $b }

ck((try &infix:<->(10)) // "DIED: " ~ $!.^name, 10, '&infix:<->(10) is 10');
ck((try &infix:<(|)>(1)) // "DIED: " ~ $!.^name, set(1), '&infix:<(|)>(1) coerces to a Set');
ck((try &infix:<~>(5)) // "DIED", "5", '&infix:<~>(5) stringifies');
ck((try &infix:<==>(3)) // "DIED", True, 'a chaining one is vacuously True');
ck(&infix:<->(10, 3), 7, 'two arguments still reach the built-in');
ck(&infix:<->(M.new(c => 5), M.new(c => 2)).c, 3, 'and the user candidate still wins for its types');
ck(&infix:<(|)>(M.new, M.new), "u", 'for each operator');
ck(([-] 10), 10, 'a one-element reduction');
ck((10,).reduce(&infix:<->), 10, 'and .reduce');
ck(&infix:<qq>(1, 2), 3, 'a user-only operator answers its own candidate');
ck((try &infix:<qq>(1)) // $!.^name, 'X::Multi::NoMatch', 'and has no one-argument form');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
