# Regression: a CArray subscript is evaluated once (issue #187).
#
# The element read evaluated the subscript to see whether it was a slice,
# then evaluated it again to read the element, so `$c[$i++]` stepped $i
# twice and read the element after the one asked for. Array and Buf were
# right. The subscript is now evaluated once and used for both.

use NativeCall;

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $c = CArray[uint8].new(10, 20, 30);
my $i = 0;
ck $c[$i++], 10, 'byte-backed: $c[$i++] reads element 0';
ck $i, 1, 'byte-backed: $i stepped once';

my $calls = 0;
sub k { $calls++; 1 }
ck $c[k()], 20, 'a call as the subscript';
ck $calls, 1, 'the subscript call runs once';

$i = 0;
ck $c[$i++ .. 1].List, (10, 20), 'a slice whose subscript has a side effect';
ck $i, 1, 'the slice subscript is evaluated once';

$i = 0;
$c[$i++] = 99;
ck $c[0], 99, 'assignment through $c[$i++]';
ck $i, 1, 'assignment steps $i once';

# a live (native) array: pointer-backed, the other read path
my $p = CArray[int32].new(5, 6, 7);
$i = 0;
ck $p[$i++] + $p[$i++], 11, 'two reads in one expression';
ck $i, 2, 'each read steps $i once';

say 'PASS';
