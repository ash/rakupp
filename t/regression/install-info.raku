# Regression (#289): `rakupp info Foo` — what the store knows about an
# installed distribution, the way `zef info` answers. There was no such command:
# `rakupp install --list` printed every dist, and a reader wanting one dist's
# description, dependencies or dependents had to open its dist/ record by hand.
#
# Everything runs against a store built in a temp directory, from dists made
# there too. The user's own store is never touched, and info reads no network.
# `rakupp info` is this engine's tool, so on Rakudo the file says so and passes.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}

if $*RAKU.compiler.name ne 'Raku++' {
    say "PASS";   # `rakupp info` is Raku++'s own tool
    exit 0;
}

my $root = $*TMPDIR.add("rakupp-info-{$*PID}");
LEAVE { try run 'rm', '-rf', $root.Str }
$root.mkdir;

# Info::Base, and Info::Top that depends on it and ships a bin script
my $base = $root.add('Info-Base');
$base.add('lib/Info').mkdir;
$base.add('META6.json').spurt:
    '{"name":"Info::Base","version":"0.1.0","auth":"zef:test","raku":"6.d",'
    ~ '"authors":["A. Writer"],"tags":["DEMO"],"source-url":"https://example.invalid/info-base.git",'
    ~ '"provides":{"Info::Base":"lib/Info/Base.rakumod","Info::Base::Util":"lib/Info/Base/Util.rakumod"},'
    ~ '"depends":[],"resources":[],"license":"Artistic-2.0","description":"the base of the info test"}';
$base.add('lib/Info/Base').mkdir;
$base.add('lib/Info/Base.rakumod').spurt("unit module Info::Base;\nsub base() is export \{ 1 \}\n");
$base.add('lib/Info/Base/Util.rakumod').spurt("unit module Info::Base::Util;\n");

my $top = $root.add('Info-Top');
$top.add('lib/Info').mkdir;
$top.add('bin').mkdir;
$top.add('META6.json').spurt:
    '{"name":"Info::Top","version":"0.2.0","auth":"zef:test","raku":"6.d",'
    ~ '"provides":{"Info::Top":"lib/Info/Top.rakumod"},'
    ~ '"depends":{"runtime":{"requires":["Info::Base:ver<0.1+>"]},"test":{"requires":["Test"]}},'
    ~ '"resources":[],"license":"MIT","description":"the top of the info test"}';
$top.add('lib/Info/Top.rakumod').spurt("unit module Info::Top;\nuse Info::Base;\n");
$top.add('bin/info-top').spurt("use Info::Top;\n");

my $store = $root.add('store');

sub tool(*@args) {
    my $p = run $*EXECUTABLE, |@args, "--to={$store.absolute}", :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    %(:$out, :$err, exit => $p.exitcode)
}

# before anything is installed: no store at all is "not installed", not a crash
my %none = tool('info', 'Info::Base');
check %none<exit>, 1, 'info on an empty store fails';
check ?%none<err>.contains('not installed: Info::Base'), True, '…saying what is not installed';

check tool('install', '--no-test', $base.Str)<exit>, 0, 'Info::Base installs';
check tool('install', '--no-test', $top.Str)<exit>, 0, 'Info::Top installs';

# 1. the base dist: its META6 fields, provides, and who depends on it
my %b = tool('info', 'Info::Base');
check %b<exit>, 0, 'info Info::Base succeeds';
my $o = %b<out>;
check $o.lines[0], 'Info::Base:ver<0.1.0>:auth<zef:test>', '…identity first';
check ?($o ~~ /'description:' \s+ 'the base of the info test'/), True, '…description';
check ?($o ~~ /'authors:' \s+ 'A. Writer'/), True, '…authors';
check ?($o ~~ /'license:' \s+ 'Artistic-2.0'/), True, '…license';
check ?($o ~~ /'source-url:' \s+ 'https://example.invalid/info-base.git'/), True, '…source-url';
check ?($o ~~ /'installed by:' \s+ 'rakupp'/), True, '…installer';
check ?($o ~~ /'depends:' \s+ '(none)'/), True, '…no dependencies';
check ?($o ~~ /'Info::Base::Util' \s+ \S*? 'sources'/), True, '…each provided module with its blob';
check ?($o ~~ /'required by:' \s+ 'Info::Top:ver<0.2.0>'/), True, '…and what depends on it';

# 2. a module name finds its dist; phase-hash depends split into phases; bin
my %t = tool('info', 'Info::Base::Util', 'Info::Top');
check %t<exit>, 0, 'info by module name and dist name succeeds';
check +%t<out>.lines.grep(*.starts-with('Info::')), 2, '…one identity per dist';
check ?(%t<out> ~~ /'depends:' \s+ 'Info::Base:ver<0.1+>'/), True, '…runtime depends';
check ?(%t<out> ~~ /'test-depends:' \s+ 'Test'/), True, '…test depends';
check ?(%t<out> ~~ /'bin/info-top' \s+ \S*? 'info-top'/), True, '…the bin wrapper';

# 3. -q: identities only; a version that does not match is not installed
check tool('-q', 'info', 'Info::Top')<out>, "Info::Top:ver<0.2.0>:auth<zef:test>\n", '-q prints the identity only';
my %v = tool('info', 'Info::Top:ver<9+>');
check %v<exit>, 1, 'a version not installed fails';
check %v<out>, '', '…and prints nothing to stdout';

# 4. mixed and empty requests are refused, and info wrote nothing
check tool('info')<exit>, 2, 'info with no name is a usage error';
check tool('info', '--list', 'Info::Top')<exit>, 2, 'info --list is refused';
check tool('info', 'Info::Top', 'Not::There')<exit>, 1, 'one missing name fails the run';

say $ok ?? "PASS" !! "FAIL";
exit $ok ?? 0 !! 1;
