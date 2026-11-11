# Regression: issue #52 (2026-11-10).
#
#   `$l ~~ /^^ \s* '=begin tomty'/ ^fff^ $l ~~ /^^ \s* '=end tomty'/` died
#   with "Undefined routine 'fff^'": a regex literal did not count as a term,
#   so the carets of the operator after it were not read as part of it.
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

my @lines = 'a', '=begin tomty', 'b', 'c', '=end tomty', 'd', '  =begin tomty', 'e', '=end tomty';
sub between(&on) { my @o; for @lines -> $l { @o.push: $l if on($l) }; @o.List }

check between(-> $l { $l ~~ /^^ \s* '=begin tomty'/ ^fff^ $l ~~ /^^ \s* '=end tomty'/ }),
    ('b', 'c', 'e'), "Tomty's shape: ^fff^ between smartmatches on regex literals";
check between(-> $l { $l ~~ /^^ \s* "=begin tomty"/ ^fff^ $l ~~ /^^ \s* "=end tomty"/ }),
    ('b', 'c', 'e'), '…with double-quoted literals in the regexes';
check between(-> $l { $l ~~ /'=begin tomty'/ fff $l ~~ /'=end tomty'/ }),
    ('=begin tomty', 'b', 'c', '=end tomty', '  =begin tomty', 'e', '=end tomty'), 'fff keeps both ends';
check between(-> $l { $l ~~ /'=begin tomty'/ ^fff $l ~~ /'=end tomty'/ }),
    ('b', 'c', '=end tomty', 'e', '=end tomty'), '^fff drops the start';
check between(-> $l { $l ~~ /'=begin tomty'/ fff^ $l ~~ /'=end tomty'/ }),
    ('=begin tomty', 'b', 'c', '  =begin tomty', 'e'), 'fff^ drops the end';
check between(-> $l { $l ~~ /'=begin tomty'/ ^ff^ $l ~~ /'=end tomty'/ }),
    ('b', 'c', 'e'), '^ff^ after a regex literal';

check (1..10).grep({ $_ == 3 fff^ $_ == 6 }).List,  (3, 4, 5), 'fff^ on numbers';
check (1..10).grep({ $_ == 3 ^fff $_ == 6 }).List,  (4, 5, 6), '^fff on numbers';
check (1..10).grep({ $_ == 3 ^fff^ $_ == 6 }).List, (4, 5),    '^fff^ on numbers';
check <x a y b z>.grep({ /a/ ^fff^ /b/ }).List, ('y',), 'bare regex operands match $_';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
