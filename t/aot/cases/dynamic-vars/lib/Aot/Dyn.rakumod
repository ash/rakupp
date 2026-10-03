# `my $*X` in a native body is declared in the routine's own frame
# (rtAotDeclDyn), so callers further down the call chain — native or
# interpreted — find it, and the caller's own `$*X` is untouched afterwards.
unit module Aot::Dyn;

sub read-native() {   #aot: native
    $*DEPTH // 'none'
}
sub read-interpreted() {   #aot: interpreted (phaser)
    LEAVE { }
    $*DEPTH // 'none'
}
sub write-native($v) {   #aot: native
    $*DEPTH = $v;
    'wrote'
}

sub declares() is export {   #aot: native
    my $*DEPTH = 1;
    read-native() ~ ' ' ~ read-interpreted()
}
sub declares-and-callee-writes() is export {   #aot: native
    my $*DEPTH = 'mine';
    my $before = $*DEPTH;
    write-native('changed by callee');
    "$before -> $*DEPTH"
}
sub nested($n) is export {   #aot: native
    my $*DEPTH = $n;
    $n > 0 ?? read-native() ~ ',' ~ nested($n - 1) !! read-native() ~ '.'
}
sub shadow-and-restore() is export {   #aot: native
    my $*DEPTH = 'outer';
    my $inner = declares();
    "$inner / still $*DEPTH"
}
sub reads-callers() is export {   #aot: native
    read-native()
}
sub through-closure() is export {   #aot: native
    my $*DEPTH = 'seen by a block';
    (1, 2).map({ $*DEPTH ~ " $_" }).join('; ')
}
