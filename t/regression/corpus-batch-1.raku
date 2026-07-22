# Regression: three corpus-round-2 fixes (2026-07-22).
# 1. glued CLI line-loop flags: -ne'CODE' / -pe'CODE' (shell strips quotes, so
#    the code arrives glued to the flag cluster) were rejected as Illegal option.
# 2. assigning an UNDEFINED value to a typed scalar type-checks: my Int $i = $u
#    (where $u is Any) dies X::TypeCheck::Assignment like Rakudo; the matching
#    type object (= Int) and Nil-reset still work; Failure still soaks in.
# 3. big-part Rat->Num converts with a single rounding (long division), not two.

my $ok = True;

my $out = qqx{$*EXECUTABLE -ne'say \$_.uc' << EOT
hi
there
EOT};
unless $out eq "HI\nTHERE\n" {
    say "FAIL: -ne glued form: {$out.raku}";
    $ok = False;
}

my $u;
my $died = False;
try { my Int $i = $u; CATCH { when X::TypeCheck::Assignment { $died = True } } }
unless $died {
    say 'FAIL: my Int $i = (undefined Any) should die';
    $ok = False;
}
{
    my Int $i = Int;      # matching type object is fine
    my Int $j = 5; $j = Nil;
    unless $j.raku eq 'Int' { say 'FAIL: Nil reset broken'; $ok = False; }
}

my $q = 100000000000000000000000001 / 300000000000000000000000000;
unless $q.Str eq '0.3333333333333333' {
    say "FAIL: big-Rat rounding: {$q}";
    $ok = False;
}

say 'PASS' if $ok;
