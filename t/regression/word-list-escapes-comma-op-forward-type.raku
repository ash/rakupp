# Regression: three more from the Weekly Challenge sweep.
#
# 1. Inside a bare `< … >` word list, a BACKSLASH escapes the next character
#    (`\<` and `\>` are the literal angles, `\\` is one backslash, anything
#    else keeps both), and `{`/`}` are ordinary word characters. rakupp counted
#    an escaped `<` as a nesting level and bailed out of word mode on a brace,
#    so a list of ASCII punctuation ran off the end of the file — the error
#    surfaced 120 lines later as an unterminated single quote.
#
# 2. The COMMA as an APPLIED operator: `@a >>,<< @b` pairs the two sides up.
#
# 3. A class declared further down the file, used as a VALUE rather than as a
#    method invocant — `G.parse($s, :actions(actions))` — is created on first
#    use like any other forward reference. It used to resolve to a stub type
#    object, so the actions never fired and every `.made` was Nil.
#
# Contract: exit 0 + last line PASS.
# NB the checks below call `check(...)` with parentheses: a `< … >` word list as
# the first argument of a LISTOP call (`check < \< > …`) is a separate parse
# question that rakupp still gets wrong, and it is not what this file is about.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# --- 1. word-list escapes and braces ---
check(< \< >.raku,      '"<"',   'an escaped opening angle is a literal word');
check(< \> >.raku,      '">"',   '…and an escaped closing one');
check(< \\ >.raku,      '"\\\\"', '…and a doubled backslash is one');
check(< \x >.raku,      '"\\\\x"', 'an escape of anything else keeps both characters');
check(< { } >.elems,    2,       'braces are ordinary words');
check(<a { b }>.elems,  4,       '…in the middle of a list too');
check(<a b>.raku,       '("a", "b")', 'and a plain list is unchanged');
# the punctuation list that found it
my @p = < ` ~ ! @ # $ % ^ & * ( ) _ = + [ { ] } \\ | ; : ' " , \< . \> / ? >;
check(@p.elems, 31, 'the full ASCII punctuation list');
check(@p[*-1],  '?', '…ends where it should');
my %h; %h<a> = 1;
check(%h<a>, 1, 'a hash subscript still works');

# --- 2. the comma as an operator ---
check((<a b> >>,<< <c d>).map(*.join('')).join(' '), 'ac bd', 'a hyper comma pairs the sides');
check(([,] 1, 2, 3).raku, '(1, 2, 3)', 'the comma reduce is unchanged');
check(((1,2) Z, (3,4)).map(*.join('')).join(' '), '13 24', '…and so is Z,');

# --- 3. a forward-declared class used as a value ---
check(use-actions(), '#[F4][B2]', 'a class named below is usable as a value above');

grammar Colour {
    token TOP { '#' <hex> <hex> }
    token hex { <xdigit> <xdigit> }
}
class Marker { method hex($/) { make '[' ~ $/.Str ~ ']' } }
sub use-actions() { [~] flat '#', Colour.parse('#F4B2', :actions(Marker))<hex>>>.made }

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
