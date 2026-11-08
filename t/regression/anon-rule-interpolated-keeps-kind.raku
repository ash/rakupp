# An anonymous `rule` or `token` keeps its adverbs where its source goes, as
# Rakudo's does: `my $r = rule { a b }; "x a b" ~~ /<$r>/` matched nothing,
# the rule spliced in without its sigspace, and a token backtracked into a
# `\d+`. And its `.raku` names its declarator, `rule { a b }`, where it was
# `rx/ a b /`. The value carries its kind; the `<$var>` assertion, a bare
# `$r` in a regex and `.raku` read it now.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $r = rule { a b };
ck ~("x a b" ~~ /<$r>/), 'a b', 'a rule as <$r> keeps its sigspace';
ck ~("x a b" ~~ / $r /), 'a b', 'and as a bare $r';
ck ("x ab" ~~ /<$r>/).so, False, 'which wants the space';
ck $r.raku, 'rule { a b }', "a rule's .raku";
my $t = token { \d+ 5 };
ck ("1235" ~~ /<$t>/).so, False, 'a token as <$t> ratchets';
ck ("1235" ~~ / $t /).so, False, 'and as a bare $t';
ck $t.raku, 'token { \d+ 5 }', "a token's .raku";
my $x = regex { \d+ 5 };
ck ~("1235" ~~ /<$x>/), '1235', 'a regex backtracks';
ck $x.raku, 'regex { \d+ 5 }', "a regex's .raku";
ck rx/ab/.raku, 'rx/ab/', "an rx's .raku";

say 'PASS';
