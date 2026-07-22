# BROKE (the other direction): before leg 24, `say *.abs` / `map *.abs, @a`
# parsed as MULTIPLICATION of the bareword (say * .abs on $_) — found via
# --ast while chasing whatever.t. The fix (startsListopArg accepts `*`
# followed by a tight .method) must never regress ordinary infix `*`.
# FIXED: 8ba1907.
my @a = map *.abs, 1, -2, 3, -4;
die "map *.abs broken: {@a.raku}" unless @a eqv [1, 2, 3, 4];
die '*.abs ~~ Code broken' unless *.abs ~~ Code;
my $x = 4;
die 'infix * broken after listop fix' unless 2 * 3 == 6 and $x * 2 == 8;
die 'whatever slice broken' unless (1, 2)[*-1..*].tail == 2;
say 'PASS';
