# Regression: a second `my $x` in the same scope is the SAME variable, so a name
# already bound to that container keeps seeing it.
#
# `my $x = 2; my $y := $x; my $x = 3` must leave $y at 3
# (S04-declarations/multiple.t, "Two lexicals with the name in same scope are
# the same variable"). While the slot holds a plain value none of this shows —
# every reader finds the variable by name — but once `:=` has promoted it to a
# shared container, redefining the slot dropped the container the alias was
# holding and left it on the old value.
#
# The other half of the pair is what `:=` must still do: REBIND the source and
# the alias stays where it was. Both directions are here on purpose — a change
# that repairs either one by breaking the other is the failure mode.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the reported shape ---------------------------------------------------
{
    my $x = 2;
    my $y := $x;
    my $x = 3;
    ck($y, 3, 'a redeclared lexical is the same variable, and the alias sees it');
    ck($x, 3, 'and the name itself reads the value it was given');
}

# --- a redeclaration with no initialiser changes nothing ------------------
{
    my $p = 2;
    my $q := $p;
    my $p;
    ck($q, 2, 'a bare redeclaration does not reset the container');
    ck($p, 2, 'nor the name');
}

# --- assignment through either name is still one container ----------------
{
    my $b = 1;
    my $a := $b;
    $a = 9;
    ck(($b, $a), (9, 9), 'assignment through the alias reaches the source');
    my $c := $b;
    $c = 7;
    ck(($a, $b, $c), (7, 7, 7), 'a second alias of one source finds the same container');
}

# --- …and REBINDING the source still detaches it (what d7dec5a fixed) -----
{
    my $b = 'one';
    my $a := $b;
    $b = 'two';
    ck($a, 'two', 'the alias tracks an assignment to the source');
    $b := 'three';
    ck($a, 'two', 'but a rebind of the source leaves the alias where it was');
    ck($b, 'three', 'while the source name holds its new container');
}

# --- the loop shape it was found in: a fresh `my` per turn ----------------
{
    my @seen;
    my @closures;
    for 1..3 -> $i {
        my $z = $i;
        @closures.push({ $z });
        @seen.push($z);
    }
    ck(@seen.join(','), '1,2,3', 'a loop body declares its own variable each turn');
    ck(@closures.map({ .() }).join(','), '1,2,3', 'and each closure kept its own');
}

# --- an inner block is a different scope, not a redeclaration -------------
{
    my $o = 2;
    my $alias := $o;
    { my $o = 3; ck($o, 3, 'an inner `my` shadows rather than redeclares') }
    ck($o, 2, 'the outer name is untouched');
    ck($alias, 2, 'and so is anything bound to it');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
