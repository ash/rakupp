# FLOAT-HEAVY-ISSUES-2026-10-22.md issue 17: native types took subs and loops
# off the interpreter's fast paths. A sub whose parameters and return name
# `num`, `int`, `Num`, `Int` or `Str` now takes the lean call path
# (callPlainSub) when each argument already is of its type, binding it as the
# general binder does, and declines to that binder otherwise; a loop kernel
# takes a variable declared `int` or `num` (full width) as it takes a plain
# Int or Num, bails where a result leaves int64, and writes the number back
# into the native cell. Interpreted, `(num $a, num $b --> num)` cost 616 ns a
# call against 246 untyped; a `num` loop 1,796 against 482. An argument the
# lean binder does not take (an allomorph, a Bool) goes the general way, and
# so does a call with a `(…)` argument, whose List keeps its variables'
# containers: from a sub's second call on, the lean path had dropped them, and
# `f(($a, 2))` writing `$x[0]` died "Cannot modify an immutable List".
#
# The answers are Rakudo 2026.09's, with the kernels on and off; and each
# native loop must enter its kernel (but `int8`'s, and the one that runs past
# int64 bails).
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-native-lean-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $src = q:to/END/;
    sub fn(num $a, num $b --> num) { $a * $b + 1e0 }
    sub fN(Num $a, Num $b --> Num) { $a * $b + 1e0 }
    sub fi(int $m --> int) { $m * 2 + 1 }
    sub fI(Int $m --> Int) { $m * 2 + 1 }
    sub fs(Str $s --> Str) { $s ~ "!" }
    sub wrongret(--> Int) { "x" }
    my $one = 1; my $r15 = 1.5; my $five = 5; my $tr = True;
    say fn(1.5e0, 2e0), " ", fn(1.5e0, 2e0).^name, " ", fN(1.5e0, 2e0), " ", fi(20), " ", fI(2**70), " ", fs("hi");
    say (try fn($one, 2e0)) // $!.^name;
    say (try fN($one, 2e0)) // $!.^name;
    say (try fi(2**70)) // $!.^name;
    say (try fI($r15)) // $!.^name;
    say (try fs($five)) // $!.^name;
    say (try wrongret()) // $!.^name;
    sub nI(Int $x) { $x.WHAT.^name }; say nI($tr);
    sub rN(--> Num) { 3 }; say (try rN()) // $!.^name;
    sub fl(Num $x --> Num) { fail "bad" if $x < 0; $x }; say fl(-1e0).^name;
    my int $ni = 5; sub tk(Int $x) { $x.^name }; say tk($ni);
    sub big(int $x --> int) { $x + 1 }; say big(9223372036854775806);
    my num $nn = 2.5e0; say fn($nn, $nn);
    sub ret5(num $a --> num) { return $a / 2e0 }; say ret5(5e0);
    sub a1(num $x --> num) { my num $s = 0e0; my int $i = 0; while $i < 8 { $s = $s + $x * 2e0; $i++ }; $s }
    say a1(1.5e0), " ", a1(0.1e0);
    sub a2() { my int $i = 9223372036854775800; my int $n = 0; while $n < 20 { $i++; $n++ }; "$i {$i.^name}" }
    say a2();
    sub a4() { my int8 $b = 100; my int $n = 0; while $n < 50 { $b++; $n++ }; $b }
    say a4();
    sub a6() { my num $s = 0e0; for ^10 -> $k { $s += $k }; $s }
    say a6(), " ", a6().^name;
    sub a8() { my num $s = 1e0; my int $n = 0; while $n < 5 { $s = $s / 3e0; $n++ }; $s }
    say a8();
    sub a12(int $m) { my int $acc = 0; my int $k = 0; while $k < $m { $acc = $acc + $k * $k; $k++ }; $acc }
    say a12(1000);
    sub a13() { my int $i = 0; while $i < 3 { $i++ }; my int $w = $i; $w = $w * 4611686018427387904; $w }
    say a13();
    my $is = <5>; my $ns = <2.5e0>; my $inf = Inf; my $nan = NaN;
    say fI($is), " ", (try fi($is)) // $!.^name, " ", fn($ns, 2e0), " ", fN($ns, 2e0);
    say fn($inf, 2e0), " ", fn($nan, 1e0), " ", fN(-Inf, 2e0), " ", fi(-9223372036854775807);
    sub un($a, $b) { $a * $b + 1e0 }; say un(1.5e0, 2e0) == fn(1.5e0, 2e0) == fN(1.5e0, 2e0);
    sub kf($x) { $x[0] = "w" }
    my $k1 = "a"; kf(($k1, 1)); my $k2 = "b"; kf(($k2, 1)); my $k3 = "c"; kf(($k3, 1)); say "$k1 $k2 $k3";
    END
my $want = q:to/END/.trim;
    4 Num 4 41 2361183241434822606849 hi!
    X::AdHoc
    X::TypeCheck::Binding::Parameter
    X::AdHoc
    X::TypeCheck::Binding::Parameter
    X::TypeCheck::Binding::Parameter
    X::TypeCheck::Return
    Bool
    X::TypeCheck::Return
    Failure
    Int
    9223372036854775807
    7.25
    2.5
    24 1.5999999999999999
    -9223372036854775796 Int
    -106
    45 Num
    0.004115226337448559
    332833500
    -4611686018427387904
    11 11 6 6
    Inf NaN -Inf 3
    True
    w w w
    END
my $f = $dir.add("native.raku");
$f.spurt($src);

# (the kernels on, whatever this run was given: the --jit gate's lane has them off)
my %env = %*ENV;
%env<RAKUPP_NO_KERNELS>:delete;
%env<RAKUPP_KERNEL_TRACE> = 1;
my $k = run($bin, $f.absolute, :out, :err, :%env);
my $kout = $k.out.slurp(:close).trim;
my $trace = $k.err.slurp(:close);
@fail.push("kernels on: {$kout.raku}") unless $kout eq $want;
for 22, 28, 30, 32, 34 -> $line {
    @fail.push("the loop at line $line entered no kernel") unless $trace.contains("loop at line $line compiled");
}
@fail.push("the loop at line 24 did not bail") unless $trace.contains("loop at line 24 bailed");
@fail.push("the int8 loop entered a kernel") if $trace.contains("loop at line 26 compiled");

%env<RAKUPP_KERNEL_TRACE>:delete;
%env<RAKUPP_NO_KERNELS> = 1;
my $n = run($bin, $f.absolute, :out, :err, :%env);
my $nout = $n.out.slurp(:close).trim; $n.err.slurp(:close);
@fail.push("kernels off: {$nout.raku}") unless $nout eq $want;

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
