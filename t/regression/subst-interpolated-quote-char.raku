# Regression: a Str holding `"` or `'`, interpolated into an `s///`
# pattern, was spliced in bare, so it opened a quoted span: `s/$q$q/Q/`
# found an empty match at 0 and put `Q` in front of the string. `m//` and
# `.subst` quote it as a literal; `s///` now quotes it the same way, every
# ASCII punctuation character escaped (issue #237).
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $q = '"';
{ my $s = 'a""b'; $s ~~ s/$q$q/Q/;   check $s, 'aQb', 'two double quotes' }
{ my $s = 'a"b';  $s ~~ s/$q/Q/;     check $s, 'aQb', 'one' }
{ my $s = 'a""b'; $s ~~ s/$q $q/Q/;  check $s, 'aQb', 'whitespace between is insignificant' }
{ my $s = 'a" "b'; $s ~~ s/$q $q/Q/; check $s, 'a" "b', '…so a space between does not match' }
{ my $s = 'a""b""c'; $s ~~ s:g/$q$q/Q/; check $s, 'aQbQc', ':g' }
{ my $p = "'"; my $s = "a''b"; $s ~~ s/$p$p/Q/; check $s, 'aQb', 'single quotes' }
for < & ` ~ % , ; > -> $c {
    my $s = "a{$c}b"; $s ~~ s/$c/Q/; check $s, 'aQb', "the punctuation $c"
}
check (S/$q/Q/ given 'a"b'), 'aQb', 'S///';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
