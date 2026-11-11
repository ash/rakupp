# Regression (2026-11-10, found auditing issue #209): each thread has its own
# `$/`, and compiled code (--exe) gave them all one.
#
#   my $t = start { for ^10000 { 'a1' ~~ /\d/ } };
#   for ^10000 { 'b2' ~~ /\d/ }
#   await $t;
#
# printed nothing and died with SIGSEGV in every run of its binary, with and
# without -O; interpreted it was fine. A compiled closure makes no scope of
# its own when it is called, so a `start` block's matches wrote `$/` and `$0`
# into the scope current where the worker was spawned, the program's own, and
# the main thread wrote the same slot at once. The worker now starts in a
# routine frame of its own (workerOwnScope, Interpreter.cpp).
#
# t/run.raku builds this file with --exe and requires PASS of the binary;
# t/race/match-var-threads.raku runs the first shape 50 times each way.
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

# the shape that crashed: one worker and the main thread matching at once
{
    my $t = start { my $wn = 0; for ^10000 { $wn++ if 'a1' ~~ /\d/ }; $wn };
    my $mn = 0;
    for ^10000 { $mn++ if 'b2' ~~ /\d/ }
    check (await $t), 10000, 'a worker matches while the main thread does';
    check $mn, 10000, '...and so does the main thread';
}

# four workers and nothing on the main thread
{
    my @p = (^4).map: -> $k { start { my $c = 0; for ^5000 { $c++ if "x$k" ~~ /\d/ }; $c } };
    check (await @p).List, (5000, 5000, 5000, 5000), 'four workers match at once';
}

# each thread reads its own captures, never another's
{
    my $t = start {
        my $wbad = 0;
        for ^5000 { 'a1' ~~ /(\d)/; $wbad++ unless ~$0 eq '1' && ~$/ eq '1' }
        $wbad
    };
    my $mbad = 0;
    for ^5000 { 'b2' ~~ /(\d)/; $mbad++ unless ~$0 eq '2' && ~$/ eq '2' }
    check (await $t), 0, "a worker's \$0 and \$/ are its own match";
    check $mbad, 0, "...and the main thread's are its own";
}

# a worker's match does not become the main thread's `$/`
{
    'z9' ~~ /(\d)/;
    await start { 'q5' ~~ /(\d)/ };
    check ~$/, '9', "the main thread's \$/ after a worker matched";
    check ~$0, '9', "...and its \$0";
}

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
