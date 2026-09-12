# Regression: `visit-children` walks the tree the way Rakudo's does
# (RAKUAST-PLAN P4).
#
# One spec, two engines, proven by the diff being empty — the same shape the
# deparse and eval cases use. The spec is
# [tools/rakuast-visit-spec.raku](../../tools/rakuast-visit-spec.raku); run
# under Rakudo it produced
# docs/dev/findings/rakuast/visit-2026.08.txt, and run here it has to produce
# the same bytes.
#
# What is being pinned is the TRAVERSAL: each syntactic child handed to the
# callback exactly once, in source order, ONE level deep — and therefore that a
# walker's own `@*LINEAGE` reports the right ancestry. Rakudo does not maintain
# that dynamic itself (measured: it is unset inside `visit-children`), so the
# walker in the spec is the whole mechanism, and it is the shape ASTQuery's
# query engine is written in.
#
# When the oracle version moves: re-run the spec under the new Rakudo, commit
# the new file, and the diff against it is the work.
#
# Contract: exit 0 + last line PASS.
my $root = $?FILE.IO.parent.parent.parent;
my $spec = $root.add('tools/rakuast-visit-spec.raku');
my $want = $root.add('docs/dev/findings/rakuast/visit-2026.08.txt');

my @fail;
my $p = run($*EXECUTABLE, $spec.Str, :out, :err);
my $got-text = $p.out.slurp(:close);
my $err      = $p.err.slurp(:close);
@fail.push("the spec file did not run: exit {$p.exitcode}\n$err") if $p.exitcode != 0;

my @want = $want.lines;
my @got  = $got-text.lines;
# A walk is a SEQUENCE, so this one is compared position by position on
# purpose: a child visited in the wrong order, or an extra level of nesting, is
# exactly the kind of error a keyed comparison would hide.
for ^max(+@want, +@got) -> $i {
    my $w = @want[$i] // '(nothing)';
    my $g = @got[$i]  // '(nothing)';
    @fail.push("line {$i + 1}:\n     rakudo {$w.raku}\n     rakupp {$g.raku}") unless $w eq $g;
    last if @fail >= 6;
}

if @fail { .say for @fail; say "FAIL ({+@fail})"; exit 1 }
say "PASS";
