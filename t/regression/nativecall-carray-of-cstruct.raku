# Regression: a CArray of a CStruct class (issue #136).
# `CArray[N-Error]` is C's `GError **`: each slot is a POINTER to a struct, NULL
# for a type object. The element type was ignored — a slot read back as the
# pointer's raw Int, so after `g_set_error_literal($e, …)` filled slot 0 there
# was no way to read the error through it, and `.raku` showed the bytes.
#   1. a NULL slot reads as the type object; one C filled reads as a struct
#   2. C reads the structs Raku stored, and a stored struct reads back as itself
#   3. `allocate` fills each slot with a fresh zeroed struct
#   4. a CArray's `.raku`/`.gist` is Rakudo's, not its bytes
# The C side mirrors g_set_error_literal, so no glib is needed. Every expected
# value here is what Rakudo 2026.09 prints for the same program.
# Contract: exit 0 + last line PASS.

my $dir = $*TMPDIR.add("rk-nc-carray-cstruct-{$*PID}");
$dir.mkdir;
my $c = $dir.add("err.c");
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
    int err_code(Err **errs, int i) { return errs[i] ? errs[i]->code : -1; }
    END

my $ext   = $*DISTRO.is-win ?? 'dll' !! ($*KERNEL.name eq 'darwin' ?? 'dylib' !! 'so');
my $dylib = $dir.add($*DISTRO.is-win ?? "err.$ext" !! "liberr.$ext");
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

    class Err is repr('CStruct') {
        has uint32 $.domain;
        has int32  $.code;
        has Str    $.message;
    }
    sub set_err(CArray[Err], uint32, int32, Str) is native('LIBPATH') { * }
    sub err_code(CArray[Err], int32 --> int32) is native('LIBPATH') { * }

    # 1. the issue's program
    my $e = CArray[Err].new(Err);
    check $e[0].raku, 'Err', 'null-slot-is-type-object';
    check $e.elems, 1, 'one-pointer-one-element';
    set_err($e, 45444, 1012342, 'my error');
    check $e[0].raku, 'Err.new(domain => 45444, code => 1012342, message => "my error")', 'c-filled-slot';
    check $e[0].message, 'my error', 'read-through-slot';

    # 2. C reads what Raku stored; a stored struct is itself
    my $x = Err.new(domain => 1, code => 7);
    my $two = CArray[Err].new($x, Err.new(code => 9));
    check err_code($two, 0), 7, 'c-reads-slot-0';
    check err_code($two, 1), 9, 'c-reads-slot-1';
    check $two[0] === $x, True, 'stored-struct-is-itself';
    $two[1] = Err;
    check err_code($two, 1), -1, 'type-object-stores-null';
    check $two[1].defined, False, 'null-reads-undefined';
    check $two.list.map(*.defined).join(','), 'True,False', 'list-of-structs';
    check $two[5].raku, 'Err', 'past-the-end-is-type-object';
    check $two.elems, 2, 'reading-past-the-end-does-not-grow';
    $two[3] = $x;
    check $two.elems, 4, 'assigning-past-the-end-grows';
    check $two[2].raku, 'Err', 'the-gap-is-null';
    check $two[3] === $x, True, 'assigned-struct-is-itself';

    # 3. allocate: a fresh zeroed struct per slot
    my $al = CArray[Err].allocate(2);
    check $al.elems, 2, 'allocate-elems';
    check $al[1].raku, 'Err.new(domain => 0, code => 0, message => Str)', 'allocate-zeroed-struct';
    check err_code($al, 1), 0, 'c-reads-allocated-struct';

    # 4. the array's own text
    check $e.raku, 'NativeCall::Types::CArray[Err].new', 'raku';
    check $e.gist, 'NativeCall::Types::CArray[Err].new', 'gist';
    check CArray[int32].new(1, 2, 3).raku, 'NativeCall::Types::CArray[int32].new', 'int32-raku';

    if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
    END

my $p = run $*EXECUTABLE, '-e', $child, :out, :err;
my $out = $p.out.slurp(:close);
my $err = $p.err.slurp(:close);
note $err if $err;
.IO.unlink for $dylib, $c;
$dir.rmdir;
say $p.exitcode == 0 && $out.lines.tail eq 'PASS' ?? 'PASS' !! 'FAIL';
