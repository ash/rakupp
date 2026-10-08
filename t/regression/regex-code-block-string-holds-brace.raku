# `/ a { $x = '}' } b /` — a `}` in a string inside a regex's code: a block,
# `<?{ }>`, `<!{ }>`, `<{ }>`, `** { }`.
#
#   Each of the engine's block scanners counted braces one by one, so the `}`
#   in `'}'` ended the block: its code was cut to `$x = '` and failed to parse
#   when the match ran ("Unable to parse expression in single quotes"), and a
#   grammar token simply failed to match. The interpolation pass read the same
#   brace as the block's end and pasted variables after it into the code. One
#   scanner (Regex::codeBlockEnd) now reads the block as Raku: through its
#   strings, its `#` comments and a nested regex's character class, with an
#   apostrophe inside a name (`don't`) no quote at all — which the lexer did
#   not know either, and lost the closing `/`.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

sub ev($code) { my $r is default(Nil) = try EVAL $code; $! ?? $!.^name !! $r }

check ev(q[so 'ab' ~~ / a { my $x = '}' } b /]),               True, 'a block';
check ev(q[so 'ab' ~~ / a <?{ '}' eq '}' }> b /]),             True, '<?{ }>';
check ev(q[so 'ab' ~~ / a <!{ '}' ne '}' }> b /]),             True, '<!{ }>';
check ev(q[so 'ab' ~~ / a <{ '}' eq '}' ?? 'b' !! 'c' }> /]),  True, '<{ }>';
check ev(q[so 'aab' ~~ / a ** { '}'.chars + 1 } b /]),         True, '** { }';
check ev(q[my $n = 0; 'ab' ~~ / a { $n = '}'.chars } b /; $n]), 1,  'the block runs';
check ev(q[grammar G { token TOP { a { my $x = '}' } b } }; so G.parse('ab')]), True, 'in a token';
check ev(q[grammar H { token TOP { a <?{ '{' ne '}' }> b } }; so H.parse('ab')]), True, '<?{ }> in a token';
# the interpolation pass leaves the block's code alone
check ev(q[my $y = 'Y'; my $o; 'ab' ~~ / a { $o = '}' ~ $y } b /; $o]), '}Y', 'a $var after the brace';
check ev(q[my $x = 'q'; so '{q' ~~ / '{' $x /]),                True, "'\{' then a \$var";
# comments, a nested class, a name with an apostrophe
check ev(qq[so 'ab' ~~ / a \{ # a \} in a comment\n 1 } b /]),  True, 'a # comment';
check ev(q[so 'ab' ~~ / a {#`( a } embedded ) 1 } b /]),        True, 'an embedded comment';
check ev(q[so 'a"}' ~~ / a { $/.orig ~~ / <["}]> / } .. /]),    True, 'a nested character class';
check ev(q[my \don't = 5; my $z = 0; 'ab' ~~ / a { $z = don't } b /; $z]), 5, "don't";
check ev(q[my \don't = 6; my $z = 0; 'ab' ~~ rx{ a { $z = don't } b }; $z]), 6, "don't in rx\{\}";
check ev(q[my \don't = 7; my $z = 0; my token t { a { $z = don't } b }; 'ab' ~~ /<t>/; $z]), 7, "don't in a token";
check ev(q[so 'ab' ~~ / a { my $q = “}” } b /]),                True, 'a } in “…”';
check ev(q[so 'a}b' ~~ /"{ “a}b” }"/]),                         True, 'a } in “…” in a quoted atom';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
