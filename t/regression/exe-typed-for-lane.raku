# `for ^$n -> int $i` and `-> Int $i` under `--exe -O`: the unboxed loop
# lane refused any `for` with a typed parameter, so the loop a Fortran
# programmer writes first bound every iteration through the typed binder
# where `-> $i` ran on a machine int. It takes the lane now (countedForParam),
# as `-> $i` does; a native operation that leaves int64 bails, and the boxed
# loop, binding the native as it always has, wraps it.
#
# This file holds the interpreter, `--exe` and `--exe -O` to Rakudo 2026.09's
# lines, and counts the lanes `-O` emits, so it cannot drift to the boxed
# path unnoticed.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-exe-typed-for-lane-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }
my $src = q:to/END/;
    sub isum($n) { my $s = 0; for ^$n -> int $i { $s = $s + $i }; $s }
    sub Isum($n) { my $s = 0; for 1 .. $n -> Int $i { $s = $s + $i * $i }; $s }
    sub wrapi($n) { my $w = 0; for ^$n -> int $i { $w = $i + 9223372036854775800 }; $w }
    sub wrapI($n) { my $w = 0; for ^$n -> Int $i { $w = $i + 9223372036854775800 }; $w }
    sub nest($n) { my $s = 0; for ^$n -> int $i { for ^$n -> int $j { $s = $s + $i * $j } }; $s }
    sub flt($n) { my $s = 0e0; for ^$n -> int $i { $s = $s + $i * 0.5e0 }; $s }
    sub ctl($n) { my $s = 0; for 1 .. $n -> int $i { next if $i %% 3; last if $i > 50; $s = $s + $i }; $s }
    say isum(1000);
    say Isum(300);
    say wrapi(100);
    say wrapI(100);
    say nest(30);
    say flt(1000);
    say ctl(1000);
    END

# What Rakudo 2026.09 prints for $src, line for line.
my @want =
    '499500',                   # isum
    '9045050',                  # Isum
    '-9223372036854775717',     # wrapi: the native wraps, after the lane's bail
    '9223372036854775899',      # wrapI: an Int does not
    '189225',                   # nest
    '249750',                   # flt
    '867';                      # ctl

sub held($what, $out) {
    my @got = $out.lines;
    for ^max(+@want, +@got) -> $k {
        @fail.push("$what, line {$k + 1}: got {(@got[$k] // '(nothing)').raku}, want {(@want[$k] // '(nothing)').raku}")
            if (@got[$k] // '') ne (@want[$k] // '');
    }
}

my $f = $dir.add("typedfor.raku");
$f.spurt($src);
my $interp = run($bin, $f.absolute, :out, :err);
held('interpreted', $interp.out.slurp(:close)); $interp.err.slurp(:close);

# Every typed loop above carries a lane (the outer `nest` loop's own lane
# holds the inner one); a count that falls means one went back to boxed.
my $cpp = run($bin, '--cpp', '-O', $f.absolute, :out, :err);
my $lanes = +$cpp.out.slurp(:close).comb('unboxed loop lane'); $cpp.err.slurp(:close);
@fail.push("only $lanes loops carry a lane, want 7") unless $lanes >= 7;

for (('--exe', '-O'), ('--exe',)) -> @mode {
    my $exe = $dir.add("typedfor-{+@mode}.bin");
    my $c = run($bin, |@mode, $f.absolute, '-o', $exe.absolute, :out, :err);
    $c.out.slurp(:close); my $cerr = $c.err.slurp(:close);
    if !$exe.e { @fail.push("{@mode} did not compile: $cerr"); next }
    @fail.push("{@mode} bundled the program: $cerr") if $cerr.contains('bundling');
    my $r = run($exe.absolute, :out, :err);
    held(@mode.join(' '), $r.out.slurp(:close)); $r.err.slurp(:close);
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
