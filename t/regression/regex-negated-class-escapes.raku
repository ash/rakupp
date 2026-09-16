# Regression: `\S` `\D` `\W` `\N` inside a character class, and a SPACED set
# operator that must keep the class open.
#
# Two faults that met in one line of Net::HTTP::URL:
#
#     token path { <+[\S] -[?#]>* }
#
# 1. A negated escape inside a class fell through to the "escaped punctuation"
#    arm and matched the LETTER: `<[\S]>` matched "S" and nothing else. The
#    Node's classFlags has carried "uppercase = negated" in its declaration all
#    along and LtmNfa already read it that way; the byteset builder and the
#    multibyte arm never had.
#
# 2. The `[` of a set operator only opened a class when the character before the
#    `-`/`+` was `<` or `]` — with a blank between, ` -[#]` read as a plain
#    group, which left the `#` free to open a comment that ate the token's
#    closing brace. The error then surfaced at end of file, 30 lines away.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# a negated escape is a POSITIVE member meaning "anything this does not match"
check so("a" ~~ / <[\S]> /),      True,  '<[\S]> matches a non-space';
check so(" " ~~ / <[\S]> /),      False, '…and not a space';
check so("a" ~~ / <-[\S]> /),     False, 'negating it again flips back';
check so(" " ~~ / <-[\S]> /),     True,  'and admits the space';
check so("a" ~~ / <+[\D]> /),     True,  '\D in a class';
check so("5" ~~ / <+[\D]> /),     False, '…refuses a digit';
check so("x" ~~ / <[\N]> /),      True,  '\N is not-a-newline';
check so("\n" ~~ / <[\N]> /),     False, '…and refuses one';
check so("é" ~~ / <[\S]> /),      True,  'and it works past ASCII';

# the lowercase family it sits beside must keep working
check so(" " ~~ / <[\s]> /),      True,  '\s still matches a space';
check so("1" ~~ / <+[\d]> /),     True,  '\d still matches a digit';

# additive then subtractive, which is the shape that found it
check so("a" ~~ / <+[\S] -[#]> /),  True,  'a non-space that is not #';
check so("#" ~~ / <+[\S] -[#]> /),  False, '…and # itself is excluded';
check ("a#b" ~~ / <+[\S] -[#]>+ /).Str, 'a', 'the class stops at the excluded member';

# …written inside a TOKEN, where the closing brace is what the comment ate
grammar Pathish {
    token TOP   { <path> }
    token path  { <+[\S] -[?#]>* }
}
check Pathish.parse('/a/b').defined, True,  'a token may carry a spaced set operator';
check so(Pathish.parse('/a?q')),     False, '…and the excluded members still bind';

grammar Named {
    token TOP { <+alnum - [#]>+ }     # blanks on BOTH sides of the operator
}
check Named.parse('abc').defined, True, 'a named member with a spaced operator too';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
