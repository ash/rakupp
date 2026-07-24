# Regression: NativeCall features 1-6, all against system libc/libm (present on
# every arch, so portable):
#   1. is rw / native-reference out-params (T* with copy-back)
#   2. mixed int + float arguments (both orderings)
#   3. nativecast
#   4. cglobal
#   5. Pointer.new / .deref / .defined / Pointer[T]
#   6. CArray element read — byte-backed and live (native memory)
# Contract: exit 0 + last line PASS.
my @fail;

# 1. is rw write-back: time(time_t*) fills the out-param and returns the same value
sub c_time(int64 $t is rw) returns int64 is native is symbol('time') {*}
my int64 $t = 0;
my $now = c_time($t);
@fail.push("is-rw ($t vs $now)") unless $t != 0 && $t == $now;

# 2. mixed int/float args, both orderings. libm's symbols live in the default
# namespace on macOS (libSystem) but not always on Linux — guard resolution, so a
# wrong result still fails while an unresolved symbol just skips.
sub ldexp(num64, int32) returns num64 is native {*}   # float, int
my $ld = try { ldexp(1.5e0, 3) };
with $ld { @fail.push("mixed-float-int ($ld)") unless $_ == 12e0 }
else     { note '# ldexp (libm) not resolvable here — skipping mixed float,int' }
sub jn(int32, num64) returns num64 is native {*}        # int, float
my $j0 = try { jn(0, 0e0) };                            # J0(0) = 1
with $j0 { @fail.push("mixed-int-float ($j0)") unless $_ == 1e0 }
else     { note '# jn (libm) not resolvable here — skipping mixed int,float' }

# 3. nativecast: int address <-> Pointer
my $p = nativecast(Pointer, 4242);
@fail.push('nativecast-ptr') unless $p.Int == 4242;

# 4. cglobal: read a libc global. sys_nerr is a small int on BSD/macOS libc but was
# removed from modern glibc — guard so Linux skips rather than crashing.
my $ne = try { cglobal('c', 'sys_nerr', int32) };
with $ne { @fail.push("cglobal ($ne)") unless $ne ~~ Int && $ne >= 0 }
else     { note '# cglobal sys_nerr not present here (glibc dropped it) — skipping' }

# 5. Pointer API
@fail.push('pointer-null')    unless !Pointer.new.defined;
@fail.push('pointer-addr')    unless Pointer.new(999).Int == 999;

# 6. CArray element read — byte-backed
my $c = CArray[uint8].new(65, 66, 67);
@fail.push('carray-bytes') unless $c[0] == 65 && $c[1] == 66 && $c[2] == 67;
# 6b. live CArray over native memory: getenv returns char*, read its first bytes
sub c_getenv(Str) returns Pointer is native is symbol('getenv') {*}
my $hp = c_getenv('PATH');
if $hp.defined && $hp.Int != 0 {
    my $arr = nativecast(CArray[uint8], $hp);
    @fail.push('carray-live') unless $arr[0] > 0;   # PATH is non-empty
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
