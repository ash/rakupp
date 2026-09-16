# Regression: Str.split on a regex that matches zero-width must not eat the
# character at the split point.
#
# The scan has to move past a zero-width hit or it matches the same spot for
# ever — but the PIECE still starts there. The implementation used one cursor
# for both, so every zero-width split point cost a character:
# "abcd".split(/""/) answered five empty strings, and String::CamelCase, whose
# wordsplit is a chain of `<?after …> <?before …>` lookarounds, turned
# "YearBBS" into "Year BS" and "ClientADClient" into "Client D lient".
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# The empty pattern splits between every character, keeping both edges.
check "abcd".split(/""/).List, ("", "a", "b", "c", "d", ""), 'empty regex keeps every character';
check "abc".split("").List,    ("", "a", "b", "c", ""),      'and the string form agrees';
check "".split(/""/).List,     ("", ""),                     'the empty subject splits once';

# A lookahead is the zero-width case that matters in the wild: the piece after
# the split point begins WITH the character that was looked at.
check "abc".split(/<?before b>/).List,  ("a", "bc"),  'lookahead keeps its character';
check "abcd".split(/<?before c>/).List, ("ab", "cd"), 'and does so mid-string';
check "x".split(/<?before x>/).List,    ("", "x"),    'even at offset zero';

# A pattern that CAN match empty must still consume when it matches wide.
check "aXXb".split(/X*/).List, ("", "a", "", "b", ""), 'a starred separator is wide where it can be';

# Non-zero-width separators were always right; they must stay right.
check "aXbXc".split(/X/).List,    ("a", "b", "c"),     'plain separators unchanged';
check "a,b,,c".split(/","/).List, ("a", "b", "", "c"), 'including an empty piece between two';

# The limit counts pieces, not matches, and cuts the same way here.
check "abc".split(/""/, 3).List,   ("", "a", "bc"), 'a limit stops the scan';
check "a1b2c".split(/\d/, 2).List, ("a", "b2c"),    'and does so for wide separators';

# :skip-empty drops the edges the empty pattern produces.
check "abc".split(/""/, :skip-empty).List, ("a", "b", "c"), 'skip-empty removes both edges';

# The case this was found in: String::CamelCase's wordsplit, whole.
sub wordsplit(Str $given) {
    $given.split(/
        <[_ \- \s]>+
        | <?after <-[A..Z]>> <?before <:Lu>>
        | <?after <:Lu>> <?before <:Lu> <:Ll>>
    /, :skip-empty).List
}
check wordsplit("YearBBS"),        ("Year", "BBS"),          'an acronym run stays whole';
check wordsplit("ClientADClient"), ("Client", "AD", "Client"), 'and splits before the trailing word';
check wordsplit("a-b c_d"),        ("a", "b", "c", "d"),     'the wide alternative still fires';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
