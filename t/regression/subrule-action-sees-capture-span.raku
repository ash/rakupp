# A subrule's action method sees the span its `<(` or `)>` marks, once: three
# of the seven places that trim a match to its capture markers tested the
# START marker to decide whether the END one was set, so a rule with `<(`
# and no `)>` handed its action a match ending at -1 — `token x { a <( b }`
# over "abc" called `method x` with "bc" from 1 to 0 — and then called it
# again with the right one; a rule with `)>` alone handed over the whole
# match first. Found by the review of src/
# (docs/dev/findings/src-review-2026-11-09.md, F1). Every line answers the
# same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @seen;
class Seen { method x($/) { @seen.push: "[$/] {$/.from}..{$/.to}" } }

grammar Open { token TOP { <x> c }; token x { a <( b } }
@seen = ();
ck ~Open.parse("abc", :actions(Seen))<x>, 'b', '<( alone: the match';
ck @seen, ['[b] 1..2'], '…and what its action saw, once';

grammar Close { token TOP { <x> c }; token x { a )> b } }
@seen = ();
ck ~Close.parse("abc", :actions(Seen))<x>, 'a', ')> alone: the match';
ck @seen, ['[a] 0..1'], '…and what its action saw, once';

grammar Both { token TOP { <x> c }; token x { a <( b )> } }
@seen = ();
ck ~Both.parse("abc", :actions(Seen))<x>, 'b', 'both markers';
ck @seen, ['[b] 1..2'], '…and its action';

grammar Back { regex TOP { <x> c }; regex x { a <( b } }
@seen = ();
ck ~Back.parse("abc", :actions(Seen))<x>, 'b', 'a backtracking rule';
ck @seen, ['[b] 1..2'], '…and its action';

say 'PASS';
