# Regression: two ways a `try` block sank a Proc where Rakudo does not.
#
# Woxi (`rakupp install https://github.com/antononcube/Raku-Woxi`) runs a child
# with Proc::Async and reads its exit status:
#     try { $result = await $completion; CATCH { default { … } } }
#     … :exitcode($result.exitcode // 0)
# and on a timeout kills the child and waits for it:
#     $process.kill; try { await $completion; CATCH { default { } } }
#
# 1. `try` and `quietly` hand their block's value on as it came, container and
#    all, as `do` does — so the assignment's Scalar reached the sink, and Rakudo
#    does not sink through a container. rakupp decontainerized at the `try` and
#    the exit-7 child died the test from outside the try's CATCH.
# 2. A CATCH (or CONTROL) written LAST is the block's last statement in Rakudo:
#    what precedes it runs in sink context INSIDE the block — so its throw goes
#    into that very CATCH — and the block answers Nil. rakupp did this for a
#    routine but not a block, so the killed child's Proc came out of the try and
#    threw X::Proc::Unsuccessful there instead.
#
# Every row was checked against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# A Proc that failed, with its exitcode already read (so nothing else is owed).
sub mkfail() { my $p = shell('exit 7', :!err, :!out); my $c = $p.exitcode; $p }
sub forty-two() { 42 }

# `b()` is a STATEMENT here, so whatever the block hands back is sunk.
sub sinker(&b) { b(); 'quiet' }
sub verdict(&b) { (try sinker(&b)) // 'threw' }

# --- 1. try / quietly keep the container ----------------------------------
ck(verdict({ my $q; try { $q = mkfail() } }),        'quiet', 'try { $q = … } is not sunk');
ck(verdict({ my $q; try $q = mkfail() }),            'quiet', '…nor the blockless try');
ck(verdict({ my $q; quietly { $q = mkfail() } }),    'quiet', '…nor quietly { $q = … }');
ck(verdict({ my $q; try { CATCH { default { } }; $q = mkfail() } }),
                                                     'quiet', '…with a CATCH written first');
ck(verdict({ my $q; do { $q = mkfail(); LEAVE { } } }), 'quiet', 'a LEAVE after the tail does not move it');
ck(verdict({ try { mkfail() } }),                    'threw', 'try { CALL } has no container: it sinks');
ck(verdict({ my $q; once { $q = mkfail() } }),       'threw', 'once decontainerizes');
{
    my $r;
    try { $r = mkfail(); CATCH { default { } } }
    ck($r.exitcode, 7, 'the failed Proc is kept, to read its exitcode');
}

# --- 2. a trailing CATCH: the tail is sunk inside, the block answers Nil ---
{
    my $caught = 0;
    try { mkfail(); CATCH { default { $caught++ } } }
    ck($caught, 1, 'try { CALL; CATCH … }: the sink throws into that CATCH');
    $caught = 0;
    do { mkfail(); CATCH { default { $caught++ } } }
    ck($caught, 1, '…in a do-block too');
    my $ctl = 0;
    ck(verdict({ try { mkfail(); CONTROL { default { $ctl++ } } } }), 'quiet', '…and a trailing CONTROL');
}
ck((try { forty-two(); CATCH { default { } } }).raku,  'Nil', 'try { 42; CATCH … } answers Nil');
ck((do { forty-two(); CATCH { default { } } }).raku,   'Nil', '…do { 42; CATCH … } too');
ck({ forty-two(); CATCH { default { } } }().raku,      'Nil', '…a called block too');
ck((try { forty-two(); CONTROL { default { } } }).raku, 'Nil', '…and a trailing CONTROL');
ck((try { CATCH { default { } }; forty-two() }).raku,  '42',  'a CATCH written FIRST leaves the value');
ck((1, 2).map({ forty-two(); CATCH { default { } } }).List, (Nil, Nil), 'map with a trailing CATCH: (Nil Nil)');
ck((for 1, 2 { forty-two(); CATCH { default { } } }).List,  (Nil, Nil), '…and for');
ck((do given 1 { when 1 { 'x' }; CATCH { default { } } }), 'x', 'a succeeding when still carries its value');
{
    sub f { for ^3 { return 7; CATCH { default { } } } }
    ck(f(), 7, 'a return out of a block with a trailing CATCH');
}

# --- the Woxi shape itself: a killed child awaited inside a try -----------
{
    my $proc = Proc::Async.new('sleep', '5');
    my $completion = $proc.start;
    sleep 0.2;
    $proc.kill;
    my $ok = (try { try { await $completion; CATCH { default { } } }; 'quiet' }) // 'threw';
    ck($ok, 'quiet', 'await of a killed child inside try { …; CATCH … } is quiet');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
