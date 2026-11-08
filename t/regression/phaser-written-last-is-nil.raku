# A phaser written last is the block's or routine's last statement, so what
# comes before it is sunk and the value is Nil, as on Rakudo: `sub f { 42;
# LEAVE { } }` answered 42, and `(1, 2).map({ $_ * 2; LEAVE { } })` (2 4). A
# trailing CATCH or CONTROL already did this; every phaser with no value of
# its own does now (LEAVE, KEEP, UNDO, PRE, POST, NEXT, LAST, CLOSE, TEMP,
# QUIT), in a sub, a method, a block and a `do`. ENTER written last is its
# own value, as before, and END is Nil already.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub f { 42; LEAVE { } }
ck f(), Nil, 'a sub';
ck (1, 2).map({ $_ * 2; LEAVE { } }).List, (Nil, Nil), 'a block';
my $x = do { 42; KEEP { } };
ck $x, Any, 'KEEP in a do';
class C { method m { 42; UNDO { } } }
ck C.m, Nil, 'a method';
for <PRE POST NEXT LAST CLOSE TEMP QUIT> -> $ph {
    ck EVAL("sub g \{ 42; $ph \{ 7 \} \}; g()"), Nil, "$ph written last";
}
sub e { 42; ENTER { 7 } }
ck e(), 7, 'ENTER written last is its value';
sub n { 42; END { } }
ck n(), Nil, 'END written last';
sub middle { LEAVE { }; 42 }
ck middle(), 42, 'a phaser before the last statement';

say 'PASS';
