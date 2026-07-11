#!/usr/bin/env raku
# A four-function calculator built from a Raku grammar and an actions class.
#
# The grammar describes the shape of an arithmetic expression; the actions
# class turns each successful match into a number as it is reduced, so
# precedence and associativity fall out of the grammar structure itself
# rather than from a hand-written parser. `%` is the separator meta-operator:
# `<term>+ % <op>` means "one or more terms separated by operators".

grammar Expr {
    rule  TOP     { <sum> }
    rule  sum     { <product>+ % <add-op> }
    rule  product { <factor>+  % <mul-op> }
    rule  factor  { <number> | '(' <sum> ')' }
    token number  { '-'? \d+ ['.' \d+]? }
    token add-op  { '+' | '-' }
    token mul-op  { '*' | '/' }
}

sub fold(@vals, @ops) {
    my $acc = @vals[0];
    for @ops.kv -> $i, $op {
        given $op {
            when '+' { $acc += @vals[$i + 1] }
            when '-' { $acc -= @vals[$i + 1] }
            when '*' { $acc *= @vals[$i + 1] }
            when '/' { $acc /= @vals[$i + 1] }
        }
    }
    $acc;
}

class Calc {
    method TOP     ($/) { make $<sum>.made }
    method factor  ($/) { make $<number> ?? +$<number> !! $<sum>.made }
    method product ($/) { make fold($<factor>».made, $<mul-op>».Str) }
    method sum     ($/) { make fold($<product>».made, $<add-op>».Str) }
}

sub MAIN() {
    my @tests =
        '1 + 2 * 3',
        '(1 + 2) * 3',
        '10 / 4',                 # stays an exact Rat (5/2), never a float
        '2 * (3 + 4) - 5 / 2',
        '-3 + 4 * -2';

    for @tests -> $src {
        my $result = Expr.parse($src, actions => Calc).made;
        say sprintf('%-22s = %s', $src, $result);
    }
}
