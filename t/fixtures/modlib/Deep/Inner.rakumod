# Depth 2 of the fixture module tree used by t/run.raku's compile-mode checks:
# reached only THROUGH Outer, so it proves the bundler follows a module's own
# `use` rather than just the main program's.
unit module Deep::Inner;
sub inner-value() is export { 'inner' }
