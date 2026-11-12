# Regression: `.write` binds ONE Blob, as every Rakudo handle's does
# (IO::Handle and IO::Socket `Blob:D $buf`, Proc::Async `Blob:D $b`), and
# `.print` is the text form. A Blob is a byte Str in Raku++, so every `.write`
# took text too — and an Int, a List, no argument or two: the text went out as
# the engine's UTF-8 whatever the handle's `:enc` said, so `$fh.write("é")` on
# a Latin-1 handle wrote (195 169) where Rakudo refuses the call and `.print`
# writes (233). The same program was green on one engine and red on the
# other, or silently wrote the wrong bytes. A named argument still binds, as a
# method's `*%_` takes it.
#
# Reported by the open pull request #257, whose change is not used.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
sub refused(&code, $what, $type, $msg) {
    code();
    @fail.push("$what: no exception");
    CATCH {
        default {
            check .^name, $type, "$what: the exception";
            check .message.lines.head.starts-with($msg), True, "$what: the message ({.message.lines.head})";
        }
    }
}
my $bind = 'X::TypeCheck::Binding::Parameter';

refused { $*OUT.write("hi") }, '$*OUT Str', $bind,
        q[Type check failed in binding to parameter '$buf'; expected Blob but got Str ("hi")];
refused { $*ERR.write("hi") }, '$*ERR Str', $bind, q[Type check failed in binding to parameter '$buf'];
refused { $*OUT.write(42) }, 'an Int', $bind, q[Type check failed in binding to parameter '$buf'; expected Blob but got Int (42)];
refused { $*OUT.write((1, 2)) }, 'a List', $bind, q[Type check failed in binding to parameter '$buf'; expected Blob but got List];
refused { $*OUT.write(Str) }, 'a Str type object', $bind, q[Type check failed in binding to parameter '$buf'; expected Blob but got Str (Str)];
refused { $*OUT.write(Blob) }, 'a Blob type object', 'X::Parameter::InvalidConcreteness',
        q[Parameter '$buf' of routine 'write' must be an object instance of type];
refused { $*OUT.write() }, 'no argument', 'X::AdHoc', 'Too few positionals passed; expected 2 arguments but got 1';
refused { $*OUT.write(Blob.new(1), Blob.new(2)) }, 'two', 'X::AdHoc', 'Too many positionals passed; expected 2 arguments but got 3';

my $f = $*TMPDIR.add("rakupp-write-blob-$*PID.txt");
{
    my $fh = open $f, :w, :enc<iso-8859-1>;
    refused { $fh.write("é") }, 'a Latin-1 file, Str', $bind, q[Type check failed in binding to parameter '$buf'];
    $fh.print("é");
    $fh.write(Blob.new(0x21), :foo);
    $fh.write(buf8.new(0x22));
    $fh.write("\x23".encode);
    $fh.close;
    check $f.slurp(:bin).list, (233, 0x21, 0x22, 0x23), '.print encodes; a Blob, a buf8 and a utf8 are bytes';
}
$f.unlink;

{
    my $p = run $*EXECUTABLE, '-e', 'print $*IN.slurp', :in, :out;
    refused { $p.in.write("x") }, 'a Proc stdin, Str', $bind, q[Type check failed in binding to parameter '$buf'];
    $p.in.write("ok".encode);
    $p.in.close;
    check $p.out.slurp(:close), 'ok', 'a Proc stdin takes the Blob';
}
refused { Proc::Async.new($*EXECUTABLE, '-e', '', :w).write("x") }, 'Proc::Async, Str', $bind,
        q[Type check failed in binding to parameter '$b'];

{
    my $srv = IO::Socket::INET.new(:listen, :localhost<127.0.0.1>, :localport(0));
    my $c = IO::Socket::INET.new(:host<127.0.0.1>, :port($srv.localport));
    refused { $c.write("x") }, 'IO::Socket::INET, Str', $bind, q[Type check failed in binding to parameter '$buf'];
    $c.close; $srv.close;
}

class H is IO::Handle {
    has $.got = '';
    method WRITE(IO::Handle:D: Blob:D \data --> True) { $!got ~= data.decode; True }
}
my $h = H.new; $h.encoding('utf8');
$h.write("b".encode);
refused { $h.write("s") }, 'an IO::Handle subclass, Str', $bind, q[Type check failed in binding to parameter '$buf'];
check $h.got, 'b', 'an IO::Handle subclass WRITEs the Blob';
class W is IO::Handle { method write($x) { "own $x" } }
check W.new.write("t"), 'own t', 'a class with its own write is left alone';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
