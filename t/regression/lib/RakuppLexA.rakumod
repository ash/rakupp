# Companion to t/regression/import-is-lexical.raku: one exported sub, one not.
unit module RakuppLexA;
sub lexa-exported() is export { "exported:" ~ lexa-helper() }
sub lexa-helper() { "helper" }
