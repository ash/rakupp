# Regression: a LEAVE/KEEP/UNDO phaser naming a variable whose `my` an early
# `return` skipped threw `Variable '$t' is not declared` here, where Rakudo runs
# the phaser with an undefined value (Grand Review phase 2, D3 F34;
# docs/dev/findings/REVIEW-GRAND-DOCS.md).
#
# In Rakudo a `my` is installed in the block's pad at COMPILE time, so its slot
# exists from block entry however control reached the exit. This engine creates
# the slot when the declaration RUNS, which is what the phaser then found
# missing. The slot is now supplied where the difference shows — inside a
# LEAVE/KEEP/UNDO body, which by definition runs while the block is left.
#
# It mattered beyond the toy case: `rakupp install --check` died on any
# POPULATED store, because its sha1 helper returns early whenever the engine has
# a native sha1 and its cleanup phaser then named the skipped declaration. An
# EMPTY store never reaches that code, which is why smoke tests missed it.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}

my @seen;

# 1. the shape that broke: the phaser runs, and reads the skipped `my` as undefined
sub h($fast) {
    return "fast" if $fast;
    my $t = "made";
    LEAVE { @seen.push($t.defined ?? "t=$t" !! 't=undef') }
    "slow"
}
check(h(False), "slow", 'the ordinary path returns its value');
check(h(True),  "fast", 'the early path returns its value');
check(@seen,    ['t=made', 't=undef'], 'the phaser runs both times, undefined when the `my` was skipped');

# 2. every sigil, so the slot is the right kind of empty
my @kinds;
sub sigils($early) {
    return 'early' if $early;
    my $s = 1; my @a = 1, 2; my %h = a => 1;
    LEAVE { @kinds.push("{$s.defined} {@a.elems} {%h.elems}") }
    'late'
}
sigils(False);
sigils(True);
check(@kinds, ['True 2 1', 'False 0 0'], 'a skipped @ is empty and a skipped % is empty, not missing');

# 3. KEEP and UNDO see the same slots
my @ku;
sub kept($early) {
    return 'early' if $early;
    my $v = 'here';
    KEEP { @ku.push('keep:' ~ ($v // 'undef')) }
    UNDO { @ku.push('undo:' ~ ($v // 'undef')) }
    'done'
}
kept(False);
kept(True);
check(@ku, ['keep:here', 'keep:undef'], 'KEEP reads the slot on both paths');

# 4. a phaser in a plain block, left by a loop control
my @loops;
for 1..2 -> $i {
    next if $i == 2;
    my $x = "iter$i";
    LEAVE { @loops.push($x // 'undef') }
}
check(@loops, ['iter1', 'undef'], 'a loop body left by `next` runs its phaser too');

# 5. the declaration is NOT resurrected outside the phaser: a name the block
#    never declared is still an error
check((try { EVAL 'sub f { LEAVE { $nope }; 1 }; f()' }).defined, False,
      'an undeclared name in a phaser is still an error');

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
