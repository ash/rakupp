# Regression: `.write` takes BYTES and refuses text — on IO::Handle and
# $*OUT/$*ERR, a Process's stdin handle, Proc::Async and IO::Socket::Async
# (whose Rakudo signatures are `Blob:D $buf`, `Blob:D $b` and `Blob $b`), on a
# sync IO::Socket::INET, and on a class deriving IO::Handle (whose WRITE gets a
# Blob); the string form is `.print`.
#
# A Blob IS a byte Str inside rakupp, so a loose `t == VT::Str` test took any
# text Str as well: it went out as the engine's internal UTF-8 whatever the
# handle's `:enc` said (`.write("é")` on an iso-8859-1 handle wrote 0xC3 0xA9,
# while `.print("é")` wrote 0xE9), and a program written on rakupp then died on
# the MoarVM A/B leg — "Type check failed in binding to parameter '$buf';
# expected Blob but got Str". Green on one engine and red on the other is worse
# than a refusal, so every write arm (IO::Handle and $*OUT/$*ERR, a Proc's stdin
# handle, sync and async sockets, Proc::Async's own `.write`, and a derived
# handle's WRITE) asks writeBytes().
#
# Expectations checked against Rakudo 2026.09; green on both.
# Contract: exit 0 + last line PASS.
my @fail;
sub ck($got, $want, $desc) { @fail.push("$desc — got {$got.raku}, want {$want.raku}") unless $got eqv $want }
sub dies-with(&code, $class, $desc) {
    my $name = 'no error';
    {
        code();
        CATCH { default { $name = .^name } }
    }
    ck $name, $class, $desc;
}

# a class deriving the builtin IO::Handle gets `.write`'s Blob in its WRITE
class CaptureHandle is IO::Handle {
    has @!seen;
    method WRITE($b) { @!seen.push($b); True }
    method seen { @!seen.List }
}

my $dir = $*TMPDIR.add("rakupp-write-blob-$*PID");
$dir.mkdir;
LEAVE { try .unlink for $dir.dir; try $dir.rmdir }

# a Blob is bytes: they go out as they are, whatever the handle's :enc says
{
    my $f = $dir.add('bytes.bin');
    my $fh = $f.open(:w, :enc<iso-8859-1>);
    ck $fh.write(Buf.new(233)), True, '.write answers True, as Rakudo';
    $fh.write(Buf.new(65, 66));
    $fh.close;
    ck $f.slurp(:bin).list, (233, 65, 66), 'a Buf goes out unencoded, :enc notwithstanding';

    my $g = $dir.add('text.bin');
    my $th = $g.open(:w, :enc<iso-8859-1>);
    $th.print("é");
    $th.close;
    ck $g.slurp(:bin).list, (233,), '.print encodes for the handle — the pair that tells them apart';
}

# …and a Str, an Int, a List, or the wrong number of them is refused, before
# anything reaches the file
{
    my $f = $dir.add('refused.bin');
    my $fh = $f.open(:w, :bin);
    dies-with -> { $fh.write("hi") }, 'X::TypeCheck::Binding::Parameter', 'a Str is not a Blob';
    dies-with -> { $fh.write(65) }, 'X::TypeCheck::Binding::Parameter', 'an Int is not a Blob';
    dies-with -> { $fh.write((65, 66)) }, 'X::TypeCheck::Binding::Parameter', 'a List is not a Blob';
    dies-with -> { $fh.write() }, 'X::AdHoc', 'no argument is an arity error';
    dies-with -> { $fh.write(Buf.new(65), Buf.new(66)) }, 'X::AdHoc', 'two positionals are too many';
    $fh.write(Buf.new(90));
    $fh.close;
    ck $f.slurp(:bin).list, (90,), 'nothing from a refused write reached the file';

    # the A/B leg's case: a standard handle took the Str and wrote it
    dies-with -> { $*OUT.write("x") }, 'X::TypeCheck::Binding::Parameter', '$*OUT refuses a Str';
    dies-with -> { $*ERR.write("x") }, 'X::TypeCheck::Binding::Parameter', '$*ERR refuses a Str';
}

# …and a class deriving IO::Handle is handed a Blob by `.write`, never text
{
    my $h = CaptureHandle.new;
    dies-with -> { $h.write("text") }, 'X::TypeCheck::Binding::Parameter', "a derived handle's write refuses a Str";
    ck $h.write(Buf.new(65, 66)), True, 'a Blob reaches the class WRITE';
    ck $h.seen.elems, 1, 'nothing from the refused write reached WRITE';
    ck $h.seen[0].list, (65, 66), 'the Blob arrives whole';
}

# Proc::Async.write is the byte path to the child's stdin
{
    my $p = Proc::Async.new(:w, 'cat');
    $p.stdout.tap: -> $s { };              # drained, so the child never blocks
    my $done = $p.start;
    dies-with -> { $p.write("text") }, 'X::TypeCheck::Binding::Parameter', 'Proc::Async.write refuses a Str';
    ck await($p.write(Buf.new(65, 66))), 2, 'a Blob is written, its byte count kept';
    $p.close-stdin;
    await $done;
}

# a sync IO::Socket::INET inherits IO::Handle's `.write`, so it takes a Blob too
{
    my $srv = IO::Socket::INET.new(:listen, :localhost<127.0.0.1>, :localport(0));
    my $c = IO::Socket::INET.new(:host<127.0.0.1>, :port($srv.localport));
    my $peer = $srv.accept;
    dies-with -> { $c.write("text") }, 'X::TypeCheck::Binding::Parameter', 'IO::Socket::INET.write refuses a Str';
    ck $c.write(Buf.new(65, 66)), True, 'a Blob is sent on a sync socket';
    $c.close;
    $peer.close;
    $srv.close;
}

# …and so is IO::Socket::Async's
{
    my $srv = IO::Socket::Async.listen('127.0.0.1', 0).tap: -> $conn { $conn.Supply(:bin).tap: -> $b { } };
    my $port = $srv.socket-port.result;
    my $c = await IO::Socket::Async.connect('127.0.0.1', $port);
    dies-with -> { $c.write("text") }, 'X::TypeCheck::Binding::Parameter', 'IO::Socket::Async.write refuses a Str';
    ck await($c.write(Buf.new(65, 66))), 2, 'a Blob is sent, its byte count kept';
    $c.close;
    $srv.close;
}

if @fail { say "FAILED:"; .say for @fail; say "FAIL"; exit 1 }
say "PASS";
