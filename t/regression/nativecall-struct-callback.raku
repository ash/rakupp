# Regression: NativeCall features 7-8 (portable — system libc only):
#   7. CStruct — is repr('CStruct') layout, .new allocates native memory,
#      field read/write, and a real C call filling a struct (gettimeofday)
#   8. callbacks — a Raku sub passed to a native function as a C function
#      pointer, invoked synchronously (qsort comparator), with CArray copy-back
# Contract: exit 0 + last line PASS.
use NativeCall;
my @fail;

# 7. CStruct new + field read/write, correct layout/alignment
class Point is repr('CStruct') { has int32 $.x is rw; has int32 $.y is rw; has num64 $.mag is rw; }
my $p = Point.new(x => 3, y => 4, mag => 5e0);
@fail.push("cstruct-read ($p.x(),$p.y(),$p.mag())") unless $p.x == 3 && $p.y == 4 && $p.mag == 5e0;
$p.x = 42; $p.y = 99;
@fail.push('cstruct-write') unless $p.x == 42 && $p.y == 99;

# 7b. a real C call fills a struct we pass; read the fields back
class TimeVal is repr('CStruct') { has int64 $.sec; has int64 $.usec; }
sub gettimeofday(TimeVal, Pointer) returns int32 is native {*}
my $tv = TimeVal.new;
gettimeofday($tv, Pointer);
@fail.push("cstruct-filled ($tv.sec())") unless $tv.sec > 1_700_000_000;

# 8. callback: qsort with a Raku comparator, native array sorted in place
sub qsort(CArray[int32], size_t, size_t, &cmp (Pointer, Pointer --> int32)) is native {*}
my $a = CArray[int32].new(5, 2, 8, 1, 9, 3);
my $fires = 0;
qsort($a, 6, 4, -> $pa, $pb {
    $fires++;
    nativecast(CArray[int32], $pa)[0] <=> nativecast(CArray[int32], $pb)[0]
});
@fail.push('callback-fired') unless $fires > 0;
my $sorted = (^6).map({ $a[$_] }).join(',');
@fail.push("qsort-result ($sorted)") unless $sorted eq '1,2,3,5,8,9';

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
