# Fixture for t/regression/ecosweep-2026-09.raku: a routine whose name an
# importing scope already has, so an import inside a block must shadow it.
unit module EcoB;
sub who() is export { 'from-EcoB' }
