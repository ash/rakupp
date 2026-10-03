# `return` inside a block leaves the ROUTINE the block was written in, however
# many frames lie between: a native body throws it at its own frame id
# (__fid), and a native frame it passes through hands it on. A LEAVE phaser
# keeps a routine interpreted, which gives the interpreted frames here.
unit module Aot::Return;

# Calls the block for each element: a native frame in between.
sub each-native(@xs, &f) {   #aot: native
    for @xs -> $x { f($x) }
    'each-native finished'
}
# The same, interpreted.
sub each-interpreted(@xs, &f) {   #aot: interpreted (phaser)
    LEAVE { }
    for @xs -> $x { f($x) }
    'each-interpreted finished'
}

# native routine, block run by a built-in
sub via-builtin(@xs) is export {   #aot: native
    @xs.map({ return "builtin: $_" if $_ > 10 }).eager;
    'builtin: none'
}
# native routine, block run by an interpreted routine
sub native-through-interpreted(@xs) is export {   #aot: native
    each-interpreted(@xs, -> $x { return "n>i: $x" if $x %% 2 });
    'n>i: none'
}
# interpreted routine, block run by a native routine
sub interpreted-through-native(@xs) is export {   #aot: interpreted (phaser)
    LEAVE { }
    each-native(@xs, -> $x { return "i>n: $x" if $x %% 3 });
    'i>n: none'
}
# native routine, block run by a native routine
sub native-through-native(@xs) is export {   #aot: native
    each-native(@xs, -> $x { return "n>n: $x" if $x > 1 });
    'n>n: none'
}
# two native frames in between, one of them the same routine recursing
sub deep(@xs, $depth) is export {   #aot: native
    return each-native(@xs, -> $x { return "deep $depth: $x" if $x == $depth }) if $depth > 0;
    'bottom'
}
# a return nested in loops and conditionals, with a list value
sub nested-loops() is export {   #aot: native
    for 1..3 -> $i {
        for 1..3 -> $j {
            if $i * $j == 4 {
                return $i, $j;
            }
        }
    }
    Nil
}
# a lexical sub's `return` ends the lexical sub, not the routine around it
sub inner-return() is export {   #aot: native
    sub inner($x) { return 'inner ' ~ $x if $x; 'inner none' }
    my $a = inner(1);
    my $b = inner(0);
    "$a, $b, and on"
}
# a bare `return`
sub bare-return($early) is export {   #aot: native
    return if $early;
    'late'
}
