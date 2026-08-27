# `.trans(/\s+/ => ' ')` replaces every MATCH of the pattern. A Pair built from a
# regex key stringified it, so `.trans` read the pattern TEXT as a character set
# and rewrote the subject with it — `"a  b".trans(/\s+/ => ' ')` mangled rather
# than collapsed. A Regex key is preserved now, and applied as a global
# substitution before the char-by-char machinery sees the pairs.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

check (/ \s+ / => ' ').key.^name, 'Regex', 'a Pair keeps its Regex key';
check "this  sentence   no verb".trans(/ \s+ / => ' '), 'this sentence no verb',
      'a regex key collapses each run';
check "AB".trans(/a/ => 'x'),  'AB',  'a pattern that does not match leaves the string alone';
check "aaa".trans(/a/ => 'b'), 'bbb', 'every match is replaced, not just the first';
check "a1b2".trans(/\d/ => ''), 'ab', 'an empty replacement deletes';
check "xay".trans(/a/ => 'LONG'), 'xLONGy', 'a longer replacement';

# two regex pairs apply in order, each over the result of the last
check "a b".trans(/a/ => 'x', /b/ => 'y'), 'x y', 'two regex pairs';

# the char-by-char forms are untouched
check "abc".trans('abc' => 'xyz'), 'xyz', 'a plain Str=>Str pair still translates by char';
check "abc".trans('a..c' => 'A..C'), 'ABC', 'a range pair still works';
check "aabbcc".trans('abc' => 'xyz', :squash), 'xyz', ':squash still works';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
