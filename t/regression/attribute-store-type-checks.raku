# Regression: an assignment to an ATTRIBUTE is checked like one to a variable.
# The refusal names the attribute by its `$!` name — however it was reached,
# `$!x = …` in a method or `.x = …` through an `is rw` accessor — and shows the
# value refused; a `:D` attribute refuses a type object (and a `Nil` reset to
# an undefined default), a `:U` one a defined value. The `:D`/`:U` smiley was
# not enforced at all, and the message said only "in assignment".
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo —
# Rakudo adds a "(perhaps Nil was assigned …)" hint to the `:D` refusals, which
# these checks do not ask about. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub refusal(&b) { try { b(); return 'lived' }; $!.^name ~ ': ' ~ $!.message }

subset Pos of Int where * > 0;
class A {
    has Int $!i = 1;
    has Int:D $!d = 7;
    has Int:U $!u;
    has Int $.x is rw;
    has Pos $.p is rw = 3;
    has Int:D $.dd is rw = 4;
    has $.w is rw where * < 10 = 3;
    method set-i($v) { $!i = $v; $!i }
    method set-d($v) { $!d = $v; $!d }
    method set-u($v) { $!u = $v; $!u }
    method nil-d()   { $!d = Nil; $!d }
}
my $a = A.new;
my $T = 'X::TypeCheck::Assignment: Type check failed in assignment to ';

ck(refusal({ $a.set-i("x") }), $T ~ '$!i; expected Int but got Str ("x")', 'a typed attribute names itself and the value');
ck(refusal({ $a.set-i(Any) }), $T ~ '$!i; expected Int but got Any (Any)', '…and an undefined value');
ck(refusal({ $a.x = "s" }), $T ~ '$!x; expected Int but got Str ("s")', 'through an `is rw` accessor, by its `$!` name');
ck(refusal({ $a.p = -2 }), $T ~ '$!p; expected Pos but got Int (-2)', 'a subset type');
ck(refusal({ $a.w = 20 }), $T ~ '$!w; expected <anon> but got Int (20)', 'a `where` constraint');
ck(refusal({ $a.set-d(Int) }).starts-with($T ~ '$!d; expected Int:D but got Int (Int)'), True, ':D refuses a type object');
ck(refusal({ $a.dd = Int }).starts-with($T ~ '$!dd; expected Int:D but got Int (Int)'), True, '…through an accessor too');
ck(refusal({ $a.nil-d }).starts-with($T ~ '$!d; expected Int:D but got Int (Int)'), True, '…and a Nil reset to an undefined default');
ck(refusal({ $a.set-u(5) }), $T ~ '$!u; expected Int:U but got Int (5)', ':U refuses a defined value');
ck($a.set-d(5), 5, ':D takes a defined value');
ck($a.set-u(Int), Int, ':U takes a type object');
$a.p = Nil;
ck($a.p, Pos, 'a Nil reset of a subset attribute is not a refusal');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
