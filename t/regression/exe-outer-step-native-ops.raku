# Regression: compiled code (`--exe`, with and without `-O`) for
#
#   · a step or a store on a variable of the scope around a routine, in a
#     program that bundles. Under -O the statement's int lane named a C++
#     local the routine does not have, and a store into a native named one in
#     both modes, so the routine ran interpreted (RAKUPP_AOT_REPORT said so;
#     the answers were right). Each routine below must compile natively.
#   · `%` and `div` on two native ints, now a direct helper: floored, with
#     `int.min div -1` wrapping and a zero divisor refused.
#   · a `try` whose block returns a live Failure: Nil with `$!` set, as
#     interpreted (compiled, it handed back the Failure and left `$!` unset);
#     with `no fatal` in the block, the Failure is the value. A Failure already
#     handled (`.so`) passes through as the value too, with `$!` Nil, in both
#     (it was consumed like a live one).
#   · a Failure used as a subscript, alone or in a slice, still throws.
#   · a native num stored into an `int` (a declaration or an assignment), now
#     an inline conversion: toward zero, saturating, NaN 0; a boxed Num is
#     still refused.
#   · `$failure.handled = True` (and back to False), which compiled code
#     refused as "Target is not assignable".
#   · a native store of a native `+ - *` (or a `%`/`div` by a non-zero
#     literal) of full-width natives skips its checks; the variable must stay
#     native (it wraps afterwards), and a sized target, a literal-only value
#     and a `%` by a variable keep the store.
#
# Expected values from Rakudo 2026.09 (2026-10-24).
# Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-outer-step-native-ops: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-outer-step-native-ops-$*PID");
mkdir $work;
my $fails = 0;

sub check(Str $desc, $got, $want) {
    if $got eq $want { say "ok - $desc" }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT  [{$got}]\nWANT [{$want}]";
    }
}

# compile SRC with --exe (and FLAGS); the binary's path, and the AOT report
sub compile(Str $name, Str $src, @flags) {
    my $bin = $work.add($name);
    my $report = $work.add($name ~ '.aot');
    my $c = run($*EXECUTABLE, '--exe', |@flags, '-o', $bin.Str, $src, :out, :err,
                :env(%*ENV, RAKUPP_AOT_REPORT => $report.Str));
    my $cout = $c.out.slurp(:close) ~ $c.err.slurp(:close);
    ($c.exitcode == 0 ?? $bin.Str !! Str), $cout, ($report.e ?? $report.slurp !! '')
}

