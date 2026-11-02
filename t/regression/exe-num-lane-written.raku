# A `my num` (or `my Num`, or a `num $x is copy`) that a loop WRITES got an
# Int lane slot under `--exe -O`. The lane typed a slot by what reached it
# inside the loop, and `$g = -$g` gives nothing a float, so the slot stayed
# I64; the seed that makes a declared float an F64 slot skipped written
# slots, and inside a sub it did not see a `my num` at all. The rtIntSlot
# guard then failed on every entry and the loop ran boxed, so the answers
# were right but slow: PERF-REVIEW-2026-11-02's numloop-n, a Leibniz sum
# over `my num`, ran at 39 ns an iteration against 0.8 untyped (49x C). The
# slot now takes its type from the declaration the parser resolved its uses
# to (VarExpr::readType), so a shadowing untyped `my $h` keeps its Int slot.
#
# This file holds the interpreter, `--exe -O` and `--exe` to Rakudo 2026.09's
# lines, and checks that no lane `-O` emits guards a num-declared slot as an
# Int.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-exe-num-lane-written-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $src = q:to/END/;
    sub leib(int $n) { my num $s = 0e0; my num $g = 1e0; loop (my int $k = 0; $k < $n; $k++) { $s += $g / (2e0 * $k + 1e0); $g = -$g }; "{$s.raku} {$g.raku}" }
    sub boxed(int $n) { my Num $s = 0e0; my Num $g = 1e0; loop (my int $k = 0; $k < $n; $k++) { $s = $s + $g; $g = $g * -5e-1 }; "{$s.raku} {$g.raku}" }
    sub halve(num $x is copy, int $n) { loop (my int $k = 0; $k < $n; $k++) { $x = $x / 2e0 }; $x.raku }
    sub reset(int $n) { my $one = 1; my num $g = 5e0; loop (my int $k = 0; $k < $n; $k++) { $g = $one }; $g.raku }
    sub dz() { my num $s = 1e0; my num $z = 0e0; loop (my int $k = 0; $k < 2; $k++) { $s = $s / $z }; $s }
    sub shadow() { my num $h = 1e0; { my $h = 5; loop (my int $k = 0; $k < 3; $k++) { $h = $h + 1 }; return $h.raku } }
    say leib(10);
    say boxed(10);
    say halve(1e0, 10);
    say (try reset(3)) // "died {$!.^name}";
    say (try dz()) // "died {$!.^name}";
    say shadow();
    my num $s = 0e0; my num $g = 1e0;
    loop (my int $k = 0; $k < 10; $k++) { $s += $g / (2e0 * $k + 1e0); $g = -$g }
    say "{$s.raku} {$g.raku}";
    my num $h = 2e0;
    { my $h = 7; loop (my int $j = 0; $j < 3; $j++) { $h = $h + 1 }; say $h.raku }
    END

# What Rakudo 2026.09 prints for $src, line for line.
my @want =
    '0.7604599047323508e0 1e0',          # leib: `my num`s in a sub
    '0.666015625e0 0.0009765625e0',       # boxed: `my Num`s
    '0.0009765625e0',                     # halve: a `num` parameter `is copy`
    'died X::AdHoc',                      # reset: an Int into a `num` (no lane)
    'died X::AdHoc',                      # dz: a num `/` by zero leaves the lane
    '8',                                  # shadow: an untyped `my $h` inside
    '0.7604599047323508e0 1e0',          # the top level
    '10';                                 # …and its untyped shadow

sub held($what, $out) {
    my @got = $out.lines;
    for ^max(+@want, +@got) -> $k {
        @fail.push("$what, line {$k + 1}: got {(@got[$k] // '(nothing)').raku}, want {(@want[$k] // '(nothing)').raku}")
            if (@got[$k] // '') ne (@want[$k] // '');
    }
}

my $f = $dir.add("written.raku");
$f.spurt($src);
my $interp = run($bin, $f.absolute, :out, :err);
held('interpreted', $interp.out.slurp(:close)); $interp.err.slurp(:close);

# Every lane's entry guard: the num-declared slots ($s $g $x $z) must be
# Num slots, and the untyped shadows ($h) Int ones.
my $cpp = run($bin, '--cpp', '-O', $f.absolute, :out, :err);
my @guards = $cpp.out.slurp(:close).lines.grep(/'if (!(rt'/); $cpp.err.slurp(:close);
for @guards.grep(/'rtIntSlot(v_s' <[sgxz]> ')'/) -> $g { @fail.push("a num slot guarded as an Int: {$g.trim}") }
my $num = +@guards.grep(/'rtNumSlot(v_s' <[sgxz]> ')'/);
@fail.push("$num lanes guard a num slot, want 5") unless $num >= 5;
my $shadow = +@guards.grep(/'rtIntSlot(v_sh)'/);
@fail.push("$shadow lanes guard the untyped \$h as an Int, want 2") unless $shadow >= 2;

for (('--exe', '-O'), ('--exe',)) -> @mode {
    my $exe = $dir.add("written-{+@mode}.bin");
    my $c = run($bin, |@mode, '-q', $f.absolute, '-o', $exe.absolute, :out, :err);
    $c.out.slurp(:close); my $cerr = $c.err.slurp(:close);
    if !$exe.e { @fail.push("{@mode} did not compile: $cerr"); next }
    my $r = run($exe.absolute, :out, :err);
    held(@mode.join(' '), $r.out.slurp(:close)); $r.err.slurp(:close);
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
