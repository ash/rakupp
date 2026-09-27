# Regression: `gather` as a coroutine (ROAST-TRACKS-PLAN track B, phase B1,
# 2026-09-27). The block of a gather runs on a stack of its own, only when
# something pulls, and only as far as the pull needs; it used to be run up to
# 64 takes when the gather was WRITTEN, and re-run from the start for more.
#
# Three kinds of check:
# 1. the laziness itself, and where the block's dynamic scope and its
#    exceptions belong (the consumer, as in Rakudo);
# 2. the readers that took a gather's buffer to be filled already — each of
#    them saw an EMPTY list the first time a gather arrived unpulled (and one,
#    `(gather {} but role {})[0]`, copied out of an object it had just freed);
# 3. a gather dropped while its block is suspended: unwound quietly, LEAVE
#    phasers silent, as when Rakudo drops the continuation.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/raku — NOT
# the bare name `raku`, which on this box has pointed at rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- 1. laziness, dynamic scope, exceptions ----------------------------------
{
    my $c = 0;
    my $g := gather { for 1..5 { $c++; take $_ } };
    ck($c, 0, 'writing a gather runs none of its block');
    ck($g[0], 1, 'the first element');
    ck($c, 1, '…ran the block as far as the first take');
    ck($g[2], 3, 'the third element');
    ck($c, 3, '…and on to the third, resuming where it stopped');
    ck($g.elems, 5, 'all of it');
}
{
    my $i = 0;
    for gather { $i++, .take for 1..5 } { last }
    ck($i, 1, '`for gather {…} { last }` pulls once');
    my @log;
    for gather { for 1..3 { @log.push("p$_"); take $_ } } { @log.push("c$_") }
    ck(@log.join(' '), 'p1 c1 p2 c2 p3 c3', 'producer and consumer interleave');
}
{
    my $n = 0;
    my @a := gather { $n = 1; take $_ for 1..5 }.values;
    ck($n, 0, '.values of a gather pulls nothing');
    ck(+@a, 5, '…and reads it all when asked');
    my $cnt = 0;
    my @l := gather { for 1..10 -> $a { take $a; $cnt++ } }.List;
    ck(@l[2], 3, '.List view, third element');
    ck($cnt, 2, '…ran the block exactly that far');
}
{
    sub mk() { my $*X = 'creator'; gather { take $*X } }
    my $g = mk();
    sub use-it($s) { my $*X = 'consumer'; $s[0] }
    ck(use-it($g), 'consumer', 'a dynamic variable resolves through the consumer');
}
{
    my $e = gather { take 1; die "oops" };
    ck($e[0], 1, 'a block that dies later still gives what it took');
    my $msg = (try { $e[1]; 'no' }) // $!.message;
    ck($msg, 'oops', '…and dies where the next pull happens');
}
{
    sub stopper { last }
    ck((gather { take 1; last; take 2 }).List, (1,), '`last` in the block ends the gather');
    ck((gather { take 1; stopper(); take 2 }).List, (1,), '…also from a routine it calls');
    my @o;
    for 1..3 -> $k {
        my @x = gather { take $k; last if $k == 2; take $k * 10 };
        @o.append: @x;
    }
    ck(@o.join(' '), '1 10 2 3 30', '…and not the loop that reads the gather');
}
{
    my @log;
    my $inner = gather { for 1..3 { @log.push("i$_"); take $_ } };
    my $outer = gather { for $inner.list { @log.push("o$_"); take $_ * 10 } };
    ck($outer[0], 10, 'a gather reading another');
    ck(@log.join(' '), 'i1 o1', '…pulls it only as far as it needs');
}
{
    ck((gather { take 1 }).is-lazy, False, 'a plain gather is not lazy');
    ck((lazy gather { take 1 }).is-lazy, True, '`lazy gather` is');
    my $d = 0;
    my @b = gather { for 1..5 { $d++; take $_ } };
    ck($d, 5, 'list assignment reads a gather whole');
    my $s = 0;
    gather { $s++; take 1; $s++ };
    ck($s, 2, 'a sunk gather statement runs its block to the end');
}
{
    # nested gathers used to re-run every level for every element; depth 200
    # took minutes, and a coroutine per level makes it linear
    sub walk($n) { gather { take $n; if $n > 0 { take $_ for walk($n - 1) } } }
    ck(walk(200).elems, 201, 'deeply nested gathers');
}

# --- 2. readers of an unpulled gather ----------------------------------------
{
    sub g() { gather { take 1; take 2 } }
    ck([g()], [1, 2], '[…] of a gather');
    ck(join('|', g()), '1|2', 'join');
    sub slurp(*@x) { @x }
    ck(slurp(g()), [1, 2], 'a *@ slurpy');
    ck((g() Z (gather { take 'a'; take 'b' })).List, ((1, 'a'), (2, 'b')), 'Z');
    ck((g() X (gather { take 'a' })).List, ((1, 'a'), (2, 'a')), 'X');
    ck((g() »+» 1).List, (2, 3), 'a hyper operator');
    ck(g() ~~ (1, 2), True, 'smartmatch against a list');
    my %h = gather { take 'a' => 1; take 'b' => 2 };
    ck(%h.keys.sort.List, ('a', 'b'), 'hash assignment');
    my ($x, $y) = g();
    ck("$x $y", '1 2', 'destructuring assignment');
    ck(so(gather { take 0 }), True, 'Bool asks for a first element');
    ck(so(gather { }), False, '…and an empty gather has none');
    sub cats($c) { gather { take 'vowel' if $c eq any <a e>; take 'letter' } }
    ck('ab'.comb.categorize(&cats)<vowel>, ['a'], 'a categorize mapper answering a gather');
    ck((gather {} but role {})[0], Nil, 'indexing a gather with a role mixed in');
}
{
    my $was = 1;
    my \one = gather { take $_ for 0..^10; $was = 0 }.lazy;
    my @two = <a b c d e f>;
    my @res = (one Z @two)[^3];
    ck($was, 1, 'Z reads a lazy gather only as far as its result is read');
    my \two = gather { take $_ for 0..^10; $was = 0 }.lazy;
    my @kv = two.kv;
    ck($was, 1, '.kv of a lazy gather pulls nothing up front');
}

# --- 3. a gather dropped while suspended -------------------------------------
{
    my $left = 0;
    my $g = gather { LEAVE { $left = 1 }; take 1; take 2 };
    ck($g[0], 1, 'a gather suspended after one take');
    $g = Nil;
    # the next gather operation unwinds the dropped one
    ck((gather { take 42 }).List, (42,), 'gathers go on working after it is dropped');
    ck($left, 0, 'its LEAVE phaser stays silent');
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
