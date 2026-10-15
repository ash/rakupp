# Regression: a report from writing a float-heavy program and running it two
# ways, interpreted and as `--exe -O` (2026-10-15). Its findings, as they
# landed (expected values from Rakudo 2026.09, 2026-10-15, where it answers):
#
#   · a labelled `next`/`last` was an exception — ~60 µs a jump interpreted,
#     nearly all of it the unwinder reading call-site tables, ~7 µs compiled.
#     Now a flag when its loop is a `for` with nothing but statements between
#     (Ast.h LastStmt::coop), and a goto in compiled code (Codegen loopLabels_).
#   · a Failure stored in a native (`my num $r = 1e0 / $zero`) became 2, the
#     size of the hash it is; now the unbox refusal Rakudo gives.
#   · `for @v -> num $x` (any typed loop parameter) left `$x` undeclared in
#     the generated C++, and clang refused the program.
#   · a compiled `for $*IN.lines` read stdin to the end before its first turn;
#     a gather ran ahead of its loop body; `Empty` was a fresh empty list.
#   · Num arithmetic and comparison, `+& +| +^ +< +>`, `Int(…)`, `.key`,
#     `.value`, `.Int`, chained comparisons, `int`-typed subs, and read-only
#     float slots in a loop lane all took slow general paths; the fast ones
#     must answer exactly what the general ones did, which is what this checks.
#   · one construct outside the native subset (a CATCH) cost the whole program
#     its native code; a bundled program now keeps native bodies for its own
#     routines (AotModules.h attachMainAot). Building that found a CONTROL
#     block compiled as a CATCH — it never saw `last A` — now refused.
#
# Each program runs interpreted and compiled (`--exe -O`); both must give the
# expected text, and the compile must stay native where the case says so.
# Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'float-exe-report: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("float-exe-report-$*PID");
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

# compile SRC with --exe -O; the binary's path, and what the compiler said
sub compile(Str $name, Str $code) {
    my $src = $work.add($name ~ '.raku');
    $src.spurt($code);
    my $bin = $work.add($name);
    my $c = run($*EXECUTABLE, '--exe', '-O', '-o', $bin.Str, $src.Str, :out, :err);
    my $cout = $c.out.slurp(:close) ~ $c.err.slurp(:close);
    ($c.exitcode == 0 ?? $bin.Str !! Str), $cout, $src.Str
}

sub agree(Str $name, Str $code, Str $want, Bool :$native = True) {
    my ($bin, $cout, $src) = compile($name, $code);
    my $i = run($*EXECUTABLE, $src, :out, :err);
    my $interp = $i.out.slurp(:close).lines.join('|');
    $i.err.slurp(:close);
    check("$name: interpreted", $interp, $want);
    without $bin { $fails++; note "compile of $name failed:\n$cout"; return }
    if $native && !$cout.contains('(native)') {
        $fails++;
        note "$name was expected to compile natively, but did not:\n$cout";
    }
    my $r = run($bin, :out, :err);
    my $compiled = $r.out.slurp(:close).lines.join('|');
    $r.err.slurp(:close);
    check("$name: compiled agrees", $compiled, $interp);
}

# 1. Labelled next/last: through nested loops, given/when, bare blocks, while,
#    C-style loop, try, declarations on the way, a sub, a gather.
my $labels-want = '000 100 x0 x2 y0 y2 w11 w21 w31 q0 q1 q2 o00 o01 o02 o10 p00 p10 p20 5 [00 10 20]';
agree('labels', q:to/END/, $labels-want);
    A: for ^2 -> $i { B: for ^2 -> $j { for ^2 -> $k { next A if $k == 1; print "$i$j$k " } } }
    X: for ^3 -> $i { given $i { when 1 { next X } }; print "x$i " }
    Y: for ^3 -> $i { { next Y if $i == 1 }; print "y$i " }
    my $n = 0;
    W: while $n < 3 { $n++; my $m = 0; while $m < 3 { $m++; next W if $m == 2; print "w$n$m " } }
    L: loop (my $q = 0; $q < 3; $q++) { for ^2 -> $j { next L if $j; print "q$q " } }
    R: for ^3 -> $i { try { for ^2 -> $j { next R if $j } }; print "r$i " }
    O: for ^3 -> $i { for ^3 -> $j { last O if $i == 1 && $j == 1; print "o$i$j " } }
    P: for ^3 -> $i { my $x = "p$i"; for ^3 -> $j { my $y = $j * 2; next P if $j == 1; print "$x$y " }; print "after " }
    sub lbl($n) { my $c = 0; ROW: for ^$n -> $i { for ^3 -> $j { next ROW if $j == 1; $c++ } }; $c }
    print lbl(5), " ";
    my @g = gather { G: for ^3 -> $i { for ^3 -> $j { next G if $j == 1; take "$i$j" } } };
    say @g;
    END

