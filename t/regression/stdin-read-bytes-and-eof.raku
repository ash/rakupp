# Regression (issue #111): `$*IN.read(1)` on a pipe returns each byte in turn,
# and `.eof` turns True only after a read has met the end. Asking `.eof` used to
# load the whole stream into a line cache, so every later read found nothing.
# Contract: exit 0 + last line PASS.
my $code = q:to/END/;
    my $f = $*IN;
    my @r;
    for ^6 { my $b = $f.read(1); @r.push: ($b.elems ?? $b[0] !! 'E') ~ ($f.eof ?? '!' !! '') ~ ($*IN.eof ?? '?' !! '') }
    say @r.join(',');
    END
my $p = run $*EXECUTABLE, '-e', $code, :in, :out;
$p.in.print("\\end\n");
$p.in.close;
my $got = $p.out.slurp(:close).trim;
my $want = '92,101,110,100,10,E!?';
say "got $got" unless $got eq $want;
my $p2 = run $*EXECUTABLE, '-e', 'say $*IN.eof', :in, :out;
$p2.in.close;
my $empty = $p2.out.slurp(:close).trim;
say "untouched empty stdin eof: $empty" unless $empty eq 'False';
say $got eq $want && $empty eq 'False' ?? 'PASS' !! 'FAIL';
