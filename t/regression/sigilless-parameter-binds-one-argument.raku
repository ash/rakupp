# Regression: under `--target=js` a sigilless parameter (`\b`) was bound as a
# CAPTURE of all the arguments — the parser spells a `|c` capture with the
# same sigil and marks it slurpy, and the emitter looked at the sigil alone —
# so `sub f(\b) { b² }` answered 0 and `\a, \b, \c` all held the whole
# argument list. A sigilless parameter is one argument, bound raw.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged; every
# expectation was checked against Rakudo 2026.09.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc - {$got.raku} vs {$want.raku}" }
}

sub one(\b) { b }
ck(one(-2), -2, 'one sigilless parameter is the argument');
sub sq(\b) { b ** 2 }
ck(sq(-2), 4, 'and computes with it');
sub disc(\a, \b, \c) { b ** 2 - 4 * a * c }
ck(disc(1, -2, 3), -8, 'three of them bind one argument each');
sub mixed(\a, $b) { "{a}-{$b}" }
ck(mixed(1, 2), "1-2", 'beside a sigilled one');
sub kind(\l) { l.WHAT.^name }
ck(kind((1, 2)), 'List', 'a list arrives whole and raw');
sub cap(|c) { c.elems }
ck(cap(1, 2, 3), 3, 'a capture still takes them all');
sub rest($x, |r) { r.list }
ck(rest(1, 2, 3), (2, 3), 'and the rest after a positional');
sub plus(+l) { l.elems }
ck(plus(1, 2, 3), 3, 'a sigilless slurpy takes the list');
ck(plus([4, 5]), 2, 'by the single-argument rule');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
