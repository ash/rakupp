# `--exe -O`: a loop in a bare block, or in a `do`/`try` block, at the top
# level types the program's top-level variables as a loop at the top level
# does. `my $a = 3e0` at the top level is a Num, so `$a / $b` in the loop is a
# float division and the loop takes the unboxed lane. Inside a block the lane
# used to know nothing of the top level (it asked `atTopLevel_`, which every
# nested block clears), seeded `$a` as an Int, read `$a / $b` as a Rat
# division and refused the lane. A `do`/`try` block has been a block of its
# own since 37935a34, which is how t/exe/fuzz.raku's 20 `div0` programs, whose
# loops sit in `(try { … })`, lost their lane.
#
# A block's own `my` of the name still shadows it (Codegen's shadowedTop_), in
# that block and the blocks inside it but not after it. A wrong guess would
# only cost the lane, since the slot's guard sends anything else to the boxed
# loop; case 6 holds that, with a top-level Num reassigned an Int.
#
# This file holds the interpreter, `--exe` and `--exe -O` to Rakudo 2026.09's
# lines, and holds `-O`'s guards to what each case's names are.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-exe-lane-top-types-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }
my $src = q:to/END/;
    my $a = 3e0;
    my $b = 2e0;
    my num $g = 1.5e0;
    my $t = 0e0;
    my $i = 0;
    { $i = 0; while $i < 4 { $t = $t + $a / $b; $i = $i + 1 } }
    say "1 $t";
    { my $a = 1; my $u = 0; $i = 0; while $i < 4 { $u = $u + $a; $i = $i + 1 }; say "2 $u {$u.^name}" }
    { my $a = 2; { my $u = 0; $i = 0; while $i < 4 { $u = $u + $a; $i = $i + 1 }; say "3 $u {$u.^name}" } }
    { my $a = 7 }
    { $t = 0e0; $i = 0; while $i < 4 { $t = $t + $a; $i = $i + 1 }; say "4 $t" }
    my $d = do { $t = 0e0; $i = 0; while $i < 4 { $t = $t + $g * $a; $i = $i + 1 }; $t };
    my $e = (try { $t = 0e0; loop (my $k = 0; $k < 3; $k++) { $t = $t + $a / $b }; $t });
    say "5 $d $e";
    $a = 5;
    { $t = 0e0; $i = 0; while $i < 4 { $t = $t + $a; $i = $i + 1 }; say "6 $t {$t.^name}" }
    END
my $want = "1 6\n2 4 Int\n3 8 Int\n4 12\n5 18 4.5\n6 20 Num\n";
my $f = $dir.add('prog.raku');
$f.spurt($src);

my $interp = run($bin, $f.absolute, :out, :err);
my $iout = $interp.out.slurp(:close); $interp.err.slurp(:close);
@fail.push("interpreted: {$iout.raku}") unless $iout eq $want;

# On Rakudo, the interpreted lines are all there is to check
if $*RAKU.compiler.name ne 'Raku++' { say @fail ?? 'FAIL' !! 'PASS'; exit @fail ?? 1 !! 0 }

# The entry check of each of the seven lanes -O emits: the top-level Nums as
# Nums, the shadowing Ints as Ints
my $cpp = run($bin, '--cpp', '-O', $f.absolute, :out, :err);
my $c = $cpp.out.slurp(:close); $cpp.err.slurp(:close);
my @l = $c.lines;
my @guards = (^(@l - 1)).grep({ @l[$_].contains('unboxed loop lane') }).map({ @l[$_ + 1] });   # (each lane's entry check)
@fail.push("only {+@guards} loops carry a lane, want 7") unless @guards >= 7;
my $numA = +@guards.grep(*.contains('rtNumSlot(v_sa)'));
my $intA = +@guards.grep(*.contains('rtIntSlot(v_sa)'));
@fail.push("\$a guarded as a Num in $numA lanes, want 5 (cases 1, 4, 5, 5, 6)") unless $numA >= 5;
@fail.push("\$a guarded as an Int in $intA lanes, want 2 (the shadows of cases 2 and 3)") unless $intA == 2;
@fail.push('$g is not guarded as a Num in the do block') unless $c.contains('rtNumSlot(v_sg)');

for ('--exe',), ('--exe', '-O') -> @mode {
    my $exe = $dir.add('prog' ~ @mode.join.subst('-', '', :g));
    my $comp = run($bin, |@mode, $f.absolute, '-o', $exe.absolute, :out, :err);
    $comp.out.slurp(:close);
    my $cerr = $comp.err.slurp(:close);
    unless $comp.exitcode == 0 { @fail.push("{@mode} did not build: $cerr"); next }
    my $r = run($exe.absolute, :out, :err);
    my $out = $r.out.slurp(:close); $r.err.slurp(:close);
    @fail.push("{@mode}: {$out.raku}") unless $out eq $want;
}

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
