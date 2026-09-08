# Regression: a `META6.json` `provides` entry resolved a module WITHOUT checking
# the `use`'s version constraint, so `use Foo:ver<9.9+>` loaded a 1.2.3 dist.
#
# The provides mapping is tried before the name-derived paths, as Rakudo's
# FileSystem repo does, and it used to `return` the moment it found a file — so
# the constraint was never read. The name-derived path had always been gated,
# which is why the bug only showed for a dist that maps its module explicitly,
# and why it was silent: the wrong module simply loaded.
#
# Every row runs in a child, because a failing `use` is a compile-time error.
# The two fixtures are independent and answer different questions:
#
#   only/  the provides entry points somewhere the module NAME does not describe,
#          so the mapping is the only way to find the file at all
#   both/  the provides entry points at the name-derived path too, so a refusal
#          there proves the fallthrough the fix adds is gated as well and is not
#          a way back in
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $tmp = $*TMPDIR.add("verprov-{$*PID}");

my $only = $tmp.add('only');
$only.add('lib/inner').mkdir;
$only.add('META6.json').spurt: q:to/JSON/;
    { "name": "OnlyDist", "version": "1.2.3", "auth": "zef:test", "api": "1",
      "provides": { "Ver::Only": "lib/inner/Whatever.rakumod" } }
    JSON
$only.add('lib/inner/Whatever.rakumod').spurt:
    "unit module Ver::Only;\nsub which() is export \{ 'only-1.2.3' }\n";

my $both = $tmp.add('both');
$both.add('lib/Ver').mkdir;
$both.add('META6.json').spurt: q:to/JSON/;
    { "name": "BothDist", "version": "1.2.3", "auth": "zef:test", "api": "1",
      "provides": { "Ver::Both": "lib/Ver/Both.rakumod" } }
    JSON
$both.add('lib/Ver/Both.rakumod').spurt:
    "unit module Ver::Both;\nsub which() is export \{ 'both-1.2.3' }\n";

# Runs `use MODULE ADVERBS` in a child and answers what came back: the module's
# own marker when it loaded, or 'not-found' when the `use` was refused. The
# marker cannot be produced by a module that did not load, and 'not-found' is
# only reachable through a failing `use`, so neither answer can be faked.
sub load($dir, $module, $adverbs = '') {
    my $p = run($*EXECUTABLE, '-I', ~$dir,
                '-e', "use $module$adverbs; print which()", :out, :err);
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $p.exitcode == 0 && $out ?? $out !! 'not-found'
}

# The bug: a provides mapping used to satisfy any constraint.
check load($only, 'Ver::Only', ':ver<9.9+>'), 'not-found',
      'an unsatisfiable :ver is refused on a META6-provides dist';
check load($only, 'Ver::Only', ':ver<2>'),    'not-found',
      'and refused for a plain too-new version too';

# …without breaking what a provides mapping is for.
check load($only, 'Ver::Only'),                'only-1.2.3',
      'an unconstrained use still resolves through provides';
check load($only, 'Ver::Only', ':ver<1.0+>'),  'only-1.2.3',
      'a satisfiable :ver resolves through provides';
check load($only, 'Ver::Only', ':ver<1.2.3>'), 'only-1.2.3',
      'an exact :ver resolves through provides';
check load($only, 'Ver::Only', ':ver<*>'),     'only-1.2.3',
      ':ver<*> is the anything requirement, provides included';

# When the mapped path is also the name-derived one, a refusal must stand: the
# fix falls through to that path rather than returning, and it is gated too.
check load($both, 'Ver::Both', ':ver<9.9+>'), 'not-found',
      'falling through to the name-derived path is not a way back in';
check load($both, 'Ver::Both', ':ver<1.0+>'), 'both-1.2.3',
      'and that dist still loads when the constraint is satisfiable';

for $only.add('lib/inner/Whatever.rakumod'), $only.add('META6.json'),
    $both.add('lib/Ver/Both.rakumod'), $both.add('META6.json') { .unlink }
for $only.add('lib/inner'), $only.add('lib'), $only,
    $both.add('lib/Ver'), $both.add('lib'), $both, $tmp { try .rmdir }

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
