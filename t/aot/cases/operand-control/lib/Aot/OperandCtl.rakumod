# `next`, `last` and `return` written as an operand (#195): each body below
# compiles natively, the control word its statement form's own jump. A
# `return` as an operand kept the routine interpreted before ("prefix
# operator 'return'"), and a `next` was thrown as an exception.
unit module Aot::OperandCtl;

sub field(%h, $k) is export {   #aot: native
    my $v = %h{$k} // return 'missing';
    "field $k=$v"
}
sub first-word($s) is export {   #aot: native
    $s.words or return 'empty';
    $s.words[0]
}
# the last statement: its value is the left side's when nothing jumps
sub tail-default($x) is export {   #aot: native
    $x // return 'undefined'
}
sub defined-values(@xs) is export {   #aot: native
    my @out;
    for @xs -> $x {
        my $v = $x // next;
        @out.push: "v$v";
    }
    @out.join(',')
}
sub until-empty(@xs) is export {   #aot: native
    my @out;
    for @xs -> $x {
        $x.chars or last;
        @out.push: $x;
    }
    @out.join(',')
}
sub pick-branch($n) is export {   #aot: native
    my $r = $n %% 2 ?? "even $n" !! return "odd $n";
    $r.uc
}
# inside a block a built-in runs: the `return` leaves this routine
sub from-block(@xs) is export {   #aot: native
    @xs.map({ $_ // return 'block saw undef' }).eager;
    'block saw none'
}
