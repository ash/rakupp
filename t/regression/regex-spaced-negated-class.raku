# Regression: `<- print>` is `<-print>` — a blank may follow the negation sign.
#
# Whitespace is insignificant in a regex, and the bracket forms already knew it
# (`<- [a..z]>` was fixed for URI::Escape). The NAMED form did not: `<- print>`
# matched none of the branches and fell through to a generic assertion that
# matched EMPTY at every position. A negated class that matches everywhere is
# the worst possible failure for a substitution — `$str.subst(/<- print>/, …,
# :g)` replaced between every character instead of nowhere, so CSS::Writer's
# `write-string`, which escapes unprintables exactly that way, turned
# "Hello World!" into an escape sequence before each letter.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# the spaced spelling means what the tight one means
check so("H" ~~ / <- print> /),  False, 'a printable is not <- print>';
check so("H" ~~ / <-print> /),   False, '…the tight spelling agrees';
check so("\x[1]" ~~ / <- print> /), True, 'a control character is';
check so("H" ~~ / <- alpha> /),  False, '<- alpha> refuses a letter';
check so("1" ~~ / <- alpha> /),  True,  '…and admits a digit';

# the substitution that found it: nothing to replace, so nothing replaced
check "Hello World!".subst(/<- print>/, "X", :g), 'Hello World!',
      'a substitution over <- print> leaves a printable string alone';
check "a\x[1]b".subst(/<- print>/, "X", :g), 'aXb',
      '…and replaces exactly the unprintable';

# the positive and bracket forms it sits beside
check so("H" ~~ / <+print> /),   True,  '<+print> still matches';
check so("a" ~~ / <- [b]> /),    True,  'the spaced BRACKET form still works';
check so("b" ~~ / <- [b]> /),    False, '…and still excludes';
check so("H" ~~ / <-space-[\"]> /), True, 'a negated class composing with a bracket';
check so("x" ~~ / <-[a]> /),     True,  'a plain negated bracket';

# a user-declared token, negated with a blank
grammar G {
    token vowel { <[aeiou]> }
    token TOP   { <- vowel>+ }
}
check G.parse('xyz').defined, True,  'a spaced negated user token matches consonants';
check so(G.parse('xay')),     False, '…and refuses a vowel';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
