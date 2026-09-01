# The WhateverCode that `+*` / `-*` curries into carried its OWN copy of prefix
# `+` and prefix `-`, much thinner than the one evalUnary runs, and it had
# drifted: it numified through toNum() and boxed a Num. So
# `"123".comb.map(+*)` produced (1e0, 2e0, 3e0) where the same operator applied
# directly gives (1, 2, 3) — and a list numified to its element count as a Num
# rather than an Int, a Bool came back as a Bool instead of 0/1, `-*` over a Rat
# gave a Num and over a Complex gave -0e0, and `-*` over a bignum saturated
# through toInt(). Both forms now run one implementation.
#
# Runs on BOTH engines and must print byte-identical output. Contract: exit 0
# with PASS as the last line.
my @fail;
sub ck($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want;
}

# ---- the reported case ------------------------------------------------
ck "123".comb.map(+*).List, (1, 2, 3),      'prefix + over Whatever numifies by shape';
ck "123".comb.map(+*).sum.WHAT, Int,        '…so the sum is an Int';
ck "123".comb.map(+*).List.raku, '(1, 2, 3)', '…and it renders as Ints';

# ---- the curried form must equal the direct one, value AND type -------
for '1', '1.5', '2e3', '-7', '0' -> $s {
    ck (+*).($s), (+$s),           "curried + matches direct + on '$s'";
    ck (+*).($s).WHAT, (+$s).WHAT, "…and the type, on '$s'";
    ck (-*).($s), (-$s),           "curried - matches direct - on '$s'";
    ck (-*).($s).WHAT, (-$s).WHAT, "…and the type, on '$s'";
}

# ---- every operand shape the thin copy got wrong ----------------------
ck (+*).('42').WHAT, Int,          'integral string curries to an Int';
ck (+*).('1.5').WHAT, Rat,         'decimal string curries to a Rat';
ck (+*).('2e3').WHAT, Num,         'exponent string curries to a Num';
ck (+*).(True), 1,                 'Bool curries to 1';
ck (+*).(False), 0,                'Bool curries to 0';
ck (+*).(True).WHAT, Int,          '…as an Int, not a Bool';
ck (+*).((1, 2, 3)), 3,            'a list curries to its element count';
ck (+*).((1, 2, 3)).WHAT, Int,     '…as an Int';
ck ((1,2,3), (4,5)).map(+*).List, (3, 2), 'element counts over a list of lists';
ck (-*).(10**30), -(10**30),       'bignum negates exactly, not through int64';
ck (+*).(10**30), 10**30,          'bignum passes through unchanged';
ck (-*).(3.5), -3.5,               'Rat negates as a Rat';
ck (-*).(3.5).WHAT, Rat,           '…and stays a Rat';
ck (-*).(3+4i), -3-4i,             'Complex negates both parts';
ck (-*).(1.5e0), -1.5e0,           'Num negates as a Num';
ck (-*).(1.5e0).WHAT, Num,         '…and stays a Num';
ck (+*).(IntStr.new(5, 'five')), 5,       'an allomorph strips to its numeric side';
ck (+*).(IntStr.new(5, 'five')).WHAT, Int, '…as a plain Int';

# ---- a non-numeric string must fail the way the direct form fails -----
# (the message text differs between engines, so only the KIND is asserted)
ck ((+*).('a1') ~~ Failure), True,  'curried + on a non-number is a Failure';
ck ((+*).('a1').defined), False,    '…and an undefined one';

# ---- the other curried prefixes must not have moved ------------------
ck (~*).('hi'), 'hi',       'curried ~';
ck (~*).(42), '42',         'curried ~ on a number';
ck (?*).(0), False,         'curried ?';
ck (!*).(0), True,          'curried !';
ck (so *).(1), True,        'curried so';
ck (+^*).(5), -6,           'curried +^';
ck (^*).(3).gist, '^3',     'curried ^';
ck ((^*).(3).elems), 3,     '…and it is the range, not an empty one';

# ---- direct prefix +/- must be unchanged in every arm ----------------
my @a = 1, 2, 3;
my %h = a => 1, b => 2;
ck +@a, 3,                  'direct + on an Array is its count';
ck -@a, -3,                 'direct - on an Array';
ck +%h, 2,                  'direct + on a Hash';
ck +(1..10), 10,            'direct + on a Range';
ck +Buf.new(1, 2, 3), 3,    'direct + on a Buf is its byte count';
ck +\(1, 2, 3), 3,          'direct + on a Capture is its positional count';
ck +True, 1,                'direct + on a Bool';
ck -(3+4i), -3-4i,          'direct - on a Complex';
ck -(1/3), -1/3,            'direct - on a Rat';
ck -(2**63), -9223372036854775808, 'direct - at the int64 boundary';
ck -(10**30), -(10**30),    'direct - on a bignum';

if @fail { note $_ for @fail; say "FAIL"; exit 1 }
say "PASS";
