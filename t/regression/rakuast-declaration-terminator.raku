# Regression: `.AST.DEPARSE` ends an `enum` or `subset` declaration with `;`,
# so `.AST.EVAL` can parse what it rendered.
#
# Both rendered with a trailing newline, as if they were block statements. The
# statement list then took them for an `if` and left out the `;`, so the next
# statement followed on the next line as a second term:
#
#     subset Pos of Int where * > 0
#     my Pos $p = 5;
#
# and `.EVAL`, which runs the rendered text, failed with "Two terms in a row
# across lines". That is what kept L10N::BG, L10N::LV, L10N::RU and L10N::UK
# 0.0.2 from installing: their suites compile each fragment with
# `$code.AST("RU").EVAL`, and the subset and enum cases died there. Plain
# English reaches it the same way.
#
# Two smaller gaps beside it, in the same rendering: `my subset` and `my enum`
# lost their `my`, and `enum C (a => 1)` came back as `enum C ((a => 1))`.
#
# Every `want` is Rakudo 2026.08's `.AST.DEPARSE` (2026.09 agrees on each).
#
# Contract: exit 0 + last line PASS.
use experimental :rakuast;

my @fail;
sub check($got, $want, $what) {
    @fail.push("$what:\n     got  {$got.raku}\n     want {$want.raku}") unless $got eq $want
}

my @cases =
    Q[subset Pos of Int where * > 0; my Pos $p = 5; $p],
        "subset Pos of Int where * > 0;\nmy Pos \$p = 5;\n\$p\n", 5,
    Q[subset Pos of Int; 5],
        "subset Pos of Int;\n5\n", 5,
    Q[subset Pos where * > 0; 5],
        "subset Pos where * > 0;\n5\n", 5,
    Q[my $z = 1; subset P of Int; 2],
        "my \$z = 1;\nsubset P of Int;\n2\n", 2,
    Q[enum C <a b>; 3],
        "enum C <a b>;\n3\n", 3,
    Q[enum C (a => 1, b => 2); C::b.value],
        "enum C (a => 1, b => 2);\nC::b.value\n", 2,
    Q[my subset P of Int where * > 0; my P $x = 2; $x],
        "my subset P of Int where * > 0;\nmy P \$x = 2;\n\$x\n", 2,
    Q[my enum C <a b>; C::b.value],
        "my enum C <a b>;\nC::b.value\n", 1,
    # Last in the list: no `;`, as for any other final statement.
    Q[enum C <a b>],
        "enum C <a b>\n", Any,
    # A `where` block ends in `}`, so it takes a newline and no `;` — the rule
    # for every statement that ends in a brace.
    Q[subset Pos of Int where { $_ > 0 }; 5],
        "subset Pos of Int where \{\n    \$_ > 0\n}\n5\n", 5;

for @cases -> $src, $deparse, $value {
    my $ast = $src.AST;
    check $ast.DEPARSE, $deparse, "DEPARSE of {$src.raku}";
    next without $value;
    my $got = try $ast.EVAL;
    check $got // "died: {$!.message.lines[0]}", $value, "EVAL of {$src.raku}";
}

if @fail {
    note "FAIL: $_" for @fail;
    say "FAIL ({+@fail})";
    exit 1;
}
say "PASS";
