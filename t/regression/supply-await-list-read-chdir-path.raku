# Regression: four answers the JavaScript backend gave differently from the
# interpreter (Codex review 2026-11-14, findings 05, 06, 07 and 09). The
# program is plain Raku; t/js/run.raku is what holds the two backends to the
# same output.
#
# - 05: a react or supply body that awaits keeps its context. The JS runtime
#   kept it on a stack that the body's first await left behind, so a
#   `whenever`, `emit` or `done` after the await died "outside the lexical
#   scope of a react/supply block".
# - 06: `.list` on a supply that finishes later taps it once. JS tapped it a
#   second time, running an on-demand supply's body twice, and the list was
#   its pending Promise unless the program's text said "Channel".
# - 07: `.read(n)` hands out n bytes. JS read the whole rest as text.
# - 09: a relative IO::Path made before a chdir still names the same file.
#   JS resolved it against the new directory.
#
# `Supply.from-list([42])` emits 42 by the single-argument rule, where JS
# emitted the array whole; it is checked on the way.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# 05
my @seen;
react {
    await Promise.in(0.01);
    whenever Supply.from-list([42]) { @seen.push($_) }
}
ck @seen, [42], 'a whenever after an await in a react block subscribes';
my $s5 = supply { await Promise.in(0.01); emit 7; emit 8 };
ck $s5.list, (7, 8), 'an emit after an await in a supply block emits';
my $after = '';
react { whenever Promise.in(0.01) { await Promise.in(0.01); $after = 'after'; done } }
ck $after, 'after', 'a done after an await in a whenever ends the react';

# 06
my $runs = 0;
my $s6 = supply { $runs++; whenever Promise.in(0.01) { emit 42 } };
my @got = $s6.list;
ck $runs, 1, '.list runs a supply\'s body once';
ck @got, [42], '…and collects what it emits';

# 07
my $f = $*TMPDIR.add("rakupp-read-n-$*PID.txt");
$f.spurt("héllo\nwörld\n");
my $h = $f.open;
ck $h.read(3).list, (104, 195, 169), '.read(3) is three bytes, not the rest';
ck $h.read(0).list, (), '.read(0) is none';
ck $h.read(4).list, (108, 108, 111, 10), '…and the next .read goes on from there';
ck $h.read(100).list, (119, 195, 182, 114, 108, 100, 10), '…to the end, short of 100';
ck $h.read(1).list, (), '…and then there is nothing left';
$h.close;
$f.unlink;

# 09
my $home = $*CWD;
my $d = $*TMPDIR.add("rakupp-chdir-path-$*PID");
$d.add('sub').mkdir;
chdir $d;
'here.txt'.IO.spurt("here\n");
my $p = 'here.txt'.IO;
chdir 'sub';
ck $p.e, True, 'a relative path made before a chdir still exists';
ck $p.slurp, "here\n", '…and reads the file it named';
ck slurp($p), "here\n", '…through slurp() too';
ck 'here.txt'.IO.e, False, 'a relative path made after it is relative to the new directory';
ck $p.parent.add('sub').d, True, 'a path derived from it keeps its directory';
chdir $home;
$d.add('here.txt').unlink;
$d.add('sub').rmdir;
$d.rmdir;

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
