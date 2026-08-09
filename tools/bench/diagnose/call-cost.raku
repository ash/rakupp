# Decomposes the cost of a CALL by differencing signatures: each row adds one
# thing to the row above it, so the deltas price the parts.
#
#     s0 - bare loop      = the call itself (frame, env, @_/$_)
#     s1 - s0             = one plain typed parameter
#     s2 - s1             = a second one
#     s2rw - s2           = what `is rw` costs on one parameter
#     s2body - s2rw       = the body (a 3-iteration nqp while)
#
# That breakdown is how the binder's `is rw` exclusion was found: the rw row
# cost ~385 ns more per call than the plain one, which is far too much for a
# flag, and the reason turned out to be that `is rw` disqualified the whole
# signature from bindParams' fast path (STRING-SCAN-QUADRATICS.md §6).
#
# READ THE RAKUPP DELTAS ONLY. Rakudo's JIT eliminates the empty-body cases
# outright — every one of them reports ~1 ms — so the Rakudo column is not a
# comparison here, unlike everything else in this directory. Use json-parse.raku
# when you want a cross-engine number.
use nqp;
my str $t = "   x" x 200000;
my sub s0()                              { }
my sub s1(str $a)                        { }
my sub s2(str $a, int $b)                { }
my sub s2rw(str $a, int $b is rw)        { }
my sub s2body(str $a, int $b is rw)      { nqp::while(nqp::iseq_i(nqp::ordat($a,$b),32), ++$b); }
my sub s2norw(str $a, int $b)            { my int $p = $b; nqp::while(nqp::iseq_i(nqp::ordat($a,$p),32), ++$p); }
my int $N = 200000;
sub bench($label, &blk) {
    my $t0 = now; blk(); my $ms = ((now - $t0) * 1000).Int;
    say $label ~ ": " ~ $ms ~ " ms";
}
bench("bare loop           ", { my int $n = 0; while $n < $N { $n = $n + 1; } });
bench("s0()                ", { my int $n = 0; while $n < $N { s0(); $n = $n + 1; } });
bench("s1(str)             ", { my int $n = 0; while $n < $N { s1($t); $n = $n + 1; } });
bench("s2(str,int)         ", { my int $n = 0; while $n < $N { s2($t, $n); $n = $n + 1; } });
bench("s2rw(str,int is rw) ", { my int $n = 0; my int $p = 0; while $n < $N { s2rw($t, $p); $n = $n + 1; } });
bench("s2body   (rw+body)  ", { my int $n = 0; my int $p = 0; while $n < $N { s2body($t, $p); $p = $p + 4; $n = $n + 1; } });
bench("s2norw   (body only)", { my int $n = 0; while $n < $N { s2norw($t, 0); $n = $n + 1; } });
