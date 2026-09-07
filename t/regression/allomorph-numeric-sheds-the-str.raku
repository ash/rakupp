# `.Numeric` and `.Real` on an allomorph answered the ALLOMORPH, not the number.
#
# The value was always right, so arithmetic worked and nothing looked broken.
# It showed only in string context, because the object handed back still
# carried its original text: `~ <0o755>.Numeric` answered "0o755" where Rakudo
# answers 493. That is why every check below asserts the TYPE and the
# STRINGIFICATION — a check on the value alone passes on the broken engine too,
# and so does a check built from `<42>`, whose two answers coincide. Every
# fixture here is written so the string half DIFFERS from the number's own
# form, which is the only way the assertion can fail when the bug is present.
#
# The same arm already stripped an enum's tag for the same reason, and `.Rat`
# on a RatStr already shed the Str side; this is those two, finished.
#
# NOT covered, deliberately: `.floor`/`.ceiling`/`.round` on an IntStr and
# `.abs` on a RatStr, where Rakudo KEEPS the allomorph (its Int.floor and
# Real.abs return `self`) and rakupp builds a fresh number. That is a real
# divergence in the opposite direction, measured on 2026-09-07 and left alone
# here; see docs/dev/findings/TRIAGE.md.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# -- .Numeric sheds, on every allomorph type ----------------------------------
{
    my $i = IntStr.new(493, "0o755");
    check($i.Numeric.WHAT.^name, 'Int', 'IntStr.Numeric is an Int');
    check(~$i.Numeric, '493', '…and stringifies as the number, not the text');
    check($i.Numeric, 493, '…with the value intact');
    check(~$i, '0o755', '…and the allomorph itself is untouched');

    my $r = RatStr.new(1.5, "1.50");
    check($r.Numeric.WHAT.^name, 'Rat', 'RatStr.Numeric is a Rat');
    check(~$r.Numeric, '1.5', '…and sheds the trailing zero the Str half carried');

    my $n = NumStr.new(2e0, "2e0");
    check($n.Numeric.WHAT.^name, 'Num', 'NumStr.Numeric is a Num');
    check(~$n.Numeric, '2', '…rendered as a number, not as "2e0"');

    my $c = ComplexStr.new(1+2i, "1+2i");
    check($c.Numeric.WHAT.^name, 'Complex', 'ComplexStr.Numeric is a Complex');
}

# -- .Real agrees, and stays illegal on a Complex -----------------------------
{
    check(IntStr.new(493, "0o755").Real.WHAT.^name, 'Int', 'IntStr.Real is an Int');
    check(RatStr.new(1.5, "1.50").Real.WHAT.^name, 'Rat', 'RatStr.Real is a Rat');
    check(NumStr.new(2e0, "2e0").Real.WHAT.^name,   'Num', 'NumStr.Real is a Num');
    my $threw = False;
    { ComplexStr.new(1+2i, "1+2i").Real; CATCH { default { $threw = True } } }
    check($threw, True, 'ComplexStr.Real still refuses — a Complex is not Real');
}

# -- the coercions that were already right stay right -------------------------
{
    my $i = IntStr.new(493, "0o755");
    check($i.Int.WHAT.^name, 'Int', '.Int unchanged');
    check($i.Num.WHAT.^name, 'Num', '.Num unchanged');
    check((+$i).WHAT.^name,  'Int', 'unary + unchanged');
    check($i + 1, 494, 'arithmetic unchanged');
    check($i.Str, '0o755', '.Str still answers the text half');
    my $r = RatStr.new(1.5, "1.50");
    check($r.Rat.WHAT.^name, 'Rat', '.Rat unchanged (it already shed)');
    check(~$r.Rat, '1.5', '…and still sheds');
}

# -- an allomorph is still an allomorph where it should be --------------------
{
    my $i = IntStr.new(42, "42");
    check($i.WHAT.^name, 'IntStr', 'the allomorph type survives its own methods');
    check(($i ~~ Int) && ($i ~~ Str), True, '…and still smartmatches both halves');
    check($i.Numeric ~~ Str, False, '…while what .Numeric returns is not a Str');
}

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
