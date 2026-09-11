# Fixture for t/regression/module-supplied-package-declarator.raku — EXPORTHOW
# only takes effect in a module that is `use`d, so the declarator has to live
# here rather than in the test file.
role Marker is export { method marked { "MARKED" } }

sub seeded-build(|) { %(seeded => 1) }

class ProbeHOW is Metamodel::ClassHOW {
    method compose(Mu \type) {
        # an attribute the METACLASS adds, with the initial value it must carry:
        # a declaration would have written `= …`; a runtime one calls set_build
        unless type.^attributes.first(*.name eq '%!___SEEDED___') {
            my $a = Attribute.new: :name<%!___SEEDED___>, :package(type),
                                   :type(Any), :!has_accessor;
            $a.set_build: &seeded-build;
            type.^add_attribute: $a;
        }
        self.add_role(type, Marker) unless type ~~ Marker;
        type.^add_method('composed', method { "COMPOSED:" ~ type.^name });
        self.Metamodel::ClassHOW::compose(type);
        type
    }
    # read the metaclass-added attribute back WITHOUT naming it privately: the
    # method literal would be compiled in this class's scope, where it is not
    # declared. The Attribute meta-object is the reader.
    method seeded-of(Mu \type, $obj) {
        type.^attributes.first(*.name eq '%!___SEEDED___').get_value($obj)
    }
    method widget-of(Mu \type) { "WIDGET-OF:" ~ type.^name }
    method attr-names(Mu \type) { self.attributes(type).map(*.name).sort.join(',') }
    method declares(Mu \type, $name) { self.declares_method(type, $name) }
}

my package EXPORTHOW {
    package DECLARE {
        constant widget = ProbeHOW;
    }
}
