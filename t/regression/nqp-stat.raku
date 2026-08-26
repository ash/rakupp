# `nqp::stat($path, nqp::const::STAT_…)` — the op behind every stat field
# IO::Path does not expose. The STAT_* constant names were already known to the
# parser; the OP was not, so `nqp::stat` fell through to an ordinary call and
# died "Undefined routine 'nqp::stat'" at runtime. That is what stopped
# Path::Finder — it matches on inode, device, uid, gid, nlinks, blocks,
# blocksize, devtype and is-dev, and keys its symlink-loop guard on
# inode+device — and with it App::Prove6, which depends on Path::Finder.
#
# Contract: exit 0 + last line PASS.
use nqp;
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub ok(Mu $c, Str $desc) { @fail.push($desc) unless $c }

my $dir  = $*TMPDIR.add("rakupp-nqp-stat-{$*PID}");
$dir.mkdir;
my $file = $dir.add('f.txt');
$file.spurt('hello');
LEAVE { try $file.unlink; try $dir.add('link').unlink; try $dir.rmdir }

my $p = nqp::unbox_s($file.absolute);
my $d = nqp::unbox_s($dir.absolute);

# the fields with an IO::Path equivalent — checked against it, not against a constant
check nqp::stat($p, nqp::const::STAT_EXISTS),   1,        'STAT_EXISTS on a file';
check nqp::stat($p, nqp::const::STAT_FILESIZE), $file.s,  'STAT_FILESIZE matches .s';
check nqp::stat($p, nqp::const::STAT_ISREG),    1,        'STAT_ISREG';
check nqp::stat($p, nqp::const::STAT_ISDIR),    0,        'STAT_ISDIR on a file';
check nqp::stat($d, nqp::const::STAT_ISDIR),    1,        'STAT_ISDIR on a directory';
check nqp::stat($p, nqp::const::STAT_ISDEV),    0,        'STAT_ISDEV';
check nqp::stat($p, nqp::const::STAT_MODIFYTIME), $file.modified.Int,
      'STAT_MODIFYTIME matches .modified';
check nqp::stat($p, nqp::const::STAT_EXISTS),
      nqp::stat(nqp::unbox_s("/no/such/file"), nqp::const::STAT_EXISTS) + 1,
      'STAT_EXISTS is 0 for a missing path';

# the platform fields, which are the whole reason the op is wanted
ok nqp::stat($p, nqp::const::STAT_PLATFORM_INODE) > 0,     'PLATFORM_INODE is a real inode';
ok nqp::stat($p, nqp::const::STAT_PLATFORM_NLINKS) >= 1,   'PLATFORM_NLINKS';
ok nqp::stat($p, nqp::const::STAT_PLATFORM_BLOCKSIZE) > 0, 'PLATFORM_BLOCKSIZE';
ok nqp::stat($p, nqp::const::STAT_UID) >= 0,               'STAT_UID';
ok nqp::stat($p, nqp::const::STAT_GID) >= 0,               'STAT_GID';
check nqp::stat($p, nqp::const::STAT_PLATFORM_DEV),
      nqp::stat($d, nqp::const::STAT_PLATFORM_DEV),
      'PLATFORM_DEV is the same for two paths on one filesystem';
ok nqp::stat($p, nqp::const::STAT_PLATFORM_INODE)
   != nqp::stat($d, nqp::const::STAT_PLATFORM_INODE),
   'two different paths have different inodes';
check nqp::stat($p, nqp::const::STAT_PLATFORM_MODE) +& 0o170000, 0o100000,
      'PLATFORM_MODE carries the S_IFREG bits';

# symlinks: STAT_ISLNK reports the LINK, and nqp::stat otherwise follows it
if (try { $dir.add('link').symlink($file); True }) {
    my $l = nqp::unbox_s($dir.add('link').absolute);
    check nqp::stat($l, nqp::const::STAT_ISLNK),  1, 'STAT_ISLNK on a symlink';
    check nqp::stat($p, nqp::const::STAT_ISLNK),  0, 'STAT_ISLNK on a plain file';
    check nqp::stat($l, nqp::const::STAT_FILESIZE), $file.s, 'nqp::stat follows the link';
    check nqp::lstat($l, nqp::const::STAT_ISREG),   0,       'nqp::lstat does not';
    check nqp::stat($l, nqp::const::STAT_PLATFORM_INODE),
          nqp::stat($p, nqp::const::STAT_PLATFORM_INODE),
          'the followed link has the target inode';
    ok nqp::lstat($l, nqp::const::STAT_PLATFORM_INODE)
       != nqp::stat($p, nqp::const::STAT_PLATFORM_INODE),
       'the link itself has its own';
}

# an unstattable path throws for every field but STAT_EXISTS, as Rakudo does
my $threw = False;
try { nqp::stat(nqp::unbox_s("/no/such/file"), nqp::const::STAT_FILESIZE); CATCH { default { $threw = True } } }
check $threw, True, 'a missing path throws rather than answering -1';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
