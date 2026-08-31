# A module that exists only to be found through `use lib` from a SUBDIRECTORY:
# the operator it exports has to be registered while the file that uses it is
# still parsing, so the parser must follow the `use lib` too.
unit module CircumfixOps;

sub circumfix:<⦃ ⦄>(|c) is export { c }
