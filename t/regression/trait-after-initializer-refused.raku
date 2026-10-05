# Regression: a trait written after a declaration's initializer is refused at
# compile time — issue #127.
#
# `has Str $.s = '' is rw` compiled, the `is rw` was dropped, and the accessor
# was read-only: the mistake surfaced as "Cannot modify an immutable 's'" at
# the first assignment, far from the class. Nothing parses `is` as an infix, so
# Rakudo calls it two terms in a row; so does rakupp now, for attributes and
# for my/our/constant declarations. The traits in the right place, and `does`
# and `but` (which ARE infixes), still compile.
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
my $dir = $*TMPDIR.add("trait-after-init-{$*PID}");
$dir.mkdir;
my $n = 0;
# a whole FILE, as the report met it: EVAL is stricter about statement ends
sub run-file(Str $code) {
    my $f = $dir.add("t{++$n}.raku");
    $f.spurt: $code;
    my $p = run $*EXECUTABLE, ~$f, :out, :err;
    my $out = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    $f.unlink;
    $p.exitcode, $out
}
sub refused(Str $code, $what) {
    my ($rc, $out) = run-file($code ~ "\nsay 'COMPILED';\n");
    @fail.push("$what: compiled") unless $rc != 0 && $out.contains('Two terms in a row') && !$out.contains('COMPILED');
}
sub compiles(Str $code, $want, $what) {
    my ($rc, $out) = run-file("say do \{ $code \};\n");
    @fail.push("$what: got $out.trim().raku()") unless $rc == 0 && $out.trim eq $want;
}

refused q[class C1 { has Str $.s = '' is rw }], 'has = … is rw';
refused q[class C2 { has $.x = 5 is built }], 'has = … is built';
refused q[class C3 { has @.x = 1, 2 handles <elems> }], 'has = … handles';
refused q[class C4 { has $.x = 5 where * > 0 }], 'has = … where';
refused q[class C5 { has $.x = 5 will build { 1 } }], 'has = … will';
refused q[class C6 { has $.x .= new is rw }], 'has .= … is rw';
refused "class C7 \{ has \$.x = 5\n  is rw;\n}", 'the trait on the next line';
refused q[my $x = 5 is rw;], 'my = … is rw';
refused q[my @a = 1, 2 is rw;], 'my @ = … is rw';
refused q[our $y = 5 is export;], 'our = … is export';

compiles q[class D1 { has Str $.s is rw = '' }; my $d = D1.new; $d.s = 'x'; $d.s], 'x', 'the trait before the =';
compiles q[role R { method r { 42 } }; class D2 { has $.x = 5 does R }; D2.new.x.r], '42', 'does after the =';
compiles q[class D3 { has $.x = 0 but True }; ?D3.new.x], 'True', 'but after the =';
compiles q[my $z = 5 if True; $z], '5', 'a statement modifier after the =';

try $dir.rmdir;

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
