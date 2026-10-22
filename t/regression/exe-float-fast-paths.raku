# The `--exe -O` fast paths FLOAT-HEAVY-ISSUES (2026-10-22) asked for, each
# held to the interpreter on the values its guards let through and on the ones
# they send the long way:
#
#   * prefix `-` is rtNeg: an inline Num / machine-Int arm, and prefixNumeric
#     for the rest. It was `0 - $x`, which answered +0 for `-$x` on 0e0 where
#     the interpreter and Rakudo answer -0, and numified an object through its
#     Bridge where prefixNumeric asks its Numeric (issue 18);
#   * `.elems`, `.defined` and a one-argument `.push` make methodCall's own
#     leading tests at the call site (issue 19);
#   * a sub with a plain `@` / `%` parameter takes the direct-Value overload,
#     binding it unitemized as the boxed path does (issue 20);
#   * a top-level `constant` of a machine Int is a literal to the int kernel
#     (issue 11).
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-fast-paths-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $src = q:to/END/;
    sub neg($x) { -$x }
    class N { method Numeric { 42 }; method Bridge { 42e0 } }
    for 0e0, -0e0, 1.5e0, 3, -9223372036854775808, 2**70, 1/3, "5", "-0e0", val("2.5e0"), val("7"), True, Inf, NaN,
        1+2i, [1, 2, 3], (1, 2), "1/2", N.new, FatRat.new(1, 3) -> $c {
        my $r = neg($c); say $r.raku, " ", $r.^name
    }
    say neg(1|2).raku;
    say neg(0e0).Str, " ", 1e0 / neg(1e300 * 1e300);
    my num $n = 0e0; say neg($n);

    sub el($x) { $x.elems }
    sub de($x) { $x.defined }
    sub pu($a, $v) { $a.push($v) }
    my @a = 1, 2, 3; my @e; my %h = a => 1, b => 2;
    say el(@a), el(@e), el((1, 2)), el((1, 2, 3).Seq), el(%h), el(5), el("abc"), el(Any), el(1..10), el([1, [2, 3]]);
    my @sh[3]; my int @ni = 1, 2; my Int @ti = 4, 5; say el(@sh), el(@ni), el(@ti);
    say (try el((1..*).map(* * 2))) // $!.^name;
    say de(1), de(1.5e0), de("x"), de(""), de(Any), de(Nil), de(Int), de(1/2), de(Failure.new("x")),
        de(any(1, Any)), de(True), de(my $u);
    class C { method defined { False } }; say de(C.new), de(C);
    my @p; pu(@p, 1); pu(@p, 1.5e0); pu(@p, "s"); pu(@p, [1, 2]); pu(@p, (3, 4)); pu(@p, Nil); pu(@p, %h);
    say @p.raku;
    my Int @tp; pu(@tp, 3); try { pu(@tp, "x") }; say $!.^name, " ", @tp.raku;
    my @shp[2]; try { pu(@shp, 1) }; say $!.^name;
    try { pu((1, 2), 3) }; say $!.^name;
    my $arr = [1]; pu($arr, 2); say $arr.raku;
    my int @np; pu(@np, 5); say @np.raku;
    say @a.push(4).elems;

    sub at(@x) { @x.elems ~ " " ~ @x.^name }
    sub ah(%h) { %h.keys.sort.join(",") }
    my $ia = [1, 2, 3];
    say at(@a), "|", at($ia), "|", at((1, 2)), "|", at(1..4), "|", ah(%h), "|", ah({x => 1});
    sub ta(@x) { @x[0] = 99 }; ta(@a); say @a[0];

    constant BIG = 1125899906842624;
    constant NEG = -5;
    sub pc($m) { ($m % BIG) + ($m div BIG) }
    sub pn($m) { $m * NEG }
    say pc(6034823946274365), " ", pn(3), " ", pc(2**70), " ", pn(2**62);
    END

my $f = $dir.add("fast.raku");
$f.spurt($src);
my $interp = run($bin, $f.absolute, :out, :err);
my $iout = $interp.out.slurp(:close); my $ierr = $interp.err.slurp(:close);
my $exe = $dir.add("fast.bin");
my $c = run($bin, '--exe', '-O', $f.absolute, '-o', $exe.absolute, :out, :err);
$c.out.slurp(:close); my $cerr = $c.err.slurp(:close);
if !$exe.e { @fail.push("did not compile: $cerr") }
else {
    # (the typed and shaped arrays bundle the mainline; the routines, where
    # the fast paths are, must all be native)
    @fail.push("a routine stayed interpreted: $cerr")
        unless $cerr ~~ /(\d+) ' of ' (\d+) ' program routines compiled natively'/ && $0 == $1;
    my $r = run($exe.absolute, :out, :err);
    my $cout = $r.out.slurp(:close); $r.err.slurp(:close);
    if $cout ne $iout {
        my @i = $iout.lines; my @c = $cout.lines;
        for ^max(+@i, +@c) -> $k {
            @fail.push("line {$k + 1}: interpreted {(@i[$k] // '').raku}, compiled {(@c[$k] // '').raku}")
                if (@i[$k] // '') ne (@c[$k] // '');
        }
    }
}
# and the answers themselves, where Rakudo's are known
@fail.push("-0e0: {$iout.lines[0]}") unless $iout.lines[0] eq '-0e0 Num';
@fail.push("pc/pn: {$iout.lines[*-1]}") unless $iout.lines[*-1] eq '405324412061250 -15 1048576 -23058430092136939520';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
