# A write through a kept List into a TYPED variable checks what it stores,
# as `=` into the variable does: `my Int $a; sub f($x) { $x[0] = "s" };
# f(($a, 2))` dies X::TypeCheck::Assignment and leaves $a, a Nil resets it to
# its default (`is default` too), and `$x[0] ~= "z"` into an Int is refused.
# Both engines stored anything. The kept item carries the variable's type,
# name and default (Interpreter::noteKeptVarType); interpreted, a store
# through it (proxyStore, the cell proxy's STORE) checks them; compiled — a
# kept List exists only in a bundle, where module-mode routines write through
# it — `X[k] = v` (rtKeptAssign) and `X[k] op= v` (RtKept, which puts the old
# value back on a refusal) check through the item their reference resolved.
#
# Each program runs interpreted and compiled, -O and plain (both bundle); the
# answers are Rakudo 2026.09's.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-kept-typed-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $basic = q:to/END/;
    my Int $a = 1; sub f($x) { $x[0] = "s" }
    try { f(($a, 2)) }; say $a, " ", $!.^name;
    sub g($x) { $x[0] = 7 }; g(($a, 2)); say $a;
    my Int $b; sub h($x) { $x[0] = Nil }; h(($b, 2)); say $b.^name;
    my $c = 5; sub k($x) { $x[0] = "t" }; k(($c, 2)); say $c;
    sub m() { my Str $s = "a"; f(($s, 1)); $s }; say m();
    sub n() { my Int $t = 0; try { f(($t, 1)) }; "$t {$!.^name}" }; say n();
    END
my $basic-want = q:to/END/.trim;
    1 X::TypeCheck::Assignment
    7
    Int
    t
    s
    0 X::TypeCheck::Assignment
    END
my $more = q:to/END/;
    class A {}; class B is A {}
    sub w($x, $v) { $x[0] = $v }
    sub cat($x) { $x[0] ~= "z" }
    sub inc($x) { $x[0]++ }
    my Int $i = 1; try { cat(($i, 0)) }; say "$i {$!.^name}";
    my Str $s = "a"; inc(($s, 0)); say $s;
    my Int $d is default(5) = 1; w(($d, 0), Nil); say $d;
    my A $o = A.new; w(($o, 0), B.new); say $o.^name; try { w(($o, 0), 3) }; say "{$o.^name} {$!.^name}";
    my $u = 1; w(($u, 0), "free"); say $u;
    my Int $r = 0; for ^3 { w(($r, 0), $_) }; say $r;
    my Int $q = 0; try { for ^3 { w(($q, 0), $_ == 2 ?? "bad" !! $_) } }; say "$q {$!.^name}";
    sub inner { my Num $n = 1e0; w(($n, 0), 2.5e0); try { w(($n, 0), 1) }; "$n {$!.^name}" }; say inner();
    END
my $more-want = q:to/END/.trim;
    1 X::TypeCheck::Assignment
    b
    5
    B
    B X::TypeCheck::Assignment
    free
    2
    1 X::TypeCheck::Assignment
    2.5 X::TypeCheck::Assignment
    END
my @progs = ('basic', $basic, $basic-want), ('more', $more, $more-want);

for @progs -> ($name, $src, $expect) {
    my $f = $dir.add("$name.raku");
    $f.spurt($src);
    my $i = run($bin, $f.absolute, :out, :err);
    my $iout = $i.out.slurp(:close).trim; $i.err.slurp(:close);
    @fail.push("$name interpreted: {$iout.raku}") unless $iout eq $expect;
    for ('-O', 'plain') -> $mode {
        my $exe = $dir.add("$name-$mode.bin");
        my @cmd = $bin, '--exe', |($mode eq '-O' ?? '-O' !! ()), $f.absolute, '-o', $exe.absolute;
        my $c = run(|@cmd, :out, :err);
        $c.out.slurp(:close); $c.err.slurp(:close);
        if !$exe.e { @fail.push("$name $mode: did not compile"); next }
        my $r = run($exe.absolute, :out, :err);
        my $cout = $r.out.slurp(:close).trim; $r.err.slurp(:close);
        @fail.push("$name $mode: {$cout.raku}") unless $cout eq $expect;
    }
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
