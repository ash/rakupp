# Regression: a NativeCall Pointer is numerically its address, an Int.
# `0 + $p` and `$p + 1` came out Num (the tagged hash reached applyArith's
# floating-point arm), and compiled code spells prefix `+` as `0 + x`, so in a
# native body `Pointer.new(+$!storage + $i * $size)` got a Num and made a NULL
# pointer — NativeHelpers::CStruct's LinearArray then wrote through address 0
# and the installed-binary gate's --exe program segfaulted.
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use NativeCall;
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

my $p = Pointer.new(4096);
ck (+$p).WHAT,      Int,  'prefix + gives an Int';
ck (0 + $p).WHAT,   Int,  '0 + $p gives an Int';
ck ($p + 1).WHAT,   Int,  '$p + 1 gives an Int';
ck $p + 8,          4104, '$p + 8 is the address plus 8';
ck $p - 96,         4000, '$p - 96 is the address minus 96';
ck $p * 2,          8192, '$p * 2';
ck $p div 3,        1365, '$p div 3';
ck $p == 4096,      True, '$p == 4096';

my $q = Pointer.new(+$p + 2 * 8);
ck +$q,             4112, 'Pointer.new(+$p + 2 * 8) keeps the address';
my Pointer $r .= new(+$p);
ck +$r,             4096, 'my Pointer $r .= new(+$p)';
ck ?Pointer.new(+Pointer.new(64)), True, 'a pointer rebuilt from +$p is not NULL';

if @fail { .say for @fail; say 'FAIL' }
else     { say 'PASS' }
