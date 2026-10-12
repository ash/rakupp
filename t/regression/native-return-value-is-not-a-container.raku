# Regression: a value a routine returns out of a native variable is a plain
# value. It carried the variable's native tags, and a bind took them: after
# `$v := f()` with f returning a native int, $v was itself a native-int slot,
# and every later `$v := …` was coerced to an integer — `$v := [7, 8]` gave 2.
# Found by JSON::Native's pure-Raku parser, whose object values came back as
# their own .elems after the first numeric one. Every expectation is Rakudo's.

use Test;

plan 9;

sub n()  { my int $acc = 1; $acc }
sub nr() { my int $acc = 1; return $acc }
sub n8() { my int8 $b = 127; $b }

{
    my $v;
    $v := n();
    $v := [7, 8];
    is-deeply $v, [7, 8], 'an Array bound after a native-int result stays an Array';
    $v := n();
    $v := 'str';
    is $v, 'str', 'a Str bound after one stays a Str';
    $v := nr();
    $v := 2.5;
    is $v, 2.5, 'so does a Rat, after an explicit `return` of a native';
}

{
    my $w := n();
    is $w.^name, 'Int', 'what is bound is a plain Int';
    my $z := n8();
    is $z + 1, 128, 'an int8 that comes back is an Int, not an int8 that wraps';
}

# what must not change: native arithmetic, assignment into a native, `is rw`
{
    my int $x = 3;
    sub f() { $x }
    is f() + 1, 4, 'a returned native still does arithmetic';
    my int $y = f();
    is $y, 3, 'and assigns into a native';
    sub g() is rw { $x }
    g() = 9;
    is $x, 9, 'an `is rw` routine still hands back the native container';
}

# the shape JSON::Native::Raku's object parser had: one variable rebound per pair
{
    sub thing($i) { my int $k = 5; $i == 0 ?? $k !! ['a', 'b', 'c'] }
    my %h;
    my $value;
    for ^2 -> $i { $value := thing($i); %h{$i} = $value }
    is-deeply %h, %('0' => 5, '1' => ['a', 'b', 'c']), 'a rebound variable takes each value as it is';
}
