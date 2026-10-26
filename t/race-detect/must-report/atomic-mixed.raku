# race-detect: report
# location: scalar $n
#
# A missing edge: `$n⚛++` in one worker and a plain `$n++` in another. Two
# atomic accesses never race, but a plain one mixed with an atomic one does.
my atomicint $n = 0;
await (start { $n⚛++ for ^2_000 }), (start { $n++ for ^2_000 });
say $n <= 4_000;
