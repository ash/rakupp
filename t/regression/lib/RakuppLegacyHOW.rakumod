# A metaclass installed through the LEGACY spelling, `EXPORTHOW.WHO.<class>`,
# and a class-level `is` trait narrower than plain inheritance. Companion to
# t/regression/how-legacy-exporthow.raku.
my class Tracer is export { }

# `is Tracer` itself inherits; `is SomeTracer` hands SomeTracer to the metaclass
multi trait_mod:<is>(Mu:U $type, Tracer:U $tracer) is export {
    $tracer === Tracer
        ?? $type.HOW.add_parent($type, $tracer)
        !! $type.HOW.add_tracer($type, $tracer);
}

my class TracingHOW is Metamodel::ClassHOW {
    has @!tracers;
    method add_tracer(Mu $obj, Tracer:U $t) { @!tracers.push($t) }
    method compose(Mu $obj) {
        for @!tracers -> $t {
            for self.methods($obj, :local) -> $m {
                $m.wrap(-> $self, |args { $t.record('in ' ~ $m.name); callsame });
            }
        }
        callsame;
    }
}

my module EXPORTHOW { }
EXPORTHOW.WHO.<class> = TracingHOW;
