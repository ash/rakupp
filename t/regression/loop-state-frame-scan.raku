# The per-loop-execution `state` frame is now allocated only when the loop's
# subtree actually declares `state` (a conservative AST scan; anything it
# cannot rule out keeps the frame). These pin the scanner's recursion paths:
# a `state` hiding under an if, a bare block, a nested loop, and a ternary
# must still get the fresh-frame-per-loop-execution semantics, while plain
# loops must still behave identically without the frame. Passes on both
# engines.

# state directly in a loop body: resets per loop STATEMENT execution
sub direct() { my $r = ''; for 1..3 { state $n = 0; $n++; $r ~= $n }; $r }
die "direct: {direct}/{direct}" unless direct() eq '123' && direct() eq '123';

# state under an if inside the loop. NOTE a known engine divergence in the
# GRANULARITY: Rakudo clones the if's block per entry (counts '111'), rakupp
# hosts the var in the per-loop-execution frame (counts '123'). What BOTH
# agree on — and what the scanner must preserve — is the reset across calls:
# the second call repeats the first exactly, starting from 1.
sub under-if() {
    my $r = '';
    for 1..3 {
        if $_ > 0 { state $n = 0; $n++; $r ~= $n }
    }
    $r
}
my ($ui1, $ui2) = under-if(), under-if();
die "under-if: $ui1/$ui2" unless $ui1 eq $ui2 && $ui1.substr(0, 1) eq '1';

# state in a bare block inside the loop: same divergence, same invariant
sub under-block() {
    my $r = '';
    for 1..3 { { state $n = 0; $n++; $r ~= $n } }
    $r
}
my ($ub1, $ub2) = under-block(), under-block();
die "under-block: $ub1/$ub2" unless $ub1 eq $ub2 && $ub1.substr(0, 1) eq '1';

# state in a nested loop: resets per INNER loop execution (= per outer iteration)
sub nested() {
    my $r = '';
    for 1..2 {
        for 1..2 { state $n = 0; $n++; $r ~= $n }
        $r ~= '|';
    }
    $r
}
die "nested: {nested}" unless nested() eq '12|12|';

# a state-free loop still works and `my` in a while COND still spans iterations
my $i = 0;
my $seen = '';
while my $x = ($i < 3 ?? ++$i !! Nil) { $seen ~= $x }
die "while-cond my: $seen" unless $seen eq '123';

# routine-level state (no loop) is untouched: persists across calls
sub counter() { state $c = 0; ++$c }
counter(); counter();
my $third = counter();
die "routine state: $third" unless $third == 3;

say "PASS";
