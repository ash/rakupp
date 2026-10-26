# Regression: compiled (--exe), a store into an element of a native array
# reached through something not declared native — a routine's `@`
# parameter, a block's, a `$` variable holding the array, an attribute —
# converts, wraps or refuses as the interpreter's (and Rakudo's) does
# (nativeElemStore). It went to the element's place, which takes anything:
# `sub f(@a) { @a[0] = 2.5 }` stored 2.5 in an `int` array given to it.
# Such a store now asks the array at run time (rtNatArr) and takes
# rtElemStoreN for a native one, the old path for any other; an array
# declared plain never asks.
#
#   · `=`, `+=`, `++` at int64's top, through a routine's `@` parameter and
#     a `$` variable; an Int into a `num` array, a Num that fits;
#   · the same routine given a plain array, which takes anything;
#   · a `for` block's `@` parameter;
#   · in a program --exe bundles (module mode): the parameter, a `$`
#     parameter, an attribute, and a `$` holding a list of an array's
#     elements, whose writes still reach the array.
#
# Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-native-elem-store-holders: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-native-elem-store-holders-$*PID");
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

my $compiled = $work.add('compiled.raku');
$compiled.spurt: q:to/END/;
    my int @w = 1, 2, 3;
    my num @v = 1e0, 2e0;
    sub f(@a) { @a[0] = 2.5; return }
    sub g(@a) { @a[1] += 0.5; return }
    sub h(@a) { @a[2] = 9223372036854775807; @a[2]++; return }
    sub k(@a, $x) { @a[0] = $x; return }
    sub m(@a) { @a[0] = 7; @a[1]++; return }
    my $x = @w;
    { f(@w); say "f ", @w; CATCH { default { say "f: ", .^name } } }
    { g(@w); say "g ", @w; CATCH { default { say "g: ", .^name } } }
    { h(@w); say "h ", @w; CATCH { default { say "h: ", .^name } } }
    { k(@v, 3); say "k ", @v; CATCH { default { say "k: ", .^name } } }
    { k(@v, 4e0); say "k num ", @v; CATCH { default { say "k num: ", .^name } } }
    { my @p = 1, 2; m(@p); k(@p, "s"); say "plain ", @p; CATCH { default { say "plain: ", .^name } } }
    { $x[0] = 2.5; say "scalar ", @w; CATCH { default { say "scalar: ", .^name } } }
    { $x[1] += 0.5; say "scalar op ", @w; CATCH { default { say "scalar op: ", .^name } } }
    { for (@w,) -> @b { @b[0] = "z" }; say "block ", @w; CATCH { default { say "block: ", .^name } } }
    END
my $wantCompiled = ('f: X::TypeCheck', 'g: X::AdHoc', 'h [1 2 -9223372036854775808]', 'k: X::TypeCheck',
                    'k num [4 2]', 'plain [s 3]', 'scalar: X::TypeCheck', 'scalar op: X::AdHoc',
                    'block: X::TypeCheck').join('|');

my $bundled = $work.add('bundled.raku');
$bundled.spurt: q:to/END/;
    my int @w = 1, 2, 3;
    sub t(&f, $what) { f(); CATCH { default { say "$what: ", .^name } } }
    sub f(@a) { @a[0] = 2.5; return }
    sub g(@a) { @a[1] += 0.5; return }
    sub h(@a) { @a[2] = 9223372036854775807; @a[2]++; return }
    sub s($x) { $x[0] = "q"; return }
    t({ f(@w); say "f ", @w }, 'f');
    t({ g(@w); say "g ", @w }, 'g');
    t({ h(@w); say "h ", @w }, 'h');
    t({ s(@w); say "s ", @w }, 's');
    my @a = 1, 2, 3;
    my $r = @a.reverse;
    $r[0] = 9; $r[1]++; $r[2] += 5;
    say "view ", @a, ' ', $r;
    class C { has int @.a; method set($x) { @!a[0] = $x; self } }
    t({ say "attr ", C.new.set(5).a; C.new.set(2.5) }, 'attr');
    END
my $wantBundled = ('f: X::TypeCheck', 'g: X::AdHoc', 'h [1 2 -9223372036854775808]', 's: X::TypeCheck',
                   'view [6 3 9] (9 3 6)', 'attr [5]', 'attr: X::TypeCheck').join('|');

for ($compiled, $wantCompiled), ($bundled, $wantBundled) -> ($src, $want) {
    my $name = $src.basename.subst('.raku', '');
    my $i = run($*EXECUTABLE, $src.Str, :out, :err);
    check("$name: interpreted", $i.out.slurp(:close).lines.join('|'), $want);
    $i.err.slurp(:close);
    for ('', '-O') -> $o {
        my $tag = "$name" ~ ($o ?? " $o" !! '');
        my $bin = $work.add($name ~ $o ~ '.bin');
        my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $src.Str, :out, :err);
        my $bout = $b.out.slurp(:close) ~ $b.err.slurp(:close);
        if $b.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $bout; next }
        check("$tag: " ~ ($src eq $bundled ?? 'bundled' !! 'not bundled'), $bout.contains('bundling the program'),
              $src eq $bundled);
        my $p = run($bin.Str, :out, :err);
        check("$tag: agrees", $p.out.slurp(:close).lines.join('|'), $want);
        $p.err.slurp(:close);
    }
}

$work.dir».unlink;
$work.rmdir;
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
