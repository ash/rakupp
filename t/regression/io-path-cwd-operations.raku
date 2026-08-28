# Regression: file OPERATIONS resolve against the path's own :CWD (2026-08-28).
# `IO::Path.new($rel, :CWD($dir))` carried the base for `.absolute`/`.CWD`, but
# `.e`, `.lines`, `open` & co stat'ed the raw string against the PROCESS cwd —
# so TAP::Harness.run(..., :cwd($dir)) died "Failed to open file" on every
# source it probed (SourceHandler's can-handle reads `$name.lines`). Found
# writing raku.online's TAP page; every check Rakudo-verified.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

my $dir = $*TMPDIR.add("rakupp-cwd-ops-$*PID");
$dir.mkdir;
$dir.add('x.txt').spurt("hello\n");
$dir.add('sub').mkdir;
END { try { .unlink for dir($dir.add('sub')); $dir.add('sub').rmdir; .unlink for dir($dir); $dir.rmdir } }

my $p = IO::Path.new('x.txt', :CWD($dir));
ck($p.e,          True,        '.e finds the file through the captured :CWD');
ck($p.f,          True,        '.f agrees');
ck($p.lines.list, ('hello',),  '.lines reads through the captured :CWD');
ck($p.slurp,      "hello\n",   '.slurp too');
ck($p.s,          6,           '.s stats the right file');
ck(slurp($p),     "hello\n",   'the slurp SUB keeps the path object intact');

# the spelling stays the user's own — only the operations resolve
ck($p.Str,      'x.txt',              '.Str keeps the relative spelling');
ck($p.absolute, $dir.add('x.txt').absolute, '.absolute resolves against the same base');

# a handle opened through :CWD works even after the process moves elsewhere
{
    my $fh = IO::Path.new('x.txt', :CWD($dir)).open;
    ck($fh.get, 'hello', '.open through the captured :CWD');
    $fh.close;
}

# writes: spurt/mkdir/unlink land where the path says, not where the process is
{
    my $q = IO::Path.new('made.txt', :CWD($dir.add('sub')));
    $q.spurt('w');
    ck($dir.add('sub/made.txt').e, True, '.spurt writes into the captured :CWD');
    ck($q.unlink, True, '.unlink removes it there too');
    ck($dir.add('sub/made.txt').e, False, '…and it is gone');
}

# dir() entries keep the argument's base, so chained ops still resolve
{
    my $d = IO::Path.new('sub', :CWD($dir));
    $d.add('inner.txt').spurt('i');
    my @entries = $d.dir;
    ck(@entries.elems, 1, '.dir lists through the captured :CWD');
    ck(@entries[0].slurp, 'i', '…and its entries stay operable');
    $d.add('inner.txt').unlink;
}

say $ok ?? 'PASS' !! 'FAIL';
exit($ok ?? 0 !! 1);
