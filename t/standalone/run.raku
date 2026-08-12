# The standalone-binary gate (MODULES-PLAN B1/B2/B4/B5): compiled binaries
# that claim to carry their modules are RUN with the module store hidden —
# HOME pointed at an empty directory and RAKULIB cleared — so "standalone" is
# a test, not a claim. Plus the B1 report lines, the B2 refusal, and the B5
# native-library listing.
#
#   build/rakupp t/standalone/run.raku      (needs a C++ toolchain, like t/slim)
#
# Fixtures are self-contained (t/fixtures/standalone + its lib/), so this
# suite does not depend on what happens to be installed on the machine.

my $ROOT = $?FILE.IO.parent.parent.parent;
my $FIX  = $ROOT.add('t/fixtures/standalone');
my $EXE  = $*EXECUTABLE.absolute;
my $tmp  = $*TMPDIR.add("standalone-gate-$*PID");
$tmp.mkdir;
my $emptyhome = $tmp.add('emptyhome');
$emptyhome.mkdir;
LEAVE { run 'rm', '-rf', $tmp.Str }

my $ok = 0;
my $bad = 0;
sub check(Bool() $cond, $desc) {
    if $cond { $ok++;  say "ok - $desc" }
    else     { $bad++; say "NOT OK - $desc" }
}

# ---- B1: the compile says what it embedded ---------------------------------
my $bin = $tmp.add('uses-mod');
my $c1 = run $EXE, '--exe', '--standalone', '-I', $FIX.add('lib').Str,
             $FIX.add('uses-mod.raku').Str, '-o', $bin.Str, :out, :err;
my $c1err = $c1.err.slurp(:close);
check $c1.exitcode == 0, 'a program with an embeddable module builds under --standalone';
check $c1err.contains('embedded 1 module') && $c1err.contains('StandaloneDemo'),
      'B1: the compile names what it embedded';

# ---- B4: the binary runs with the module store HIDDEN ----------------------
my $r = run 'env', "HOME={$emptyhome}", 'RAKULIB=', $bin.Str, :out, :err;
my $out = $r.out.slurp(:close);
check $r.exitcode == 0 && $out eq "42\nhello, standalone\n",
      'B4: the binary runs with HOME empty and RAKULIB cleared';

# ---- B2: an unembeddable module refuses the build --------------------------
my $c2 = run $EXE, '--exe', '--standalone', $FIX.add('uses-missing.raku').Str,
             '-o', $tmp.add('nope').Str, :out, :err;
my $c2err = $c2.err.slurp(:close);
check $c2.exitcode != 0, 'B2: --standalone refuses a module it cannot embed';
check $c2err.contains('not embedded: No::Such::Module::ForTheGate')
   && $c2err.contains('not found on the module search path'),
      'B2: …and names the module and the reason';

# …while the same build WITHOUT --standalone succeeds, loudly
my $c3 = run $EXE, '--exe', $FIX.add('uses-missing.raku').Str,
             '-o', $tmp.add('lax').Str, :out, :err;
my $c3err = $c3.err.slurp(:close);
check $c3.exitcode == 0, 'without --standalone the build still succeeds';
check $c3err.contains('the binary will need the disk at run time'),
      'B1: …but the degradation is loud now, not silent';

# ---- B5: native libraries are NAMED, not hidden ----------------------------
my $c4 = run $EXE, '--exe', '--standalone', $FIX.add('uses-native.raku').Str,
             '-o', $tmp.add('uses-native').Str, :out, :err;
my $c4err = $c4.err.slurp(:close);
check $c4.exitcode == 0, 'a NativeCall program builds under --standalone';
check $c4err.contains('native libraries the binary will dlopen') && $c4err.contains(' m'),
      'B5: the native library list names libm';
my $r4 = run 'env', "HOME={$emptyhome}", 'RAKULIB=', $tmp.add('uses-native').Str, :out, :err;
check $r4.exitcode == 0 && $r4.out.slurp(:close).starts-with('0'),
      'B5: …and the binary itself still runs (the dlopen is the run-time contract)';

# ---- run mode refuses the flag (message + no program run, the illegalOpt
# convention every non-compile flag follows) --------------------------------
my $c5 = run $EXE, '--standalone', $FIX.add('uses-mod.raku').Str, :out, :err;
check $c5.err.slurp(:close).contains('Illegal option --standalone')
   && !$c5.out.slurp(:close).contains('42'),
      '--standalone outside a compile mode is refused';

say "standalone gate: $ok ok, $bad failed";
exit 1 if $bad;
