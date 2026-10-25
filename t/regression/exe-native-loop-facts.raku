# Regression: a loop inside a routine compiled to a native C++ function
# (Codegen planNative) checks the facts its element forms test once, on
# entry, for each file-scope native array it reads or writes by word, and
# then tests an element's index against the length it kept. Anything boxed
# that runs in the loop, and a native routine's call, may change an array,
# and after each the facts are checked again. These must answer as the
# interpreter (and Rakudo 2026.09) does:
#
#   · reads and writes in a loop; a store past the end, through the boxed
#     side, that grows the array and moves its words, then writes through
#     the loop (they were lost while the old address was kept);
#   · a called routine that assigns the array a longer list (the loop read
#     the old words); a boxed statement in the loop that unpacks it;
#   · a boxed call in a condition's left side, then an element on its right;
#     an element in a `while` condition beside a method call;
#   · nested loops, the inner one's array changed by the outer body;
#   · an array of the loop's own, declared in its body, under the same name;
#   · a read past the end; the routine run in worker threads (the facts
#     answer no words there, and the per-access forms answer).
#
# The `--cpp -O` C++ must hold the native functions and the facts' forms.
# Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-native-loop-facts: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-native-loop-facts-$*PID");
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

my $src = $work.add('facts.raku');
$src.spurt: q:to/END/;
    my num @a = 1e0, 2e0, 3e0, 4e0;
    my int @b = 1, 2, 3, 4;
    my int $n = 4;
    sub scale(num $f) {
        loop (my int $i = 0; $i < $n; $i++) { @a[$i] = @a[$i] * $f }
    }
    sub grow(int $k) {
        loop (my int $i = 0; $i < $k; $i++) { @b[$i + 4] = @b[$i] + 10 }
    }
    sub pushy(int $k) {
        my int $s = 0;
        loop (my int $i = 0; $i < $k; $i++) {
            $s = $s + @b[$i];
            @b.push(100 + $i);
        }
        say "pushy $s ", @b.elems; return;
    }
    sub refill(int $m) { @a = 5e0, 7e0, 9e0; return }
    sub calls(int $k) {
        my num $s = 0e0;
        loop (my int $i = 0; $i < $k; $i++) {
            if $i == 1 { refill(0) }
            $s = $s + @a[$i];
        }
        say "calls $s"; return;
    }
    sub side($i) { @b.unshift(0) if $i == 0; True }
    sub cond(int $k) {
        my int $c = 0;
        loop (my int $i = 0; $i < $k; $i++) {
            if side($i) && @b[$i] > 2 { $c++ }
        }
        say "cond $c"; return;
    }
    sub scan(int $x --> int) {
        my int $i = 0;
        while $i < @b.elems && @b[$i] != $x { $i++ }
        $i
    }
    sub nest(int $k) {
        my num $s = 0e0;
        loop (my int $i = 0; $i < $k; $i++) {
            loop (my int $j = 0; $j < $k; $j++) {
                $s = $s + @a[$j] * @b[$i];
            }
            @a.unshift(10e0) if $i == 0;
        }
        say "nest $s"; return;
    }
    sub own(int $k) {
        loop (my int $i = 0; $i < $k; $i++) {
            my int @b = 7, 8, 9;
            @b[$i] = @b[$i] + 1;
            say "own {@b[$i]}";
        }
    }
    sub total(--> num) {
        my num $s = 0e0;
        loop (my int $i = 0; $i < @a.elems; $i++) { $s = $s + @a[$i] }
        $s
    }
    sub past(int $k) {
        my num $s = 0e0;
        loop (my int $i = 0; $i < $k; $i++) { $s = $s + @a[$i] }
        say "past $s"; return;
    }
    scale(2e0); say @a;
    grow(3); say @b;
    pushy(3);
    calls(3);
    cond(4);
    say scan(101), ' ', scan(42);
    nest(2);
    own(2); say @b[0];
    say total();
    my @r = await (^3).map: -> $w { start { total() } };
    say @r;
    { past(9); CATCH { default { say "past: ", .^name } } }
    sub pushw(int $k) {
        loop (my int $i = 0; $i < $k; $i++) {
            @b.append(0 xx 64);
            @b[$i] = 50 + $i;
        }
    }
    pushw(3); say @b[^4], ' ', @b.elems;
    my int @c = 1, 2, 3;
    sub far(int $k) {
        loop (my int $i = 0; $i < $k; $i++) {
            @c[$i * 1000 + 500] = 9;
            @c[$i] = 40 + $i;
        }
    }
    far(3); say @c[^3], ' ', @c.elems;
    my num @d = 1e0, 2e0;
    sub big(int $m) { @d = (^200).map(* + 0.5e0); return }
    sub use-big(int $k) {
        my num $s = 0e0;
        loop (my int $i = 0; $i < $k; $i++) {
            if $i == 1 { big(0) }
            $s = $s + @d[$i];
            @d[$i + 1] = @d[$i + 1] + 100e0;
        }
        say "big $s ", @d[^4]; return;
    }
    use-big(3);
    END
my $want = ('[2 4 6 8]', '[1 2 3 4 11 12 13]', 'pushy 6 10', 'calls 18', 'cond 1', '9 11', 'nest 15',
            'own 8', 'own 9', '0', '31', '[31 31 31]', 'past 31', '[50 51 52 3] 203', '[40 41 42] 2501',
            'big 105 [0.5 1.5 102.5 103.5]').join('|');

my $i = run($*EXECUTABLE, $src.Str, :out, :err);
my $interp = $i.out.slurp(:close).lines.join('|');
$i.err.slurp(:close);
check('interpreted', $interp, $want);

my $cpp = $work.add('facts.cpp');
my $c = run($*EXECUTABLE, '--cpp', '-O', '-o', $cpp.Str, $src.Str, :out, :err);
$c.out.slurp(:close); $c.err.slurp(:close);
my $text = $cpp.e ?? $cpp.slurp !! '';
for <scale grow pushy calls cond scan nest own total past pushw far use_2dbig> -> $r {
    check("n_$r is a native function", so($text ~~ / ^^ 'static ' ['int64_t'|'double'|'void'] ' n_' $r '(' /), True);
}
for <__nCache( __nCWN( __nCWI( __nCSetWN( __nCSetWI( __nPk(> -> $w {
    check("the C++ has $w", $text.contains($w), True);
}

for ('', '-O') -> $o {
    my $tag = $o ?? "compiled $o" !! 'compiled';
    my $bin = $work.add('facts' ~ $o);
    my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $src.Str, :out, :err);
    my $bout = $b.out.slurp(:close) ~ $b.err.slurp(:close);
    if $b.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $bout; next }
    my $p = run($bin.Str, :out, :err);
    my $out = $p.out.slurp(:close).lines.join('|');
    $p.err.slurp(:close);
    check("$tag: agrees", $out, $interp);
}

$work.dir».unlink;
$work.rmdir;
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
