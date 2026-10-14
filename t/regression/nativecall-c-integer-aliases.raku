# Regression: NativeCall's C integer spellings refused an Int.
#
# `long`, `ulong`, `longlong`, `ulonglong`, `size_t`, `ssize_t` (and `bool`)
# are native integer types, as `int64`/`uint64` are, and Rakudo binds an Int
# to a parameter of one and accepts one as a typed constant's value. Here a
# constant died "Type check failed in constant …; expected ulong but got
# Int" and a parameter "expected ulong but got Int (5)". DBDish::mysql
# declares `our ulong constant ulong_zero is export = 0`, so DBIish could
# not load its mysql driver and its suite failed. Rakudo 2026.09's answers.
#
# Contract: exit 0 + last line PASS.

use NativeCall;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku}, want {$want.raku}" }
}

our ulong constant ulong_zero is export = 0;
my long constant L = -5;
my size_t constant S = 7;
my bool constant B = 3;
ck ulong_zero, 0, 'our ulong constant … is export';
ck L, -5, 'long constant';
ck S, 7, 'size_t constant';
ck B, 3, 'bool constant';
sub f(ulong $x) { $x }
sub g(size_t $x) { $x }
sub h(longlong $x) { $x }
ck f(5), 5, 'an ulong parameter binds an Int';
ck g(6), 6, 'a size_t parameter';
ck h(-7), -7, 'a longlong parameter';
my ulong $v = 9;
ck $v, 9, 'an ulong variable';
ck 5 ~~ ulong, False, 'an Int is still no ulong to smartmatch';

say $fails ?? "FAIL: $fails" !! 'PASS';
exit $fails ?? 1 !! 0;
