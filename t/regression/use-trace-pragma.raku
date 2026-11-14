# `use trace` announces each statement on $*ERR before it runs, numbered and
# placed as Rakudo numbers and places them (rakudo#5546): it was ignored.
# `no trace`, and the end of the block, stop it.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my $prog = q:to/END/;
my $a = 1;
use trace;
my $b = 2;
sub f($x) {
    my $y = $x * 2;
    $y + 1
}
say f($a);
for ^2 { say $_ }
if $b > 1 { say "big" }
no trace;
say "untraced";
END
my $file = $*TMPDIR.add("rakupp-trace-{$*PID}.raku");
$file.spurt($prog);
my $proc = run $*EXECUTABLE, $file, :out, :err;
my $out = $proc.out.slurp(:close);
my $err = $proc.err.slurp(:close);
$file.unlink;
check $out, "3\n0\n1\nbig\nuntraced\n", 'the program runs as before';
my $want = q:to/END/;
3 (FILE line 3)
my $b = 2
4 (FILE line 4)
sub f($x) {
    my $y = $x * 2;
    $y + 1
}
8 (FILE line 8)
say f($a)
5 (FILE line 5)
my $y = $x * 2
6 (FILE line 6)
$y + 1
9 (FILE line 9)
for ^2 { say $_ }
10 (FILE line 9)
say $_
10 (FILE line 9)
say $_
12 (FILE line 10)
if $b > 1 { say "big" }
13 (FILE line 10)
say "big"
END
check $err, $want.subst('FILE', $file, :g), 'each statement, as Rakudo traces it (rakudo#5546)';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
