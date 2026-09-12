#!/usr/bin/env raku
# The `visit-children` spec (RAKUAST-PLAN P4) — ONE FILE, TWO ENGINES.
#
#     raku  tools/rakuast-visit-spec.raku     # the oracle
#     rakupp tools/rakuast-visit-spec.raku    # us
#     …and `t/regression/rakuast-visit.raku` diffs the two.
#
# What it pins is the TRAVERSAL, not the vocabulary: that the callback is
# handed each syntactic child exactly once, in source order, one level deep,
# and that a walker's own `@*LINEAGE` therefore reports the right ancestry.
# Rakudo does NOT maintain that dynamic itself — measured: it is unset inside
# `visit-children` — so the walker below is the whole mechanism, and it is the
# shape ASTQuery's query engine is written in.
#
# The programs are chosen to isolate the TRAVERSAL from the vocabulary, so two
# recorded differences stay out of them deliberately: a `VarDeclaration::Simple`
# holds its `desigilname` as a `Name` that Rakudo's own `visit-children` does not
# visit while our mechanical walk does, and a `Parameter` upstream carries the
# lexical declaration it implies, which a view of the syntax does not build.
# Both are measured by the tree oracle; neither is what this file is for. The
# `BEGIN` block has an empty body for the same reason — Rakudo RUNS it during
# `.AST` and we do not, which is a recorded divergence, not a traversal one.
use experimental :rakuast;

my @programs =
    'say 1',
    'say 1 + 2 * 3',
    'if 1 { say 2 } else { say 3 }',
    'for 1, 2 { say $_ }',
    'say "a{1+1}b"',
    '1 ?? 2 !! 3',
    'BEGIN { 1 }',
    'while 0 { say 1 }',
    'say [1, 2].map({ $_ })',
;

# One level only: the callback must be handed the CHILDREN, never a grandchild.
sub one-level(Mu $n) {
    my @k;
    $n.visit-children(-> Mu $c { @k.push($c.^name.subst('RakuAST::', '')) if $c.defined });
    @k
}

# …and the recursive walk, with the lineage the walker keeps itself. The
# `my @*LINEAGE` has to sit in a NESTED block: declaring it in the same scope
# that already read the outer one is "Illegal post-declaration of dynamic
# variable" upstream. ASTQuery's walker is written with the same nesting, which
# is not a coincidence — it is the only way to write it.
sub deep(Mu $n, @out) {
    my @lineage = (@*LINEAGE // ()).list;
    @out.push('  ' x @lineage.elems ~ $n.^name.subst('RakuAST::', ''));
    {
        my @*LINEAGE = $n, |@lineage;
        $n.visit-children(-> Mu $c {
            deep($c, @out) if $c.defined && (try $c.^name.starts-with('RakuAST::'))
        });
    }
}

for @programs -> $src {
    say "### $src";
    say "  children: " ~ one-level($src.AST.statements.head).join(', ');
    my @out;
    deep($src.AST, @out);
    .say for @out;
}
