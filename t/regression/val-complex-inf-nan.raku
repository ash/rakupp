# BROKE: val took a spelling without a digit for a word, so `val('NaN+Inf\i')`,
# `val('Inf\i')` and `val('NaN\i')` were Str where Rakudo gives ComplexStr
# (found 2026-10-23: FatRoast asks val which words of a word list are
# numbers, and would have mapped the letters of `<NaN+Inf\i>` on Raku++ alone).
# FIXED: valAllomorph (src/Interpreter.cpp) keeps a digitless Complex when it
# ends in `Inf\i`, or is `NaN\i`; as in Rakudo, an imaginary NaN with a sign or
# a real part (`-NaN\i`, `1+NaN\i`) and a bare `i` stay Str.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

for Q[NaN+Inf\i], Q[Inf\i], Q[NaN\i], Q[-Inf\i], Q[+Inf\i], Q[Inf+Inf\i],
    Q[-Inf-Inf\i], Q[NaN-Inf\i], Q[1+Inf\i], Q[NaN+1i], Q[1+2i] -> $w {
    ck(val($w).^name, 'ComplexStr', "val('$w')");
    ck(~val($w), $w, "…keeps its spelling");
}
for Q[i], Q[+i], Q[-i], Q[\i], Q[Infi], Q[Inf+Infi], Q[is],
    Q[-NaN\i], Q[1+NaN\i], Q[Inf+NaN\i], Q[NaN+NaN\i], Q[-Inf-NaN\i] -> $w {
    ck(val($w).^name, 'Str', "val('$w')");
}
ck(val(Q[NaN+Inf\i]).im, Inf, 'the imaginary part');
ck(val(Q[NaN+Inf\i]).re.isNaN, True, 'the real part');
ck(<a NaN+Inf\i>[1].^name, 'ComplexStr', 'a word of a word list');

if @fail { note $_ for @fail; say 'FAIL' }
else { say 'PASS' }
