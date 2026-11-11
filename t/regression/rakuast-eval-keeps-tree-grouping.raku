# Regression: `RakuAST.EVAL` compiled the tree's deparsed text, and that
# text writes no parentheses, so a tree whose shape the text cannot carry
# came back as a different program: `(not 0) && (not 1)`, built as nodes,
# deparses to `not 0 && not 1`, which is `not (0 && not 1)` — True where
# the tree is False (issue #247). For `.EVAL` an operand that is itself an
# infix, prefix, list infix or ternary is now grouped; `.DEPARSE` still
# writes Rakudo's text, byte for byte.
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
use v6.e.PREVIEW;
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

sub pre($p, $o) { RakuAST::ApplyPrefix.new(prefix => RakuAST::Prefix.new($p), operand => $o) }
sub inf($l, $op, $r) { RakuAST::ApplyInfix.new(left => $l, infix => RakuAST::Infix.new($op), right => $r) }
sub lit($i) { RakuAST::IntLiteral.new($i) }

my $ast = inf(pre("not", lit(0)), "&&", pre("not", lit(1)));
check $ast.DEPARSE, 'not 0 && not 1', 'DEPARSE writes no parentheses';
check so($ast.EVAL), False, '(not 0) && (not 1) is False';
check inf(inf(lit(1), "+", lit(2)), "*", lit(3)).EVAL, 9, '(1 + 2) * 3';
check inf(inf(lit(1), "+", lit(2)), "*", lit(3)).DEPARSE, '1 + 2 * 3', '…deparsed as Rakudo does';
check inf(lit(2), "**", inf(lit(1), "+", lit(1))).EVAL, 4, '2 ** (1 + 1)';
check pre("-", inf(lit(1), "+", lit(2))).EVAL, -3, '-(1 + 2)';
check inf(lit(10), "-", inf(lit(3), "-", lit(2))).EVAL, 9, '10 - (3 - 2)';
my $list = RakuAST::ApplyListInfix.new(infix => RakuAST::Infix.new(","), operands => (lit(1), lit(2)));
check pre("+", $list).EVAL, 2, '+(1, 2)';
check inf(lit(1), "+", lit(2)).EVAL, 3, 'a flat one as before';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
