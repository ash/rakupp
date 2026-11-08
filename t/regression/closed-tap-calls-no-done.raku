# Closing the tap of a supply block calls none of the tapper's callbacks, as
# on Rakudo: `supply { whenever Supply.interval(10) { } }.tap(done => {…}).close`
# ran the `done` callback, from the inner tap that closing ends. A supply that
# runs out still calls `done`, and its CLOSE phaser still runs on a close.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @log;
supply { whenever Supply.interval(10) { } }.tap(done => { @log.push('done') }).close; sleep 0.2;
ck @log, [], 'closing an interval as it is tapped';
my $t = supply { whenever Supply.interval(10) { } }.tap(done => { @log.push('done') }, quit => { @log.push('quit') });
$t.close; sleep 0.2;
ck @log, [], 'closing an interval';
@log = ();
my $s = Supplier.new;
my $t2 = supply { whenever $s.Supply { emit $_ }; CLOSE { @log.push('close') } }.tap(done => { @log.push('done') });
$s.emit(1); $t2.close; sleep 0.1;
ck @log, ['close'], 'closing a Supplier: CLOSE, no done';
@log = ();
supply { whenever Supply.from-list(1, 2) { } }.tap(done => { @log.push('done') });
sleep 0.1;
ck @log, ['done'], 'a supply that runs out';
@log = ();
my $s3 = Supplier.new;
supply { whenever $s3.Supply { } }.tap(done => { @log.push('done') });
$s3.done; sleep 0.1;
ck @log, ['done'], 'a source that is done';

say 'PASS';
