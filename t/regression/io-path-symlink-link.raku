# Once missing: IO::Path.symlink and IO::Path.link — the METHOD forms.
#
# The sub forms (`symlink($target, $name)`, `link($target, $name)`) existed —
# File::Find's suite builds its fixture tree with them — but the methods Rakudo
# documents, `$target.IO.symlink($name)` and `$target.IO.link($name)`, answered
# X::Method::NotFound. tools/install.raku's ensure-raku-name dates from that
# gap and shells out to `ln -s`; with the method in place under both engines
# it may switch to .symlink whenever convenient.
#
# Semantics pinned against Rakudo 2026.08: the invocant is the TARGET and the
# argument the new NAME; True on success; a relative symlink target is
# absolutized into the link (the OS reads a relative target against the LINK's
# directory, so the raw spelling would dangle from anywhere else); failures
# are X::IO::Symlink / X::IO::Link; Str has neither method.
#
# Contract: exit 0 + last line PASS.

if $*KERNEL.name.starts-with('win') {
    # creating a symlink on Windows needs a privilege most users lack — the
    # POSIX behavior is what this file pins
    say "PASS";
    exit 0;
}

my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my $dir = $*TMPDIR.add("symlink-method-gate-$*PID");
$dir.mkdir;
my $home = $*CWD;
LEAVE { chdir $home; run 'rm', '-rf', $dir.Str }

my $target = $dir.add('target.txt');
$target.spurt("content-42\n");

# --- symlink: create, identify, read through, resolve ------------------------
my $sl = $dir.add('sl');
check $target.symlink($sl.Str), True, '.symlink returns True';
check $sl.l, True, 'the link answers .l';
check $sl.e, True, '...and .e — the target exists';
check $sl.slurp, "content-42\n", 'reading through the link reads the target';
check $sl.resolve.Str, $target.resolve.Str, '.resolve lands on the target';

# --- a relative invocant is absolutized into the link ------------------------
chdir $dir;
check "target.txt".IO.symlink('rel-sl'), True, 'a relative target links';
check "rel-sl".IO.readlink.Str.starts-with('/'), True,
    'the stored target is absolute';
chdir $home;
check $dir.add('rel-sl').slurp, "content-42\n",
    'so the link still reads from another cwd';

# --- hard link: same inode, survives the original -----------------------------
my $hl = $dir.add('hl');
check $target.link($hl.Str), True, '.link returns True';
check $hl.slurp, "content-42\n", 'the hard link reads the content';
$target.unlink;
check $hl.slurp, "content-42\n", '...and keeps it after the original is unlinked';
check $sl.e, False, 'while the symlink now dangles';
check $sl.l, True, '...but is still a link (.l is lstat)';

# --- failures are the typed errors Rakudo throws ------------------------------
check (try $hl.symlink($hl.Str)), Nil, 'symlink onto an existing name fails';
check $!.^name, 'X::IO::Symlink', '...as X::IO::Symlink';
check $!.message.starts-with('Failed to create symlink called '), True,
    '...with Rakudo\'s message shape';
check (try $dir.add('no-such').link($dir.add('hl2').Str)), Nil,
    'hard-linking a missing target fails';
check $!.^name, 'X::IO::Link', '...as X::IO::Link';

# --- Str has neither method, as in Rakudo -------------------------------------
try "x".symlink("y");
check $!.^name, 'X::Method::NotFound', 'Str.symlink is not a thing';
try "x".link("y");
check $!.^name, 'X::Method::NotFound', 'Str.link is not a thing';

# --- the sub forms stay pinned beside the methods -----------------------------
check symlink($hl.Str, $dir.add('sub-sl').Str), True, 'sub-form symlink';
check $dir.add('sub-sl').slurp, "content-42\n", '...reads through';
check link($hl.Str, $dir.add('sub-hl').Str), True, 'sub-form link';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
