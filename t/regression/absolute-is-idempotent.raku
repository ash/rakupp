# Regression: `.absolute` treated a leading `/` as the only mark of an absolute
# path, so on Windows it prepended the current directory to paths that already
# were one:
#
#   $*EXECUTABLE.absolute
#   -> C:\Users\me\proj/C:\Users\me\proj\build\rakupp.exe
#
# A colon in the middle of a path is ERROR_INVALID_NAME — "the filename,
# directory name, or volume label syntax is incorrect" — so nothing could be
# started from it. `rakupp install` runs every distribution's tests through
# `run $*EXECUTABLE.absolute, …`, which meant NO distribution could install on
# Windows; the failure surfaced as the first test file failing, which sent
# three people looking at the wrong module.
#
# Both assertions below are platform-neutral and were already true on Unix, so
# this pins the property rather than the platform: `.absolute` is IDEMPOTENT,
# and the path it produces can actually start a process.
#
# The drive-letter test is Windows-only on purpose: `C:foo` is an ordinary
# RELATIVE filename on Unix, and Rakudo resolves it against the current
# directory there. Asserted below so a future tidy-up cannot quietly widen it.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $abs = $*EXECUTABLE.absolute;
check $abs.IO.absolute.Str, $abs, '.absolute is idempotent on an already-absolute path';
check $abs.IO.e,            True, '…and the result still names the file it came from';

# The operation the installer actually performs, and the one that failed.
my $p = run($abs, '-e', 'print "child-ok"', :out, :err);
my $out = $p.out.slurp(:close);
$p.err.slurp(:close);
check $out, 'child-ok', 'a child starts from $*EXECUTABLE.absolute';

# A relative path is still resolved, or `.absolute` would mean nothing.
check ('lib'.IO.absolute.Str.starts-with($*CWD.Str)), True, 'a relative path is still made absolute';

# Unix keeps `C:foo` relative — the drive-letter rule must not leak here.
unless $*DISTRO.is-win {
    check ('C:foo'.IO.absolute.Str.starts-with($*CWD.Str)), True,
          'a drive-letter-looking name stays relative off Windows';
}

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say 'PASS';
