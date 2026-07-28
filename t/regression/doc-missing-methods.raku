# Conformance: methods the documentation exercises that simply did not exist —
# each was a flat "No such method" in the three-way sweep.
#
# Buf.splice is the interesting one: it mutates IN PLACE, and a Buf's bytes are a
# plain std::string rather than a shared_ptr the way an Array's elements are, so
# unlike Array.splice it cannot mutate through a copy. methodCall takes its
# invocant BY VALUE, so it lives beside bufBitOp as a Value&-taking member and is
# reached from the interpreter with the invocant's own slot.
# Contract: exit 0 + last line PASS.
use v6.e.PREVIEW;   # Format is 6.e-only in Rakudo
use experimental :pack;
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

my $p := Proc::Async.new: 'cat', 'some', 'files';
check($p.command.gist, '(cat some files)', 'Proc::Async.command is its argv');

my $d = Format.new("%05d%3x:%s");
check($d.directives.gist, '(d x s)', 'Format.directives names each conversion');
check(Format.new("100%%").directives.gist, '()', 'a literal %% is not a directive');

my $b = Buf.new(1, 1, 2, 3, 5);
check($b.splice(0, 3, <3 2 1>).gist, 'Buf:0x<01 01 02>', 'Buf.splice answers the removed bytes');
check($b.raku, 'Buf.new(3,2,1,3,5)', 'and mutates the buffer in place');

my $b2 = Buf.new(1, 2, 3);
check($b2.splice(1).gist, 'Buf:0x<02 03>', 'a bare offset splices to the end');
check($b2.raku, 'Buf.new(1)', 'leaving the prefix');

check(Blob.new(1..10).unpack("C*").gist, '(1 2 3 4 5 6 7 8 9 10)', 'Blob.unpack C* is every byte');
check(Blob.new(65, 66, 67).unpack("A3").raku, '"ABC"', 'A takes a counted string');
check(Blob.new(65, 66, 67).unpack("C1").raku, '65', 'and a single result is the VALUE, not a 1-element list');
check(Blob.new(65, 66, 67).unpack("C2").raku, '(65, 66)', 'while two results are a List');
check(Blob.new(1, 2, 3, 4).unpack("n*").gist, '(258 772)', 'n is big-endian 16-bit');

my $repo = CompUnit::Repository::FileSystem.new(prefix => $*CWD);
check($repo.files('bin/zef').head.<name> // "Nada", 'Nada',
      'a FileSystem repo answers .files (empty — rakupp does not enumerate dists)');

# An IO::Handle had no rendering of its own and dumped buffer/mode/path as a hash,
# exactly like the Proc gist bug in issue #10.
my $tmp = $*TMPDIR.add("rakupp-fh-test");
$tmp.spurt: "x";
my $fh = $tmp.open;
check($fh.Str, $tmp.Str, 'an IO::Handle Strs as its path');
check($fh.gist, 'IO::Handle<"' ~ $tmp.Str ~ '".IO>(opened)', 'and gists as an IO::Handle');
$fh.close;
$tmp.unlink;

# IO::Path::Parts is Positional as well as Associative, in DECLARATION order
my $parts = IO::Path::Parts.new('C:', '/some/dir', 'foo.txt');
check($parts<volume>, 'C:', 'a part reads by name');
check($parts[0].gist, 'volume => C:', 'and by position, as a Pair');
check($parts[0].^name, 'Pair', 'which really is a Pair');
check($parts[2].gist, 'basename => foo.txt', 'position follows declaration order');
# NOT asserted: .list/.pairs/`for` on a Parts. Rakudo treats it as ONE item there
# (`.list` is `(IO::Path::Parts.new(…),)`) while rakupp spreads it as a Hash — a
# pre-existing divergence that needs the zen-slice `$x[]` semantics to close.

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
