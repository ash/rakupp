# Regression: IO::Path.chown and the chown sub exist (issue #213).
#
# `$file.chown(uid => 0, gid => 0)` died with "No such method 'chown' for
# invocant of type 'IO::Path'". As in Rakudo, an id left out keeps the file's
# own, the method answers True, and a refusal is a Failure carrying
# X::IO::Chown; the sub answers the paths it changed. Changing the owner to
# someone else needs root, so this keeps to the file's own ids. Every answer
# below is Rakudo 2026.09's.

if $*DISTRO.is-win { say 'PASS'; exit 0 }   # (no chown(2) there)

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $dir = $*TMPDIR.add("rakupp-chown-{$*PID}"); $dir.mkdir;
LEAVE { .unlink for $dir.dir; $dir.rmdir }
my $f = $dir.add('c.txt'); $f.spurt('x');

ck $f.chown, True, 'no ids: nothing changes, and it says True';
ck $f.chown(:uid($f.user), :gid($f.group)), True, 'its own ids';
ck $f.chown(:uid($f.user)), True, 'one id, the other kept';

my $r = $dir.add('nope.txt').chown(:uid($f.user));
ck $r.defined, False, 'a missing file is a Failure';
ck $r.WHAT, Failure, '…of course';
ck $r.exception.^name, 'X::IO::Chown', '…carrying X::IO::Chown';
ck $r.exception.message, "Failed to change owner of '{$dir.add('nope.txt').absolute}' to {$f.user}/: Path does not exist",
   '…with Rakudo\'s message';

ck chown(:uid($f.user), $f.Str, $dir.add('nope.txt').Str).List, ($f.Str,), 'the sub answers the paths it changed';
ck X::IO::Chown.new(:path<p>, :uid(1), :gid(2), :os-error<boom>).message,
   "Failed to change owner of 'p' to 1/2: boom", 'a hand-built X::IO::Chown';
ck (try 'c.txt'.chown).defined, False, 'a Str has no chown';

say 'PASS';
