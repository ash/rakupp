# For t/regression/stash-term-before-pointy-block.raku: a module whose
# EXPORT::DEFAULT stash the test walks.
unit module RakuppStashTermFoo;
sub stash-foo is export { 'foo' }
