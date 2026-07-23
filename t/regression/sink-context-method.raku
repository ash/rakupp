# Regression: a statement whose value is discarded (sink context) invokes the
# result object's user-defined `sink` method, and every top-level statement is
# in sink context. Surfaced by HTTP::Status, which registers each status code by
# creating instances as bare statements (`HTTP::Status.new: 404, 'Not Found'`)
# whose `method sink { @codes[$!code] = self }` populates a lookup table.
my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @reg;
class Node {
    has $.id;
    method sink() { @reg[$!id] = "node-$!id" }
    method new($id) { self.bless(:$id) }
}

# bare top-level statements: sink fires, table populated
Node.new(1);
Node.new(2);
ck(@reg[1], "node-1", 'top-level bare statement fires sink');
ck(@reg[2], "node-2", 'top-level bare statement fires sink (2)');

# a value that IS used must NOT be sunk
my @reg2;
class Once {
    has $.k;
    method sink() { @reg2[$!k] = 1 }
    method new($k) { self.bless(:$k) }
}
my $kept = Once.new(5);   # assigned → not sunk
ck(@reg2[5].defined, False, 'assigned value is not sunk');

# sink inside a nested block: non-final statements sink
sub run-them() {
    Node.new(8);
    Node.new(9);
    42;              # final value returned, not the nodes
}
run-them();
ck(@reg[8], "node-8", 'nested-block non-final statement sinks');
ck(@reg[9], "node-9", 'nested-block non-final statement sinks (2)');

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
