# t/regression/exe-module-closure-lexicals.raku: a module whose routines CALL
# the blocks another module hands them. It declares the same names as the
# caller's module (SKIP, is-skip), so a block that resolved them in this
# module's frame instead of its own would answer this module's.
unit module RakuppClosureWalker;
my constant SKIP = <a d>;
sub is-skip($) { True }
sub keep(@names, :&prune = { False }) is export { @names.grep({ !prune($_) }).List }
class Keeper is export { method keep(@names, &prune) { @names.grep({ !prune($_) }).List } }
