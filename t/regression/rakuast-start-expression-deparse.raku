# `.AST.DEPARSE` writes `start EXPR` as it was written: `my $p = start 5`
# came back as `my $p = start {⏎    5⏎}`, because the parser wraps the
# expression in a block, which the view rendered. A `start` whose block the
# parser made, holding one expression, is that expression again; a written
# block stays a block, and each round deparses the same.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

for Q[my $p = start 5], "my \$p = start 5\n",
    Q[await start 42], "await start 42\n",
    Q[my $x; my $q = start $x + 1], "my \$x;\nmy \$q = start \$x + 1\n",
    Q[start { 5 }], "start \{\n    5\n}\n",
    Q[start { say 1; 2 }], "start \{\n    say 1;\n    2\n}\n" -> $code, $want {
    ck $code.AST.DEPARSE, $want, $code;
    ck $code.AST.DEPARSE.AST.DEPARSE, $want, "$code, twice";
}

say 'PASS';