# …and one through a loop with LAST phasers: the inner loop's runs, as the
# exception's path ran it; and a CONTROL block beside the inner loop sees the
# `last A` as the CX::Last it is (S04-statements/label.t: a flag there would
# slip past it). Both stay bundled: phasers and CONTROL are not native.
agree('labels-phaser', q:to/END/, 'li z0 lz|control: CX::Last', :!native);
    Z: for ^3 -> $i { LAST { print "lz" }; for ^2 -> $j { LAST { print "li " }; last Z if $i == 1 }; print "z$i " }
    say "";
    sub ctl() { A: for 1 { for 1 { last A }; CONTROL { default { die $_ } } } }
    { ctl(); CATCH { default { say "control: ", .^name } } }
    END

# 2. Natives: a Failure is refused, typed loop parameters bind, `int` subs keep
#    their results, read-only float slots in a loop.
my $natives-want = (
    'A: This type cannot unbox to a native number: P6opaque, Failure',
    'B: This type cannot unbox to a native integer: P6opaque, Failure',
    'C: This type cannot unbox to a native number: P6opaque, Failure',
    'D: This type cannot unbox to a native number: P6opaque, Failure',
    '3', '6', '45', '1', 'typecheck: X::TypeCheck::Binding::Parameter',
    '9223372036854775809', '9223372036854775812', '-9223372036854775808', '6',
    'big: Cannot unbox 71 bit wide bigint into native integer. Did you mix int and Int or literals?',
    '6765', '24 24 48 24 12', '-9223372036854775808', '0.75').join('|');
agree('natives', q:to/END/, $natives-want);
    my num $z = 0e0;
    { my num $r = 1e0 / $z; say $r; CATCH { default { say "A: ", .message } } }
    { my int $i = 1 div 0; say $i; CATCH { default { say "B: ", .message } } }
    { my num $f = Failure.new("x"); say $f; CATCH { default { say "C: ", .message } } }
    { my num $h; $h = 1e0 / 0e0; say $h; CATCH { default { say "D: ", .message } } }
    sub f(@v) { my num $s = 0e0; for @v -> num $x { $s = $s + $x }; $s }
    say f([1e0, 2e0]);
    sub g(@v) { my $s = 0; for @v -> Int $i { $s += $i }; $s }
    say g([1, 2, 3]);
    sub h($n) { my int $t = 0; for ^$n -> int $i { $t = $t + $i }; $t }
    say h(10);
    { for (1, "x") -> Int $i { say $i }; CATCH { default { say "typecheck: ", .^name } } }
    sub ti(int $m --> int) { $m * 2 }
    sub tg(int $m --> int) { 5 }
    say ti(1) + 9223372036854775807;
    say tg(1) + 9223372036854775807;
    say ti(4611686018427387904);
    my int $n = 3; say ti($n);
    { say ti(2**70); CATCH { default { say "big: ", .message } } }
    sub fib(int $n --> int) { $n < 2 ?? $n !! fib($n - 1) + fib($n - 2) }
    say fib(20);
    sub fl(num $x --> num) { my num $s = 0e0; my int $i = 0; while $i < 8 { $s = $s + $x * 2e0; $i++ }; $s }
    sub fu($x) { my $s = 0e0; my $i = 0; while $i < 8 { $s = $s + $x * 2e0; $i++ }; $s }
    sub fw($x) { my $s = 0; my $i = 0; while $i < 8 { $s = $s + $x; $i++ }; $s }
    say fl(1.5e0), " ", fu(1.5e0), " ", fu(3), " ", fw(3), " ", fw(1.5e0);
    my int $a = 9223372036854775807; my int $b = $a + 1; say $b;
    my num $c = 1.5e0; my num $d = $c * $c - $c; say $d;
    END

