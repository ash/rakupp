# Regression: under `--exe` a `do { … }` or `try { … }` block in expression
# position opened no declaration scope of its own, as a bare block does
# (Codegen's DeclScope). Two answers came out of that:
#
# - `my $o = (try { loop (my $a = 0; …) { … }; 'ran' })` did not build, with or
#   without -O: the C++ assigned `v_sa` and never declared it. The loop
#   header's `my` was taken as declared already. Found by t/exe/fuzz.raku's
#   array matrix, which wraps its loops that way.
# - `my $j = 7; my $r = do { my $j = 100; $j }; say $j` printed 100 compiled:
#   at the top level the inner `my` assigned the global.
#
# Each case is checked interpreted (on Rakudo too: those are the answers) and,
# on Raku++, compiled with and without -O against the interpreter.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}

my $prog = q:to/RAKU/;
    my $t = 0;
    my $o = (try { loop (my $a = 0; $a < 4; $a++) { $t += $a }; 'ran' });
    say "$o $t";
    my $u = do { loop (my $a = 0; $a < 3; $a++) { $t += $a }; 'ran' };
    say "$u $t";
    sub f { my $s = 0; my $r = (try { loop (my $i = 0; $i < 4; $i++) { $s += $i }; 'ran' }); "$r $s" }
    say f();
    my $j = 7;
    my $r = do { my $j = 100; $j };
    say "$r $j";
    my $k = 1;
    my $q = (try { my $k = 50; $k + 1 });
    say "$q $k";
    my $n = 1e0;
    my $w = (try { my @x = 1e0 xx 4; loop (my $c = 0; $c < 4; $c++) { $n += @x[$c] }; $n });
    say "$w $n";
    RAKU
my $want = "ran 6\nran 9\nran 6\n100 7\n51 1\n5 5\n";

my $dir = $*TMPDIR.add("rakupp-do-try-scope-{$*PID}");
$dir.mkdir;
LEAVE { for $dir.dir { .unlink }; try $dir.rmdir }
my $src = $dir.add('prog.raku');
$src.spurt($prog);
check(run($*EXECUTABLE, $src.Str, :out).out.slurp(:close), $want, 'interpreted');

if $*RAKU.compiler.name eq 'Raku++' {
    for (), ('-O',) -> @opt {
        my $bin = $dir.add("prog-bin{@opt.join}");
        my $comp = run $*EXECUTABLE, '--exe', |@opt, $src.Str, '-o', $bin.Str, :out, :err;
        $comp.out.slurp(:close); $comp.err.slurp(:close);
        check($comp.exitcode, 0, "--exe {@opt} builds (the C++ named an undeclared variable)");
        check(run($bin.Str, :out).out.slurp(:close), $want, "--exe {@opt} answers as interpreted")
            if $comp.exitcode == 0;
    }
}

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
