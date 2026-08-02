# Regression: the libffi-backed NativeCall marshaller (src/Ffi.h + callNative).
# Each check below was WRONG or impossible before libffi was wired in — the
# fixed 8-integer/8-float prototype could not express any of them:
#   1. num32 arguments and returns (a float passed/read as a double: garbage)
#   2. more than 8 integer register arguments (a clean X::NYI before)
#   3. variadic calls — `*@args` marks where C's `...` begins. Silently wrong
#      before on every ABI that passes varargs on the stack (Apple ARM64).
#   4. typed callbacks: Pointer parameters, any arity, more than 64 of them
#   5. without libffi the same programs must THROW, never compute garbage
# Portable — system libc/libm only, no C compiler needed.
# Contract: exit 0 + last line PASS.
use NativeCall;
my @fail;

sub ldexpf(num32, int32 --> num32) is native {*}
sub strtof(Str, Pointer --> num32) is native {*}
sub nine(int32, int32, int32, int32, int32, int32, int32, int32, int32 --> int32)
    is native is symbol('abs') {*}
sub snprintf(Buf, size_t, Str, *@args --> int32) is native {*}

# 0. Layout — true on both backends, so it runs before the split below.
#    nativesizeof used to answer from a table of its own that disagreed with the
#    marshaller placing the value (`int` was 4 there, 8 everywhere else) and gave
#    a flat 8 for any CStruct class. C's `bool` is one byte, not a word.
@fail.push("sizeof-int ({nativesizeof(int)})")       unless nativesizeof(int)   == 8;
@fail.push("sizeof-bool ({nativesizeof(bool)})")     unless nativesizeof(bool)  == 1;
@fail.push("sizeof-num32 ({nativesizeof(num32)})")   unless nativesizeof(num32) == 4;
class Layout is repr('CStruct') { has int32 $.i; has num64 $.d; has uint8 $.b; }
@fail.push("sizeof-cstruct ({nativesizeof(Layout)})") unless nativesizeof(Layout) == 24;
# a CUnion overlays its fields at offset 0 and is as wide as its widest member
class U is repr('CUnion') { has int32 $.i is rw; has num64 $.d is rw; has uint8 $.b is rw; }
@fail.push("sizeof-cunion ({nativesizeof(U)})") unless nativesizeof(U) == 8;
my $u = U.new(d => 1e0);
$u.i = 0x41424344;
# (0x44 little-endian, 0x41 big-endian — the point is that the fields overlap)
@fail.push("cunion-alias ({$u.b.fmt('%x')})")
    unless $u.i == 0x41424344 && ($u.b == 0x44 || $u.b == 0x41);

# Which backend is live? RAKUPP_FFI=0 (and any platform without a libffi to
# load) takes the fixed-prototype path, where every check below is expected to
# THROW instead — that is the whole point of section 5, so the file has to
# assert the other half of the contract there rather than skip.
my $ffi = ?(try ldexpf(3e0, 2) == 12e0);

unless $ffi {
    for 'num32'    => { ldexpf(3e0, 2) },
        'num32-ret'=> { strtof('2.5', Pointer) },
        'variadic' => { snprintf(buf8.allocate(8), 8, "%d", 1) },
        'nine-args'=> { nine(-7,1,2,3,4,5,6,7,8) }
    -> (:key($what), :value($try)) {
        my $threw = ?(try { $try(); False } // True);
        @fail.push("no-libffi: $what should throw") unless $threw;
    }
    if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
    exit;
}

# 1. num32 — single precision really is single precision
@fail.push("num32-arg ({ldexpf(3e0, 2)})")     unless ldexpf(3e0, 2) == 12e0;
@fail.push("num32-ret ({strtof('2.5', Pointer)})") unless strtof('2.5', Pointer) == 2.5e0;
# and num64 still goes through the other register bank correctly
sub ldexp(num64, int32 --> num64) is native {*}
@fail.push('num64-arg') unless ldexp(3e0, 2) == 12e0;

# 2. past the old 8-register ceiling (nine is declared at the top). abs() reads
# only its first argument; the rest exist to make the marshaller place nine.
@fail.push("nine-args ({nine(-7,1,2,3,4,5,6,7,8)})") unless nine(-7,1,2,3,4,5,6,7,8) == 7;

# 3. variadics: everything after the slurpy is a `...` argument, promoted the
# way C promotes them (int → int64, num → double, Str → char*).
my $b = buf8.allocate(64);
my $n = snprintf($b, 64, "%d and %s and %.2f", 42, "hi", 3.5e0);
my $got = $b.decode('latin-1').substr(0, $n max 0);
@fail.push("variadic ($n:$got)") unless $got eq '42 and hi and 3.50';

# 4. a callback with declared Pointer parameters over 8-byte elements
sub qsort(CArray[num64], size_t, size_t, &cmp (Pointer, Pointer --> int32)) is native {*}
my $a = CArray[num64].new(3.5e0, 1.25e0, 9e0, 2e0);
qsort($a, 4, 8, -> Pointer $x, Pointer $y {
    my $p = nativecast(CArray[num64], $x)[0];
    my $q = nativecast(CArray[num64], $y)[0];
    $p < $q ?? -1 !! ($p > $q ?? 1 !! 0)
});
@fail.push("qsort-num64 ({(^4).map({ $a[$_] }).join(',')})")
    unless (^4).map({ $a[$_] }).join(',') eq '1.25,2,3.5,9';

# 4b. more than the 64 the old trampoline pool could hold
my $made = 0;
for ^80 {
    my &cb = -> Pointer $x, Pointer $y { 0 };
    qsort($a, 1, 8, &cb);   # a 1-element sort never calls cmp; the pointer is still made
    $made++;
}
@fail.push("closures ($made)") unless $made == 80;

# 5. with the backend switched off, the cases it alone can do must throw —
# the old path would have returned garbage for them.
my $child = $*TMPDIR.add("nc-libffi-off-{$*PID}.raku");
$child.spurt: q:to/SRC/;
    use NativeCall;
    sub ldexpf(num32, int32 --> num32) is native {*}
    say ldexpf(3e0, 2);
    SRC
%*ENV<RAKUPP_FFI> = '0';
my $p = run($*EXECUTABLE.absolute, $child.absolute, :out, :err);
my $err = $p.err.slurp(:close); $p.out.slurp(:close);
%*ENV<RAKUPP_FFI>:delete;
$child.unlink;
@fail.push("fallback-loud ({$err.lines.head // 'no error'})")
    unless $err.contains('num32') && $err.contains('libffi');

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
