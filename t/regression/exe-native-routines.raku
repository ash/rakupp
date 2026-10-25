# Regression: a sub whose parameters are all full-width `int` / `num`, with a
# native result or none, compiles under --exe to a plain C++ function over
# int64_t and double (Codegen planNative) beside its Value function. These
# programs hold that form to the interpreter's answers, with and without -O:
#
#   · recursion between such routines, native arithmetic that wraps, file-scope
#     natives read and written, loops with `next` / `last`, early returns;
#   · statements with no native form (array and hash stores, `say`, string
#     building, calls of ordinary subs) emitted boxed, reading the natives as
#     the boxed code would — including a boxed local hiding a file-scope native;
#   · a zero divisor (`%` on ints, `/` on nums) answering what the boxed code
#     answers; a native num stored into an `int`; a call's result added to a
#     native, which the boxed code does not wrap;
#   · the Value function a boxed caller reaches: a native result, Nil, or the
#     boxed body when the routine's value is neither.
#
# Each case also names the routines that must get the native form: the test
# reads the C++ `--cpp -O` writes. Expected values from Rakudo 2026.09.
# Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-native-routines: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-native-routines-$*PID");
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

# `:@native`: the C++ names of the native functions the program must get
sub agree(Str $name, Str $code, Str $want, :@native) {
    my $src = $work.add($name ~ '.raku');
    $src.spurt($code);
    my $i = run($*EXECUTABLE, $src.Str, :out, :err);
    my $interp = $i.out.slurp(:close).lines.join('|');
    $i.err.slurp(:close);
    check("$name: interpreted", $interp, $want);
    my $cpp = $work.add($name ~ '.cpp');
    my $c = run($*EXECUTABLE, '--cpp', '-O', '-o', $cpp.Str, $src.Str, :out, :err);
    $c.out.slurp(:close); $c.err.slurp(:close);
    my $text = $cpp.e ?? $cpp.slurp !! '';
    for @native -> $fn {
        check("$name: $fn is a native function",
              so($text ~~ / ^^ 'static ' ['int64_t'|'double'|'void'] ' ' $fn '(' /), True);
    }
    for ('', '-O') -> $o {
        my $tag = $o ?? "$name $o" !! $name;
        my $bin = $work.add($name ~ $o);
        my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $src.Str, :out, :err);
        my $bout = $b.out.slurp(:close) ~ $b.err.slurp(:close);
        if $b.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $bout; next }
        my $p = run($bin.Str, :out, :err);
        my $compiled = $p.out.slurp(:close).lines.join('|');
        $p.err.slurp(:close);
        check("$tag: compiled agrees", $compiled, $interp);
    }
}

# 1. Recursion, native results, loops, zero divisors, conversions, wrappers.
agree('routines', q:to/END/, 'calls 16 acc 7.5 seen 0,1,2|144 25 111 54|-4611686018427387895 27 -28 2|moddiv: X::AdHoc|divnum: X::AdHoc|tagged: 9223372036854775812|Nil 5 20', :native<n_leaf n_tree n_sq n_hyp n_collatz n_firsts n_conv n_moddiv n_divnum n_tagged n_drive>);
    my int $calls = 0;
    my num $acc = 0e0;
    my @seen;
    my int $lim = 3;
    sub leaf(int $i, num $p) {
        $calls++;
        $acc = $acc + $p * $i;
        @seen.push("$i") if $i < $lim;
    }
    sub tree(int $depth, int $i, num $p) {
        if $depth == 0 { leaf($i, $p); return }
        my num $h = $p * 0.5e0;
        tree($depth - 1, $i * 2, $h);
        tree($depth - 1, $i * 2 + 1, $p - $h);
    }
    sub sq(int $x --> int) { $x * $x }
    sub hyp(num $a, num $b --> num) { my num $s = $a * $a + $b * $b; $s }
    sub collatz(int $n --> int) {
        my int $steps = 0;
        my int $m = $n;
        while $m != 1 {
            if $m % 2 == 0 { $m = $m div 2 }
            else { $m = 3 * $m + 1 }
            $steps++;
        }
        $steps
    }
    sub firsts(int $n --> int) {
        my int $found = 0;
        loop (my int $k = 2; $k < $n; $k++) {
            next if $k % 3 == 0;
            last if $k > 40;
            $found = $found + sq($k) % 7;
        }
        $found
    }
    sub wraps(int $a --> int) { my int $b = $a * 4611686018427387904; $b + sq(3) }
    sub conv(num $x --> int) { my int $i = floor($x * 10e0); $i }
    sub moddiv(int $a, int $b --> int) { my int $r = $a % $b; $r }
    sub divnum(num $a, num $b --> num) { my num $r = $a / $b; $r }
    sub tagged(int $a) { my $b = $a; say "tagged: ", $b + 9223372036854775807 }
    sub mixed(int $a) { $calls = $calls + $a; @seen.elems }
    sub drive(int $a) { tagged($a); return }
    tree(4, 0, 1e0);
    say "calls $calls acc $acc seen {@seen.join(',')}";
    say sq(12), " ", hyp(3e0, 4e0), " ", collatz(27), " ", firsts(100);
    say wraps(3), " ", conv(2.75e0), " ", conv(-2.75e0), " ", moddiv(-7, 3);
    { say moddiv(1, 0); CATCH { default { say "moddiv: ", .^name } } }
    { say divnum(1e0, 0e0); CATCH { default { say "divnum: ", .^name } } }
    drive(5);
    say tree(1, 0, 1e0).raku, " ", mixed(2), " ", $calls;
    END

