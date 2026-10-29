# Regression: a `(|)` signature in a file that declares `infix:<(|)>`.
#
# A declared operator was lexed wherever its spelling appeared, so once a file
# declared `sub infix:<(|)>` — anywhere, even below — `proto sequence(|) {*}`
# read as `proto sequence` followed by that infix: the proto's signature was
# `()` and every call died "Calling sequence(Block, Block) will never work with
# signature of the proto ()". FunctionalParsers declares exactly that pair and
# failed while loading (issue #60).
#
# A declared `(…)` spelling now yields where the built-in set operators do:
# after a routine declarator it opens a signature, and glued to a name it is
# that name's argument list. Spaced between terms it is still the operator.

proto sequence(|) {*}
multi sub sequence(&p) { 'one' }
multi sub sequence(&p1, &p2) { 'two' }

proto spaced (|) {*}
multi sub spaced($x) { "spaced $x" }

my &anon = sub (|) { 'anon' };

sub takes-block(&) { 'block' }

die "proto one"    unless sequence({1}) eq 'one';
die "proto two"    unless sequence({1}, {2}) eq 'two';
die "proto sig"    unless &sequence.signature.raku eq ':(|)';
die "spaced proto" unless spaced(5) eq 'spaced 5';
die "anon sub"     unless anon(1, 2, 3) eq 'anon';
die "sub (&)"      unless takes-block({ 1 }) eq 'block';

# the operators themselves, declared after their use above
die "infix (|)" unless (1 (|) 2) eq 'U[1 2]';
die "infix (&)" unless (1 (&) 2) eq 'I[1 2]';
die "chained"   unless (sequence({1}) (|) 3) eq 'U[one 3]';

sub infix:<(|)>(*@a) { "U[@a[]]" }
sub infix:<(&)>(*@a) { "I[@a[]]" }

say 'PASS';
