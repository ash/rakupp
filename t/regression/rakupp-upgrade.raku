# Regression: `rakupp upgrade` — the engine updater (tools/upgrade.raku).
#
# Network-free. Every row here is about a decision the tool makes BEFORE it
# would fetch anything: whose install this is, and whether the release named is
# newer than the one running. `--version` short-circuits the "what is the
# latest" question, so nothing below touches GitHub.
#
# The fixtures are HARD LINKS to the running engine, not symlinks: rakupp
# resolves $*EXECUTABLE through a symlink to its real path, so a symlinked
# fixture would report the REAL prefix and every row would test the same thing.
# A hard link has no target to resolve, so the engine reports the fixture's own
# path — which is the whole point of the fixture.
#
# Three traps these rows exist for:
#
#  1. VERSION ORDER IS NUMERIC. 3.9.0 is older than 3.27.0 and sorts after it
#     as a string, so an engine on 3.27.0 offered 3.9.0 would "upgrade"
#     backwards. See docs/dev/plans/VERSIONS.md.
#  2. THE RECEIPT IS ACTUALLY READ. It is parsed with `try`, and `$j = try EXPR
#     unless $j` does NOT skip the assignment — `try` is a statement prefix, so
#     it takes the modifier inside itself and assigns the skipped statement's
#     empty Slip over the hash just parsed. That shape silently turned every
#     managed install into "not installed by the rakupp installer".
#  3. A FOREIGN PREFIX IS REFUSED, and refused LOUDLY. Overwriting a Homebrew,
#     Nix, Guix or distribution rakupp is that package manager's business, and
#     a refusal that exited 0 would let a script believe it had upgraded.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $rakupp = $*EXECUTABLE.absolute;
my $tmp    = $*TMPDIR.add("rakupp-upgrade-{$*PID}");
$tmp.mkdir;

END { try { rmtree($tmp) } }

sub rmtree($d --> Nil) {
    return unless $d.defined && $d.IO.e;
    for $d.IO.dir -> $e {
        if $e.d && !$e.l { rmtree($e) } else { try $e.unlink }
    }
    try rmdir($d);
}

# A prefix with a rakupp in its bin/, hard-linked so the engine reports THIS
# path as its own. Returns Nil when the link cannot be made (a $TMPDIR on
# another filesystem), and the caller skips rather than fails: a fixture that
# could not be built has proved nothing either way.
sub fixture(Str $name, :$receipt, :$cmake) {
    my $prefix = $tmp.add($name);
    $prefix.add('bin').mkdir;
    my $exe = $prefix.add('bin').add('rakupp');
    my $p = try run 'ln', $rakupp, $exe.absolute, :out, :err;
    if $p { $p.out.slurp(:close); $p.err.slurp(:close) }
    return Nil unless $p && $p.exitcode == 0 && $exe.e;
    $prefix.add('rakupp-install.json').spurt($receipt) if $receipt;
    $prefix.add('CMakeCache.txt').spurt("# a build tree\n") if $cmake;
    return $exe;
}

sub managed-receipt(Str $prefix) {
    qq:to/JSON/;
    \{
      "installed_by": "install.sh",
      "version": "0.0.0",
      "asset": "rakupp-macos-universal.tar.gz",
      "prefix": "$prefix",
      "raku_symlink": false,
      "rc_files": []
    \}
    JSON
}

sub upgrade($exe, *@args) {
    my $p = run $exe.absolute, 'upgrade', |@args, :out, :err;
    my $out = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    return ($p.exitcode, $out);
}

sub check($ok, $what, $detail = '') {
    @fail.push("$what" ~ ($detail ?? " — $detail" !! '')) unless $ok;
}

my $running = $*RAKU.compiler.id;
say "# running engine reports $running";

# ---- a managed install: the receipt is read, and order is numeric -----------
my $managed = fixture('managed', receipt => managed-receipt($tmp.add('managed').absolute));
if $managed {
    my ($rc, $out) = upgrade($managed, '--check', '--version', 'v99.0.0');
    check($rc == 0, '--check on a managed install exits 0', "exit $rc");
    check($out.contains('99.0.0 is available'),
          'a newer release is offered', $out.lines[0] // '');
    # The receipt was READ: without it the tool refuses instead of reporting.
    check(!$out.contains('not installed by the rakupp installer'),
          'the receipt is parsed, not silently lost', $out.lines[*-1] // '');

    # 3.9.0 is OLDER than 3.27.0 and sorts AFTER it as a string.
    ($rc, $out) = upgrade($managed, '--check', '--version', 'v3.9.0');
    check($rc == 0, '--check against an older release exits 0', "exit $rc");
    check(!$out.contains('is available'),
          'an older release is not offered as an upgrade', $out.trim);

    # And the same shape one component further out, so a comparison that
    # happened to work on two digits is not mistaken for a correct one.
    ($rc, $out) = upgrade($managed, '--check', '--version', 'v3.27.0');
    check($rc == 0 && !$out.contains('is available'),
          'the running version is not offered as an upgrade', $out.trim);
}
else {
    say '# skip: could not hard-link a fixture (a $TMPDIR on another filesystem?)';
}

# ---- no receipt: refuse, and say what WOULD work ---------------------------
my $bare = fixture('bare');
if $bare {
    my ($rc, $out) = upgrade($bare, '--check');
    check($rc != 0, 'an unmanaged prefix is refused with a non-zero exit', "exit $rc");
    check($out.contains('install.sh'),
          'and the refusal names the one-liner that would work', $out.trim);
}

# ---- Homebrew: its files, its command --------------------------------------
my $brew = fixture('Cellar/rakupp/9.9.9');
if $brew {
    my ($rc, $out) = upgrade($brew, '--check');
    check($rc != 0, 'a Homebrew prefix is refused', "exit $rc");
    check($out.contains('brew upgrade rakupp'),
          'and the refusal names brew upgrade', $out.trim);
}

# ---- a build tree is git's, not ours ---------------------------------------
my $build = fixture('checkout', :cmake);
if $build {
    my ($rc, $out) = upgrade($build, '--check');
    check($rc != 0, 'a build tree is refused', "exit $rc");
    check($out.contains('build tree'),
          'and the refusal says so', $out.trim);
}

if @fail {
    say "FAIL: $_" for @fail;
    say 'FAIL';
    exit 1;
}
say 'PASS';
