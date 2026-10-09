# Regression: `.hyper` / `.race` and their `.map` / `.grep` ran on the calling
# thread (2026-10-09). The HyperSeq was a tagged list and `.map` on it was the
# serial `.map`, so `@a.hyper.map(&f)` took exactly as long as `@a.map(&f)`.
#
# - The block now runs on worker threads, in batches (Interpreter::hyperSeqCall
#   over runParallel, the scheduler `hyper for` uses). The answer is a HyperSeq /
#   RaceSeq again, in source order, and read once, as a Seq is.
# - A die in the block reaches the caller doing X::HyperRace::Died.
# - `:batch` / `:degree` carry on through `.map` / `.grep`, from a Range too.
# - Each call owns its `$/`, as a `start` block's does.
#
# Expectations checked against Rakudo 2026.09 (/opt/homebrew/bin/rakudo). Green
# on both, except the one `$/` check marked below: there Rakudo's workers share
# a `$/` and some calls read another's match.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $main = $*THREAD.id;
ck (^2000).hyper.map({ $*THREAD.id }).grep(* != $main).elems, 2000, '.hyper.map runs its block off the calling thread';
ck (^2000).race.map({ $*THREAD.id }).grep(* != $main).elems, 2000, '.race.map too';
ck (^2000).hyper.grep({ $*THREAD.id != $main }).elems, 2000, '.hyper.grep too';
ck (^9).hyper.map({ $*THREAD.id }).grep(* != $main).elems, 9, '…a list shorter than a batch as well';

ck (1..10).hyper.map(* * 2).^name, 'HyperSeq', '.hyper.map answers a HyperSeq';
ck (1..10).race.map(* * 2).^name, 'RaceSeq', '.race.map answers a RaceSeq';
ck (1..10).hyper.grep(* %% 2).^name, 'HyperSeq', '.hyper.grep answers a HyperSeq';
ck (1..10).race.grep(* > 1).^name, 'RaceSeq', '.race.grep answers a RaceSeq';
ck (1..10).hyper.map(* + 1).grep(* > 5).^name, 'HyperSeq', 'a chain stays one';
ck (1..10).hyper.map(* + 1).serial.^name, 'Seq', '.serial makes it a Seq';

ck (^3000).hyper.map(* * 2).List, (^3000).map(* * 2).List, 'the values, in source order';
ck (^3000).race.map(* * 2).sort.List, (^3000).map(* * 2).List, 'race: the same values';
ck (^3000).hyper.grep(* %% 7).List, (^3000).grep(* %% 7).List, 'grep: the same values, in order';
ck (^3000).hyper(:batch(7), :degree(3)).map(* + 1).List, (1..3000).List, 'a batch and degree of its own';
ck (1, 2, 3).hyper.map(* + 1).List, (2, 3, 4), 'a short list';
ck %(a => 1, b => 2).hyper.map(*.key).sort.List, <a b>, 'a Hash source';
ck (1..4).hyper.map(*.Str).hyper.map(* ~ '!').List, <1! 2! 3! 4!>, 'hyper again on the answer';

ck (1..10).hyper.map({ next if $_ == 3; $_ }).List, (1, 2, 4, 5, 6, 7, 8, 9, 10), 'next drops a value';
ck (^300).hyper(:batch(10)).map({ last if $_ == 150; $_ }).List, (^150).List, 'last keeps everything before it, nothing after';
ck (1..4).hyper.map({ slip $_, $_ }).List, (1, 1, 2, 2, 3, 3, 4, 4), 'a Slip spreads';
ck (1..4).hyper.map({ ($_, $_) }).List, ((1, 1), (2, 2), (3, 3), (4, 4)), 'a List stays one element';
ck (1..10).hyper.map(-> $a, $b { $a + $b }).List, (3, 7, 11, 15, 19), 'a two-parameter block takes two at a time';
{
    my @m = 1..5;
    @m.hyper.map({ $_ *= 10 }).eager;
    ck @m.List, (10, 20, 30, 40, 50), 'a write to $_ in .map reaches the array';
    # (a `.hyper` given :batch handed out a COPY of the array, so the writes
    # landed there and were lost)
    my @b = ^2000;
    @b.hyper(:batch(8)).map({ $_ *= 2 }).eager;
    ck @b.List, (^2000).map(* * 2).List, '…given a :batch as well';
    my @g = 1..5;
    my @r = @g.hyper.grep({ $_ = 0 if $_ == 2; True });
    ck (@g.List, @r.List), ((1, 0, 3, 4, 5), (1, 0, 3, 4, 5)), '…and in .grep, kept as written';
}
{
    my $*D = 5;
    ck (1..3).hyper.map({ $*D + $_ }).List, (6, 7, 8), 'a dynamic variable reaches the workers';
}
{
    my $e = do { try (1..10).hyper.map({ die "boom" if $_ == 3; $_ }).eager; $! };
    ck $e ~~ X::HyperRace::Died, True, 'a death in the block is X::HyperRace::Died';
    ck $e.message, 'boom', '…with its own message';
}
ck (1..6).hyper(:batch(2)).map({ $_ }).configuration.batch, 2, ':batch carries through .map, from a Range';
ck (1..6).hyper(:batch(2)).grep({ $_ }).configuration.batch, 2, '…and through .grep';
ck (1..6).race(:degree(3)).map({ $_ }).configuration.degree, 3, ':degree too';
{
    my $r = (1..3).race.grep(* > 1);
    $r.list;
    ck (try { $r.list; 'read twice' }) // $!.^name, 'X::Seq::Consumed', 'read once, like a Seq';
    my $c = (1..3).hyper.map(* + 1).cache;
    ck ($c.list, $c.list), ((2, 3, 4), (2, 3, 4)), '…unless cached';
}
# what stays serial still answers as the parallel path would
ck (1..5).hyper.map({ state $s = 0; $s++ }).elems, 5, 'a block with `state`';
ck (1..4).hyper.grep(/2|3/).List, (2, 3), 'a matcher that is not a block';
ck (1..10).hyper.grep(* > 5, :k).List, (5, 6, 7, 8, 9), 'grep with an adverb';
ck (1..10).hyper.grep(* > 5, :k).^name, 'HyperSeq', '…still a HyperSeq';

# Raku++ only: Rakudo 2026.09's workers share one `$/` here, and some calls
# read another call's match
if $*VM.name ne 'moar' {
    my @w = (^5000).map({ "x$_" });
    ck @w.hyper(:batch(8)).map({ /x(\d+)/; +$0 }).List, (^5000).List, 'each call matches into its own $/';
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
