# Regression: two things the first Scalar-container change broke on its way in
# (ROAST-TRACKS-PLAN track A, phase A1, 2026-09-27). Both were caught by the
# full Roast gate before the change landed; neither was covered anywhere else.
#
# 1. A name bound to a VALUE became immutable (`my $r := 42; $r = 1` dies, as
#    in Rakudo) — and the same flag then refused REBINDING it, which is always
#    legal: `my $r := $v +& $m; $r := $r - $m - 1` died "Cannot assign to an
#    immutable value". S03-buf/write-int.t does exactly that on every value and
#    fell from 2530 passing assertions to 942.
# 2. `=:=` between two ELEMENTS started comparing their slots, so two elements
#    holding equal values are no longer one container. A `take-rw`n element is
#    a compact alias of the slot it came from, and comparing the alias's own
#    address with the slot's said False (S04-statements/gather.t test 38).
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/rakudo — NOT
# the bare name `raku`, which on this box has pointed at rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- 1. a value-bound name is immutable, and still rebindable ----------------
{
    my $v = 13;
    my $r := $v +& 7;
    my $died = False;
    try { $r = 1; CATCH { default { $died = True } } }
    ck($died, True, 'a name bound to a value refuses assignment');
    $r := $r - 8 - 1;
    ck($r, -4, 'and rebinding it is still allowed');
    $r := 3;
    ck($r, 3, 'and again, to a literal');
}
{
    my \z := once 42;
    my $died = False;
    try { z = 100; CATCH { default { $died = True } } }
    ck($died, True, '`once` does not containerize its value');
}

# --- 2. element identity sees through a take-rw alias -----------------------
{
    my @spot = [1, 2], [3, 4];
    my @got;
    @got[0] = eager gather { take-rw @spot[0][1] };   # an item: no list copy
    ck(@got[0][0] =:= @spot[0][1], True, 'a take-rw element is the slot it came from');
    ck(@got[0][0] =:= @spot[1][1], False, '…and not another slot holding a value');
}
{
    my @a = 1, 2, 3;
    my @b = 1, 2, 3;
    ck(@b[1] =:= @a[1], False, 'two elements with equal values are two containers');
    @b[1] := @a[1];
    ck(@b[1] =:= @a[1], True, 'binding one element to another shares the container');
    @a[1] = 100;
    ck(@b[1], 100, '…so a write through one is seen through the other');
    @b = 1, 2, 3;
    ck(@b[1] =:= @a[1], False, 'list assignment gives fresh containers again');
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
