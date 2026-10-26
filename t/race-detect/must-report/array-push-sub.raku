# race-detect: report
# location: array shape @out
#
# array-push.raku with the sub form, `push @out, $_`, which the `push`
# builtin does in place, past the method's mutator stripe (found by the P4
# sweep: Log::Async's t/05-concurrent pushes this way). Every push writes the
# shape either way.
my @out;
await (^2).map: { start { push @out, $_ for ^2_000 } };
say @out.elems <= 4_000;
