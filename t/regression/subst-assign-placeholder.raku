# BROKE: leg 19 — an unconditional mainline-placeholder check inside
# evalString fired on INTERNAL re-parses too. The S{}=repl substitution form
# re-parses its replacement through evalString, so `S{5}=$^a` in a map block
# silently stopped substituting (subst.t 191 -> 190, caught by gate dz1).
# FIXED: same leg — evalString(src, mainlinePH=false default); only
# user-facing EVAL surfaces enable the check.
$_ = "12345";
my @r = [3,4].map:{S{5}=$^a};
die "S{}=placeholder substitution broken: {@r.raku}" unless @r eqv ['12343', '12344'];
say 'PASS';
