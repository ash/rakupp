# A subscript's key is evaluated once. The `@plainvar[intexpr]` fast arm of
# evalIndex evaluated the key, and when it was no machine Int (a Rat, a Str, a
# list, a Range) fell through to the general path, which evaluated it again:
# `@a[$i++ + 0.5]` read @a[1] and left $i at 2, where Rakudo reads @a[0] and
# leaves 1. The general path now takes the key the fast arm evaluated, and a
# Range written as the key never enters the fast arm, whose Int path it
# cannot take. Each line is Rakudo 2026.09's answer; the stores, `:exists`,
# `$scalar[…]` and hash subscripts already evaluated once and are held to it.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my @a = 10, 20, 30, 40, 50;
my $i = 0;
check @a[$i++ + 0.5], 10, 'a Rat key';
check $i, 1, '…evaluated once';
$i = 0;
check @a[($i++, 1)].List, (10, 20), 'a list key';
check $i, 1, '…evaluated once';
$i = 0;
check @a[$i++ .. 2].List, (10, 20, 30), 'a Range written as the key';
check $i, 1, '…evaluated once';
$i = 0;
check @a[$i++ ~ ""], 10, 'a Str key';
check $i, 1, '…evaluated once';
$i = 0;
check @a[*-1 - $i++], 50, 'a WhateverCode key';
check $i, 1, '…evaluated once';
$i = 0;
check @a[[$i++, 1]].List, (10, 20), 'an Array key';
check $i, 1, '…evaluated once';
$i = 0;
check @a[$i++ + 1], 20, 'an Int key';
check $i, 1, '…evaluated once';
my $calls = 0;
sub key() { $calls++; 1.5 }
check @a[key()], 20, 'a key from a call';
check $calls, 1, '…called once';
my @e;
$i = 0;
check @e[$i++ + 0.5], Any, 'an empty array';
check $i, 1, '…evaluated once';
$i = 0;
check @a[$i++ + 0.5]:exists, True, ':exists';
check $i, 1, '…evaluated once';
my $r = @a;
$i = 0;
check $r[$i++ + 0.5], 10, 'an array in a scalar';
check $i, 1, '…evaluated once';
my %h = a => 1, b => 2;
$i = 0;
check %h{($i++, "a")}.List, (Any, 1), 'a hash, a list key';
check $i, 1, '…evaluated once';

my @b = 1, 2, 3, 4;
$i = 0;
@b[$i++ + 0.5] += 10;
check (@b.List, $i), ((11, 2, 3, 4), 1), '`+=`';
$i = 0;
@b[$i++ .. 1] = 7, 8;
check (@b.List, $i), ((7, 8, 3, 4), 1), 'a slice store';
$i = 0;
@b[$i++ + 0.5] //= 9;
check (@b.List, $i), ((7, 8, 3, 4), 1), '`//=`';
my int @n = ^4;
$i = 0;
@n[$i++ + 0.5] = 9;
check (@n.List, $i), ((9, 1, 2, 3), 1), 'a native array';

if @fail { .say for @fail; say "FAIL"; exit 1 }
say "PASS";
