# Native-codegen parity probes that must ALSO run under the interpreter, so
# t/run.raku can diff the two outputs directly (the older native-parity.raku
# fixture is compile-only: its 30k-deep recursion exceeds the interpreter's
# stack guard by design). Everything here is deterministic.
#
# All three cases below worked interpreted and under Rakudo but were wrong in
# a compiled binary until 2026-08-13.

# 1. A class-scoped `my` is visible to that class's own methods. Native
#    codegen dropped class-body statements entirely, so the method referenced
#    an undeclared C++ identifier and the compile failed outright.
class Reg {
    my %kind = a => 'alpha', b => 'beta';
    my $sep  = '/';
    method look($k) { %kind{$k} ~ $sep ~ %kind.elems }
}
say "classmy: ", Reg.look('a');

# 2. `now` is a real Instant and advances. It used to fall through to the
#    "unknown bareword is a type object" default, so `now.Num` was 0 — every
#    benchmark, timeout and timestamp in a compiled program silently read
#    zero, with no error anywhere.
my $t0 = now;
my $acc = 0;
for ^300_000 { $acc += $_ }
my $elapsed = (now - $t0).Num;
say "now: ", now.WHAT.gist, " epoch-sane: ", (now.Num > 1_600_000_000),
    " advanced: ", ($elapsed > 0 && $elapsed < 120);

# 3. `time` resolves to the builtin (it used to stringify as "(time)").
say "time: ", time.WHAT.gist, " epoch-sane: ", (time > 1_600_000_000);

# 4. the imaginary unit `i` — same shared constant table as now/time/pi
say "i: ", (2 * i).WHAT.gist, " ", (2 * i).im;
