# BROKE: leg 21 — stripping lexer-prepended adverbs (":P5 ") off regex token
# text consumed TIGHT INLINE adverbs too: in /:s^<alpha>.../ there is no
# trailing space, so the stripper ate the whole pattern and declared it a
# null regex (S05-metasyntax/repeat.t -> GONE at parse, caught by gate nx2).
# FIXED: same leg — only prefixes ending in ' ' (the lexer's format) strip.
# Locks that the tight-adverb form COMPILES and simple :s matching works.
my $rx = /:s^ <alpha> +% \, $/;
die 'tight inline adverb regex did not compile' unless $rx ~~ Regex;
die 'plain :s regex broken' unless so "a b" ~~ /:s a b/;
say 'PASS';