# 2. Boxed statements and scopes inside native routines.
agree('scopes', q:to/END/, '5 local 4 10|15 22 22|17 27|80 -1|a=7 b=2.5 sum=8 boxed=14|5 7/2.5|12 1 0|4 3 1 2', :native<n_shadow n_uses_2dglobal n_nested n_early n_talk n_drive2 n_redecl n_cmp_2dmixed n_ternary n_unlessy>);
    my int $g = 10;
    my @log;
    sub boxed-helper($x) { $x * 2 }
    sub shadow(int $a --> int) {
        my $g = "local";
        @log.push("$g $a");
        my int $r = $a + 1;
        $r
    }
    sub uses-global(int $a --> int) { $g = $g + $a; $g }
    sub nested(int $n --> int) {
        my int $count = 0;
        loop (my int $i = 0; $i < $n; $i++) {
            loop (my int $j = 0; $j < $n; $j++) {
                next if $j == $i;
                last if $j > $i + 2;
                $count++;
            }
            last if $count > 20;
        }
        $count
    }
    sub early(int $n --> int) {
        my int $i = 0;
        while $i < 100 {
            return $i * 10 if $i * $i > $n;
            $i++;
        }
        -1
    }
    sub talk(int $a, num $b) {
        say "a=$a b=$b sum={$a + 1} boxed={boxed-helper($a)}";
        my $s = $a ~ "/" ~ $b;
        say $s.chars, " ", $s;
    }
    sub redecl(int $n --> int) {
        my int $t = 0;
        loop (my int $k = 0; $k < $n; $k++) { $t = $t + $k }
        loop (my int $k = 0; $k < $n; $k++) { $t = $t + $k }
        $t
    }
    sub cmp-mixed(int $a, num $b --> int) { $a < $b ?? 1 !! 0 }
    sub ternary(int $a --> int) { $a > 5 ?? $a - 5 !! 5 - $a }
    sub unlessy(int $a --> int) { my int $r = 2; unless $a > 3 { $r = 1 }; $r }
    sub drive2(int $a, num $b) { talk($a, $b); return }
    say shadow(4), " ", @log.join(';'), " ", $g;
    say uses-global(5), " ", uses-global(7), " ", $g;
    say nested(5), " ", nested(10);
    say early(50), " ", early(100000);
    drive2(7, 2.5e0);
    say redecl(4), " ", cmp-mixed(2, 2.5e0), " ", cmp-mixed(3, 2.5e0);
    say ternary(9), " ", ternary(2), " ", unlessy(2), " ", unlessy(9);
    END

# 3. Natives stored into array and hash elements by a native routine.
agree('elements', q:to/END/, '9223372036854775812 9223372036854775812 9223372036854775812|[5, 1.5e0, 5] Int Num|4611686018427387904', :native<n_put n_go>);
    my @x;
    my %h;
    sub put(int $a, num $b) {
        @x[0] = $a;
        @x[1] = $b;
        %h<k> = $a;
        @x.push($a);
    }
    sub go(int $a) { put($a, 1.5e0); return }
    go(5);
    say @x[0] + 9223372036854775807, " ", @x[2] + 9223372036854775807, " ", %h<k> + 9223372036854775807;
    say @x.raku, " ", @x[0].^name, " ", @x[1].^name;
    my int $n = @x[0];
    say $n * 4611686018427387904;
    END

$work.dir».unlink;
$work.rmdir;
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
