# A packed `int` / `num` array read or written with a COMPUTED key,
# `@a[$i + 1]`, answers from its words in the interpreter, as `@a[$i]` and
# `@a[3]` did. The read took evalIndex's general arm, whose first step unpacked
# the array for good; the store took the general path, which did the same. A
# --cnp kernel entered after that found no words to read, so the stencil
# loops and the flat matmul of FORTRAN-LESSONS-2026-10-15 ran boxed.
#
# The read now evaluates its key once, before the arm unpacks anything, and
# hands it on when it is no machine Int. The store lane takes a key computed
# with no effects (packedRhsPure), which the general path may evaluate again;
# a key with an effect, `$k++ + 1`, still goes the general way, once. Each
# line below is Rakudo 2026.09's answer.
#
# What changed is the speed, and a87a6b73's build passes this file too: it
# holds the answers the two new arms must keep, the evaluate-once rule above
# among them. t/cnp/cases/packed-array-words.raku covers the words a kernel
# then finds.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# reads
my int @n = ^5;
my $i = 0;
check @n[$i++ + 0.5], 0, 'a Rat key with an effect';
check $i, 1, '…evaluated once';
my $j = 0;
check @n[($j++, 1)].List, (0, 1), 'a list key with an effect';
check $j, 1, '…evaluated once';
my $k = 1;
check @n[$k + 1], 2, 'a computed Int key';
check @n[$k + 10], 0, 'a computed key past the end';
check @n[$k + 1.5], 2, 'a computed Rat key';
check @n["2"], 2, 'a Str key';
check @n[$k ~ ""], 1, 'a computed Str key';
check @n[*-1], 4, 'a Whatever key';
check @n[$k + *-3], 3, 'a computed WhateverCode key';
check @n[$k, $k + 1].List, (1, 2), 'a slice of computed keys';
my num @f = 0e0, 0.5e0, 1e0;
my $q = 0;
check @f[$q + 1], 0.5e0, 'a num array, a computed key';
check @f[$q + 2] * 2, 2e0, '…another';

# stores
my int @s = ^6;
my $m = 1;
@s[$m + 1] = 20;
@s[$m * 3] += 5;
@s[$m + 3]++;
@s[$m - 1] = @s[$m + 1] + 1;
check @s.List, (21, 1, 20, 8, 5, 5), 'stores, `+=` and `++` through computed keys';
my @idx = 4, 0;
@s[@idx[0]] = 99;
check @s.List, (21, 1, 20, 8, 99, 5), 'a key read from another array';
@s[$m + 10] = 7;
check (@s.elems, @s[11], @s[8]), (12, 7, 0), 'a computed key past the end grows the array';
my num @g = 0e0 xx 4;
@g[$q + 2] = 2.5e0;
check @g.List, (0e0, 0e0, 2.5e0, 0e0), 'a num array, a computed key';
my int @z = ^4;
my $c = 0;
@z[$c++ + 1] = 9;
check (@z.List, $c), ((0, 9, 2, 3), 1), 'a key with an effect, evaluated once';

if @fail { .say for @fail; say "FAIL"; exit 1 }
say "PASS";
