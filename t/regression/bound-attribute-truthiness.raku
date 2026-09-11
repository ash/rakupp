# Regression: a `:=`-bound attribute reads through its container in boolean
# context, `Rakudo::Iterator.OneValue`/`.Empty` exist, and `…` in a `< … >`
# word list is the character, not the `...` operator.
#
# paths binds every option straight into an attribute (`$!recurse :=
# $recurse`); with `$recurse` undefined, `$!recurse || $!dir-accepts-files`
# was TRUE — the bound slot's container was true as a Hash — so a rejected
# directory was still descended. The same walker answers a lone file through
# `Rakudo::Iterator.OneValue($path)`. String::Utils' suite spells its
# shortened strings `<f…z fo…az>` and asks their `.chars`; the lexer's
# ellipsis-to-`...` alias ran inside the word list and made them longer.
#
# Every expectation was checked against Rakudo.

use nqp;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

class W {
    has $!recurse;
    has $!accepts;
    method !set($r) { $!recurse := $r; $!accepts := False; self }
    method new($r) { nqp::create(self)!set($r) }
    method descend() { ($!recurse || $!accepts) ?? 'yes' !! 'no' }
    method both() { ($!recurse && $!accepts) ?? 'yes' !! 'no' }
}
ck(W.new(Any).descend, 'no',  'a bound undefined attribute is false in ||');
ck(W.new(True).descend, 'yes', 'and a bound True is true');
ck(W.new(True).both, 'no',    '&& reads through the container too');

ck(Seq.new(Rakudo::Iterator.OneValue(42)).List, (42,), 'Rakudo::Iterator.OneValue');
ck(Seq.new(Rakudo::Iterator.Empty).elems, 0, 'Rakudo::Iterator.Empty');

ck(<f…z fo…az>.map(*.chars).List, (3, 5), 'an ellipsis inside a word list is one char');
ck(<f…z>[0], "f…z", 'and stays the ellipsis');
ck((1 … 3).List, (1, 2, 3), 'the sequence operator spelled … still works');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
