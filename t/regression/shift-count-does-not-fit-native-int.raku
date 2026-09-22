# Regression: a shift count that does not fit a native int.
#
# `1 +< (2**70)` HUNG until something killed it. The count went through
# bitwiseInt, which saturates a bigint to the int64 maximum, and the left shift
# then asked BigInt for two to the power of nine quintillion. `+>` was quieter
# and merely wrong: it answered 0 or -1 for a count it should have refused.
#
# Rakudo unboxes the count into a native int and says so when it cannot, naming
# the exact bit width — S02-types/declare.t asserts "Cannot unbox 101 bit wide
# bigint into native integer", so the width has to be right, not estimated.
# (Int-Num-Rat semantics sheet, N-17.)
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# the four shifts that used to hang or lie
check (try 1 +< 2**70) // $!.^name,          'X::AdHoc', 'a left shift by 2**70 is refused';
check (try 1 +> 2**70) // $!.^name,          'X::AdHoc', 'and a right shift by it';
check (try -1 +> 2**70) // $!.^name,         'X::AdHoc', 'whatever the sign of the operand';
check (try (2**70) +< (2**70)) // $!.^name,  'X::AdHoc', 'and with a bigint on both sides';

# the width in the message is EXACT, not an estimate off a logarithm
check ((try 1 +< 2**100) // $!.message).starts-with('Cannot unbox 101 bit wide bigint'),
      True, '2**100 is 101 bits';
check ((try 1 +< 2**70)  // $!.message).starts-with('Cannot unbox 71 bit wide bigint'),
      True, '2**70 is 71 bits';
check ((try 1 +< (2**64 - 1)) // $!.message).starts-with('Cannot unbox 64 bit wide bigint'),
      True, 'and 2**64-1 is 64';

# every count that DOES fit still shifts, including the negative ones that
# reverse the direction and the ones that escalate into BigInt
check 1 +< 62,          4611686018427387904, 'a count inside the native range still shifts';
check (1 +< 100).chars, 31,                  'and one whose RESULT needs a bigint';
check 5 +< -1,          2,                   'a negative count shifts the other way';
check 5 +> -1,          10,                  'in both directions';
check -123 +> 32,       -1,                  'an arithmetic right shift keeps the sign';
check 123 +> 1000,      0,                   'and a huge-but-native count saturates';
check (2**70) +> 69,    2,                   'a bigint OPERAND is still fine';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
