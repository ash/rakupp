# `CompUnit::Repository::FileSystem.resolve($spec)` answers a CompUnit for a
# module the tree holds, and Nil for one it does not. It had no arm of its own,
# so the call fell through to IO::Path's `.resolve` and came back as a PATH:
# `isa-ok compunit($class, $dir), CompUnit` failed for Identity::Utils and the
# five identity modules that share its first assertion. A distribution root is
# read through its META6 `provides`, as Rakudo reads it; `.prefix` answers too.
use Test;
plan 5;

my $dir = $*TMPDIR.add("rakupp-fsrepo-{$*PID}");
$dir.add('lib/Deep').mkdir;
$dir.add('lib/Deep/Mod.rakumod').spurt: "unit module Deep::Mod;\n";
$dir.add('META6.json').spurt: q:to/META/;
    { "name" : "Deep", "provides" : { "Deep::Mod" : "lib/Deep/Mod.rakumod" } }
    META

sub spec($n) { CompUnit::DependencySpecification.new(short-name => $n) }

my $lib-repo = CompUnit::Repository::FileSystem.new(:prefix($dir.add('lib')), :next-repo($*REPO));
is $lib-repo.prefix.absolute, $dir.add('lib').absolute, 'the repository knows its prefix';
isa-ok $lib-repo.resolve(spec 'Deep::Mod'), CompUnit,   'a module under the prefix resolves';
nok $lib-repo.resolve(spec 'No::Such::Module').defined, '…and one that is absent answers Nil';

my $dist-repo = CompUnit::Repository::FileSystem.new(:prefix($dir), :next-repo($*REPO));
isa-ok $dist-repo.resolve(spec 'Deep::Mod'), CompUnit,  'a dist root resolves through META6 provides';
nok $dist-repo.resolve(spec 'Deep::Gone').defined,      '…and still answers Nil for what it lacks';

END {
    try $dir.add('lib/Deep/Mod.rakumod').unlink; try $dir.add('META6.json').unlink;
    try $dir.add('lib/Deep').rmdir; try $dir.add('lib').rmdir; try $dir.rmdir;
}
