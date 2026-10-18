# A colon call whose first argument is an integer literal followed by `<`:
# `.grep: 60 < * < 70` compares. The lexer's radix-colonpair rule (`:16<FF>`,
# where the angle content is digits, not code) fired on the method colon too,
# because it only asked "is the token before the integer a `:`" — so the
# spaced `: 60 <` read as `:60<…>` and everything up to the next `>` became a
# word list ("couldn't find final '>'", or a stray-operator error when a `>`
# turned up later on the line). The raku-course's advanced/colon-calls page
# shows `say (('a' .. 'z')».uc».ord.grep: 60 < * < 70)».chr`.
#
# A radix pair is glued to its colon; a colon call takes whitespace after it.
#
# Every expectation was checked against Rakudo v2026.09 (2026-10-18).
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

check (('a' .. 'z')».uc».ord.grep: 60 < * < 70)».chr, <A B C D E>, 'the course example';
check ((1, 2, 80).grep: 0 < * < 70).List, (1, 2), 'a chained comparison after the colon';
check ((1, 2, 80).grep: 1 < *).List, (2, 80), 'a single comparison after the colon';
my @p; @p.push: 1 < 2;
check @p.List, (True,), 'two literals compared';
check ((5, 50).grep: 10 <= * <= 60).List, (50,), '<= as well';

# the radix forms the rule exists for still read as radix numbers
check :16<FF>, 255, ':16<FF> is still a radix number';
check :2<1010>, 10, ':2<1010> too';
check :2<1.1*10**10>, 15000000000.0, 'and the exponent form';
check (1, 255).grep(:16<FF>).List, (255,), 'a radix number as a call argument';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
