# `+$n` of a native is the BOXED number (2026-11-06): prefix:<+> has no
# native candidate in Rakudo, so `f(+$n)` takes a multi's `Int` (`Num` for a
# native num) where `f($n)` takes its `int`. rakupp kept the native's tags, and
# answered `int` — and the --cnp lane, which follows Rakudo's static rule
# (nativeNumericStatic), then disagreed with the interpreter. `-$n` stays
# native in both. Every answer is Rakudo 2026.09's.

my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}
multi f(int $x) { "int" }
multi f(Int $x) { "Int" }
multi f(num $x) { "num" }
multi f(Num $x) { "Num" }
my int $n = 3;
my num $d = 1.5e0;
check(f($n), 'int', 'a native int read');
check(f(+$n), 'Int', '+ of a native int');
check(f(-$n), 'int', '- of a native int');
check(f(+$n + 1), 'Int', '+ of a native int, then a literal');
check(f($d), 'num', 'a native num read');
check(f(+$d), 'Num', '+ of a native num');
check(f(-$d), 'num', '- of a native num');
check(+$n, 3, 'its value');
check((+$d).WHAT, Num, 'its type');

if @fail { note $_ for @fail; say "FAIL" } else { say "PASS" }
exit @fail ?? 1 !! 0;
