# Regression: a single-dash spelling of a known long option (`-exe`, `-cpp`,
# `-lint`, `-version`, …) was silently mis-parsed — `-exe file` became `-e`
# with glued code "xe", running the empty program "xe" and exiting 0 with no
# output. Now normalized to the `--` form with a note. Real short options
# (-e, -c) and glued `-e'code'` stay untouched. Reported 2026-07-23.

my $ok = True;
my $rk = $*EXECUTABLE;

# -version normalizes (note on stderr, real version on stdout)
my $v = qqx{$rk -version 2>/dev/null}.trim;
unless $v.contains('Raku++') {
    say "FAIL: -version did not act as --version: {$v.raku}";
    $ok = False;
}

# -exe actually compiles (not silently running program "xe")
my $src = 'temp_sd_' ~ $*PID ~ '.raku';
my $exe = 'temp_sd_' ~ $*PID ~ '.bin';
spurt $src, 'print "compiled-ran"';
qqx{$rk -exe $src -o $exe 2>/dev/null};
my $out = $exe.IO.e ?? qqx{./$exe} !! '(no binary produced)';
unlink $src; unlink $exe if $exe.IO.e;
unless $out eq 'compiled-ran' {
    say "FAIL: -exe did not compile+run: {$out.raku}";
    $ok = False;
}

# real short -e is NOT touched
unless qqx{$rk -e 'print 7*6'}.trim eq '42' {
    say 'FAIL: real -e broken';
    $ok = False;
}
# glued -e'code' still runs
unless qqx{$rk -e'print "g"'}.trim eq 'g' {
    say 'FAIL: glued -e broken';
    $ok = False;
}

say 'PASS' if $ok;
