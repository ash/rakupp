# (t/regression/module-quote-word-sub-scope.raku) — routines named like quote
# words that the module keeps to itself, and one it imports from a module that
# does the same
unit module RakuppQuoteSubPrivate;
use RakuppQuoteSubInner;
sub q($v) { "<$v>" }
sub tr($x) { "tr($x)" }
sub s($x) { "s($x)" }
sub private-q-works is export { q('in') ~ inner-q-works() }
