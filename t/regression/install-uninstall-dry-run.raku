# Regression: `rakupp uninstall --dry-run Foo` removed Foo. The flag was parsed
# and honoured by install and --gc, but the uninstall and reinstall paths never
# passed it on, so a command meant to preview a removal performed it — and
# printed "uninstalled Foo", the same line a real run prints.
#
# Everything runs against a store built in a temp directory, from a dist made
# there too. The user's own store is never touched. `rakupp install` is this
# engine's tool, so on Rakudo the file says so and passes.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}

if $*RAKU.compiler.name ne 'Raku++' {
    say "PASS";   # `rakupp install` is Raku++'s own tool
    exit 0;
}

my $root = $*TMPDIR.add("rakupp-undry-{$*PID}");
LEAVE { try run 'rm', '-rf', $root.Str }
$root.mkdir;

# a dist with a module AND a bin script, so the wrapper is covered too
my $src = $root.add('Dry-Run');
$src.add('lib/Dry').mkdir;
$src.add('bin').mkdir;
$src.add('META6.json').spurt:
    '{"name":"Dry::Run","version":"0.0.1","auth":"zef:test","raku":"6.d",'
    ~ '"provides":{"Dry::Run":"lib/Dry/Run.rakumod"},'
    ~ '"depends":[],"resources":[],"license":"Artistic-2.0","description":"x"}';
$src.add('lib/Dry/Run.rakumod').spurt("unit module Dry::Run;\nsub dry() is export \{ 42 \}\n");
$src.add('bin/dry-run').spurt("use Dry::Run;\nsay dry;\n");

my $store = $root.add('store');

sub installer(*@args) {
    my $p = run $*EXECUTABLE, |@args, "--to={$store.absolute}", :out, :err;
    my $out = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    %(:$out, exit => $p.exitcode)
}

# Every file in the store, with its content, so "nothing changed" is checked
# on what is there and not on what the tool says. A symlink is recorded by
# where it points: on a machine with no rakupp on PATH the installer links
# bin/rakupp to the engine, and reading through that link took the whole
# binary in as a List of Ints -- gigabytes, which a 4 GB container did not have.
sub snapshot() {
    my %s;
    my @todo = $store;
    while @todo {
        my $d = @todo.shift;
        for $d.dir -> $e {
            if $e.l { %s{$e.relative($store)} = "-> {$e.readlink}" }
            elsif $e.d { @todo.push: $e }
            elsif $e.basename ne 'repo.lock' { %s{$e.relative($store)} = $e.slurp(:bin).List }
        }
    }
    %s
}

my %in = installer('install', '--no-test', $src.Str);
check %in<exit>, 0, 'the dist installs into a temp store';
check $store.add('bin/dry-run').e, True, '…with its bin wrapper';
my %before = snapshot();

# 1. uninstall --dry-run says what it would do, and does none of it
my %un-dry = installer('uninstall', '--dry-run', 'Dry::Run');
check %un-dry<exit>, 0, 'uninstall --dry-run succeeds';
check ?%un-dry<out>.contains('would uninstall Dry::Run'), True, '…and says what it would remove';
check ?%un-dry<out>.contains("\nuninstalled") || %un-dry<out>.starts-with('uninstalled'), False,
      '…without claiming to have removed it';
check snapshot() eqv %before, True, '…and the store is exactly as it was';

# 2. the refusals still speak under --dry-run: a name that is not installed
my %missing = installer('uninstall', '--dry-run', 'Not::There');
check %missing<exit>, 1, 'uninstall --dry-run of a missing name still fails';

# 3. reinstall --dry-run: neither the removal nor the install happens
my %re-dry = installer('reinstall', '--dry-run', $src.Str);
check %re-dry<exit>, 0, 'reinstall --dry-run succeeds';
check ?%re-dry<out>.contains('would uninstall Dry::Run'), True, '…reporting the removal half';
check snapshot() eqv %before, True, '…and the store is exactly as it was';

# 4. and without the flag, uninstall still removes
my %un = installer('uninstall', 'Dry::Run');
check %un<exit>, 0, 'a real uninstall succeeds';
check $store.add('bin/dry-run').e, False, '…and removes the bin wrapper';
check $store.add('dist').dir.elems, 0, '…and the dist record';

say $ok ?? "PASS" !! "FAIL";
exit $ok ?? 0 !! 1;
