# Regression: a sub declared inside a routine and closing over its variables
# keeps `--exe` from compiling natively only where one call's instance could be
# told from another's — it escapes (`&f`, a call from a closure) or the routine
# around it calls itself. A helper called only within its routine's own
# activation (showcase/ledger's recursive `walk`) is compiled natively again;
# refusing every such sub had sent ledger to the bundling fallback.
#
# t/exe/parity.raku compiles this file and compares the binary with the
# interpreter. Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

# a helper in its own activation: recursive, closing over the routine's hash
sub tree(%kids) {
    my @out;
    sub walk($n, $d) { @out.push: "{' ' x $d}$n"; walk($_, $d + 1) for (%kids{$n} // ()).sort }
    walk('root', 0);
    @out
}
ck tree({ root => <a b>, a => <c> }), ['root', ' a', '  c', ' b'], 'a recursive helper';

# the routine calls itself: each activation's `get` reads its own $x
sub outer($n) { my $x = $n; sub get { $x }; outer($n - 1) if $n > 0; get() }
ck outer(3), 3, 'a re-entered routine';

# the sub escapes through a closure: two instances keep their own $c
sub mk { my $c = 0; sub b { $c++ }; my &f = { b(); $c + 0 }; &f }
my &g = mk(); g();
my &h = mk();
ck (g(), h(), g()), (2, 1, 3), 'an escaped instance';

say @fail ?? "FAIL\n" ~ @fail.join("\n") !! 'PASS';
