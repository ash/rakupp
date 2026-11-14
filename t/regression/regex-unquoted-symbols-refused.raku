# A symbol that is no regex syntax must be quoted to match literally, as in
# Rakudo (rakudo#3775): `/a,b/`, `/a=b/`, `/a-b/`, a backtick and symbols
# beyond ASCII were matched as themselves. `=` still aliases a capture
# (`$<x>=`), not an assertion (`<digit>=`, rakudo#4927).

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want.any { say "FAIL: $what: got $got, want {$want.join(' or ')}"; $fails++ }   # (a want may list alternatives)
}
sub refused($code) { try EVAL $code; $! ?? $!.^name.subst(/'+{' .* $/, '') !! 'ok' }

for <, = - ` … → € © ¿ ½ ✓> -> $c {
    check refused("'a{$c}b' ~~ /a{$c}b/"), <X::Syntax::Regex::UnrecognizedMetachar X::Comp::Group>,
          "a bare $c (rakudo#3775)";
}
check refused(q['123' ~~ m/ ^ <digit>=(\d)+/]), <X::Syntax::Regex::UnrecognizedMetachar X::Comp::Group>,
      '<digit>= (rakudo#4927)';
check ('ab' ~~ / $<x>=\w /).Str, 'a', '$<x>= aliases a capture';
check ('ab' ~~ / @<x>=\w+ /).Str, 'ab', '@<x>= too';
check ('a,b' ~~ / a ',' b /).Str, 'a,b', 'a quoted comma';
check ('a=b' ~~ / a \= b /).Str, 'a=b', 'an escaped =';
check ('x→y' ~~ / x '→' y /).Str, 'x→y', 'a quoted arrow';
check ('aé' ~~ / aé /).Str, 'aé', 'a letter beyond ASCII';
check ('a word' ~~ / «word» /).Str, 'word', '« » are word boundaries';
check ('1,2' ~~ / \d+ % ',' /).Str, '1,2', 'a separator';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
