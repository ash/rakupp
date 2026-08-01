# `val` and `lc` are built-ins. The rule (loadModule's publish carve-out) is
# that an `is export`ed sub of that name WINS for the importer, while a plain
# one stays module-private — it must not shadow the built-in outside, but the
# module's own code still resolves it.
unit module BuiltinShadow;

sub val() is export { 'export-wins' }

sub lc($s) { "private-lc($s)" }             # NOT exported: no shadowing outside
sub uses-its-own() is export { lc('X') }    # …but in here, `lc` is this one
