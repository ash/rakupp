# Regression: `my int $x = EXPR` and its kin inside a loop take the native
# declaration lane (Assign::simpleSlot 9), which skips the declaration's
# per-execution type checks. What the full path did with the value must not
# change: the wrap to the width, the refusals and their messages, one
# variable per turn of the loop.
#
# Expectations checked against Rakudo v2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my int $n = 3;
my @rows;
for ^3 -> $i {
    my int $a = $i * 2 + $n;
    my num $b = $i + 0.5e0;
    my int8 $c = 126 + $i;
    my uint8 $d = -1 - $i;
    my int $f = $a;
    @rows.push: ($a, $b, $c, $d, $f);
}
ck @rows[0], (3, 0.5e0, 126, 255, 3), 'int, num, int8, uint8 and a native copy, first turn';
ck @rows[2], (7, 2.5e0, -128, 253, 7), '…and the third: int8 and uint8 wrap';

my @subs;
for ^3 -> $i { my int $q = $i * 10; @subs.push: { $q } }
ck @subs».().List, (0, 10, 20), 'each turn declares a variable of its own';

my int $s = 0;
for ^5 { my int $w = $s; $s = $w + 1 }
ck $s, 5, 'a declaration reading the variable another one writes';

my $out;
for ^2 { my int $z = 1; $z = $z + 1; $out = $z }
ck $out, 2, 'a native declared in a loop takes later assignments';

# (a Str is refused in both engines, in different words: Rakudo's is
# "This type cannot unbox to a native integer", rakupp's a type check)
my (@died, @msgs);
for ^1 {
    try { my int $bad = "abc" };  @died.push: $! ~~ Exception;
    try { my int $bad = Nil };    @msgs.push: $!.message;
    try { my int $bad = 2**65 };  @msgs.push: $!.message;
    try { my num $bad = "1.5x" }; @died.push: $! ~~ Exception;
}
ck @died.List, (True, True), 'a Str into an int or a num is refused';
ck @msgs[0], 'Cannot unbox a type object (Nil) to int.', 'Nil into an int is refused';
ck @msgs[1], 'Cannot unbox 66 bit wide bigint into native integer. Did you mix int and Int or literals?', 'a bigint into an int is refused';

my num32 $third;
for ^1 { my num32 $t = 1e0 / 3e0; $third = $t }
ck $third == (1e0 / 3e0), False, 'num32 rounds to single precision';

say $fails ?? "FAILED $fails" !! 'PASS';
