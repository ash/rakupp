# `.resolve(:completely)` must FAIL when a component of the path does not exist
# — that is the whole point of the adverb. The adverb was ignored here, so the
# method answered a path with the unresolved tail glued on, and
# IO::Path::ChildSecure — which decides whether a child is really a child by
# resolving both sides completely — never saw the Failure its contract is built
# on. Ninety distributions sit behind that one.
use Test;
plan 5;

my $missing = $*TMPDIR.add('no-such-dir-98217').add('kid');
my $res = $missing.resolve(:completely);
isa-ok $res, Failure,                   'a non-resolving path fails';
ok $res.exception ~~ X::IO::Resolve,    '…with X::IO::Resolve';
$res.so;  # defuse

my $real = $*TMPDIR.resolve(:completely);
isa-ok $real, IO::Path,                 'an existing path still resolves';
ok $real.absolute.chars,                '…to an absolute path';

my $lenient = $missing.resolve;
isa-ok $lenient, IO::Path,              'without the adverb it stays lenient';
