# Regression: `my ($sql, @bind) := (…)` — a BINDING target list is a signature,
# and `@x` in one is an ordinary positional parameter constrained to Positional.
# It takes ONE argument and binds to it; the same shape under `=` slurps the
# rest. rakupp slurped in both cases, so
#
#     my ($sql, @bind) := do given $pair { .key, .value }
#
# — how Red reads a prepared statement out of a `sql => @binds` pair — put the
# whole array inside a one-element list, and its only member then went to the
# database as a phantom bind parameter ("Wrong number of arguments to method
# execute: got 1, expected 0").
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the reported shape ---------------------------------------------------
{
    my $pair = "BEGIN" => [];
    my ($sql, @bind) := do given $pair { .key, .value }
    ck($sql, 'BEGIN', 'the scalar takes the key');
    ck(+@bind, 0,     'and an EMPTY array binds as itself, not as one element');
}
{
    my $pair = "SELECT ?" => [1, 2];
    my ($sql, @bind) := do given $pair { .key, .value }
    ck(@bind.List, (1, 2), 'a non-empty array binds as itself');
}

# --- every combination, both operators ------------------------------------
{
    my ($a, @b) := ("x", [7, 8]);
    ck(@b.List, (7, 8), ':= binds the array');
}
{
    my (@b, @c) := ([1], [2, 3]);
    ck(@b.List, (1,),    ':= binds each array separately');
    ck(@c.List, (2, 3),  '…so a later target is not left empty');
}
{
    my ($a, %h) := ("x", {:k<v>});
    ck(%h<k>, 'v', ':= binds a hash target');
}
{
    my ($a, @b) = ("x", [7, 8]);
    ck(@b.List, ([7, 8],), '= still SLURPS, so the array is one element');
}
{
    my ($a, @b) = ("x", 7, 8);
    ck(@b.List, (7, 8), '…and a flat list fills it');
}
{
    my ($a, %h) = ("x", {:k<v>});
    ck(%h<k>, 'v', '= with a hash target');
}

# --- scalars either way ---------------------------------------------------
{
    my ($p, $q) := (1, 2);
    my ($r, $s) = (3, 4);
    ck(($p, $q), (1, 2), 'scalar targets bind');
    ck(($r, $s), (3, 4), '…and assign');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
