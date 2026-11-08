# When a supply's own code dies, its CLOSE phasers run before the tapper's
# quit handler hears of it, as on Rakudo, where the die unwinds the supply
# block first: here the tapper heard first, in every whenever arm (a Supply,
# a Promise, a timer, an interval, a Channel), for a die in the block itself,
# in a LAST, and in a QUIT phaser that dies. A `done` in the block runs CLOSE
# before the tapper's done callback too. A source's quit that only passes
# through (no QUIT phaser, or one that matches nothing or just notes it), a
# source's done and a block that runs out reach the tapper first, then CLOSE,
# on Rakudo as here.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @log;
my $l = Lock.new;
sub log($s) { $l.protect: { @log.push: $s } }
# run the setup, then wait for its two events
sub order(&setup) {
    $l.protect: { @log = () };
    setup();
    for ^500 { last if $l.protect({ @log.elems }) >= 2; sleep 0.01 }
    $l.protect: { @log.join(' ') }
}

my $s1 = Supplier.new;
ck order({
    supply { whenever $s1.Supply { die "own" }; CLOSE log 'CLOSE' }.tap: -> $ { }, quit => { log 'quit' };
    $s1.emit(1);
}), 'CLOSE quit', 'a Supply whenever body dies';
ck order({ supply { die "body"; CLOSE log 'CLOSE' }.tap: -> $ { }, quit => { log 'quit' } }),
   'CLOSE quit', 'the block dies';
ck order({ supply { whenever Promise.in(0.02) { die "p" }; CLOSE log 'CLOSE' }.tap: -> $ { }, quit => { log 'quit' } }),
   'CLOSE quit', 'a Promise whenever body dies';
ck order({ supply { whenever Supply.interval(0.02) { die "i" }; CLOSE log 'CLOSE' }.tap: -> $ { }, quit => { log 'quit' } }),
   'CLOSE quit', 'an interval whenever body dies';
my $c = Channel.new;
ck order({
    supply { whenever $c { die "c" }; CLOSE log 'CLOSE' }.tap: -> $ { }, quit => { log 'quit' };
    $c.send(1);
}), 'CLOSE quit', 'a Channel whenever body dies';
ck order({ supply { whenever Supply.from-list(1, 2) { die "fl" }; CLOSE log 'CLOSE' }.tap: -> $ { }, quit => { log 'quit' } }),
   'CLOSE quit', 'a from-list whenever body dies';
ck order({ supply { whenever supply { emit 1 } { die "inner" }; CLOSE log 'CLOSE' }.tap: -> $ { }, quit => { log 'quit' } }),
   'CLOSE quit', 'a supply block whenever body dies';
my $s2 = Supplier.new;
ck order({
    supply { whenever $s2.Supply { LAST { die "last" } }; CLOSE log 'CLOSE' }.tap: -> $ { }, quit => { log 'quit' };
    $s2.done;
}), 'CLOSE quit', 'a LAST dies';
my $s3 = Supplier.new;
ck order({
    supply { whenever $s3.Supply { QUIT { default { die "again" } } }; CLOSE log 'CLOSE' }.tap: -> $ { }, quit => { log 'quit' };
    $s3.quit("q");
}), 'CLOSE quit', 'a QUIT phaser dies';
ck order({ supply { whenever Promise.broken("b") { QUIT { default { die "re" } } }; CLOSE log 'CLOSE' }.tap: -> $ { }, quit => { log 'quit' } }),
   'CLOSE quit', "a broken Promise's QUIT phaser dies";
ck order({ supply { emit 1; done; CLOSE log 'CLOSE' }.tap: -> $ { }, done => { log 'done' } }),
   'CLOSE done', '`done` in the block';
ck order({ supply { whenever Promise.in(0.02) { done }; CLOSE log 'CLOSE' }.tap: -> $ { }, done => { log 'done' } }),
   'CLOSE done', '`done` in a whenever';

my $s4 = Supplier.new;
ck order({
    supply { whenever $s4.Supply { }; CLOSE log 'CLOSE' }.tap: -> $ { }, quit => { log 'quit' };
    $s4.quit("q");
}), 'quit CLOSE', "a source's quit passes through";
my $s5 = Supplier.new;
ck order({
    supply { whenever $s5.Supply { QUIT { when X::AdHoc { } } }; CLOSE log 'CLOSE' }.tap: -> $ { }, quit => { log 'quit' };
    $s5.quit(X::NYI.new(:feature<x>));
}), 'quit CLOSE', 'a QUIT phaser that matches nothing';
ck order({ supply { whenever Promise.broken("b") { }; CLOSE log 'CLOSE' }.tap: -> $ { }, quit => { log 'quit' } }),
   'quit CLOSE', 'a broken Promise';
my $s6 = Supplier.new;
ck order({
    supply { whenever $s6.Supply { }; CLOSE log 'CLOSE' }.tap: -> $ { }, done => { log 'done' };
    $s6.done;
}), 'done CLOSE', "a source's done";
ck order({ supply { emit 1; CLOSE log 'CLOSE' }.tap: -> $ { }, done => { log 'done' } }),
   'done CLOSE', 'the block runs out';

say 'PASS';
