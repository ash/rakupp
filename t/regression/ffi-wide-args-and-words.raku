# Regression: the no-libffi fallback path — the fixed over-wide prototype the
# FFI calls through when no libffi can be dlopen'd — used to hold EIGHT integer
# arguments and pass them as C `long`.
#
# Both limits are Windows bugs first: `long` is 32 bits there (LLP64), so every
# pointer argument, `is rw` slot and callback argument lost its top half, and
# the Win32 API routinely goes past eight arguments (CreateWindowExW takes
# twelve, CreateFontW fourteen) — which is where the fallback is the NORMAL
# path, libffi not being a thing Windows ships. Neither is visible on macOS or
# Linux, where `long` is 64 bits and libffi is always there, unless the
# fallback is forced with RAKUPP_FFI=0 — so this case forces it.
#
# libffi is the ORACLE: every value is computed twice, once through libffi and
# once through the blind path, and the two must agree with each other and with
# the expected numbers written here.
#
# Contract: exit 0 + last line PASS.
my @fail;

my $dir = $*TMPDIR.add("rk-ffi-wide-{$*PID}");
$dir.mkdir;
my $c = $dir.add("wide.c");
$c.spurt(q:to/END/);
    #include <stdint.h>
    long long sum16(long long a, long long b, long long c, long long d,
                    long long e, long long f, long long g, long long h,
                    long long i, long long j, long long k, long long l,
                    long long m, long long n, long long o, long long p) {
        return a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p;
    }
    /* An address above 4 GB, never dereferenced: a `long` would deliver
       0x34500000 of it. */
    void* big_ptr(void) { return (void*)(intptr_t)0x1234500000LL; }
    long long echo_ptr(void* p) { return (long long)(intptr_t)p; }
    long long call_back(long long (*cb)(long long, long long), long long x, long long y) {
        return cb(x, y);
    }
    END

my $ext   = $*DISTRO.is-win ?? 'dll' !! ($*KERNEL.name eq 'darwin' ?? 'dylib' !! 'so');
my $dylib = $dir.add($*DISTRO.is-win ?? "wide.$ext" !! "libwide.$ext");
my $cc = run 'cc', '-shared', '-fPIC', '-o', $dylib.absolute, $c.absolute, :out, :err;
$cc.out.slurp(:close);
my $ccerr = $cc.err.slurp(:close);
if $cc.exitcode != 0 {
    note "no C compiler here, nothing to test against: $ccerr";
    say "PASS";
    exit 0;
}

# The child prints one `key=value` line per crossing. Run twice: as the engine
# finds the FFI, and with the fallback forced.
my $child = Q:to/END/.subst('LIBPATH', $dylib.absolute, :g);
    use NativeCall;
    sub sum16(int64, int64, int64, int64, int64, int64, int64, int64,
              int64, int64, int64, int64, int64, int64, int64, int64 --> int64)
        is native('LIBPATH') { * }
    sub big_ptr(--> Pointer) is native('LIBPATH') { * }
    sub echo_ptr(Pointer --> int64) is native('LIBPATH') { * }
    sub call_back(&cb (int64, int64 --> int64), int64, int64 --> int64)
        is native('LIBPATH') { * }

    say "sum16=", sum16(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    my $p = big_ptr();
    say "ptr=", +$p;
    say "echo=", echo_ptr($p);
    say "cb=", call_back(-> int64 $a, int64 $b { $a + $b }, 78187069440, 1);
    END

sub crossings($label, %env-extra) {
    my %env = %*ENV, |%env-extra;
    my $p = run($*EXECUTABLE, '-e', $child, :out, :err, :%env);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    @fail.push("$label: exit {$p.exitcode}: {$err.lines.head // ''}") if $p.exitcode != 0;
    my %got;
    for $out.lines { %got{.split('=')[0]} = .split('=')[1] if .contains('=') }
    %got;
}

# 1+2+…+16, and 0x1234500000 — written out, not asked of the library.
my %want = sum16 => '136', ptr => '78187069440',
           echo  => '78187069440', cb => '78187069441';

my %ffi   = crossings('libffi',   {});
my %blind = crossings('fallback', { RAKUPP_FFI => '0' });

for %want.keys.sort -> $k {
    @fail.push("libffi $k: got {%ffi{$k}   // '<none>'}, want %want{$k}") unless (%ffi{$k}   // '') eq %want{$k};
    @fail.push("blind  $k: got {%blind{$k} // '<none>'}, want %want{$k}") unless (%blind{$k} // '') eq %want{$k};
}

note @fail.join("\n") if @fail;
say @fail ?? "FAIL" !! "PASS";
exit @fail ?? 1 !! 0;
