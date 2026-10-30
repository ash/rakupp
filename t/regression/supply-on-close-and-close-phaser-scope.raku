# Regression: three things Cro's in-memory test connector and Cro::RPC::JSON's
# actor handler need from supplies:
#
#   * `supply { whenever $channel { … } }.on-close({ … })` HUNG: on-close was
#     answered after the supply's eager drain, which waited on the channel
#     forever (Cro::HTTP::Test's every request);
#   * the tap of such a supply ran no on-close hook when closed;
#   * a CLOSE phaser closed over the supply block's DEFINITION scope until the
#     body first emitted, so a body that only reacts could not call its own
#     `my sub` from CLOSE ("Undefined routine").
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @log;
my $lock = Lock.new;
sub log($x) { $lock.protect: { @log.push: $x } }

my $in = Channel.new;
my $s = supply { whenever $in -> $d { emit "msg:$d"; LAST done } }.on-close({ log 'on-close' });
ck $s.^name, 'Supply', 'on-close over a channel-fed supply answers at once';
my $t = $s.tap: -> $v { log $v };
$in.send("a"); $in.send("b");
for ^100 { last if @log >= 2; sleep 0.01 }
$t.close;
ck @log.List, ('msg:a', 'msg:b', 'on-close'), '…delivers what the channel sends, and runs the hook on close';

my $closed = Promise.new;
my $v = $closed.vow;
my $s2 = supply {
    my sub mark($what) { $v.keep($what) }
    whenever Supply.interval(10) { }
    CLOSE { mark('closed') }
};
my $t2 = $s2.tap;
$t2.close;
await Promise.anyof($closed, Promise.in(5));
ck ($closed.status ~~ Kept ?? $closed.result !! 'not run'), 'closed',
   "a CLOSE phaser calls a sub of the body that never emitted";

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
