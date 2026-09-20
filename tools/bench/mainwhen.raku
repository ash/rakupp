# A `given`/`when` dispatch inside a loop, written AT THE MAINLINE — 200_000
# iterations over a three-way `when`/`default` ladder.
#
# The mainline is the point of the kernel, not an accident of how it is
# written. The cooperative `when`/`default` path was OFF at the mainline from
# the day it was written: "no frame is armed" was spelled 0, which is also the
# mainline's own frameTop, so a `when` outside a routine threw a C++ exception
# instead of taking the cheap path. Moved into a sub this program measures
# something else entirely — the path that always worked.
#
# Same source and same iteration count as perf-guard's `mainwhen` kernel, so
# the published row and the gated one are the same workload.
my $n = 0;
for ^200_000 -> $i {
    given $i % 3 {
        when 0 { $n = $n + 1 }
        when 1 { $n = $n + 2 }
        default { $n = $n + 3 }
    }
}
say $n;
