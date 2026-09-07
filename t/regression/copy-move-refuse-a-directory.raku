# `copy` and `move` of a DIRECTORY reported success and did the wrong thing.
#
# copy opened the directory (an ifstream on one succeeds), read nothing out of
# it, and wrote a zero-byte file over the target — then answered True. move
# handed the path to rename(2), which takes a directory happily, so the tree
# moved and the program was told a file had. Rakudo refuses both, and the docs
# say it outright: move "does not" work with directories and points at rename,
# which does. So rename keeps working on one here; copy and move refuse.
#
# The refusals are FAILURES, not throws, as Rakudo's are: all three routines
# `fail`, so `my $ok = copy(…); if $ok {…}` is a program that works. This arm
# used to throw from every path except the same-file check.
#
# Found while probing issue #71 (open/spurt reporting a write that never
# landed) — same family: an operation that could not have worked said it did.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

my $base = $*TMPDIR.add("cpmv-$*PID");
$base.mkdir;
sub fresh-dir($n) { my $d = $base.add($n); $d.mkdir; $d.add('inner').spurt('x'); $d }

# -- copy of a directory refuses, and leaves no rubble ------------------------
{
    my $src = fresh-dir('src-copy');
    my $dst = $base.add('out-copy');
    my $r = copy($src.Str, $dst.Str);
    check($r.^name, 'Failure', 'copy of a directory answers a Failure');
    check($r.exception.^name, 'X::IO::Copy', '…armed with X::IO::Copy');
    check($r.exception.Str.contains('is a directory'), True, '…and says why');
    check($dst.e, False, '…and wrote no zero-byte file over the target');
    check($src.d, True, '…and left the source alone');
}

# -- move of a directory refuses, and does not move it ------------------------
{
    my $src = fresh-dir('src-move');
    my $dst = $base.add('out-move');
    my $r = move($src.Str, $dst.Str);
    check($r.^name, 'Failure', 'move of a directory answers a Failure');
    check($r.exception.^name, 'X::IO::Move', '…armed with X::IO::Move');
    check($dst.e, False, '…and the directory did not move');
    check($src.add('inner').e, True, '…and the source still has its contents');
}

# -- rename still moves a directory: it is the routine that may ---------------
{
    my $src = fresh-dir('src-rename');
    my $dst = $base.add('out-rename');
    check(rename($src.Str, $dst.Str), True, 'rename of a directory still succeeds');
    check($dst.d, True, '…and the directory is at the new name');
    check($dst.add('inner').slurp, 'x', '…with its contents');
    check($src.e, False, '…and gone from the old one');
}

# -- files still copy and move, and their refusals are Failures too -----------
{
    my $f = $base.add('a.txt'); $f.spurt('hello');
    my $c = $base.add('b.txt');
    check(copy($f.Str, $c.Str), True, 'copying a file still works');
    check($c.slurp, 'hello', '…with its content');
    check(move($c.Str, $base.add('c.txt').Str), True, 'moving a file still works');
    check($base.add('c.txt').slurp, 'hello', '…with its content');
    check($c.e, False, '…and the source is gone');

    my $missing = $base.add('not-here').Str;
    for (copy => 'X::IO::Copy', move => 'X::IO::Move', rename => 'X::IO::Rename') -> (:key($op), :value($type)) {
        my $r = do given $op {
            when 'copy'   { copy($missing, $base.add('o1').Str) }
            when 'move'   { move($missing, $base.add('o2').Str) }
            when 'rename' { rename($missing, $base.add('o3').Str) }
        };
        check($r.^name, 'Failure', "$op of a missing source is a Failure, not a throw");
        check($r.exception.^name, $type, "…armed with $type");
    }
}

# -- the method forms agree (they share the one implementation) ---------------
{
    my $src = fresh-dir('src-method');
    my $r = $src.copy($base.add('out-method').Str);
    check($r.^name, 'Failure', '.IO.copy of a directory agrees with copy()');
    check($r.exception.^name, 'X::IO::Copy', '…same type');
}

# clean up whatever survived
for $base.dir -> $e { $e.d ?? (try { .unlink for $e.dir; $e.rmdir }) !! (try $e.unlink) }
try $base.rmdir;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
