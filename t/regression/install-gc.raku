# Regression: `rakupp install --gc` reclaims the blobs `--check` counts as
# unreferenced. Before it, the checker could NAME wasted disk ("5 unreferenced
# blobs") and nothing could remove it — the only code that ever freed a blob was
# uninstall's mark-and-sweep, which considers only the dist being removed. An
# orphan left by an interrupted install or a replaced file stayed forever.
#
# The store is content-addressed: dist/<id> records point at blobs under
# sources/, resources/ and bin/. A blob nothing points at is waste. The
# collector and the checker share ONE live-set routine, so a blob the report
# calls live can never be a blob the collector deletes — which is the property
# these cases are here to hold.
#
# Everything runs against a store built in a temp directory. The user's own
# store is never touched. `rakupp install` is this engine's tool, so on Rakudo
# the file says so and passes.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}

if $*RAKU.compiler.name ne 'Raku++' {
    say "PASS";   # `rakupp install` is Raku++'s own tool
    exit 0;
}

my $root = $*TMPDIR.add("rakupp-gc-{$*PID}");
LEAVE { try { for $root.dir(:!all) { }; run 'rm', '-rf', $root.Str } }

my $LIVE   = 'AAAA1111' x 5;    # 40 hex chars, the store's blob-name shape
my $ORPHAN = 'BBBB2222' x 5;
my $SECOND = 'CCCC3333' x 5;

# short/<sha1 of the module name, uppercase hex>/<dist-id> is how the store
# indexes a provided module; the checker calls a dist with a missing entry
# BROKEN, so a realistic store needs the real directory name.
constant SHORT-KEEP-ME = '544C009F2BBC0069BCAECFFF7FE687599C7F3DA5';   # sha1("Keep::Me")

sub build-store($dir) {
    $dir.add('dist').mkdir;
    $dir.add('sources').mkdir;
    $dir.add('short').add(SHORT-KEEP-ME).mkdir;
    $dir.add('dist').add('DIST1').spurt:
        '{"name":"Keep::Me","version":"1.0","auth":"zef:x",'
        ~ '"files":{"lib/Keep/Me.rakumod":"' ~ $LIVE ~ '"},'
        ~ '"provides":{"Keep::Me":"' ~ $LIVE ~ '"}}';
    $dir.add('sources').add($LIVE).spurt("live content\n");
    $dir.add('short').add(SHORT-KEEP-ME).add('DIST1').spurt("Keep::Me\n1.0\nzef:x\n$LIVE\nDIST1\n");
}

sub gc($dir, *@extra) {
    my $p = run $*EXECUTABLE, 'install', '--gc', "--to={$dir.absolute}", |@extra, :out, :err;
    my $out = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    ($out, $p.exitcode)
}

# 1. --dry-run lists the orphan and removes nothing
{
    my $d = $root.add('a'); $d.mkdir; build-store($d);
    $d.add('sources').add($ORPHAN).spurt("orphan\n");
    my ($out, $rc) = gc($d, '--dry-run');
    check($rc, 0, 'dry run exits 0');
    check($out.contains($ORPHAN), True, 'dry run names the orphan');
    check($out.contains('nothing removed'), True, '…and says nothing was removed');
    check($d.add('sources').add($ORPHAN).e, True, '…the orphan is still there');
    check($d.add('sources').add($LIVE).e,   True, '…and so is the live blob');
}

# 2. the real run removes the orphan and keeps the live blob
{
    my $d = $root.add('b'); $d.mkdir; build-store($d);
    $d.add('sources').add($ORPHAN).spurt("orphan\n");
    $d.add('sources').add($SECOND).spurt("another orphan\n");
    my ($out, $rc) = gc($d);
    check($rc, 0, 'the sweep exits 0');
    check($out.contains('reclaimed'), True, 'it reports what it reclaimed');
    check($d.add('sources').add($ORPHAN).e, False, 'the first orphan is gone');
    check($d.add('sources').add($SECOND).e, False, 'the second orphan is gone');
    check($d.add('sources').add($LIVE).e,   True,  'the live blob survives');
    check($d.add('dist').add('DIST1').e,    True,  '…and so does the dist record');
}

# 3. a blob a short/ entry points at is live even with no dist file naming it
{
    my $d = $root.add('c'); $d.mkdir; build-store($d);
    my $only-short = 'DDDD4444' x 5;
    $d.add('sources').add($only-short).spurt("referenced by an index entry\n");
    $d.add('short').add('BBB').mkdir;
    $d.add('short').add('BBB').add('DIST1').spurt("Other::Mod\n1.0\nzef:x\n$only-short\nDIST1\n");
    my ($out, $rc) = gc($d);
    check($d.add('sources').add($only-short).e, True, 'an index entry keeps its blob alive');
}

# 4. a store it cannot read in full is left alone — deleting against an
#    incomplete live set is how a collector eats an installation
{
    my $d = $root.add('d'); $d.mkdir; build-store($d);
    $d.add('sources').add($ORPHAN).spurt("orphan\n");
    $d.add('dist').add('UNREADABLE').spurt("this is not json");
    my ($out, $rc) = gc($d);
    check($rc, 1, 'a damaged store makes it refuse, exit 1');
    check($out.contains('refusing'), True, '…and say so');
    check($d.add('sources').add($ORPHAN).e, True, '…having removed nothing');
}

# 5. nothing to do is not an error
{
    my $d = $root.add('e'); $d.mkdir; build-store($d);
    my ($out, $rc) = gc($d);
    check($rc, 0, 'a clean store exits 0');
    check($out.contains('nothing to reclaim'), True, '…and says there was nothing to do');
    my $empty = $root.add('f'); $empty.mkdir;
    my ($o2, $rc2) = gc($empty);
    check($rc2, 0, 'a store with no dist/ exits 0');
    check($o2.contains('nothing installed'), True, '…and says so');
}

# 6. --check and --gc agree: what one counts, the other removes
{
    my $d = $root.add('g'); $d.mkdir; build-store($d);
    $d.add('sources').add($ORPHAN).spurt("orphan\n");
    my $c = run $*EXECUTABLE, 'install', '--check', "--to={$d.absolute}", :out, :err;
    my $cout = $c.out.slurp(:close); $c.err.slurp(:close);
    check($cout.contains('1 unreferenced blob'), True, 'the checker counts one');
    my ($out, $rc) = gc($d);
    check($out.contains('from 1 blob'), True, '…and the collector removes one');
    my $c2 = run $*EXECUTABLE, 'install', '--check', "--to={$d.absolute}", :out, :err;
    my $c2out = $c2.out.slurp(:close); $c2.err.slurp(:close);
    check($c2out.contains('0 unreferenced'), True, '…leaving the store clean');
    check($c2out.contains('0 broken'), True, '…and unbroken');
}

# 7. the modes are exclusive, and belong to `install`
{
    my $d = $root.add('h'); $d.mkdir; build-store($d);
    my $p = run $*EXECUTABLE, 'install', '--gc', '--check', "--to={$d.absolute}", :out, :err;
    $p.out.slurp(:close); my $e = $p.err.slurp(:close);
    check($p.exitcode, 2, '--gc with --check is a usage error');
    check($e.contains('pick one'), True, '…which says to pick one');
    my $u = run $*EXECUTABLE, 'uninstall', '--gc', 'Foo', :out, :err;
    $u.out.slurp(:close); my $ue = $u.err.slurp(:close);
    check($u.exitcode, 2, '`uninstall --gc` is a usage error');
    check($ue.contains('belong to `rakupp install`'), True, '…naming where --gc belongs');
}

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
