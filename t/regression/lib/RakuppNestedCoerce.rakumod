# Companion to t/regression/exported-nested-class-coercion.raku: an exported
# class nested in a unit class and in a unit module, each with a COERCE.
unit class RakuppNestedCoerce;

class NCInner is export {
    has $.v;
    method COERCE($x) { NCInner.new(v => $x) }
}
