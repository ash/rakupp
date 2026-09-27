# Regression: three things the container-elements work (ROAST-TRACKS-PLAN track
# A, phase A2, 2026-09-27) broke on its way in, each caught by a gate before it
# landed and covered nowhere else.
#
# 1. A Capture literal's `$x` part became `$x`'s CONTAINER (so `\($a)[0]++`
#    steps $a), and slipping that Capture into a call (`|$c`) passed the
#    container itself: Test::Util's doesn't-hang builds `\($*EXECUTABLE, '-e',
#    $code)` and hands it to Proc::Async.new, which stringified the container
#    and ran a garbage program. S04-statements/loop.t, S17-procasync/kill.t,
#    S17-supply/interval.t and S03-junctions/misc.t all went partial.
# 2. `$k => $v` now holds $v's container, and `$y := :$y` built that Pair and
#    then bound $y INTO the container it had just captured — a Pair containing
#    itself, rendered `:y(:y(:y(…)))` (S02-types/pair.t test 172).
# 3. `.value = …` through a Pair not held in a variable (`%h.pairs[0].value`)
#    became writable for every Pair, so a Set's read-only pairs took the write
#    silently (S02-types/set.t test 190).
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/raku — NOT
# the bare name `raku`, which on this box has pointed at rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- 1. a Capture's container parts slip in as VALUES -----------------------
{
    my $code = 'say 42';
    my $c = \('-e', $code);
    sub first-two($a, $b) { "$a|$b" }
    ck(first-two(|$c), '-e|say 42', 'a slipped Capture passes what its parts hold');
    my $n = 41;
    my $d = \($n);
    $d[0]++;
    ck($n, 42, '…while the Capture still holds the container itself');
}

# --- 2. rebinding a name to a Pair built over it ----------------------------
{
    my $y = 42;
    $y := :$y;
    ck($y.raku, ':y(42)', '`$y := :$y` binds $y to a Pair holding the OLD container');
    my $z = 1;
    $z := (a => $z, b => 2);
    ck($z.raku, '(:a(1), :b(2))', '…and so does a list of them');
}

# --- 3. only a Pair over a live container takes a write ----------------------
{
    my $s = <a b c>.Set;
    my $died = False;
    try { $s.pairs[0].value = 0; CATCH { default { $died = True } } }
    ck($died, True, "a Set's pairs refuse a write");
    my %h = a => 1;
    %h.pairs[0].value = 9;
    ck(%h<a>, 9, "…where a Hash's write through to the hash");
    my $died2 = False;
    try { (1, 2).pairs[0].value = 5; CATCH { default { $died2 = True } } }
    ck($died2, True, "…and a List's refuse too");
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
