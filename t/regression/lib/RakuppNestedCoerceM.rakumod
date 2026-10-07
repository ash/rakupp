# Companion to t/regression/exported-nested-class-coercion.raku.
unit module RakuppNestedCoerceM;

class NCInnerM is export {
    has $.v;
    method COERCE($x) { NCInnerM.new(v => $x * 10) }
}
