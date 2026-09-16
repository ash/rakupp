# Regression: the `msb` and `lsb` builtins.
#
# The position of an Int's highest and lowest set bit, counting from 0. Neither
# existed here; Rat::Precise sizes a decimal expansion with
# `msb(self.denominator)` and died at "Undefined routine 'msb'".
#
# The negative cases are the ones worth pinning: Rakudo answers the TWO'S
# COMPLEMENT position, so msb(-1) is 0 and msb(-255) is 8 — one more than
# msb(255) — because the sign needs a bit of its own. That is the bit length of
# |n| - 1. lsb ignores the sign entirely: a negative has the same trailing zeros
# as its magnitude. Zero has no set bit and both answer Nil rather than dying.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

check msb(1),    0, 'msb(1)';
check msb(2),    1, 'msb(2)';
check msb(255),  7, 'msb(255)';
check msb(256),  8, 'msb(256)';
check msb(1024), 10, 'msb(1024)';

check lsb(1),    0, 'lsb(1)';
check lsb(2),    1, 'lsb(2)';
check lsb(8),    3, 'lsb(8)';
check lsb(255),  0, 'lsb(255)';
check lsb(256),  8, 'lsb(256)';

# zero has no set bit
check msb(0), Nil, 'msb(0) is Nil';
check lsb(0), Nil, 'lsb(0) is Nil';

# the two's-complement answers for negatives
check msb(-1),   0, 'msb(-1) — the sign bit alone';
check msb(-2),   1, 'msb(-2)';
check msb(-3),   2, 'msb(-3) — not msb(3)';
check msb(-4),   2, 'msb(-4)';
check msb(-255), 8, 'msb(-255) is one MORE than msb(255)';
check msb(-256), 8, 'msb(-256)';

check lsb(-1),   0, 'lsb(-1)';
check lsb(-2),   1, 'lsb(-2)';
check lsb(-3),   0, 'lsb(-3)';
check lsb(-4),   2, 'lsb(-4)';
check lsb(-256), 8, 'lsb(-256)';

# and past a machine word, where the answer cannot come from a double
check msb(2**70), 70, 'msb of a bigint';
check lsb(2**70), 70, 'lsb of a bigint';
check msb(2**200 + 1), 200, 'msb of a wider bigint';
check lsb(2**200 + 1),   0, 'lsb of the same';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
