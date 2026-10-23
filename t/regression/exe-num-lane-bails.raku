# The unboxed loop lane's Num operations that leave it (NUM-UNBOX-PLAN P0).
# Each was a wrong answer compiled, with `-O` and without — the lanes are on in
# both — where the interpreter and Rakudo 2026.09 agree:
#
#   * a Num `/` with a zero divisor (-0e0 and an Int 0 too) divided in the
#     lane and answered Inf. UNBOX-PLAN P1 took Inf from a boxed path that
#     answers a Failure now, as Rakudo does (rtSlash hands it to applyArith);
#   * a lane with no Int slot had no landing for a bail, so one fell through
#     to the commit and the loop counted as finished with the values it held:
#     a Num `**` that underflows answered the last good power instead of an
#     X::Numeric::Underflow Failure, and the fixed `/` answered the quotient
#     before the zero;
#   * a bail in the right side of `&&` / `||` ran ahead of the whole
#     condition, so `$d != 0e0 && 1e0 / $d > …` left the lane on every zero
#     `$d` it steps around (slower, not wrong: the answers below hold either
#     way, and the lane count says the guarded loops still get a lane);
#   * prefix `-` past int64 bailed with a `break`, which left only the inner
#     loop (also not wrong, the boxed re-run answers it, but the outer loop
#     ran on a value already abandoned).
#
# t/exe/run.raku cannot see these: it compares `-O` with plain `--exe`, and
# both carry the lane. So this file compiles its program both ways and holds
# each to the values Rakudo prints.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-num-lane-bails-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $src = q:to/END/;
    sub outcome($v) { $v ~~ Failure ?? 'Failure ' ~ $v.exception.^name !! $v.^name ~ ' ' ~ $v.Str }
    sub run(&b) { (try b()) // 'died ' ~ $!.^name }
    sub acc($x) { my $s = 0e0; my $i = 0; while $i < 3 { $s = $s + 1e0 / $x; $i++ }; outcome($s) }
    sub store($x) { my $t = 0e0; my $i = 0; while $i < 3 { $t = 1e0 / $x; $i++ }; outcome($t) }
    sub int0($n) { my $t = 0e0; my $i = 0; while $i < 3 { $t = 1e0 / $n; $i++ }; outcome($t) }
    # (`/=` the lane's type pass refuses: the boxed loop, as a control)
    sub comp($x, $k) { my $t = 1e0; my $i = 0; while $i < $k { $t /= $x; $i++ }; outcome($t) }
    sub forr($x) { my $t = 0e0; for 1 .. 3 -> $k { $t = $k / ($x + 0e0) }; outcome($t) }
    sub nest() {
        my $t = 0e0; my $j = 2e0;
        while $j > -1e0 { my $i = 0; while $i < 2 { $t = $t + 1e0 / $j; $i++ }; $j = $j - 1e0 }
        outcome($t)
    }
    sub mid() { my $t = 0e0; my $i = 0e0; while $i < 5e0 { $t = 1e0 / ($i - 2e0); $i = $i + 1e0 }; outcome($t) }
    sub gand($x) {
        my $c = 0; my $d = $x; my $i = 0;
        while $i < 4 { if $d != 0e0 && 1e0 / $d > 4e-1 { $c++ }; $d = $d - 1e0; $i++ }
        outcome($c)
    }
    sub gor($x) {
        my $c = 0; my $d = $x; my $i = 0;
        while $i < 4 { if $d == 0e0 || 1e0 / $d < 0e0 { $c++ }; $d = $d - 1e0; $i++ }
        outcome($c)
    }
    sub nan0() { my $t = 0e0; my $z = 0e0; my $i = 0; while $i < 2 { $t = ($z + 0e0) / $z; $i++ }; outcome($t) }
    sub bynan() { my $t = 0e0; my $n = NaN; my $i = 0; while $i < 2 { $t = 1e0 / $n; $i++ }; outcome($t) }
    sub pw($x) { my $t = 0e0; my $k = 0e0; while $k < 3e0 { $t = $x ** ($k + 1e0); $k = $k + 1e0 }; outcome($t) }
    sub um($x) { my $y = 0; my $k = 0; while $k < 2 { my $j = 0; while $j < 1 { $y = -$x; $j++ }; $k++ }; outcome($y) }
    say run({ acc(0e0) });
    say run({ acc(4e0) });
    say run({ store(0e0) });
    say run({ store(-0e0) });
    say run({ store(4e0) });
    say run({ int0(0) });
    say run({ comp(0e0, 1) });
    say run({ comp(0e0, 3) });
    say run({ forr(0e0) });
    say run({ forr(2e0) });
    say run({ nest() });
    say run({ mid() });
    say run({ gand(2e0) });
    say run({ gor(2e0) });
    say run({ nan0() });
    say run({ bynan() });
    say run({ pw(1e-200) });
    say run({ pw(5e-1) });
    say run({ um(-9223372036854775807 - 1) });
    my $tl = 0e0; my $tz = 0e0; my $ti = 0; while $ti < 2 { $tl = 3e0 / $tz; $ti++ };
    say outcome($tl);
    END

# What Rakudo 2026.09 prints for $src, line for line.
my @want =
    'died X::Numeric::DivideByZero',      # acc(0e0): the quotient used in the loop
    'Num 0.75',                           # acc(4e0)
    'Failure X::Numeric::DivideByZero',   # store(0e0): was Num Inf
    'Failure X::Numeric::DivideByZero',   # store(-0e0): was Num -Inf
    'Num 0.25',                           # store(4e0)
    'Failure X::Numeric::DivideByZero',   # int0(0): a Num over an Int zero
    'Failure X::Numeric::DivideByZero',   # comp(0e0, 1)
    'died X::Numeric::DivideByZero',      # comp(0e0, 3)
    'Failure X::Numeric::DivideByZero',   # forr(0e0): a `for` over a Range
    'Num 1.5',                            # forr(2e0)
    'died X::Numeric::DivideByZero',      # nest(): the zero on the outer loop's third pass
    'Num 0.5',                            # mid(): a bail mid-loop, the loop re-run boxed
    'Int 2',                              # gand(2e0)
    'Int 2',                              # gor(2e0)
    'Failure X::Numeric::DivideByZero',   # nan0(): 0e0 / 0e0
    'Num NaN',                            # bynan(): NaN is not a zero divisor
    'Failure X::Numeric::Underflow',      # pw(1e-200): was Num 1e-200
    'Num 0.125',                          # pw(5e-1)
    'Int 9223372036854775808',            # um(-2**63)
    'Failure X::Numeric::DivideByZero';   # the top level

sub held($what, $out) {
    my @got = $out.lines;
    for ^max(+@want, +@got) -> $k {
        @fail.push("$what, line {$k + 1}: got {(@got[$k] // '(nothing)').raku}, want {(@want[$k] // '(nothing)').raku}")
            if (@got[$k] // '') ne (@want[$k] // '');
    }
}

my $f = $dir.add("bails.raku");
$f.spurt($src);
my $interp = run($bin, $f.absolute, :out, :err);
held('interpreted', $interp.out.slurp(:close)); $interp.err.slurp(:close);

# Every loop but `comp`'s carries a lane; a count that falls means a case
# here has moved to the boxed path, and no longer tests the lane.
my $cpp = run($bin, '--cpp', '-O', $f.absolute, :out, :err);
my $lanes = +$cpp.out.slurp(:close).comb('unboxed loop lane'); $cpp.err.slurp(:close);
@fail.push("only $lanes loops carry a lane, want 15") unless $lanes >= 15;

for (('--exe', '-O'), ('--exe',)) -> @mode {
    my $exe = $dir.add("bails-{+@mode}.bin");
    my $c = run($bin, |@mode, '-q', $f.absolute, '-o', $exe.absolute, :out, :err);
    $c.out.slurp(:close); my $cerr = $c.err.slurp(:close);
    if !$exe.e { @fail.push("{@mode} did not compile: $cerr"); next }
    my $r = run($exe.absolute, :out, :err);
    held(@mode.join(' '), $r.out.slurp(:close)); $r.err.slurp(:close);
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
