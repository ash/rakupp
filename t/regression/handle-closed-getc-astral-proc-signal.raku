# Regression: three IO answers the JavaScript backend gave differently from the
# interpreter (Codex review 2026-11-14, findings 08, 10 and 11). The program is
# plain Raku; t/js/run.raku is what holds the two backends to the same output.
#
# - `.getc` hands out a character. The JS host indexed a JavaScript string, so
#   an astral character came out as its high surrogate (55357 for U+1F600)
#   with the low one left behind, and a combining mark came off its base.
# - A closed handle refuses to read or write (X::IO::Closed). The JS host set
#   a flag on close that no reader looked at, and kept answering from the
#   text it had already read.
# - A child killed by a signal has exit code 0 and that signal, and is not a
#   success. The JS host had no signal at all (always 0) and called the
#   missing exit status 1.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $f = $*TMPDIR.add("rakupp-getc-astral-$*PID.txt");
$f.spurt("\x[1F600]e\x[301]x");
my $h = $f.open;
ck $h.getc.ord, 0x1F600, '.getc takes a whole astral character';
ck $h.getc, "e\x[301]", '.getc takes a base and its combining mark';
ck $h.getc, 'x', '…and the next .getc starts after both';
ck $h.getc.defined, False, '…and then there is nothing left';
$h.close;

ck (try $h.get) // $!.^name, 'X::IO::Closed', '.get on a closed handle';
ck (try $h.getc) // $!.^name, 'X::IO::Closed', '.getc on a closed handle';
my $w = $f.open(:w);
$w.close;
ck (try $w.print('x')) // $!.^name, 'X::IO::Closed', '.print on a closed handle';
$f.unlink;

if $*KERNEL.name eq 'win32' {
    say "ok - (no POSIX signals on Windows)" for ^3;
}
else {
    my $p = run '/bin/sh', '-c', 'kill -TERM $$';
    ck $p.exitcode, 0, 'a child killed by a signal has exit code 0';
    ck $p.signal, 15, '…and the signal that killed it';
    ck ?$p, False, '…and is not a success';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
