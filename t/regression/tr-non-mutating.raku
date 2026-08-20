# Regression: `TR///` — the non-mutating transliteration, as `S///` is to
# `s///`. It was not recognised at all: `TR/a..j/0..9/` parsed as a call to an
# undeclared routine `a`. It ANSWERS the transliterated string and leaves the
# target alone; the smartmatch form `$x ~~ TR///` transliterates $x and matches
# the result against it (so `1 ~~ TR/\#//` is True, nothing having been
# deleted, while `"ab" ~~ TR/ab/01/` is False).
#
# And the bare `tr///` against `$_` answers a StrDistance, like the `~~` form
# already did — it stringifies to the RESULT and numifies to the COUNT, where
# the plain count could only do the latter.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# --- TR/// answers the new string, leaving the target alone ---
my $s = "ab";
check (TR/a..j/0..9/ with $s), '01', 'TR/// transliterates';
check $s, 'ab',                      '…and does not touch the target';
$_ = "cd";
check TR/a..j/0..9/, '23', 'TR/// against the topic';
check $_, 'cd',            '…leaves it alone too';
check (1 ~~ TR/\#//), True,  'the smartmatch form matches when nothing changed';
check ("ab" ~~ TR/ab/01/), False, '…and does not when it did';
check $s, 'ab', '…still without mutating';

# --- tr/// still mutates, and answers a StrDistance ---
$_ = "ab";
my $d = tr/ab/01/;
check $_, '01',            'tr/// mutates the topic';
check ~$d, '01',           '…and answers a StrDistance that strings to the result';
check +$d, 2,              '…and numifies to the count';
my $t = "ab";
$t ~~ tr/ab/01/;
check $t, '01',            'the ~~ form still mutates its left side';
# a case the range spelling has to keep working
$_ = "hello";
check TR/a..z/A..Z/, 'HELLO', 'TR with two ranges';

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