# 3. The fast paths: type terms, Empty, a gather's order in a loop, Num and
#    bitwise arithmetic, coercion calls, .key/.value/.Int, chained comparisons.
my $fast-want = (
    '(Int)', 'Any', 'True', '2', 'True True True', 'g1 t1 g2 t2 g3 t3 ',
    '5.25 True 0.30000000000000004 0.25 1.4142135623730951',
    '2.5 3 True False False Inf',
    '405324412061245 5 -3 10',
    '4611686018427387904 6917529027641081856 -6917529027641081856 18446744073709551616 -1 15 6',
    '2 -2 42 3 True 100000000000000000000', '42 3 False 0.5 4 Int(Any)',
    'nan: X::Numeric::CannotConvert', '6 five', '14',
    '(2 0 100000000000000000000 -9300000000000000000 0 3)', 'inf.Int: X::Numeric::CannotConvert',
    '(True False False True False True)', 'True True').join('|');
agree('fastpaths', q:to/END/, $fast-want);
    say Int; say Any.^name; say (Empty === Empty);
    my @a = 1, Empty, 2; say @a.elems;
    say (5 ~~ Int), " ", (True ~~ Int), " ", (5 !~~ Bool);
    for gather { for 1..3 { print "g$_ "; take $_ } } -> $x { print "t$x " }
    say "";
    say 1.5e0 * 2.5e0 + 1.5e0, " ", 1.5e0 < 2, " ", 0.1e0 + 0.2e0, " ", 1e0 / 4, " ", 2e0 ** 0.5e0;
    say 3 - 0.5e0, " ", 2 * 1.5e0, " ", 1e0 == 1, " ", 1e0 != 1, " ", NaN < 1, " ", 1e300 * 1e10;
    say 6034823946274365 +& 1125899906842623, " ", 6034823946274365 +> 50, " ", -5 +> 1, " ", 5 +> -1;
    say 1 +< 62, " ", 3 +< 61, " ", -3 +< 61, " ", 1 +< 64, " ", -1 +> 70, " ", 12 +| 3, " ", 12 +^ 10;
    say Int(2.5e0), " ", Int(-2.5e0), " ", Int("42"), " ", Int(7/2), " ", Int(True), " ", Int(1e20);
    say Str(42), " ", Num(3), " ", Bool(0), " ", Rat(0.5e0), " ", Real(4), " ", Int().^name;
    { say Int(NaN); CATCH { default { say "nan: ", .^name } } }
    my $pair = (5 => "five"); say $pair.key + 1, " ", $pair.value;
    my @kv = (1 => 2, 3 => 4); say @kv.map({ .key * .value }).sum;
    say (2.68e0, -0.5e0, 1e20, -9.3e18, 0e0, 3).map(*.Int);
    { say Inf.Int; CATCH { default { say "inf.Int: ", .^name } } }
    say (150, 100, 200, 150.5e0, 99.9e0, "150").map({ 101 <= $_ < 200 });
    say 1 < 2 < 3 > 2 >= 2 == 2 != 3, " ", "a" lt "b" lt "c";
    END

# 4. A compiled `for $*IN.lines` answers each line as it arrives: with stdin
#    still open after one line, the reply to it must come back.
{
    my ($bin, $cout) = compile('stdin-lines', q:to/END/);
        for $*IN.lines -> $line { say "got: $line"; $*OUT.flush }
        END
    without $bin { $fails++; note "compile of stdin-lines failed:\n$cout" }
    with $bin {
        my $proc = Proc::Async.new(:w, $bin);
        my $first = Promise.new;
        my $vow = $first.vow;
        my $buf = '';
        my $lock = Lock.new;
        $proc.stdout.tap(-> $s { $lock.protect({
            $buf ~= $s;
            $vow.keep($buf.lines[0]) if $buf.contains("\n") && $first.status ~~ Planned;
        }) });
        my $done = $proc.start;
        await $proc.write("a\n".encode);
        await Promise.anyof($first, Promise.in(10));
        check('stdin-lines: compiled answers before stdin closes',
              $first.status ~~ Kept ?? $first.result !! 'no answer in 10 s', 'got: a');
        $proc.close-stdin;
        await $done;
    }
}

