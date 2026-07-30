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

# 1. default-namespace + literal-lib subs → true native compile, right answers
my $src1 = q:to/P/;
    use NativeCall;
    sub getpid(--> int32) is native(Str) {*}
    say getpid() > 0;
    P
$src1 ~= q:to/P/ if $have-sqlite;
    sub sqlite3_libversion(--> Str) is native('sqlite3') {*}
    say so sqlite3_libversion() ~~ /^\d+\.\d+/;
    P
my $out1 = compile-and-run('nc-native', $src1, :expect-native);
my $want1 = $have-sqlite ?? "True\nTrue" !! "True";
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
