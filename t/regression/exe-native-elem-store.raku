# Regression: compiled (--exe), a store into an element of an array declared
# native converts, wraps or refuses as the interpreter's (and Rakudo's) does
# (Interpreter::nativeElemStore). It went to the element's place, which took
# anything: a Str, a Rat or a Failure into a `num` or `int` array, an Int into
# a `num` one, an Int too wide; a Bool was not made 0 or 1, and a step at the
# top of an `int` did not wrap. Every form goes through rtElemStoreN now:
#
#   · `@a[$i] = v` as a statement and as a value, chained;
#   · `+=`, `max=`, `//=` and the rest, through the element's container;
#   · `++` / `--`, prefix and postfix, as a statement and as a value;
#   · a key evaluated once, a store past the end;
#   · the same inside routines compiled to native functions.
#
# Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-native-elem-store: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-native-elem-store-$*PID");
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

my $src = $work.add('stores.raku');
$src.spurt: q:to/END/;
    my num @v = 0e0 xx 3;
    my int @w = 0 xx 3;
    sub plus(int $i) { @w[$i] += 2.5; return }
    sub into(int $i) { @v[$i] = $i; return }
    sub good(int $i) { @w[$i] = @w[$i] * 2; @v[$i] = @v[$i] + 0.5e0; return }
    { @v[1] = "abc"; say "str ", @v; CATCH { default { say "str: ", .^name } } }
    { @v[1] = 1 / 0; say "failure ", @v; CATCH { default { say "failure: ", .^name } } }
    { @w[1] = 2.5; say "rat ", @w; CATCH { default { say "rat: ", .^name } } }
    { @w[1] = "x"; say "str int ", @w; CATCH { default { say "str int: ", .^name } } }
    { @v[2] = 3; say "int into num ", @v; CATCH { default { say "int into num: ", .^name } } }
    { @w[0] = True; say "bool ", @w; CATCH { default { say "bool: ", .^name } } }
    { @w[0] = 2**70; say "wide ", @w; CATCH { default { say "wide: ", .^name } } }
    { my $r = (@v[1] = "abc"); say "expr ", @v; CATCH { default { say "expr: ", .^name } } }
    { @w[1] += 2.5; say "op ", @w; CATCH { default { say "op: ", .^name } } }
    { @w[2] = 9223372036854775807; @w[2]++; say "step ", @w; CATCH { default { say "step: ", .^name } } }
    { @w[1] max= 10; @w[0] //= 5; say "ops ", @w; CATCH { default { say "ops: ", .^name } } }
    { @v[0] *= 2; @v[1] += 1; say "num ops ", @v; CATCH { default { say "num ops: ", .^name } } }
    { my $i = 1; @w[$i++] = 40; @w[$i++] += 5; say "key once ", @w, " $i"; CATCH { default { say "key: ", .^name } } }
    { @w[5] = 9; say "past ", @w; CATCH { default { say "past: ", .^name } } }
    { my $r = (@v[0] = 4e0) + 1; say "value $r ", @v; CATCH { default { say "value: ", .^name } } }
    { @w[0] = @w[1] = 6; say "chain ", @w; CATCH { default { say "chain: ", .^name } } }
    { --@w[0]; @w[1]--; say "steps ", @w[0], ' ', @w[1], ' ', @w[2]++, ' ', ++@w[2]; CATCH { default { say "steps: ", .^name } } }
    { plus(1); say "plus ", @w; CATCH { default { say "plus: ", .^name } } }
    { into(1); say "into ", @v; CATCH { default { say "into: ", .^name } } }
    { good(1); say "good ", @w, ' ', @v; CATCH { default { say "good: ", .^name } } }
    END
my $want = ('str: X::TypeCheck', 'failure: X::TypeCheck', 'rat: X::TypeCheck', 'str int: X::TypeCheck',
            'int into num: X::TypeCheck', 'bool [1 0 0]', 'wide: X::AdHoc', 'expr: X::TypeCheck', 'op: X::AdHoc',
            'step [1 0 -9223372036854775808]', 'ops [1 10 -9223372036854775808]', 'num ops [0 1 0]',
            'key once [1 40 -9223372036854775803] 3', 'past [1 40 -9223372036854775803 0 0 9]', 'value 5 [4 1 0]',
            'chain [6 6 -9223372036854775803 0 0 9]', 'steps 5 5 -9223372036854775803 -9223372036854775801',
            'plus: X::AdHoc', 'into: X::TypeCheck', 'good [5 10 -9223372036854775801 0 0 9] [4 1.5 0]').join('|');

my $i = run($*EXECUTABLE, $src.Str, :out, :err);
my $interp = $i.out.slurp(:close).lines.join('|');
$i.err.slurp(:close);
check('interpreted', $interp, $want);

my $cpp = $work.add('stores.cpp');
my $c = run($*EXECUTABLE, '--cpp', '-O', '-o', $cpp.Str, $src.Str, :out, :err);
$c.out.slurp(:close); $c.err.slurp(:close);
my $text = $cpp.e ?? $cpp.slurp !! '';
check('the program compiles, not bundled', so($text ~~ / '__rakupp_main_body' /), True);
for <plus into good> -> $r {
    check("n_$r is a native function", so($text ~~ / ^^ 'static void n_' $r '(' /), True);
}

for ('', '-O') -> $o {
    my $tag = $o ?? "compiled $o" !! 'compiled';
    my $bin = $work.add('stores' ~ $o);
    my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $src.Str, :out, :err);
    my $bout = $b.out.slurp(:close) ~ $b.err.slurp(:close);
    if $b.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $bout; next }
    my $p = run($bin.Str, :out, :err);
    my $out = $p.out.slurp(:close).lines.join('|');
    $p.err.slurp(:close);
    check("$tag: agrees", $out, $want);
}

$work.dir».unlink;
$work.rmdir;
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
