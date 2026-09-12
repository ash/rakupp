# Regression: the `Doc::` view — `.rakudoc` and the pod subtree
# (RAKUAST-PLAN P5).
#
# One spec, two engines, proven by the diff being empty — the same shape the
# deparse, eval and visit cases use. The spec is
# [tools/rakuast-doc-spec.raku](../../tools/rakuast-doc-spec.raku); run under
# Rakudo it produced docs/dev/findings/rakuast/doc-2026.08.txt, and run here it
# has to produce the same bytes.
#
# The view is over rakupp's OWN pod DOM — the one `$=pod` has always answered,
# which for a document gives the same classes in the same order as Rakudo's.
# So this is a view over a parse, exactly as P1 is, and not a second pod parser.
#
# The spec compares SHAPE, not text: Rakudo keeps a block's raw source
# including trailing newlines and our pod DOM trims them. That is a difference
# in the pod parser, not in the view, and it is recorded in the plan rather
# than pinned here.
#
# Contract: exit 0 + last line PASS.
my $root = $?FILE.IO.parent.parent.parent;
my $spec = $root.add('tools/rakuast-doc-spec.raku');
my $want = $root.add('docs/dev/findings/rakuast/doc-2026.08.txt');

my @fail;
my $p = run($*EXECUTABLE, $spec.Str, :out, :err);
my $got-text = $p.out.slurp(:close);
my $err      = $p.err.slurp(:close);
@fail.push("the spec file did not run: exit {$p.exitcode}\n$err") if $p.exitcode != 0;

my @want = $want.lines;
my @got  = $got-text.lines;
for ^max(+@want, +@got) -> $i {
    my $w = @want[$i] // '(nothing)';
    my $g = @got[$i]  // '(nothing)';
    @fail.push("line {$i + 1}:\n     rakudo {$w.raku}\n     rakupp {$g.raku}") unless $w eq $g;
    last if @fail >= 6;
}

if @fail { .say for @fail; say "FAIL ({+@fail})"; exit 1 }
say "PASS";
