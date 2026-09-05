# Fixture for t/regression/ecosweep-2026-09.raku: an exported `our` variable is
# ONE container — the module writes it, the importer reads it back.
unit module EcoA;
our $counter is export = 0;
sub bump() is export { $counter++ }
