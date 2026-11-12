# Regression (issues #240, #242, #243): a block built inside a module routine
# that `--exe` compiled natively, and run by ANOTHER module's routine, resolved
# its bareword names — a `my constant`, a sub of its own module — in the frame
# of the routine running it rather than its own scope:
#   `prune => -> $n { $n ∈ SKIP }`   read the walker's SKIP, or none (#240)
#   `prune => -> $n { is-skip($n) }` died "Undefined routine 'is-skip'" (#242)
# A native body now binds each name it resolves by name at entry, from its own
# lexical scope, so a closure it hands out keeps seeing them. The walker
# module declares SKIP and is-skip too, so a lookup in its frame is wrong
# rather than missing.
#
# And `.map(|*.list)` (#243) handed `.map` the Whatever itself: the `*`
# curries the whole `|*.list`, so the argument is the WhateverCode
# `{ |.list }`, not a slip of one.
#
# t/exe/run.raku compiles this file and compares the binary with the
# interpreter. Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppClosureUser;

my @fail;
sub ck(&f, $want) { my $got = (try f()) // "died: $!"; @fail.push("{&f.name}: got {$got.raku}") unless $got eqv $want }

ck &t-own-sub,    <a d>;
ck &t-own-amp,    <a d>;
ck &t-amp-in-blk, <a d>;
ck &t-constant,   <a d>;
ck &t-str-const,  <a b d>;
ck &t-method,     <a d>;
ck &t-whatever,   <a d>;
ck &t-lexical,    <a d>;
ck &t-my-var,     <a d>;
ck { flat-pairs([(1, 2), (3, 4)]) }, (1, 2, 3, 4);
ck { flat-whole([(1, 2), (3, 4)]) }, (1, 2, 3, 4);
# …and in the program itself
ck { [(1, 2), (3, 4)].map(|*.list).List }, (1, 2, 3, 4);
sub pf(@p) { @p.map(|*.list).List }
ck { pf([(5, 6), (7,)]) }, (5, 6, 7);

say @fail ?? "FAIL\n" ~ @fail.join("\n") !! 'PASS';
