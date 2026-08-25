# Regression: an IO::Path captures $*CWD at CREATION, and `.absolute` resolves
# against that captured base — not against whatever the process directory
# happens to be at call time (2026-08-25, the tail of issue #26's family).
# Rakudo's model throughout: $*CWD is a LOGICAL name — chdir/indir keep the
# spelling the program used (symlinks stay unresolved), `.` and `..` collapse
# textually, and every path made along the way remembers the cwd it was born
# in. Every check below is Rakudo-verified byte-for-byte.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

my $R = "/tmp/rakupp-t-io-cwd-$*PID";
mkdir "$R/sub";

# a path born inside indir keeps that directory as its base, forever
my $p;
indir $R, { $p = "foo".IO }
ck($p.absolute, "$R/foo", '.absolute resolves against the CREATION-time $*CWD');
ck($p.CWD, $R, '.CWD answers the captured base');
ck($p.absolute("/opt"), "/opt/foo", 'an explicit $base still wins');

# $*CWD inside indir is the caller's spelling — /tmp stays /tmp, no realpath
indir $R, { ck($*CWD.Str, $R, '$*CWD keeps the logical spelling inside indir') }

# derived paths inherit the parent's base
indir $R, { $p = "a/b".IO }
ck($p.parent.absolute, "$R/a", '.parent keeps the captured base');
ck($p.child("c").absolute, "$R/a/b/c", '.child keeps the captured base');

# an explicit :CWD outranks the captured one
my $q = IO::Path.new("rel", :CWD("/base"));
ck($q.absolute, "/base/rel", 'IO::Path.new(:CWD) is the base for .absolute');

# .relative's DEFAULT base is the call-time $*CWD, not the path's own
my $r = "$R/sub/x".IO;
indir $R, { ck($r.relative, "sub/x", '.relative defaults to the call-time $*CWD') }

# chdir: answers the new cwd, absolute; a later `..` collapses textually
my $save = $*CWD.Str;
chdir $R;
my $c = chdir "sub";
ck($c.Str, "$R/sub", 'a relative chdir answers the JOINED absolute path');
ck($*CWD.Str, "$R/sub", '$*CWD followed');
chdir "..";
ck($*CWD.Str, $R, 'chdir ".." pops one segment');
ck("z".IO.absolute, "$R/z", 'a path born after chdir is based there');
chdir $save;

# dir() entries carry the call-time base too
spurt "$R/one.txt", "x";
ck(dir($R).sort(*.Str)».CWD.unique.join(","), $save, 'dir() entries capture the caller cwd');

# a carried RELATIVE path: indir resolves it against ITS base, not the caller's
indir $R, { $p = "sub".IO }
indir $p, { ck($*CWD.Str, "$R/sub", 'indir resolves a relative IO::Path via its :CWD') }

unlink "$R/one.txt"; rmdir "$R/sub"; rmdir $R;

say 'PASS' if $ok;
