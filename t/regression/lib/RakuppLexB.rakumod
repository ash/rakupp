# Companion to t/regression/import-is-lexical.raku: uses RakuppLexA itself.
unit module RakuppLexB;
use RakuppLexA;
sub lexb() is export { lexa-exported() }
