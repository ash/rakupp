# A ｢…｣ quote in a regex is a literal, whatever brackets it holds, and
# `<&?BLOCK>` calls the regex itself, as in Rakudo (rakudo#3775): the issue's
# `[｢{｣ <&?BLOCK>? ｢}｣]?` failed to parse ("couldn't find final ')'"), the
# `?` of `&?BLOCK` read as a quantifier, and `<&?BLOCK>` matched nothing.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

check ('a(b' ~~ / a ｢(｣ b /).Str, 'a(b', 'a ( in corner quotes';
check ('a)b' ~~ / a ｢)｣ b /).Str, 'a)b', 'a )';
check ('a[b' ~~ / a ｢[｣ b /).Str, 'a[b', 'a [';
check ('a{b}' ~~ / a ｢{｣ b ｢}｣ /).Str, 'a{b}', 'a { and a }';
check ('xx' ~~ / [｢x｣+] /).Str, 'xx', 'a quantified corner quote';
check ('((()))' ~~ / '(' <&?BLOCK>? ')' /).Str, '((()))', '<&?BLOCK> recurses';
check ('[[x]]' ~~ / '[' [ <&?BLOCK> | x ] ']' /).Str, '[[x]]', '…in an alternation';
check ('A{B}A' ~~ / (\w) [｢{｣ <&?BLOCK>? ｢}｣]? /).Str, 'A{B}', 'the issue\'s shape (rakudo#3775)';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
