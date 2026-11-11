# Regression: issue #142 (2026-11-10).
#
# A name captured more than once -- `<n> [ ',' <n> ]*` -- is a list of
# matches. A code block saw that list; a code assertion, `<?{ }>` or `<!{ }>`,
# saw only the last match, so `<!{ $<n>.list.first(...) }>` refused nothing.
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

grammar T {
    token in-assertion  { <n> [ ',' <n> ]* <?{ $*SEEN = $<n>.map(*.Str).List; True }> }
    token in-code-block { <n> [ ',' <n> ]* { $*SEEN = $<n>.map(*.Str).List } }
    token count-assert  { <n> [ ',' <n> ]* <?{ $<n>.elems == 3 }> }
    token refuse-a      { <n> [ ',' <n> ]* <!{ $<n>.list.first({ .Str eq 'a' }) }> }
    token refuse-a-dq   { <n> [ ',' <n> ]* <!{ $<n>.list.first({ .Str eq "a" }) }> }
    token need-a        { <n> [ ',' <n> ]* <?{ $<n>.list.first({ .Str eq 'a' }) }> }
    token n { \w }
}

my $*SEEN;
T.parse('a,b,c', :rule<in-assertion>);
check $*SEEN, <a b c>, '$<n> in <?{ }> is every match';
$*SEEN = Nil;
T.parse('a,b,c', :rule<in-code-block>);
check $*SEEN, <a b c>, '$<n> in { } is every match';
check so(T.parse('a,b,c', :rule<count-assert>)), True, '$<n>.elems in an assertion counts all three';
check so(T.parse('a,b',   :rule<count-assert>)), False, '...and two is not three';

for 'a', 'b,a', 'a,b', 'b,a,c' -> $s {
    check so(T.parse($s, :rule<refuse-a>)),    False, '<!{ }> refuses ' ~ $s;
    check so(T.parse($s, :rule<refuse-a-dq>)), False, '<!{ }> refuses ' ~ $s ~ ' ("a")';
    check so(T.parse($s, :rule<need-a>)),      True,  '<?{ }> takes ' ~ $s;
}
for 'b', 'b,c', 'c,d,e' -> $s {
    check so(T.parse($s, :rule<refuse-a>)), True,  '<!{ }> takes ' ~ $s;
    check so(T.parse($s, :rule<need-a>)),   False, '<?{ }> refuses ' ~ $s;
}

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
