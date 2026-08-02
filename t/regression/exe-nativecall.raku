# Regression: NativeCall under --exe. Codegen had NO `is native` support — the
# stub body `{ * }` compiled silently and every native call returned Any, so a
# compiled binary gave WRONG ANSWERS with no diagnostic (`getpid() > 0` printed
# False). Now:
#   - literal-lib and default-namespace (`is native(Str)` / type object) subs
#     compile to a bridge through the runtime's own marshaller (RT.callNative,
#     with its per-Callable dlopen/dlsym cache);
#   - shapes the bridge can't carry (`is native(&sub)`, expression lib names,
#     `is rw` out-params, buffer params needing copy-back) raise CodegenError,
#     so --exe takes its designed bundling fallback and stays CORRECT.
# Compiling links the full runtime (~15 s per --exe run), so one program covers
# the native-bridge shapes and one covers the must-fall-back shape.
# Contract: exit 0 + last line PASS.
use NativeCall;

my @fail;
my $rakupp = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-exenc-{$*PID}");
$dir.mkdir;

sub compile-and-run(Str $name, Str $src, Bool :$expect-native) {
    my $f = $dir.add("$name.raku");
    $f.spurt($src);
    my $c = run($rakupp, '--exe', $f.absolute, '-o', $dir.add($name).absolute, :out, :err);
    my $cout = $c.out.slurp(:close) ~ $c.err.slurp(:close);
    return "compile failed: $cout" if $c.exitcode != 0;
    with $expect-native {
        return "expected native compile, got fallback: $cout"
            if $expect-native  && !$cout.contains('(native)');
        return "expected bundling fallback, got native"
            if !$expect-native && $cout.contains('(native)');
    }
    my $r = run($dir.add($name).absolute, :out, :err);
    $r.out.slurp(:close).trim ~ ($r.exitcode == 0 ?? '' !! " [exit {$r.exitcode}]")
}

# needs sqlite for the literal-lib case; the default-namespace case is libc
sub sqlite3_libversion(--> Str) is native('sqlite3') {*}
my $have-sqlite = so try sqlite3_libversion();

# 1. default-namespace + literal-lib subs → true native compile, right answers.
#    The num32/9-argument/variadic lines are the libffi-era shapes: the bridge
#    rebuilds the parameter list in generated C++, and it once dropped `slurpy`,
#    so a variadic call the interpreter got right came out WRONG in the binary
#    (prepared as a fixed call, whose `...` arguments land in registers instead
#    of on the stack). They are also the check that a compiled binary finds and
#    uses libffi at all — it links the same runtime, so it must.
#    They need the libffi backend, so they are appended only when it is live —
#    with RAKUPP_FFI=0, or on a platform with no libffi to load, those same
#    calls are expected to throw and the compiled binary would exit non-zero.
sub ldexpf(num32, int32 --> num32) is native {*}
my $have-ffi = ?(try ldexpf(3e0, 2) == 12e0);

my $src1 = q:to/P/;
    use NativeCall;
    sub getpid(--> int32) is native(Str) {*}
    say getpid() > 0;
    P
$src1 ~= q:to/P/ if $have-ffi;
    sub ldexpf(num32, int32 --> num32) is native {*}
    say ldexpf(3e0, 2);
    sub nine(int32,int32,int32,int32,int32,int32,int32,int32,int32 --> int32)
        is native is symbol('abs') {*}
    say nine(-7,1,2,3,4,5,6,7,8);
    sub snprintf(Str, size_t, Str, *@args --> int32) is native {*}
    say snprintf(Str, 0, "%d-%s", 42, "hi");   # measures "42-hi" without writing
    P
$src1 ~= q:to/P/ if $have-sqlite;
    sub sqlite3_libversion(--> Str) is native('sqlite3') {*}
    say so sqlite3_libversion() ~~ /^\d+\.\d+/;
    P
my $out1 = compile-and-run('nc-native', $src1, :expect-native);
my $want1 = "True";
$want1 ~= "\n12\n7\n5" if $have-ffi;
$want1 ~= "\nTrue"     if $have-sqlite;
@fail.push("native bridge: $out1") unless $out1 eq $want1;

# 2. an `is rw` out-param → CodegenError → bundling fallback, still correct
if $have-sqlite {
    my $out2 = compile-and-run('nc-fallback', q:to/P/, :!expect-native);
        use NativeCall;
        sub open_rw(Str, Pointer is rw --> int32) is native('sqlite3') is symbol('sqlite3_open') {*}
        sub sqlite3_close(Pointer --> int32) is native('sqlite3') {*}
        my Pointer $h .= new;
        my $rc = open_rw(':memory:', $h);
        say "rc=$rc h={+$h != 0}";
        sqlite3_close($h);
        P
    @fail.push("fallback shape: $out2") unless $out2 eq 'rc=0 h=True';
}
else {
    note '# libsqlite3 not loadable — literal-lib and fallback cases skipped';
}

unlink($_) for dir($dir);

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
