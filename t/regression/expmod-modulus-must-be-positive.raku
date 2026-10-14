# Regression: expmod with a modulus of zero or less answered, where Rakudo
# refuses it (libtommath's "Value out of range", an X::AdHoc): `expmod(3, 5, 0)`
# was 0, `expmod(3, 5, -7)` 5 and `expmod(-3, 5, -7)` -12, which is no residue
# of anything. Found by t/oracle/int.raku (ORACLE-PLAN P2). The --target=js
# runtime had its own expmod with more wrong: a negative base gave a negative
# answer (JS's `%` keeps the dividend's sign), a negative exponent died
# instead of inverting, and the modulus went unchecked; it is the
# interpreter's now, and t/js/run.raku holds this file to the interpreter.
#
# Expectations checked against Rakudo 2026.09. The messages differ by design
# (Rakudo's names the library routine), so only the type is asserted.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub refused(&code, $desc) {
    my $died = 'nothing';
    { code(); CATCH { default { $died = .^name } } }
    ck $died, 'X::AdHoc', $desc;
}

refused { expmod(3, 5, -7) },  'a negative modulus is refused';
refused { expmod(3, 5, 0) },   'a zero modulus is refused';
refused { expmod(-3, 5, -7) }, 'a negative base and modulus are refused';
refused { expmod(3, 0, -7) },  'a zero exponent does not excuse the modulus';
refused { expmod(3, -1, -7) }, 'nor does a negative exponent';
refused { 3.expmod(5, -7) },   'the method form too';
refused { expmod(2**100, 5, -(2**70)) }, 'a big negative modulus';
refused { expmod(2, -1, 4) },  '2 has no inverse modulo 4';

ck expmod(3, 5, 7),   5, '3**5 mod 7';
ck expmod(-3, 5, 7),  2, 'a negative base answers a residue in 0..m-1';
ck expmod(3, -1, 7),  5, 'a negative exponent raises the inverse';
ck expmod(3, -2, 7),  4, 'the inverse squared';
ck expmod(5, 0, 1),   1, 'a zero exponent is 1, even modulo 1, as in Rakudo';
ck expmod(2**100, 2**80 + 1, 10**30 + 57), 447945431207347091379221252226,
   'big operands (Python: pow(2**100, 2**80 + 1, 10**30 + 57))';

say $fails ?? 'FAIL' !! 'PASS';
