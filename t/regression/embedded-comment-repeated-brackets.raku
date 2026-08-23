# Regression: an embedded comment opened with a REPEATED bracket
# (`#`{{ … }}`) closes only on the closer repeated as many times, and nesting
# counts only the full n-char sequences — a lone `{` inside `#`{{ … }}` is
# literal text, not a nest. Found by the full ecosystem sweep: Gnome::N
# comments out whole methods with `#`{{ … }}`, and the commented-out code
# carries unbalanced single braces (an `if … {` with its body elided), which
# the single-char depth count read as a nest that never closed — the file
# then "failed to parse" at end of file, 700 lines after the cause.
#
# Oracle-verified against Rakudo 2026.07 shape by shape.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

my @hit;

@hit.push(0);
#`{{
   commented-out code with an unbalanced brace:
   if $x ~~ m/ foo / {
}}
@hit.push(1);
#`{{ a {{ genuinely nested }} b }}
@hit.push(2);
#`{ single-brace form still nests on { singles } as before }
@hit.push(3);
#`(( doubled parens ( with a lone one ) inside ))
@hit.push(4);
#`[[ doubled squares [ lone ] inside ]]
@hit.push(5);
#`{{ a lone closer } is literal too }}
@hit.push(6);

ok(@hit.join eq '0123456', 'every statement between the comments ran');

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
