# Regression: `{ +(X) => v }` was a Block, not a Hash.
#
# braceLooksHash recognised a composer whose first key is a plain term or a
# postfix chain (`{ $d.name => 1 }`), and nothing else, so a key behind a
# prefix operator or in parentheses made the braces a block. DBDish::Oracle
# declares `constant %sqltype-map = { +(SQLT_CHR) => Str, … }`, which then
# died with "No such method 'Map' for invocant of type 'Block'" — and with it
# DBIish's own suite, so `rakupp install DBIish` refused to install it.
# Every answer below is Rakudo 2026.09's; the last ones must stay blocks.
#
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku}, want {$want.raku}" }
}

constant ONE = 1;
constant %by-code = { +(ONE) => 'one', +(2) => 'two' };
ck %by-code{1}, 'one', 'a constant hash keyed by +(CONSTANT)';
ck { +1 => 'a' }.^name, 'Hash', '{ +1 => … }';
ck { -(1) => 'a' }.^name, 'Hash', '{ -(1) => … }';
ck { ~(1) => 'a' }.^name, 'Hash', '{ ~(1) => … }';
ck { !0 => 'a' }.^name, 'Hash', '{ !0 => … }';
ck { (1) => 'a' }.^name, 'Hash', '{ (1) => … }';
ck { (1, 2) => 'a' }.^name, 'Hash', '{ (1, 2) => … }';
ck { +$_ => 1 }.^name, 'Block', 'a key that reads the topic is still a block';
ck { (1) + 2 }.^name, 'Block', 'parentheses with no arrow are a block';
ck { -1 }.^name, 'Block', 'a lone negative number is a block';

say $fails ?? "FAIL: $fails" !! 'PASS';
exit $fails ?? 1 !! 0;
