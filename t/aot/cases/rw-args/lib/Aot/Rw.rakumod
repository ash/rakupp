# A parameter that writes back to its argument (`is rw`, `is raw`, sigilless)
# keeps its routine interpreted. A native caller that passes such a parameter a
# variable cannot pass its C++ local — the write would land in a copy — so that
# statement is delegated: the interpreter runs the call with the local copied
# in, and back out.
unit module Aot::Rw;

sub inc($n is rw) {   #aot: interpreted (`is rw`/`is raw` parameter)
    $n++
}
sub append-raw($s is raw, $tail) {   #aot: interpreted (`is rw`/`is raw` parameter)
    $s ~= $tail
}
sub double-sigilless(\x) {   #aot: interpreted (a sigilless parameter)
    x = x * 2
}
sub swap($a is rw, $b is rw) {   #aot: interpreted (`is rw`/`is raw` parameter)
    ($a, $b) = ($b, $a)
}
class Counter is export {
    has $.total = 0;
    method add-into($sink is rw, $n) {   #aot: interpreted (`is rw`/`is raw` parameter)
        $sink += $n;
        $!total += $n
    }
}
my $module-var = 10;
sub module-var() is export {   #aot: native
    $module-var
}

sub locals() is export {   #aot: native
    my $c = 0;
    inc($c);   #aot: delegated
    inc($c);   #aot: delegated
    my $s = 'a';
    append-raw($s, 'b');   #aot: delegated
    my $d = 21;
    double-sigilless($d);   #aot: delegated
    "$c $s $d"
}
sub elements() is export {   #aot: native
    my @a = 1, 2, 3;
    my %h = k => 5;
    inc(@a[1]);   #aot: delegated
    inc(%h<k>);   #aot: delegated
    "{@a} {%h<k>}"
}
sub swapped() is export {   #aot: native
    my ($x, $y) = 'x', 'y';
    swap($x, $y);   #aot: delegated
    "$x$y"
}
sub through-method(Counter $c) is export {   #aot: native
    my $sink = 100;
    $c.add-into($sink, 5);   #aot: delegated
    $c.add-into($sink, 6);   #aot: delegated
    "$sink {$c.total}"
}
# An outer name passed to `is rw`: the binding turns the variable's slot into a
# shared cell in the middle of the call, and the native body has to read the
# value through the cell afterwards (it read the inert holder, Any, once).
sub outer-name() is export {   #aot: native
    inc($module-var);   #aot: delegated
    $module-var
}
# a native local not passed to the rw parameter stays native around the call
sub mixed() is export {   #aot: native
    my $kept = 'kept';
    my $n = 1;
    inc($n);   #aot: delegated
    $kept ~ ' ' ~ $n
}
