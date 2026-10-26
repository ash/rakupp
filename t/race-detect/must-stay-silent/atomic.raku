# race-detect: silent
# edges: atomic
# expect: 40000
#
# The FAQ's atomicint counter (guide/faq/threads.md), scaled down. Two atomic
# accesses never race; each `⚛++` is an acquire and a release on $n, and the
# read after the `await` is ordered by the promises.
my atomicint $n = 0;
await (^4).map: { start { $n⚛++ for ^10_000 } };
say $n;
