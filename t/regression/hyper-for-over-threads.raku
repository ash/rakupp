# Regression: `hyper for` / `race for` ran their body on the calling thread,
# and `$*THREAD` inside any worker said it was the initial thread
# (S07-hyperrace/for.t, 2026-09-27).
#
# - The two prefixes parsed as `do for`: same values, never a second thread.
#   The loop now hands its iterations out in batches to worker threads
#   (Interpreter::runHyperLoop) — the values, `next`, `last` and a write
#   through `$_` still come out as the serial loop gives them.
# - `$*THREAD` answered id 1 wherever the code ran, unless the thread came
#   from Thread.start, so a `start` block could not tell it had left the main
#   thread either. A worker now has a Thread of its own.
#
# Expectations checked against Rakudo 2026.08 as `rakudo`
# (/opt/homebrew/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $main = $*THREAD.id;

ck (await start { $*THREAD.id != $main }), True, 'a start block runs on a thread of its own';
ck (await start { $*THREAD.is-initial-thread }), False, '…which is not the initial thread';
ck $*THREAD.is-initial-thread, True, 'the mainline is';

{
    my $elsewhere = False;
    hyper for ^2000 { $elsewhere = True if $*THREAD.id != $main }
    ck $elsewhere, True, 'a hyper for runs its body over threads';
}
{
    my $elsewhere = False;
    race for ^2000 { $elsewhere = True if $*THREAD.id != $main }
    ck $elsewhere, True, 'a race for too';
}
# …and a body that passes a block (a Lock's `protect`) as well: a block was
# assumed to hold a `state`, which kept such a loop serial (2026-10-09)
{
    my @ids; my $l = Lock.new;
    hyper for ^2000 { $l.protect: { @ids.push: $*THREAD.id } }
    ck @ids.elems, 2000, 'a body passing a block runs every iteration';
    ck @ids.unique.elems > 1, True, '…over threads';
}
{
    my $elsewhere = False;
    for (^2000).hyper { $elsewhere = True if $*THREAD.id != $main }
    ck $elsewhere, False, 'a plain for over a HyperSeq stays on this thread';
}

ck (hyper for ^300 { $_ * 2 }), (^300).map(* * 2).List, 'hyper for: the values, in order';
ck (race for ^300 { $_ * 2 }).sort.List, (^300).map(* * 2).List, 'race for: the same values';
ck (hyper for ^20 { next if $_ %% 2; $_ }), (1, 3 ... 19).List, 'next drops a value';
ck (hyper for ^300 { last if $_ == 150; $_ }), (^150).List, 'last keeps everything before it, nothing after';
ck (hyper for 1..3 { slip $_, $_ }), (1, 1, 2, 2, 3, 3), 'a slip flattens';
ck (hyper for <a b c> -> $x { $x.uc }), <A B C>.List, 'a pointy loop variable';

{
    my @a = 1..200;
    hyper for @a { $_ *= 2 }
    ck @a[0, 99, 199], (2, 200, 400), 'a write through $_ reaches the array';
}

{
    my $err;
    try { hyper for ^200 { die "boom $_" if $_ == 170 }; CATCH { default { $err = $_ } } }
    ck $err ~~ X::HyperRace::Died, True, 'a die arrives doing X::HyperRace::Died';
    ck $err.message, 'boom 170', '…with its own message';
}

{
    my @matched = hyper for <ab cd ae> { m/a(.)/ ?? ~$0 !! '-' }
    ck @matched, ['b', '-', 'e'], 'a match in the body sees its own $/';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
