# A metaclass that records which DECLARATION hooks it was given and adds to the
# class through each of them — the shape OO::Monitors is written in. Companion
# to t/regression/how-declaration-hooks.raku.
class MetamodelX::HookProbe is Metamodel::ClassHOW {
    has $!secret;
    has @!order;

    # `new_type` is called on the HOW TYPE OBJECT — there is no instance yet, so
    # reaching our own is done the long way, through the type's `.HOW`.
    method new_type(|) {
        my \type = callsame();
        type.HOW.setup(type);
        type
    }
    method setup(Mu \type) {
        @!order.push('new_type');
        $!secret = Attribute.new(name => '$!SECRET', type => Int, package => type);
        self.add_attribute(type, $!secret);
    }

    # Wrapping the build-plan routine or `clone` would change what the
    # CONSTRUCTOR answers (the wrapper's value is its value), so those names are
    # recorded and passed through — the same three OO::Monitors leaves alone.
    method add_method(Mu \type, $name, $meth) {
        @!order.push("add_method:$name");
        unless $name eq 'BUILDALL' | 'POPULATE' | 'clone' {
            $meth.wrap: -> \SELF, | { 'W:' ~ callsame };
        }
        self.Metamodel::ClassHOW::add_method(type, $name, $meth);
    }

    method compose(Mu \type) {
        @!order.push('compose');
        my $s := $!secret;
        # No POPULATE/BUILDALL of its own, so this adds one — and it must RUN,
        # or nothing it sets up per instance is ever there.
        self.add_method(type, 'POPULATE', anon method POPULATE(Mu \SELF: |) {
            $s.set_value(SELF, 4242);
            callsame
        });
        self.Metamodel::ClassHOW::compose(type);
    }

    # read-back, for the test
    method hook-order(Mu \type) { @!order.join(',') }
    method secret-of(Mu \type, \obj) { $!secret.get_value(obj) }
}

my package EXPORTHOW {
    package DECLARE {
        constant hooked = MetamodelX::HookProbe;
    }
}
