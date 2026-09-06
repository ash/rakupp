# Regression: the Grand Review, batch E — the regex engine
# (docs/dev/findings/REVIEW-GRAND.md). Every case is what Rakudo answers; each
# used to be a silent wrong answer here.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}
sub dies(&code) { my $lived = False; try { code(); $lived = True }; !$lived }

# 1. Escapes inside a character class are the classes, not the letters.
check(?("h" ~~ /<[\h]>/),        False, '<[\h]> is not the letter h');
check(?(" " ~~ /<[\h]>/),        True,  '<[\h]> is horizontal whitespace');
check(?(" " ~~ /<-[\h\v]>/),     False, '<-[\h\v]> excludes a space');
check(?("x" ~~ /<-[\h\v]>/),     True,  '…and admits a letter');
check(?("\n" ~~ /<[\v]>/),       True,  '<[\v]> is vertical whitespace');
check(?("e" ~~ /<[\e]>/),        False, '<[\e]> is not the letter e');
check(?("\e" ~~ /<[\e]>/),       True,  '<[\e]> is ESC');

# 2. \h, \v and \N beyond ASCII.
check(?("\x[A0]" ~~ /\h/),       True,  '\h matches NBSP');
check(?("\x[A0]" ~~ /\H/),       False, '\H does not');
check(?("\x[3000]" ~~ /\h/),     True,  '\h matches an ideographic space');
check(?("\x[2028]" ~~ /\v/),     True,  '\v matches LINE SEPARATOR');
check(?("\x[85]" ~~ /\v/),       True,  '\v matches NEL');
check(?("\x[85]" ~~ /\N/),       False, '\N is the complement of \n (NEL is a newline)');
check(?("\x[B]" ~~ /\N/),        False, '…VT too');
check(?("x" ~~ /\N/),            True,  '\N matches a letter');

# 3. `|` binds tighter than `||`.
check(("ab" ~~ / a | ab || c /).Str,   'ab',  'a | ab || c: the LTM half still ranks');
check(("abc" ~~ / a | ab || abc /).Str, 'ab', '…and the || group is tried after');
check(("c" ~~ / a | ab || c /).Str,    'c',   '…the second group when the first fails');
check(("abc" ~~ / a || ab | abc /).Str, 'a',  'a || [ab | abc]: first-match at the || level');

# 4. `.` in a single-character rule matches a newline and keeps CRLF whole.
grammar GDot { token TOP { <c>+ }; token c { . } }
check(GDot.parse("a\nb").defined,       True, 'token c { . } matches a newline');
check(GDot.parse("a\r\nb")<c>.elems,    3,    '…and CRLF is one character');

# 5. ^^ and $$ around a trailing newline.
check("a\n".match(/^^/, :g).elems,     1, '^^ does not match after a final newline');
check("a\n".match(/$$/, :g).elems,     1, '$$ matches once before the final newline');
check("a\nb\n".match(/^^/, :g).elems,  2, '…two lines, two ^^');
check("a\nb".match(/$$/, :g).elems,    2, '…and $$ at a bare end');

# 6. Inline :r / :ratchet.
check(?("aab" ~~ /:r a+ b/),           True,  ':r is :ratchet');
check(?("aaab" ~~ /:ratchet a+ ab/),   False, ':ratchet forbids the give-back');

# 7. Unknown escapes and subrules are errors, not quiet matches.
check(dies({ EVAL 'so "y" ~~ /\y/' }),            True, '/\y/ is a compile error');
# (`<nosuch>` still matches the empty string here: Rakudo built-ins we lack, `<commit>`
#  among them, take the same path — the loud form killed S05-mass/rx.t. Ledger L8 F27.)

# 8. A `die` inside a regex block leaves the parse.
check(dies({ "ab" ~~ / a { die "inner" } b / }),        True, 'die in a block');
check(dies({ "ab" ~~ / a <?{ die "in-assert" }> b / }), True, 'die in an assertion');
grammar GDie { token TOP { a { die "in-token" } b } }
check(dies({ GDie.parse("ab") }),                        True, 'die in a token');

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
