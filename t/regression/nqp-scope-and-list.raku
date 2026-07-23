# Regression: the fixes that completed JSON::Fast (to-json direction), v2
# campaign 2026-07-23. All three are general correctness fixes.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. istype: an Array is NOT a Seq (a Seq is one-shot); a lazy map IS
{
    use nqp;
    ck(nqp::istype([1,2], Seq), 0, 'Array is not Seq');
    ck(nqp::istype(<a b>.map(*.uc), Seq), 1, 'map result is Seq');
    ck(nqp::istype([1,2], Positional), 1, 'Array is Positional');
}

# 2. a `my` declared in one ternary/nqp branch is visible in a sibling branch
#    (hoists to the enclosing block, like Rakudo)
sub sib($which) {
    $which == 1 ?? (my $shared = 10) !! ($shared = 20);
    $shared;
}
ck(sib(2), 20, 'my hoists across ternary branches');
ck(sib(1), 10, 'declaring branch still works');

# 3. a bare `nqp::op` with no parens is a zero-arg call; mutations share storage
{
    use nqp;
    my $l := nqp::list_i;              # no parens
    nqp::push_i($l, 5); nqp::push_i($l, 7);
    ck(nqp::elems($l), 2, 'bare nqp::list_i is a real list');
    my $t := nqp::list_i;
    nqp::bindpos_i($t, 110, 10);       # sparse
    ck(nqp::atpos_i($t, 110), 10, 'sparse bindpos_i');
    ck(nqp::elems($t), 111, 'sparse grow');
}

say 'PASS' if $ok;
