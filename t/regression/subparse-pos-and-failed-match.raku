# `.parse` and `.subparse` start at `:pos`, and a failed `.subparse` answers a
# failed Match, as Rakudo does (rakudo#1243): `:pos` was ignored, so
# `G.subparse("ABCDE", :pos(4))` matched all five letters, and a failed
# subparse was Any.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

grammar G { token TOP { <[A..Z]>+ } }

check G.subparse("ABCDE", :pos(4)).gist, '｢E｣', ':pos (rakudo#1243)';
check G.subparse("ABCDE", :pos(2)).from, '2', '…the match starts there';
check G.parse("ABCDE", :pos(2)).gist, '｢CDE｣', '.parse takes :pos too';
check G.subparse("xAB", :pos(1)).Str, 'AB', 'a :pos past a prefix it cannot match';
check G.parse("ABCDE", :pos(10)).gist, 'Nil', 'a :pos past the end fails';
grammar U { token TOP { \w+ } }
check U.subparse("äöü", :pos(1)).gist, '｢öü｣', ':pos counts characters';

my $f = G.subparse("abc");
check $f.gist, '#<failed match>', 'a failed subparse';
check $f.^name, 'G', '…is the grammar\'s match type';
check $f.Bool, 'False', '…false';
check $f.defined, 'True', '…but defined';
check "$f.from() $f.to() $f.pos()", '0 -3 -3', '…with Rakudo\'s positions';
check "[$f]", '[]', '…and no text';
check G.subparse("abc", :pos(1)).from, '1', 'a failed one from :pos';
G.subparse("abc");
check $/.gist, '#<failed match>', '$/ holds it';
check G.parse("abc").gist, 'Nil', 'a failed .parse is still Nil';
check G.subparse("ABx").Str, 'AB', 'a successful subparse is unchanged';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
