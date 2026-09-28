# Companion to t/regression/import-is-lexical.raku: requires a file in a method.
class RakuppReqOuter { method load() { require "RakuppReqInner.rakumod"; ::("RakuppReqInner") !~~ Failure } }
