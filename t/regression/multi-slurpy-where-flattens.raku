# Regression: a multi candidate's `where` on a flattening slurpy saw the
# arguments UNFLATTENED. `multi du(*@gs where @gs.all ~~ Int:D)` called as
# `du([1, 2])` checked `[[1, 2],]`, so the candidate was rejected and the call
# died "Cannot resolve caller" — while the binder, had the candidate won, would
# have bound two elements. Graph's `disjoint-union(*@gs where @gs.all ~~
# Graph:D)` is called with an Array of graphs (issue #47: Graph would not
# install).
#
# The where now sees the list the binder builds, for each slurpy kind. Answers
# below are rakudo 2026.09's.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

multi du(Str $x) { 'one' }
multi du(*@gs where @gs.all ~~ Int:D) { "many {@gs.elems}" }
check du([1, 2]), 'many 2', '*@ flattens an Array argument';
check du(1, 2, 3), 'many 3', '*@ several arguments';

multi nest(Str $x) { 'one' }
multi nest(*@gs where { .elems == 2 }) { @gs.elems }
check nest([[1, 2], [3, 4]]), 2, '*@ stops at the inner Arrays';

multi plus(Str $x) { 'one' }
multi plus(+@gs where { .elems == 3 }) { 'three' }
multi plus(+@gs) { 'other' }
check plus([1, 2, 3]), 'three', '+@ single-argument rule';
check plus([1, 2], [3]), 'other', '+@ several arguments stay whole';

multi lol(Str $x) { 'one' }
multi lol(**@gs where { .elems == 1 }) { 'one arg' }
check lol([1, 2, 3]), 'one arg', '**@ does not flatten';

multi slip(Str $x) { 'one' }
multi slip(*@gs where { .elems == 3 }) { 'three' }
check slip(1, |(2, 3)), 'three', 'a Slip spreads';

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say "PASS";
