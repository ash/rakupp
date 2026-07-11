#!/usr/bin/env raku
# A Reverse Polish Notation (postfix) calculator driven by an explicit stack.
#
# Postfix needs no parentheses and no precedence rules: read left to right,
# push every number, and when an operator turns up pop its two operands,
# combine them, and push the result back. An ordinary Array used as a stack
# (`push`/`pop`) is the whole machine. Arithmetic stays exact — dividing two
# integers yields a `Rat`, so 15 / 5 is 3 and 1 / 3 is a true one-third.

sub rpn(Str $expr) {
    my @stack;
    for $expr.words -> $tok {
        if $tok ~~ /^ '-'? \d+ ['.' \d+]? $/ {
            @stack.push: $tok.contains('.') ?? $tok.Rat !! $tok.Int;
        }
        else {
            # Operator: pop the right operand first, then the left.
            my $b = @stack.pop;
            my $a = @stack.pop;
            given $tok {
                when '+' { @stack.push: $a + $b }
                when '-' { @stack.push: $a - $b }
                when '*' { @stack.push: $a * $b }
                when '/' { @stack.push: $a / $b }
                default  { die "unknown operator '$tok'" }
            }
        }
    }
    @stack.pop;   # the sole survivor is the answer
}

sub fmt($n) {
    # Show exact fractions as numerator/denominator, integers plainly.
    return ~$n if $n ~~ Int;
    $n.denominator == 1 ?? ~$n.numerator !! $n.nude.join('/');
}

sub MAIN() {
    my @tests =
        '3 4 +',                 # 7
        '3 4 + 5 *',             # 35
        '5 1 2 + 4 * + 3 -',     # 14
        '15 7 1 1 + - / 3 *',    # 9
        '2 3 4 * +',             # 14
        '10 2 /',                # 5
        '1 3 /',                 # exact 1/3, not 0.333...
        '100 5 / 3 -';           # 17

    for @tests -> $expr {
        say sprintf('%-22s = %s', $expr, fmt(rpn($expr)));
    }
}
