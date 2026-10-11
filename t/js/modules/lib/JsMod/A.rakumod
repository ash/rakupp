# A module with private state, a private sub, an exported sub, and the two
# things another module reaches by their package name: an `our sub` and an
# `our constant`.
unit module JsMod::A;
my $calls = 0;
sub helper($x) { $calls++; $x * 10 }
sub f($x) is export { helper($x) + 1 }
our sub g() { "g saw $calls calls" }
our constant K = 3;
