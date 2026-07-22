# Regression: numifying a Match follows the Str ladder (digits -> Int,
# decimals -> Rat), not a blanket Num. Found 2026-07-22 when raku-spec's
# gen-roast-map.raku died: `sub f(... --> Int) { ...; return +$0 }` hit the
# typed-return check because +$0 of a digit capture produced Num(9).
# Rakudo: +$0 is Int, -$0 is Int, -"9.5" is Rat.

my $ok = True;

"plan 9" ~~ /plan \s+ (\d+)/;
unless (+$0).WHAT.raku eq 'Int' && +$0 == 9 {
    say 'FAIL: +$0 of a digit capture is not Int';
    $ok = False;
}
unless (-$0).WHAT.raku eq 'Int' && -$0 == -9 {
    say 'FAIL: -$0 of a digit capture is not Int';
    $ok = False;
}
unless (-"9.5").WHAT.raku eq 'Rat' {
    say 'FAIL: -"9.5" is not Rat';
    $ok = False;
}

sub plan-of(Str $s --> Int) {
    $s ~~ /plan \s+ (\d+)/;
    return +$0;
}
unless plan-of("plan 42") == 42 {
    say 'FAIL: typed-return sub rejects +$0';
    $ok = False;
}

say 'PASS' if $ok;
