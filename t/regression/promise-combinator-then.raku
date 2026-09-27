# Regression: `.then` on a Promise.allof/anyof waited for nothing
# (S07-hyperrace/basics.t "Concurrent iterator access", 2026-09-27).
#
# A combinator keeps no state of its own — its status is folded from its
# members — so `.then` found nothing to queue on and ran its block at once:
# the promise it returned was Kept before a single member had settled.
# basics.t awaits `Promise.anyof(Promise.allof(@workers).then({…}),
# Promise.in(30))` once per round, so the await came back at t=0 and the
# workers of one round counted into the next.
#
# Two more folds fixed with it: a nested combinator's status (it read its
# member's stored "Planned" for ever), and a timer member, which nothing
# settles, now settles a `.then` at its moment.
#
# Expectations checked against Rakudo 2026.08 as `rakudo`
# (/opt/homebrew/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{
    my $gate = Promise.new;
    my @w = (^3).map: -> $i { start { await $gate; $i } };
    my $t = Promise.allof(@w).then({ @w.map(*.status).join(',') });
    ck ~$t.status, 'Planned', 'allof(…).then is Planned while its members are';
    $gate.keep;
    ck (await $t), 'Kept,Kept,Kept', '…and its block runs once every member has settled';
}

{
    my $a = Promise.new;
    my $b = Promise.new;
    my $t = Promise.anyof($a, $b).then({ ~$b.status });
    ck ~$t.status, 'Planned', 'anyof(…).then is Planned while no member has settled';
    $b.keep(2);
    ck (await $t), 'Kept', '…and its block runs once one of them has';
}

{
    my $t = Promise.allof(start { die 'x' }, start { 1 }).then({ ~.status });
    ck (await $t), 'Kept', 'a broken member does not break allof';
}

{
    # (Rakudo keeps a combinator from the thread pool, a moment after the
    # member: hence the await, capped so a wait on nothing cannot hang)
    my $p = Promise.new;
    my $outer = Promise.anyof(Promise.allof($p), Promise.new);
    ck ~$outer.status, 'Planned', 'a nested combinator: Planned while its inner allof is';
    $p.keep;
    await Promise.anyof($outer, Promise.in(5));
    ck ~$outer.status, 'Kept', '…Kept, and awaited, once the inner allof is';
}

{
    my $t0 = now;
    my $t = Promise.anyof(Promise.in(0.3), Promise.new).then({ now - $t0 });
    ck ~$t.status, 'Planned', 'anyof(timer, …).then waits for the timer';
    ck (await $t) >= 0.25, True, '…and runs once the timer has fired';
}

{
    # the basics.t shape: every worker of one round has finished before the next
    my atomicint $done = 0;
    my $early = 0;
    for ^30 {
        my @w = (^4).map: { start { sleep 0.001; ++⚛$done } };
        await Promise.anyof(Promise.allof(@w).then({ 1 }), Promise.in(30));
        $early++ if @w.grep({ .status ~~ Planned });
    }
    ck $early, 0, 'await anyof(allof(@w).then, timer) returns after the workers';
    ck $done, 120, '…every one of them';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
