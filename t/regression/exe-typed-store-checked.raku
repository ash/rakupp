# A compiled store into a typed scalar skipped the type check everywhere but
# `=`. `$x op= V` (applyArithInto, rtCatAssign, …) and `$x++` stored what they
# computed as it was, so `my Small $x = 5; $x += 100` held 105, `my Int $i = 1;
# $i += 1.5` 2.5 and `$i ~= "a"` "1a", under `--exe` as under `-O`. The int
# lanes did the same: `-O`'s statement lane wrote `.i` into the box, or
# replaced it with a plain Int (`my Num $g = 5e0; $g = $one` held 1, a UInt
# went negative), and a loop lane's store-back wrote `.i` into a subset's box
# (`while $y < 300 { $y += 100 }` on a `my Small $y` ended at 305). A bundled
# program's routines get lanes without `-O`, so plain `--exe` did too.
#
# Each of those stores is now checked as `=` checks it (Codegen::scalarStore):
# an increment or `op=` stores through rtTypedStore, the statement lane keeps
# `.i` in place only under a type every Int passes, and a loop lane refuses a
# slot it writes whose type checks more than the lane's guard ensures.
#
# This file holds the interpreter, `--exe` and `--exe -O` to Rakudo 2026.09's
# lines for two programs: one that compiles natively (`my Num`, UInt, `my Int`,
# `my Str`), and one with a `subset`, which bundles and compiles its routines
# natively — the report must say `native` for each. It also requires `-O`'s
# lane for a `my Int` accumulator, which every Int passes.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-exe-typed-store-checked-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $native = q:to/END/;
    my $one = 1;
    my Num $g = 5e0;
    say (try { $g = $one; $g }) // "died {$!.^name}";
    say (try { $g = $one + 1; $g }) // "died {$!.^name}";
    say $g;
    sub num-local() { my Num $h = 5e0; my $o = 1; $h = $o; $h }
    say (try num-local()) // "died {$!.^name}";
    my UInt $u = 3;
    say (try { $u = $one - 5; $u }) // "died {$!.^name}";
    say (try { $u--; $u--; $u--; $u--; $u }) // "died {$!.^name}";
    say $u;
    sub uint-loop() { my UInt $w = 5; loop (my int $k = 0; $k < 9; $k++) { $w = $w - 1 }; $w }
    say (try uint-loop()) // "died {$!.^name}";
    sub uint-for() { my UInt $w = 5; for ^9 { $w-- }; $w }
    say (try uint-for()) // "died {$!.^name}";
    my Int $i = 1;
    say (try { $i += 1.5; $i }) // "died {$!.^name}";
    say (try { $i ~= "a"; $i }) // "died {$!.^name}";
    say (try { $i //= 2; $i *= 3; $i++; $i }) // "died {$!.^name}";
    sub int-loop() { my Int $j = 0; loop (my int $k = 0; $k < 10; $k++) { $j = $j + $k; $j += 1 }; $j }
    say int-loop();
    sub str-local() { my Str $s = "a"; my $o = 1; $s = $o * 2; $s }
    say (try str-local()) // "died {$!.^name}";
    END

my $subset = q:to/END/;
    subset Small of Int where * < 10;
    sub s-add()   { my Small $x = 5; $x += 100; $x }
    sub s-eq()    { my Small $x = 5; my $o = 1; $x = $o + 100; $x }
    sub s-ok()    { my Small $x = 5; $x += 3; $x++; $x }
    sub s-while() { my Small $y = 5; while $y < 300 { $y += 100 }; $y }
    sub s-loop()  { my Small $y = 1; loop (my int $k = 0; $k < 3; $k++) { $y = $y + 100 }; $y }
    sub s-for()   { my Small $y = 1; for ^30 { $y++ }; $y }
    sub s-kept()  { my Small $x = 5; try { $x += 100 }; $x }
    my Small $c = 0;
    sub bump()    { loop (my int $k = 0; $k < 20; $k++) { $c = $c + 1 }; $c }
    for &s-add, &s-eq, &s-ok, &s-while, &s-loop, &s-for, &s-kept, &bump -> &c {
        say &c.name, ' ', (try c()) // "died {$!.^name}";
    }
    say $c;
    my $n = 0;
    sub count()   { loop (my int $k = 0; $k < 5; $k++) { $n = $n + 2 }; $n }
    say count();
    END

my $TC = 'died X::TypeCheck::Assignment';
# What Rakudo 2026.09 prints for each, line for line.
my %want =
    native => [
        $TC,      # `$g = $one` into a `my Num`: the statement lane's Int
        $TC,      # …and `$g = $one + 1`
        '5',      # $g kept its Num
        $TC,      # the same in a sub
        $TC,      # `$u = $one - 5` into a UInt
        $TC,      # `$u--` past 0
        '0',
        $TC,      # `$w = $w - 1` in a loop lane, past 0
        $TC,      # `$w--` in a `for` lane, past 0
        $TC,      # `$i += 1.5` into a `my Int`: a Rat
        $TC,      # `$i ~= "a"`: a Str
        '4',      # `//=`, `*=`, `++` that pass
        '55',     # a `my Int` accumulator in a loop lane
        $TC],     # `$s = $o * 2` into a `my Str`
    subset => [
        "s-add $TC", "s-eq $TC", 's-ok 9', "s-while $TC", "s-loop $TC", "s-for $TC",
        's-kept 5',            # a refused `+=` leaves the old value
        "bump $TC",            # a file-scope `my Small` written in a routine's loop
        '9', '10'];

sub held($what, @want, $out) {
    my @got = $out.lines;
    for ^max(+@want, +@got) -> $k {
        @fail.push("$what, line {$k + 1}: got {(@got[$k] // '(nothing)').raku}, want {(@want[$k] // '(nothing)').raku}")
            if (@got[$k] // '') ne (@want[$k] // '');
    }
}

for <native subset> -> $name {
    my $f = $dir.add("$name.raku");
    $f.spurt($name eq 'native' ?? $native !! $subset);
    my $interp = run($bin, $f.absolute, :out, :err);
    held("$name, interpreted", %want{$name}, $interp.out.slurp(:close)); $interp.err.slurp(:close);
    for (('--exe', '-O'), ('--exe',)) -> @mode {
        my $exe = $dir.add("$name-{+@mode}.bin");
        my $report = $dir.add("$name-{+@mode}.report");
        my $c = run($bin, |@mode, '-q', $f.absolute, '-o', $exe.absolute, :out, :err,
                    :env(%*ENV, RAKUPP_AOT_REPORT => $report.absolute));
        $c.out.slurp(:close); my $cerr = $c.err.slurp(:close);
        if !$exe.e { @fail.push("$name, {@mode} did not compile: $cerr"); next }
        if $name eq 'native' {
            @fail.push("native, {@mode} bundled: {$cerr.trim}") if $cerr.contains('bundling');
        } else {
            # every routine of the bundled program compiled natively, or the
            # lines above say nothing about the compiled stores
            my @r = ($report.e ?? $report.slurp !! '').lines.grep(/^ 'routine' /);
            @fail.push("subset, {@mode}: {+@r} routines reported, want 9") unless @r == 9;
            @fail.push("subset, {@mode}: {.split("\t")[3]} is not native") for @r.grep({ !.ends-with("\tnative") });
        }
        my $r = run($exe.absolute, :out, :err);
        held("$name, {@mode.join(' ')}", %want{$name}, $r.out.slurp(:close)); $r.err.slurp(:close);
    }
}

# The check costs a `my Int` nothing: its accumulator keeps the loop lane.
my $cpp = run($bin, '--cpp', '-O', $dir.add('native.raku').absolute, :out, :err);
my $code = $cpp.out.slurp(:close); $cpp.err.slurp(:close);
@fail.push('no loop lane guards the `my Int $j` accumulator') unless $code.contains('rtIntSlot(v_sj)');

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
