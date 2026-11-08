# An anonymous `rule { … }` kept in a variable matches with sigspace, and a
# `token { … }` with ratchet, as the literal itself does: `my $r = rule { a b };
# 'a b' ~~ $r` was Nil, and 'ab' matched. regexLitValue, which makes a written
# `/…/` on the right of `=` or in an argument list a Regex value, made the
# `rule` a plain `rx//` and dropped its kind. A routine's return value kept it.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub m($s, $rx) { my $m = $s ~~ $rx; $m ?? ~$m !! Nil }

my $r = rule { a b };
ck m('a b', $r), 'a b', 'a rule in a variable takes the space';
ck m('ab', $r), Nil, '…and needs it';
my @a = rule { a b };
ck m('a b', @a[0]), 'a b', 'in an array';
my %h = r => rule { a b };
ck m('a b', %h<r>), 'a b', 'in a hash';
my $b := rule { a b };
ck m('a b', $b), 'a b', 'bound';
ck m('a b', rule { a b }), 'a b', 'the literal (as before)';
sub f { rule { a b } }
ck m('a b', f()), 'a b', 'returned (as before)';

my $t = token { a* ab };
ck m('aaab', $t), Nil, 'a token in a variable ratchets';
my $q = regex { a* ab };
ck m('aaab', $q), 'aaab', 'a regex backtracks';
my $t2 = token { a \s* b };
ck m('a  b', $t2), 'a  b', 'a token without sigspace';

say 'PASS';
