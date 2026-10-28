# (t/regression/module-quote-word-sub-scope.raku) — a module used only by
# RakuppQuoteSubPrivate, whose own `sub qq` must reach neither file above it
unit module RakuppQuoteSubInner;
sub qq($v) { "[$v]" }
sub inner-q-works is export { qq('inner') }
