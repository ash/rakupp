# The B3 fixture's module: everything it answers comes from %?RESOURCES or
# $?DISTRIBUTION, so a compiled binary that carries neither cannot fake it.
unit module StandaloneResources;

sub motd() is export { %?RESOURCES<motd.txt>.slurp.chomp }
sub nested() is export { %?RESOURCES<data/nested.txt>.slurp.chomp }
sub resource-count() is export { %?RESOURCES.elems }
sub dist-version() is export { ~($?DISTRIBUTION.meta<version> // '(none)') }
