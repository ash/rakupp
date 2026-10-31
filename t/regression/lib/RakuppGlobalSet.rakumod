# Fills a GLOBAL hash from a module, as Red's `red-defaults` does (see dynamic-falls-back-to-global.raku).
unit module RakuppGlobalSet;
sub set-default($v) is export { %GLOBAL::RAKUPP-DEFAULTS = default => $v }
