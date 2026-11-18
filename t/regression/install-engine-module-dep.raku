# Regression (issue #272): a dist that declares `"depends": ["Data::Native"]`
# installed, but the installer printed "skipped: Data::Native — not in the
# ecosystem index" in its plan and closed with "warning: 1 module was not
# installed", although the engine answers Data::Native itself. Declaring it is
# the honest META: on Rakudo it makes zef stop early, which is right for a dist
# that needs Raku++. A dependency the engine provides now counts as satisfied
# when no dist of that name is published; a pin newer than the interface the
# engine implements, and a module nobody provides, still warn.
#
# The index is an empty file (RAKUPP_INSTALL_INDEX), which also keeps the REA
# fallback off, and the store is a temp directory: no network, and the user's
# own store is never touched. `rakupp install` is this engine's tool, so on
# Rakudo the file says so and passes.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}

if $*RAKU.compiler.name ne 'Raku++' {
    say "PASS";   # `rakupp install` is Raku++'s own tool
    exit 0;
}

my $root = $*TMPDIR.add("rakupp-enginedep-{$*PID}");
LEAVE { try run 'rm', '-rf', $root.Str }
$root.mkdir;
my $index = $root.add('index.json');
$index.spurt('[]');

sub install-dist(Str $name, Str $dep) {
    my $src = $root.add($name);
    $src.add('lib').mkdir;
    $src.add('META6.json').spurt:
        qq[\{"name":"$name","version":"0.0.1","auth":"zef:test","depends":["$dep"],]
        ~ qq["provides":\{"$name":"lib/$name.rakumod"\}\}];
    $src.add("lib/$name.rakumod").spurt:
        "unit module $name;\nuse Data::Native;\nsub f is export \{ sha256-hex('a'.encode) \}\n";
    my $p = run $*EXECUTABLE, 'install', '--no-test', "--to={$root.add('store').absolute}", $src.Str,
                :out, :err, :env(%*ENV, RAKUPP_INSTALL_INDEX => $index.absolute);
    my $out = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    %(:$out, exit => $p.exitcode)
}

# 1. the issue: Data::Native, unpinned — installed, nothing skipped, no warning
my %r = install-dist('Foo', 'Data::Native');
check %r<exit>, 0, 'a dist depending on Data::Native installs';
check ?%r<out>.contains('skipped'), False, '…with no skipped line in the plan';
check ?%r<out>.contains('warning'), False, '…and no closing warning';
my $use = run $*EXECUTABLE, "-Iinst#{$root.add('store').absolute}", '-e', 'use Foo; print f()', :out, :err;
check $use.out.slurp(:close), 'ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb',
      '…and the installed module runs';
$use.err.slurp(:close);

# 2. the other names the engine answers, with a pin the engine meets
for <JSON::Native CSV::Native Digest::Native Compress::Zlib::Native> -> $dep {
    my %k = install-dist("K{$++}", $dep);
    check ?%k<out>.contains('warning'), False, "$dep is satisfied by the engine";
}
check ?install-dist('Pinned', 'Data::Native:ver<0.0.1+>')<out>.contains('warning'), False,
      'a floor the engine meets is satisfied';

# 3. a floor above the engine's interface is a real dist the store needs
my %new = install-dist('TooNew', 'Data::Native:ver<9.0+>');
check ?%new<out>.contains('Data::Native'), True, 'a pin newer than the engine still warns';

# 4. a module nobody provides still warns, as before
my %none = install-dist('Missing', 'Not::A::Module');
check ?%none<out>.contains('not in the ecosystem index'), True, 'an unknown module still warns';

say $ok ?? "PASS" !! "FAIL";
exit $ok ?? 0 !! 1;
