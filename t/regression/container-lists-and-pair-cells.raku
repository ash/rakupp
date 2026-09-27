# Regression: four things the container-elements work (ROAST-TRACKS-PLAN track
# A, phase A2, 2026-09-27) broke on its way in, covered nowhere else. The gates
# caught the first three before it landed; the module battery caught the fourth
# after.
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
# 4. …and over a name bound to a VALUE — a readonly parameter, `my $x := 42` —
#    the Pair shared that slot too, readonly mark and all, so whatever copied
#    the Pair's value copied the mark: `C.new(q => $v)` left the attribute
#    unassignable, and a TWEAK's `$!t := …` died the same way (URI, 11 test
#    files, and Trap). Such a name has no container, so the Pair holds its
#    value, read-only.
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

# --- 4. a Pair over a name bound to a VALUE holds the value ------------------
{
    my class Q { has Str $.q; method set($n) { $!q = $n; $!q } }
    sub via-new($v) { Q.new(q => $v) }
    ck(via-new('a').set('x'), 'x', 'an attribute built from a readonly parameter stays assignable');
    my class B { has Str $.q; submethod BUILD(:$!q = '') {}; method set($n) { $!q = $n; $!q } }
    sub via-build($v) { B.new(:q($v)) }
    ck(via-build('a').set('y'), 'y', '…and so does one BUILD(:$!q) took');
    my class T { has $.t; submethod TWEAK { $!t := 42 } }
    sub via-tweak($v) { T.new(t => $v) }
    ck(via-tweak(1).t, 42, "…and a TWEAK's bind to it succeeds");
    sub write-through($v) {
        my $p = (k => $v);
        my $died = False;
        try { $p.value = 5; CATCH { default { $died = True } } }
        $died
    }
    ck(write-through(1), True, 'the Pair holds the value itself: a write through it dies');
    my $x := 3;
    my $q = (k => $x);
    my $died = False;
    try { $q.value = 5; CATCH { default { $died = True } } }
    ck($died, True, '…as it does over `my $x := 3`');
    my $y = 3;
    my $r = (k => $y);
    $r.value = 5;
    ck($y, 5, '…while over `my $y = 3` it writes $y');
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
