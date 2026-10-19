# Regression: a pointy block in front of a `for` modifier.
#
#  * Under `do`, or inside parentheses, the block was each iteration's VALUE
#    rather than the loop's body: `do -> $x { print "ran " } for 1..2` printed
#    nothing, and `(-> $x { $x + 1 } for 1..3)` was three Blocks. Rakudo runs
#    the block per element, as it does without the `do`.
#  * The statement form kept only the parameter NAMES, so `<->` and `is rw`
#    lost their write-through (`<-> $x { $x++ } for @a` died: "Cannot resolve
#    caller postfix:<++>(Int)"), and `-> { … } for 1..2` bound nothing where
#    any element is one positional too many.
#  * `<->` and `is rw` bound a value with no container, so `for 1..3 <-> $x`
#    ran, and over `.kv` the write went through. Rakudo refuses the binding
#    with X::Parameter::RW; over `.kv` only the keys have no container, and
#    `-> $k, $v is rw` must keep writing through the values.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged; every
# expectation was checked against Rakudo 2026.08 on 2026-10-19.
use MONKEY-SEE-NO-EVAL;
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub out-of($code) {
    my $o = '';
    my $*OUT = class { method print(*@a) { $o ~= @a.join } }.new;
    EVAL $code;
    $o
}
sub dies-with($code) {
    my $t = 'lived';
    { EVAL $code; CATCH { default { $t = .^name } } }
    $t
}

ck(out-of('do -> $x { print "ran$x " } for 1..2;'), 'ran1 ran2 ', 'do -> $x {…} for runs the block per element');
ck(out-of('do -> $x, $y { print "$x$y " } for 1..4;'), '12 34 ', 'two at a time');
ck((do -> $x { $x * 2 } for 1..3), (2, 4, 6), 'and collects its values');
my @r = do -> $x { $x * 2 } for 1..3;
ck(@r, [2, 4, 6], 'into an array too');
ck((-> $x { $x + 1 } for 1..3), (2, 3, 4), 'a pointy block in parens is the body');
ck([-> $x { $x * 3 } for 1..2], [3, 6], 'and in brackets');
ck(((-> $x { $x + 1 }) for 1..2).map(*.^name).List, ('Block', 'Block'), 'a parenthesized block stays a value');
ck(out-of('-> $x { print "ran$x " } for 1..2;'), 'ran1 ran2 ', 'the plain statement form is unchanged');

my @a = 1, 2;
<-> $x { $x++ } for @a;
ck(@a, [2, 3], '<-> $x {…} for @a writes through');
do <-> $x { $x *= 10 } for @a;
ck(@a, [20, 30], 'and under do');
-> $x is rw { $x-- } for @a;
ck(@a, [19, 29], '-> $x is rw too');

ck(dies-with('-> { print "x" } for 1..2;'), 'X::AdHoc', '-> {…} for a non-empty list: too many positionals');
ck(dies-with('do -> { print "x" } for 1..2;'), 'X::AdHoc', 'and under do');
ck(dies-with('-> { } for ();'), 'lived', 'but an empty list runs nothing');

ck(dies-with('for 1..3 <-> $x { }'), 'X::Parameter::RW', 'for 1..3 <-> $x: no container');
ck(dies-with('my @a = 1, 2, 3; for @a.kv <-> $k, $v { $v = 9 }'), 'X::Parameter::RW', '<-> over .kv: the key has none');
ck(dies-with('do <-> $x { } for 1..2;'), 'X::Parameter::RW', 'do <-> $x {…} for 1..2');
ck(dies-with('<-> $x { } for 1..2;'), 'X::Parameter::RW', '<-> $x {…} for 1..2');
ck(dies-with('for <a b> <-> $x { }'), 'X::Parameter::RW', 'word lists are bare values');
ck(dies-with('for 1..3 -> Int $x is rw { }'), 'X::Parameter::RW', 'a typed is rw parameter');
ck(dies-with('my @a = 1, 2; for @a.pairs <-> $p { }'), 'X::Parameter::RW', '.pairs makes fresh Pairs');
ck(dies-with('for (1, 2).kv -> $k, $v is rw { }'), 'X::Parameter::RW', '.kv of a literal list has no containers at all');
ck(dies-with('my @r = do for 1..2 <-> $x { $x }'), 'X::Parameter::RW', 'a loop used as a value too');
my $msg = '';
{ EVAL 'for 1..3 <-> $x { }'; CATCH { default { $msg = .message } } }
ck($msg, "Parameter '\$x' expects a writable container (variable) as an argument,\nbut got '1' (Int) as a value without a container.", 'Rakudo\'s message');

my @b = 1, 2, 3;
for @b.kv -> $k, $v is rw { $v = $k * 10 }
ck(@b, [0, 10, 20], '-> $k, $v is rw over .kv writes the values');
for @b <-> $x { $x = 7 }
ck(@b, [7, 7, 7], 'for @a <-> $x writes the elements');
my %h = a => 1;
for %h.values <-> $v { $v = 5 }
ck(%h<a>, 5, 'for %h.values <-> $v writes the values');
ck(dies-with('for () <-> $x { }'), 'lived', 'an empty list binds nothing');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
