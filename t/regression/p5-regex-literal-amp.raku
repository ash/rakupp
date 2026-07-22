# BROKE: leg 21 — the X::Syntax::Regex::NullRegex trailing-|/& heuristic
# fired on :P5 regexes where & and | are LITERAL characters:
# rx:P5/^[<>]&/ died at parse and took out all of S05-modifier/Perl_5.t
# (102/102 -> GONE, caught by gate nx1).
# FIXED: same leg — the branch heuristic skips P5 regexes. This locks that
# such literals COMPILE (matching semantics for these P5 shapes vary).
my $amp  = rx:P5/^[<>]&/;
my $pipe = rx:P5/a|$/;
die 'P5 trailing-& literal did not compile' unless $amp ~~ Regex;
die 'P5 alternation with trailing anchor broken' unless so "a" ~~ $pipe;
say 'PASS';
