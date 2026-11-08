# `.AST.DEPARSE` of a `start` block leaves out the fresh `my $/; my $!` the
# parser gives it, which are implicit upstream: it wrote them out, and each
# round of `.AST.DEPARSE.AST.DEPARSE` added another pair. A declaration the
# program wrote stays.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck Q[my $p = start { 1 }].AST.DEPARSE, "my \$p = start \{\n    1\n}\n", 'one round';
ck Q[my $p = start { 1 }].AST.DEPARSE.AST.DEPARSE, "my \$p = start \{\n    1\n}\n", 'a fixed point';
ck Q[my $q = start { my $/; 2 }].AST.DEPARSE, "my \$q = start \{\n    my \$/;\n    2\n}\n", 'a written one stays';
ck (await EVAL Q[start { 7 }].AST.DEPARSE), 7, 'it still runs';

say 'PASS';