# 5. A whole-program refusal names the lines it comes from.
{
    my ($bin, $cout) = compile('kept-list', q:to/END/);
        sub total(@x) { my $s = 0e0; $s += $_ for @x; $s }
        sub bump($arr) { $arr[0] = $arr[0] + 1 }
        my $p = 0.25e0; my $q = 0.75e0;
        say total(($p, $q));
        END
    check('kept-list: the note names both lines',
          so $cout ~~ /'(line 4)' .* '(line 2)'/, True);
}

# 6. A program --exe has to bundle (a CATCH block) keeps native bodies for its
#    other routines: the build says so, the binary attaches them (the AST
#    fingerprint matched its run-time parse), and its output is the
#    interpreter's — with the bodies, with RAKUPP_NO_AOT=1, and with a table
#    that does not fit (RAKUPP_AOT_FAULT_TABLE), which must run interpreted.
{
    my $code = q:to/END/;
        sub mandel(Int $w, Int $h, Int $maxit) {
            my $total = 0;
            for ^$h -> $y {
                for ^$w -> $x {
                    my $cr = $x * 3.5e0 / $w - 2.5e0;
                    my $ci = $y * 2e0 / $h - 1e0;
                    my $zr = 0e0; my $zi = 0e0; my $i = 0;
                    while $i < $maxit && $zr * $zr + $zi * $zi < 4e0 {
                        my $t = $zr * $zr - $zi * $zi + $cr;
                        $zi = 2e0 * $zr * $zi + $ci;
                        $zr = $t;
                        $i++;
                    }
                    $total += $i;
                }
            }
            $total
        }
        sub safe-div($a, $b) { CATCH { default { return Inf } }; die "zero" if $b == 0; $a / $b }
        class Acc { has $.n = 0; method add($x) { $!n += $x; self } }
        # an EVAL reaches the routine's locals by name: this one stays interpreted
        sub ev() { my $x = 1; EVAL '$x = 5'; $x }
        # …and so does a `my $*X` (roast S16-io/say.t's capture): a native
        # body's dynamic declaration was not where `code` looked it up
        class FakeIO { has $.Str = ''; method print(\arg) { $!Str ~= arg } }
        sub cap(&code) { my $*OUT = FakeIO.new; code(); $*OUT.Str }
        # a bare block as the last statement is the routine's value
        sub tail-block($n) { { $n * 3 } }
        my $acc = Acc.new;
        $acc.add(mandel(30, 15, 50)) for ^2;
        say $acc.n, " ", safe-div(1, 0), " ", safe-div(6, 3), " ", ev(), " ",
            cap({ print "cap" }), " ", tail-block(5);
        END
    my ($bin, $cout, $src) = compile('bundle-bodies', $code);
    my $i = run($*EXECUTABLE, $src, :out, :err);
    my $want = $i.out.slurp(:close);
    $i.err.slurp(:close);
    check('bundle-bodies: interpreted', $want, "13198 Inf 2 5 cap 15\n");
    without $bin { $fails++; note "compile of bundle-bodies failed:\n$cout" }
    with $bin {
        check('bundle-bodies: the build compiles its routines',
              so $cout ~~ /'3 of 7 program routines compiled natively'/, True);
        for ('native bodies' => {}, 'RAKUPP_NO_AOT=1' => {RAKUPP_NO_AOT => 1},
             'a table that does not fit' => {RAKUPP_AOT_FAULT_TABLE => '<main>'}) -> $case {
            my $r = run($bin, :out, :err, :env(%*ENV, RAKUPP_AOT_TRACE => 1, |$case.value));
            my $got = $r.out.slurp(:close);
            my $trace = $r.err.slurp(:close);
            check("bundle-bodies: compiled, {$case.key}", $got, $want);
            if $case.key eq 'native bodies' {
                check('bundle-bodies: the binary attached them',
                      so $trace ~~ /'<main>: 3 of 7 routines native'/, True);
            }
        }
    }
}

run('rm', '-rf', $work.Str);
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
