# Closures a native body makes and returns: they run after its frame has been
# reused, so they hold their captures themselves — shared cells for anything
# written after the closure was made, by it or by the routine.
unit module Aot::Closure;

my $prefix = '#';

sub make-counter($start) is export {   #aot: native
    my $n = $start;
    -> { $n++ }
}
# the routine writes the variable after the closure is made
sub make-reader() is export {   #aot: native
    my $v = 'old';
    my &r = -> { $v };
    $v = 'new';
    &r
}
# two closures over one variable: one writes, one reads
sub make-pair() is export {   #aot: native
    my $shared = 0;
    (-> $by { $shared += $by }, -> { $shared })
}
sub over-param($p) is export {   #aot: native
    -> $x { "$p:$x" }
}
# one closure per iteration, each with its own variable
sub per-iteration() is export {   #aot: native
    my @subs;
    for 1..3 -> $i {
        my $sq = $i * $i;
        @subs.push: -> { "$i^2=$sq" };
    }
    @subs
}
sub over-module() is export {   #aot: native
    -> $s { $prefix ~ $s }
}
sub set-prefix($p) is export {   #aot: native
    $prefix = $p;
}
sub nested-maker($a) is export {   #aot: native
    -> $b { -> $c { "$a$b$c" } }
}
# a lexical sub handed out as a value
sub lexical-sub() is export {   #aot: native
    my $calls = 0;
    sub tick() { ++$calls }
    &tick
}
class Account is export {
    has $.balance is rw = 0;
    method reporter() {   #aot: native
        -> { "balance {$!balance}" }
    }
    method depositor() {   #aot: native
        -> $amt { $!balance += $amt; self }
    }
}
# a native routine that calls closures made elsewhere
sub call-all(@subs) is export {   #aot: native
    @subs.map({ .() }).join(' ')
}