# The program interpreted must give WANT; compiled, plain and -O, the same.
# :@native names the routines the report must list as compiled natively.
sub agree(Str $name, Str $code, Str $want, :@native) {
    my $src = $work.add($name ~ '.raku');
    $src.spurt($code);
    my $i = run($*EXECUTABLE, $src.Str, :out, :err);
    my $interp = $i.out.slurp(:close).lines.join('|');
    $i.err.slurp(:close);
    check("$name: interpreted", $interp, $want);
    for ('', '-O') -> $o {
        my $tag = $o ?? "$name $o" !! $name;
        my ($bin, $cout, $report) = compile($name ~ $o, $src.Str, $o ?? ($o,) !! ());
        without $bin { $fails++; say "not ok - $tag: compiles"; note $cout; next }
        for @native -> $r {
            my $line = $report.lines.first({ (.split("\t")[3] // '') eq "sub $r" }) // '';
            check("$tag: sub $r native", $line.split("\t")[4] // 'not in the report', 'native');
        }
        my $p = run($bin, :out, :err);
        my $compiled = $p.out.slurp(:close).lines.join('|');
        $p.err.slurp(:close);
        check("$tag: compiled agrees", $compiled, $interp);
    }
}

# 1. Steps and stores on outer variables (native, untyped, typed), in a program
#    a typed `@` the backend refuses (`my Str @keep`) makes bundle.
agree('outer', q:to/END/, '2 2 2|1 1 1|0 0 0|10 10 10|7 7 7|-9223372036854775808', :native<s1 s2 s3 s4 s5 s6>);
    my Str @keep = "x";
    my int $ni = 0; my $nu = 0; my Int $nt = 0;
    sub s1() { $ni++; $nu++; $nt++ }
    sub s2() { ++$ni; ++$nu; ++$nt }
    sub s3() { $ni--; $nu--; $nt-- }
    sub s4() { --$ni; --$nu; --$nt }
    sub s5() { $ni = $ni + 10; $nu = $nu + 10; $nt = $nt + 10 }
    sub s6(int $x) { $ni = $x; $nu = $x; $nt = $x }
    s1(); s2(); say "$ni $nu $nt";
    s3(); say "$ni $nu $nt";
    s4(); say "$ni $nu $nt";
    s5(); say "$ni $nu $nt";
    s6(7); say "$ni $nu $nt";
    $ni = 9223372036854775807; s1(); say $ni;
    END

# 2. `%` and `div` on native ints.
agree('moddiv', q:to/END/, '1/2 2/-3 -2/-3 -1/2 0/2 0/0 0/-9223372036854775808 0/-9223372036854775808 6/-1317624576693539402 -1/-4611686018427387904|% 0: X::AdHoc|div 0: X::AdHoc|1.5');
    sub m(int $a, int $b --> int) { my int $r = $a % $b; $r }
    sub d(int $a, int $b --> int) { my int $r = $a div $b; $r }
    my int $min = -9223372036854775807 - 1;
    my @r;
    for (7, 3), (-7, 3), (7, -3), (-7, -3), (6, 3), (0, 5), ($min, -1), ($min, 1), ($min, 7), (9223372036854775807, -2) -> ($a, $b) {
        @r.push: "{m($a, $b)}/{d($a, $b)}";
    }
    say @r.join(' ');
    for '%', 'div' -> $op {
        my $r = try { $op eq '%' ?? m(1, 0) !! d(1, 0) };
        say "$op 0: ", $!.^name;
    }
    my num $x = 7.5e0; my num $y = 2e0;
    my num $z = $x % $y;
    say $z;
    END

# 3. `try` around a block that returns a Failure: in a whole compiled program,
#    and in routines of a bundled one.
my $try-code = q:to/END/;
    sub h($a, $b) { $a / $b }
    sub probe() {
        my $t = try { h(1e0, 0e0) };
        say $t.^name, " ", $!.^name;
        my $u = try { h(1e0, 2e0) };
        say $u, " ", $!.^name;
    }
    sub lax() { my $v = try { no fatal; h(1e0, 0e0) }; say $v.^name, " ", $!.^name }
    sub handled() {
        my $f = h(1e0, 0e0); $f.so;
        my $x = try { $f };
        say $x.^name, " ", $!.^name, " ", $x.handled;
        my $g = h(1e0, 0e0);
        my $y = try { $g };
        say $y.^name, " ", $!.^name, " ", $g.handled;
    }
    probe(); lax(); handled(); say "after";
    END
my $try-want = 'Any X::Numeric::DivideByZero|0.5 Nil|Failure Nil|Failure Nil True|Any X::Numeric::DivideByZero True|after';
agree('try-failure', $try-code, $try-want);
agree('try-failure-bundled', "my Str \@keep = 'x';\n" ~ $try-code, $try-want, :native<probe lax handled>);

# 4. A Failure as a subscript.
agree('failure-key', q:to/END/, 'key: X::Str::Numeric|slice: X::Str::Numeric|[1 2 3]|2');
    my @a = 1, 2, 3;
    { @a["x".Int] = 5; CATCH { default { say "key: ", .^name } } }
    { say @a[0, "x".Int]; CATCH { default { say "slice: ", .^name } } }
    say @a;
    say @a[1];
    END

# 5. A native num into a native int.
agree('num-to-int', q:to/END/, '4/4 -4/-4 0/0 0/0 9223372036854775807/9223372036854775807 -9223372036854775808/-9223372036854775808 0/0 9223372036854775807/9223372036854775807 -9223372036854775808/-9223372036854775808 9200000000000000000/9200000000000000000 -9223372036854775808/-9223372036854775808|1250 -2500|boxed: X::AdHoc');
    sub conv(num $n --> int) { my int $i = $n; $i }
    sub later(num $n) { my int $m = 0; $m = $n; $m }
    sub fl(num $a) { my int $M = floor($a * 1000e0); $M }
    my @r;
    for 4.7e0, -4.7e0, 0e0, -0e0, 1e30, -1e30, NaN, Inf, -Inf, 9.2e18, -9.3e18 -> $x {
        @r.push: "{conv($x)}/{later($x)}";
    }
    say @r.join(' ');
    say fl(1.25e0), " ", fl(-2.5e0);
    my $boxed = 1.5e0;
    { my int $b = $boxed; say $b; CATCH { default { say "boxed: ", .^name } } }
    END

# 6. Writing a Failure's `handled`.
agree('failure-handled', q:to/END/, 'before: False|after: True|try: Failure Nil|reset: False|try: Any X::Numeric::DivideByZero|main: True');
    sub h($a, $b) { $a / $b }
    sub set-it() {
        my $f = h(1e0, 0e0);
        say "before: ", $f.handled;
        $f.handled = True;
        say "after: ", $f.handled;
        my $x = try { $f };
        say "try: ", $x.^name, " ", $!.^name;
    }
    sub unset-it() {
        my $f = h(1e0, 0e0);
        $f.handled = True;
        $f.handled = False;
        say "reset: ", $f.handled;
        my $x = try { $f };
        say "try: ", $x.^name, " ", $!.^name;
    }
    set-it(); unset-it();
    my $m = h(2e0, 0e0); $m.handled = True; say "main: ", $m.handled;
    END

# 7. Native stores of native operations.
agree('native-store', q:to/END/, '-9223372033854775781 -9223372036854775804 -3 9223372033854775768 -1294967275|2147483648|2.5 -1.5|-9223372036854775808 6|modzero died');
    sub ints(int $a, int $b) {
        my int $x = $a * $b + 7;
        $x = $x + 9223372036854775807;
        my int $y = 2 + 3;
        $y = $y + 9223372036854775807;
        my int $n = -$a;
        my int $w = -$a * 2 - $b;
        $w = $w * $a;
        $w = $w + 9223372036854775807;
        my int32 $t = $a * $b;
        "$x $y $n $w $t"
    }
    sub sized(int $a) { my int32 $s = 2147483647; my int $x = $s + $a; "$x" }
    sub nums(num $p, num $q) {
        my num $y = $p * 2e0 + $q;
        $y = $y - $q * 3e0;
        my num $z = -$p;
        "$y $z"
    }
    sub modlit(int $a) { my int $r = $a % 7; my int $q = $a div -3; $r = $r + 9223372036854775807; "$r $q" }
    sub modzero(int $a, int $b) { my int $z = ($a % $b) * 3; $z }
    say ints(3, 1_000_000_007);
    say sized(1);
    say nums(1.5e0, 0.25e0);
    say modlit(-20);
    { say modzero(1, 0); CATCH { default { say "modzero died" } } }
    END

$work.dir».unlink;
$work.rmdir;
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
