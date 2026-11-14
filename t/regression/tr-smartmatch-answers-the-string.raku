# `$x ~~ TR///` answers the transliterated Str, as Rakudo does (rakudo#1551):
# `'hello' ~~ TR/e/1/` was False, the result smartmatched against 'hello'.
# And a tr/// StrDistance shows as its constructor, as Rakudo's .gist and
# .raku do, while it still stringifies to the new string.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

check ('hello' ~~ TR/e/1/), 'h1llo', 'TR/// under ~~ (rakudo#1551)';
check ('ab' ~~ TR/ab/01/), '01', 'every character transliterated';
check (1 ~~ TR/\#//), '1', 'nothing to transliterate';
check ('hello' ~~ TR/e/1/).^name, 'Str', 'a Str';
my $s = 'abc';
my $d = ($s ~~ tr/a/A/);
check $s, 'Abc', 'tr/// still changes its target';
check $d.gist, 'StrDistance.new(before => "abc", after => "Abc")', 'a StrDistance\'s gist';
check $d.raku, 'StrDistance.new(before => "abc", after => "Abc")', '…and its .raku';
check "$d", 'Abc', 'it stringifies to the new string';
check +$d, '1', 'it numifies to the count';
check [$d].gist, '[StrDistance.new(before => "abc", after => "Abc")]', 'nested';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
