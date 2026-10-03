# RAKUPP_AOT_FAULT_DECLINE (set by main.raku for the native run) attaches, for
# each routine it names, a body that declines at entry — as a real one does
# when it cannot bind an outer name. The interpreter must then run the
# routine's statements itself, on every path a call enters a native body by:
# a plain sub (callPlainSub), any other routine (callCallableRaw), a method
# (invokeMethod) — and a routine called through a code reference.
unit module Aot::Decline;

my @trace;

sub plain($a, $b) is export {               #aot: native
    @trace.push("plain");
    "$a+$b"
}
sub typed(Str $s, :$times = 2) is export {  #aot: native
    @trace.push("typed");
    $s x $times
}
sub by-reference($x) {                      #aot: native
    @trace.push("ref");
    $x ~ '!'
}
sub mapped(@xs) is export {                 #aot: native
    @xs.map(&by-reference).join(' ')
}
sub fine($x) is export {                    #aot: native
    "fine $x"
}
class Obj is export {
    has $.v = 'v';
    method meth($suffix) {                  #aot: native
        @trace.push("meth");
        $!v ~ $suffix
    }
}
sub trace() is export {                     #aot: native
    @trace.join(',')
}
