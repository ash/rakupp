# Regression guard: seven constructs `--exe` refused now compile, and five
# answers compiled code gave wrongly.
#
# Each refused construct bundled the whole program with the interpreter (the
# 2026-11-19 census of the files that do not compile natively): a class whose
# parent is a core type (`is Exception`, `is Cool`); a block or routine whose
# last statement is a declaration (`lives-ok { my $x = f() }`); a declaration
# as a `while`, `if` or `given` condition; `my @a := …` of a value no one else
# holds; `\(…)` of values; a sigilless name in a routine (`my \x`, `sub f(\x)`)
# and as a loop or block parameter (`-> \c`).
#
# Wrong answers found as files became native, each in a top-level program too:
#  - one `$!` for the whole binary: a `try` in a sub rewrote its caller's
#  - `$buf."$write"(…)` died "No such method": a mutator named at run time
#    went to a copy, not the variable (rtMutMethod, as a written name does)
#  - `&foo.arity` counted a parameter with a default as required
#  - `\(*)` curried the whole capture into a WhateverCode
#  - a compiled sub checked no argument count: `strict($one)` against
#    `($a, $b)` ran with $b unbound (rtMiscall, rtArityCheck)
#  - `-> Rat $_ { $_ * 2 }` read the outer topic, not its parameter
#  - `my $x := 1, 2` itemized the list it binds
#  - a block whose value is a variable's (`{ $p }`) had that value sunk by
#    the statement calling it, a Failure detonating (valContained)
#  - (refused instead) a class or type declared in a class body was dropped,
#    and `&multi.candidates` answered for one dispatcher
# This file must compile natively (t/run.raku's list) and print the
# interpreter's answers, which are Rakudo 2026.09's.

my @got;
sub o(**@a) { @got.push: @a.map(*.Str).join(' ') }

class MyEx is Exception { has $.why; method message { "boom: $!why" } }
class F is Cool { has $.v; method Str { "F$!v" }; method Numeric { $!v } }

sub tail-decl { my $x = 5 }
sub tail-list { my @l = 1, 2, 3 }
sub sig(\x) { x + 1 }
sub sig-local { my \y = 7; y * 2 }
sub sig-closure(\i) { my $c = { i ~ '!' }; $c() }
sub pi-shadow(\pi) { pi }
sub inner-try { try { die 'inner' }; 1 }
sub defaults(Int $i, @stuff, $blah = 5) { 1 }
sub strict($a, $b) { $a + $b }
sub mkf() { my $f = Failure.new(X::AdHoc.new(payload => 'BOOM')); $f }
sub sinker(&b) { b(); 'quiet' }
sub verdict(&b) { (try sinker(&b)) // 'threw' }

for ^3 {
    try MyEx.new(why => 'x').throw;
    o $!.message, $! ~~ Exception, $! ~~ MyEx;
    my $f = F.new(v => 3);
    o $f ~~ Cool, $f.Str, $f + 1;
    o tail-decl(), tail-list().elems, (sub { my $s = 'q' })();
    my @chars = <a b>;
    my $seen = '';
    while (my $ch = @chars.shift) { $seen ~= $ch }
    given my $g = 2 { $seen ~= " given $g" }
    if my $m = 'x' { $seen ~= " if $m" }
    o $seen;
    my @bound := (1, 2, 3);
    try @bound.push(4);
    o @bound.^name, $!.^name;
    my @shaped := Array.new(:shape(2;2));
    o @shaped.shape;
    my $c = \(1, 2, :x(3), :x(4));
    o $c.raku, $c<x>, \(*).raku;
    my $out = '';
    for 1, 2 -> \n { $out ~= n * 10 }
    o $out, sig(4), sig-local(), sig-closure('z'), pi-shadow(3);
    try die 'outer';
    inner-try();
    o "after: $!";
    my $w = 'write-uint8';
    my $buf = buf8.new(0, 0);
    $buf."$w"(1, 8);
    my \sb = buf8.new(0, 0);
    sb."$w"(0, 7);
    o $buf.list, sb.list;
    o &defaults.arity, &defaults.count;
    my $one = 1;
    my @three = 1, 2, 3;
    my &g = &strict;
    o (try strict($one)) // 'threw', (try strict(|@three)) // 'threw', (try g(1)) // 'threw', strict(1, 2);
    my &dbl = -> Rat $_ { $_ * 2 };
    my $z := 1, 2;
    o dbl(2.5), $z.raku;
    o verdict({ my $p = mkf(); $p }), verdict({ mkf() }), verdict({ my $p = mkf(); if 1 { $p } });
}

my @want = flat (q:to/WANT-END/.lines) xx 3;
boom: x True True
True F3 4
5 3 q
ab given 2 if x
List X::Immutable
2 2
\(1, 2, :x(4)) 4 \(*)
1020 5 14 z! 3
after: outer
0 8 7 0
2 3
threw threw threw 3
5 (1, 2)
quiet threw quiet
WANT-END
for ^max(+@got, +@want) -> $i {
    next if (@got[$i] // '(nothing)') eq (@want[$i] // '(nothing)');
    say "FAIL: line {$i + 1} — got {(@got[$i] // '(nothing)').raku}, want {(@want[$i] // '(nothing)').raku}";
    exit 1;
}
say 'PASS';
