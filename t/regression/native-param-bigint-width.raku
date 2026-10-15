# Regression: an Int no machine word holds, bound to a native integer
# parameter, is refused with the message `my int $x = …` gives: the WIDTH is
# the value's bit length (2**70 is 71 bits wide), and it ends "Did you mix int
# and Int or literals?". Before, a signed parameter said one bit more ("72 bit
# wide") and neither kind had the hint; an `atomicint` parameter took 2**70
# without a word. Both bind paths (a plain signature and one that is not) and
# every signed width. Oracle-checked against Rakudo 2026.09.
# Contract: exit 0 + last line PASS.
my @fail;
sub refused(&code, $want, $label) {
    my $got = (try { code(); 'no exception' }) // "{$!.^name}: {$!.message}";
    @fail.push("$label: got $got.raku(), want $want.raku()")
        unless $got eq "X::AdHoc: Cannot unbox $want bit wide bigint into native integer. Did you mix int and Int or literals?";
}

sub fi(int $x)            { $x }
sub fi-slow(int $x, :$n)  { $x }
sub fi64(int64 $x)        { $x }
sub fi8(int8 $x)          { $x }
sub fi8-slow(int8 $x, $y?) { $x }
sub fa(atomicint $x)      { $x }
sub fu(uint $x)           { $x }
sub fu-slow(uint $x, :$n) { $x }
sub fu64(uint64 $x)       { $x }
sub fb(byte $x)           { $x }

for (2**63, 64), (2**64, 65), (2**70, 71), (-2**63 - 1, 64), (-2**70, 71), (2**100, 101) -> ($v, $w) {
    refused { fi($v) },       $w, "int $v";
    refused { fi-slow($v) },  $w, "int $v (slow bind)";
    refused { fi64($v) },     $w, "int64 $v";
    refused { fi8($v) },      $w, "int8 $v";
    refused { fi8-slow($v) }, $w, "int8 $v (slow bind)";
    refused { fa($v) },       $w, "atomicint $v";
    refused { my int $m = $v }, $w, "my int $v, as before";
}
for (2**64, 65), (2**70, 71), (2**100, 101) -> ($v, $w) {
    refused { fu($v) },      $w, "uint $v";
    refused { fu-slow($v) }, $w, "uint $v (slow bind)";
    refused { fu64($v) },    $w, "uint64 $v";
    refused { fb($v) },      $w, "byte $v";
}
{ class K { method m(int $x) { $x } }; refused { K.m(2**70) }, 71, 'a method parameter' }
refused { (-> int $x { $x })(2**70) }, 71, 'a pointy block parameter';

# the widest values that fit still bind
@fail.push('int.max must bind')  unless fi(9223372036854775807) == 9223372036854775807;
@fail.push('int.min must bind')  unless fi(-9223372036854775808) == -9223372036854775808;
@fail.push('2**64-1 into uint must bind') unless fu(2**64 - 1) == 2**64 - 1;
@fail.push('an int8 wraps what a machine word holds') unless fi8(2**63 - 128) == -128;

say @fail ?? "FAIL: {@fail.join('; ')}" !! 'PASS';
exit @fail ?? 1 !! 0;
