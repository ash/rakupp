# BROKE: `--highlight` read names in ASCII only, and let a `-` or `'` join a
# name before anything alphanumeric. So `$соу` was a lone `$` and plain text,
# `née` three spans, and `$a̲` lost its mark; `$n-1` was ONE variable where
# Raku reads `$n - 1`; and in `1&'a'` the `&'a` was a variable, so the string
# that followed ran on to the next quote mark, lines later. `qq:!c/…/` took
# the `!` of the negated adverb for the opening delimiter, with the same
# result. FastRoast, which edits Roast files through these spans, renamed code
# it took for a string.
# FIXED: a name starts with a letter the UCD calls one (L*, Nl) and continues
# with letters, digits and combining marks; a `-` or `'` joins it only inside
# and only before a letter, as the lexer joins them; a quote adverb may be
# negated.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub hl(Str $code) {
    my $p = run($*EXECUTABLE, '--highlight', '-e', $code, :out, :err);
    my $o = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $o
}
sub spans(Str $code, Str $want, Str $what) {
    my $got = hl($code);
    @fail.push("$what: {$want.raku} missing from {$got.trim.raku}")
        unless $got.contains($want);
}

# names in other scripts are names
spans 'my $соу = 1', '<span class="nv">$соу</span>', 'a Cyrillic variable';
spans 'sub née() { }', '<span class="n">née</span>', 'a sub with a precomposed letter';
spans 'my @𝐚 = 1', '<span class="nv">@𝐚</span>', 'an astral letter';
spans 'my $a̲ = 2', '<span class="nv">$a̲</span>', 'a combining mark continues a name';
spans 'my $変数 = 2', '<span class="nv">$変数</span>', 'CJK';
spans 'say $x×3', '<span class="nv">$x</span>×', 'an operator symbol is not a letter';
spans 'my $x²', '<span class="nv">$x</span>²', 'nor is a superscript';

# `-` and `'` join only before a letter
spans 'say $n-1', '<span class="nv">$n</span>-<span class="mi">1</span>', '$n-1 is $n - 1';
spans 'say foo-1', '<span class="n">foo</span>-<span class="mi">1</span>', 'foo-1 is foo - 1';
spans ｢say $don't｣, ｢<span class="nv">$don't</span>｣, 'an apostrophe inside a name';
spans 'my $from-json', '<span class="nv">$from-json</span>', 'a hyphen inside a name';
spans ｢say 1&'a'; say 'z'｣, ｢<span class="s">'a'</span>｣, 'an & before a quote is a junction, not a sigil';

# a negated quote adverb
spans 'my $x = qq:!c/a {1} b/; say "z!"', '<span class="s">qq:!c/a {1} b/</span>', 'qq:!c/…/';

if @fail {
    note "FAILED: @fail.join('; ')";
    say 'FAIL';
}
else {
    say 'PASS';
}
