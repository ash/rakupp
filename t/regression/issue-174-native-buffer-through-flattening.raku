# Regression: a native call fills a CArray or a Buf that reached it through
# `|@args` (issue #174).
# A byte-backed CArray or Buf was handed to C as a per-call COPY, and the bytes
# C wrote were copied back through the call's argument EXPRESSIONS. `f(|@args)`
# is one expression for every argument, so nothing led back: Gnome::N's
# pattern, a `*@arguments` wrapper that calls `$native(|@arguments)`, left the
# GError slot g_set_error_literal had filled unread. C now gets the storage
# every copy of the value shares, which is what Rakudo's reference type gives it.
#   1. the issue's program: a slurpy wrapper, `|@arguments`, a nativecast callable
#   2. `|@a` and `|$capture` at the top level, CArray[uint8] and Buf, short and long
#   3. the storage is shared, so it must be shared correctly: `Buf.new` of a few
#      bytes is one buffer for every copy (it was a copy each), and `.clone`
#      is a second buffer (a Buf's was the first one under another name)
# The C side mirrors g_set_error_literal, so no glib is needed. Every expected
# value here is what Rakudo 2026.09 prints for the same program.
# Contract: exit 0 + last line PASS.

my $dir = $*TMPDIR.add("rk-nc-flatten-{$*PID}");
$dir.mkdir;
my $c = $dir.add("fill.c");
$c.spurt(q:to/END/);
    #include <stdlib.h>
    #include <string.h>
    typedef struct { unsigned int domain; int code; char *message; } Err;
    /* g_set_error_literal's shape: fill *err only while it is NULL */
    void set_err(Err **err, unsigned int domain, int code, const char *message) {
        if (!err || *err) return;
        Err *e = malloc(sizeof *e);
        e->domain = domain; e->code = code; e->message = strdup(message);
        *err = e;
    }
    void fill(unsigned char *p, int v, size_t n) { memset(p, v, n); }
    END

my $ext   = $*DISTRO.is-win ?? 'dll' !! ($*KERNEL.name eq 'darwin' ?? 'dylib' !! 'so');
my $dylib = $dir.add($*DISTRO.is-win ?? "fill.$ext" !! "libfill.$ext");
my $cc = run 'cc', '-shared', '-fPIC', '-o', $dylib.absolute, $c.absolute, :out, :err;
$cc.out.slurp(:close);
my $ccerr = $cc.err.slurp(:close);
if $cc.exitcode != 0 {
    note "no C compiler here, nothing to test against: $ccerr";
    say "PASS";
    exit 0;
}

my $child = Q:to/END/.subst('LIBPATH', $dylib.absolute, :g);
    use NativeCall;
    my @fail;
    sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

    class N-Error is repr('CStruct') {
        has uint32 $.domain;
        has int32  $.code;
        has Str    $.message;
    }
    sub set_err(CArray[N-Error], uint32, int32, Str) is native('LIBPATH') { * }
    sub fill-c(CArray[uint8], int32, size_t) is native('LIBPATH') is symbol('fill') { * }
    sub fill-b(Buf, int32, size_t) is native('LIBPATH') is symbol('fill') { * }
    my $filled = 'N-Error.new(domain => 45444, code => 1012342, message => "my error")';
    my uint32 $domain = 45444;
    my int32 $code = 1012342;
    my Str $message = 'my error';

    # 1. the issue's program
    my $inner;
    sub call-native-sub(*@arguments) {
        set_err(|@arguments);
        $inner = @arguments[0][0].raku;
    }
    my $e = CArray[N-Error].new(N-Error);
    call-native-sub($e, $domain, $code, $message);
    check $inner, $filled, 'slurpy-wrapper-sees-it';
    check $e[0].raku, $filled, 'slurpy-wrapper-caller-sees-it';

    sub native-function(*@parameters --> Callable) {
        my @parameterList = @parameters.map({ Parameter.new(type => $_) });
        my Signature $signature .= new(:params(|@parameterList), :returns(Pointer[void]));
        nativecast($signature, cglobal('LIBPATH', 'set_err', Pointer))
    }
    sub call-cast(*@arguments) {
        my Callable $f = native-function(CArray[N-Error], uint32, int32, Str);
        $f(|@arguments);
    }
    $e = CArray[N-Error].new(N-Error);
    native-function(CArray[N-Error], uint32, int32, Str)($e, $domain, $code, $message);
    check $e[0].raku, $filled, 'cast-direct';
    $e = CArray[N-Error].new(N-Error);
    call-cast($e, $domain, $code, $message);
    check $e[0].raku, $filled, 'cast-through-slurpy';

    # 2. the other ways an argument arrives flattened
    $e = CArray[N-Error].new(N-Error);
    my @a = $e, $domain, $code, $message;
    set_err(|@a);
    check $e[0].raku, $filled, 'flattened-array';
    $e = CArray[N-Error].new(N-Error);
    my $cap = \($e, $domain, $code, $message);
    set_err(|$cap);
    check $e[0].raku, $filled, 'flattened-capture';

    my $ca = CArray[uint8].new(0 xx 4);
    my @c = $ca, 7, 4;
    fill-c(|@c);
    check $ca.list.join(','), '7,7,7,7', 'carray-uint8-flattened';
    my $short = Buf.new(0 xx 4);
    my @b = $short, 8, 4;
    fill-b(|@b);
    check $short.list.join(','), '8,8,8,8', 'short-buf-flattened';
    my $long = Buf.new(0 xx 40);
    @b = $long, 9, 40;
    fill-b(|@b);
    check $long.list.sum, 360, 'long-buf-flattened';
    my $direct = Buf.new(0 xx 3);
    fill-b($direct, 5, 3);
    check $direct.list.join(','), '5,5,5', 'buf-direct';
    my $view = buf8.new(0 xx 3);
    fill-b($view, 6, 3);
    check $view.list.join(','), '6,6,6', 'buf8-direct';

    # 3. one buffer per buffer
    for Buf.new(1, 2), Buf.new(1 xx 30), buf8.new(1, 2), Buf[uint8].new(1, 2) -> $b {
        my $alias = $b;
        $alias[0] = 99;
        check $b[0], 99, "copy-shares-{$b.elems}-{$b.^name}";
        my $clone = $b.clone;
        $clone[1] = 50;
        check $b[1], $b.elems == 2 ?? 2 !! 1, "clone-is-its-own-{$b.elems}-{$b.^name}";
        check $clone[0], 99, "clone-has-the-bytes-{$b.elems}-{$b.^name}";
        check $clone === $b, False, "clone-is-another-object-{$b.elems}-{$b.^name}";
        check $alias === $b, True, "copy-is-the-same-object-{$b.elems}-{$b.^name}";
    }
    my $cb = Buf.new(0 xx 4);
    my $cbc = $cb.clone;
    fill-b($cbc, 3, 4);
    check $cb.list.join(','), '0,0,0,0', 'c-write-into-a-clone-stays-there';
    check $cbc.list.join(','), '3,3,3,3', 'c-write-into-a-clone';
    my $bl = Blob.new(1 xx 30);
    my $bf = Buf.new($bl);
    $bf[0] = 2;
    check $bl[0], 1, 'buf-from-blob-is-a-copy';

    if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
    END

my $p = run $*EXECUTABLE, '-e', $child, :out, :err;
my $out = $p.out.slurp(:close);
my $err = $p.err.slurp(:close);
note $err if $err;
.IO.unlink for $dylib, $c;
$dir.rmdir;
say $p.exitcode == 0 && $out.lines.tail eq 'PASS' ?? 'PASS' !! 'FAIL';
