# Regression: `.AST` keeps what a declaration says beyond its name, so
# `.AST.DEPARSE` renders it and `.AST.EVAL` runs the program that was written.
#
# The view built each of these without the detail, and each rendering was
# valid Raku meaning something else — so the round-trip harness, which only
# asks that the text parse again, counted them as fine:
#
#   has $.x is rw              → has $.x              (the accessor no longer assigns)
#   sub f($a is rw)            → sub f($a)            (the body cannot write to $a)
#   class C is Int does R      → class C              (no parent, no role)
#   our sub f                  → sub f                (not reachable from its package)
#   if $x -> $y { … }          → if $x { … }          ($y undeclared)
#   for @a -> Int $i / ($p, $q) → for @a { … }        (the whole binder gone)
#   no strict                  → use strict           (the opposite pragma)
#   use lib "x"                → use lib              (no path added)
#   $x.=abs                    → $x.abs               ($x left as it was)
#
# Three parts: the rendering, against Rakudo 2026.09's `.AST.DEPARSE` of the
# same source byte for byte; the PROGRAM, as `EVAL $src` against
# `$src.AST.EVAL` on this engine, which holds whatever the rendering is to
# meaning the same; and what has no view, which says so by name.
#
# Contract: exit 0 + last line PASS.
use experimental :rakuast;
use MONKEY-SEE-NO-EVAL;

my @fail;
sub check($got, $want, $what) {
    @fail.push("$what:\n     got  {$got.raku}\n     want {$want.raku}") unless $got eq $want
}

# ---- part 1: the rendering ---------------------------------------------------
my role R { }; my role RP[::T] { };
my ($x, $y, @a);
my @render =
    Q[class A1 { has $.x is rw; has $.y is readonly; has $.z is required; has $!w is built }],
        "class A1 \{\n    has \$.x is rw;\n    has \$.y is readonly;\n    has \$.z is required;\n    has \$!w is built\n}\n",
    Q[class A2 { has $.u is required("why"); has $.v is built(False); has Int $.n is rw = 3 }],
        "class A2 \{\n    has \$.u is required(\"why\");\n    has \$.v is built(False);\n    has Int \$.n is rw = 3\n}\n",
    Q[my $c = -> $a is rw, $b is copy, $r is raw, $d is copy = 2, :$e is copy { 1 }],
        "my \$c = -> \$a is rw, \$b is copy, \$r is raw, \$d is copy = 2, :\$e is copy \{\n    1\n}\n",
    Q[class A3 is Int does R { has $.q }],
        "class A3 is Int does R \{\n    has \$.q\n}\n",
    Q[class A4 does R is Int { has $.q }],
        "class A4 does R is Int \{\n    has \$.q\n}\n",
    Q[class A5 is Int does R does RP[Str] is rw { has $.q }],
        "class A5 is Int does R does RP[Str] is rw \{\n    has \$.q\n}\n",
    Q[module A6 { our sub f { 1 }; my sub g { 2 } }],
        "module A6 \{\n    our sub f \{\n        1\n    }\n    sub g \{\n        2\n    }\n}\n",
    Q[if $x -> $a { 1 } elsif $y -> $b { 2 } else -> $c { 3 }],
        "if \$x -> \$a \{\n    1\n}\nelsif \$y -> \$b \{\n    2\n}\nelse -> \$c \{\n    3\n}\n",
    Q[with $x -> $a { 1 } else -> $b { 2 }],
        "with \$x -> \$a \{\n    1\n}\nelse -> \$b \{\n    2\n}\n",
    Q[given $x -> $a { 1 }],
        "given \$x -> \$a \{\n    1\n}\n",
    Q[for @a -> Int $i { 1 }; for @a -> $j is rw { 1 }; for @a -> ($p, $q) { 1 }],
        "for \@a -> Int \$i \{\n    1\n}\nfor \@a -> \$j is rw \{\n    1\n}\nfor \@a -> (\$p, \$q) \{\n    1\n}\n",
    Q[my $d = -> @ ($h, *@t) { 1 }],
        "my \$d = -> \@ (\$h, *\@t) \{\n    1\n}\n",
    Q[no strict; use strict; no worries; use lib "x"; use lib "a", "b"; use lib $*PROGRAM.parent],
        "no strict;\nuse strict;\nno worries;\nuse lib \"x\";\nuse lib \"a\", \"b\";\nuse lib \$*PROGRAM.parent\n",
    Q[$x.=abs; $x .= substr(1); $x.=uc.lc],
        "\$x .= abs;\n\$x .= substr(1);\n(\$x .= uc).lc\n",
;
for @render -> $src, $want { check $src.AST.DEPARSE, $want, "DEPARSE of {$src.raku}" }

# ---- part 2: the same program ------------------------------------------------
# Every class here is `my`: `EVAL` and `.AST.EVAL` run in one scope, and a
# package-scoped class would be declared twice.
my @run =
    'attribute is rw',   Q[my class P1 { has $.x is rw }; my $p = P1.new(x => 1); $p.x = 5; $p.x],
    'param is rw',       Q[sub f1($a is rw) { $a++ }; my $v = 1; f1($v); $v],
    'param is copy',     Q[sub f2($a is copy) { $a++; $a }; f2(1)],
    'class is/does',     Q[my role R1 { method r { "r" } }; my class C1 is Int does R1 { }; my $isint = C1 ~~ Int; C1.new.r ~ $isint],
    'our sub',           Q[module M1 { our sub g { 42 } }; M1::g()],
    'if binder',         Q[my $x = 7; my $out; if $x -> $y { $out = $y * 2 }; $out],
    'elsif/else binder', Q[my $out; if 0 { } elsif 3 -> $b { $out = $b } else -> $c { $out = 0 }; $out],
    'with binder',       Q[my $out; with 5 -> $a { $out = $a + 1 }; $out],
    'for typed binder',  Q[my $s = 0; for 1, 2, 3 -> Int $i { $s += $i }; $s],
    'for rw binder',     Q[my @l = 1, 2; for @l -> $i is rw { $i *= 10 }; @l.join(",")],
    'for destructuring', Q[my $s = 0; for (1, 2), (3, 4) -> ($p, $q) { $s += $p * $q }; $s],
    'no strict',         Q[no strict; $undeclared = 3; $undeclared],
    '.= assigns',        Q[my $z = -4; $z.=abs; $z],
    '.= then a call',    Q[my $z = "Ab"; my $r = $z.=uc.lc; "$z $r"],
;
for @run -> $label, $src {
    my $direct = (try EVAL $src) // "died: {$!.message.lines[0]}";
    my $via    = (try $src.AST.EVAL) // "died: {$!.message.lines[0]}";
    check $via, $direct, "$label: .AST.EVAL runs what EVAL runs";
}
# …and `M1::g` reached from outside is the point of `our`
check (try M1::g()) // 'unreachable', 42, '`our sub` is reachable from its package';

# ---- part 3: what has no view says so ----------------------------------------
for 'no Test; 1',                           '`no` with a module',
    'my @l = -1, 2; @l>>.=abs',             'a `.=` through `>>.`',
    'sub h(&cmp (Int, Int --> Int)) { 1 }', 'a sub-signature with `-->`'
  -> $src, $what {
    my $e = (try $src.AST) // $!;
    check $e.^name, 'X::NYI', "$what is refused by name";
}

if @fail {
    note "FAIL: $_" for @fail;
    say "FAIL ({+@fail})";
    exit 1;
}
say "PASS";
