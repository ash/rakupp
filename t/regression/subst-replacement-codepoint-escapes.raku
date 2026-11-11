# Regression: the replacement side of `s///` and `S///` dropped the
# backslash of a multi-character escape and kept the rest as text, so
# `s/X/\x41/` put in `x41` where Rakudo puts in `A` — `\x`, `\x[…]`, `\o`,
# `\c[NAME]` and `\c65` alike. A single-quoted right side of `s[…] = '…'`
# is literal, and stays `\x41` (issue #236).
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

{ my $s = 'aXb'; $s ~~ s/X/\x41/;    check $s, 'aAb', '\x41' }
{ my $s = 'aXb'; $s ~~ s/X/\x[41]/;  check $s, 'aAb', '\x[41]' }
{ my $s = 'aXb'; $s ~~ s/X/\x[41,42]/; check $s, 'aABb', '\x[41,42]' }
{ my $s = 'aXb'; $s ~~ s/X/\o101/;   check $s, 'aAb', '\o101' }
{ my $s = 'aXb'; $s ~~ s/X/\c[LATIN CAPITAL LETTER A]/; check $s, 'aAb', '\c[NAME]' }
{ my $s = 'aXb'; $s ~~ s/X/\c65/;    check $s, 'aAb', '\c65' }
{ my $s = 'aXb'; $s ~~ s/X/\x263a/;  check $s, "a\x[263a]b", 'a wide one' }
{ my $s = 'aXb'; $s ~~ s/X/\x41\x42/; check $s, 'aABb', 'two in a row' }
{ my $s = 'aXb'; $s ~~ s/X/[\n\t]/;  check $s, "a[\n\t]b", 'the one-letter escapes still' }
check (S/X/\x41/ given 'aXb'), 'aAb', 'S///';
{ my $s = 'aXb'; $s ~~ s{X} = "\x41"; check $s, 'aAb', 's{} = "…"' }
{ my $s = 'aXb'; $s ~~ s[X] = '\x41'; check $s, 'a\x41b', 's[] = \'…\' stays literal' }
{ my $s = '""hi""'; $s ~~ s:g/'""'/\x22/; check $s, '"hi"', 'un-doubling quotes' }

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
