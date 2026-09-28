# Companion to t/regression/import-is-lexical.raku: an exported code variable.
unit module RakuppLexV;
our &lexv-code is export = sub { "code-var" };
