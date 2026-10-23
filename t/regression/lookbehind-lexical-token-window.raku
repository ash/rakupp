# Regression: a lookbehind that called a LEXICAL token scanned back to the
# start of the string at every position — quadratic in the input.
#
# `<?after X>` tries start positions within X's possible width of the current
# one (Regex::nodeWidth). A call to a `my token` had no width the matcher knew,
# so the window was the whole prefix: `<?after ^ | <sp>>` with `my token sp {
# <[\s]> | '&nbsp;' }` cost 0.5 s per substitution on a 1,600-character text
# and grew with its square. wptexturize, ported to Raku in RakuPress, has six
# such substitutions, and one blog comment took 4.6 s to render. A lexical
# token's width is now read from the token's own pattern; a token whose body
# depends on runtime values is still taken as unbounded, as before.
#
# Correctness is checked against Rakudo 2026.09, but for the longer branch
# and the different widths: Rakudo matches a subrule inside `<?after …>`
# against the reversed text, so `<?after <two>> 'q'` passes 'baq' and not
# 'abq'. Those two hold the lookbehind's own definition, which the
# interpreter gives (and compiled code, since exe-lexical-token-calls.raku's
# fix). The time is checked by size, as t/scaling does it: 8x the text must
# cost well under 64x the time.
#
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku}, want {$want.raku}" }
}

my token sp { <[\r\n\t\x20\xA0]> | '&nbsp;' }
my token two { 'ab' | 'xyz' }

ck 'a - b -c'.subst(/ <?after ^ | <sp>> '-' <?before <sp>> /, '–', :g), 'a – b -c', '<?after ^ | <sp>>';
ck 'x-&nbsp;-y'.subst(/ <?after <sp>> '-' /, '=', :g), 'x-&nbsp;=y', 'the token\'s longer branch';
ck '-a b-'.subst(/ <!after <sp>> '-' /, '+', :g), '+a b+', '<!after <sp>>';
ck 'abq xyzq aq'.comb(/ <?after <two>> 'q' /).elems, 2, 'a token with branches of different widths';
ck 'q'.match(/ <?after <two>> 'q' /), Nil, 'and nothing to look back at';

sub elapsed(Int $n) {
    my $text = ('x - y "q" -- z ' x $n);
    my $t = now;
    $text.subst(/ <?after ^ | <sp>> '-' <?before <sp>> /, '–', :g) for ^3;
    now - $t;
}
elapsed(20);                                 # warm up
my $small = elapsed(100);
my $large = elapsed(800);
my $ratio = $large / ($small max 0.0005);
say "# 100 → {$small.round(0.001)}s, 800 → {$large.round(0.001)}s, ratio {$ratio.round(0.1)}";
ck $ratio < 24, True, 'the time grows linearly with the text';

say $fails ?? "FAIL: $fails" !! 'PASS';
exit $fails ?? 1 !! 0;
