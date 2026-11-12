# Regression: a LEAVE in a program compiled with --exe runs however its scope
# is left — also by an error in a statement written BEFORE it. The binary
# armed a LEAVE that reads the routine's variables only where it was written,
# so `my $r = R.new; LEAVE $r.close` with a dying `new` never ran the LEAVE:
# the caller saw "cannot open" alone, where the interpreter and Rakudo run the
# LEAVE on the undefined `$r` and its "No such method 'close'" follows. A LEAVE
# after other statements now keeps its routine interpreted; one written first
# (or reading nothing the block declares) is still a native scope guard.
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }
sub why(&c) { my @m; { c(); CATCH { default { @m.push: .message } } }; @m.join("|") }

my @log;
sub first-leave { LEAVE @log.push('left'); die 'body' }
ck why(&first-leave), 'body', 'a LEAVE written first: the body error goes on';
ck @log, ['left'], '…and the LEAVE ran';
class R { method new(|) { die 'cannot open' }; method close { } }
sub opener { my $r = R.new; LEAVE $r.close; 1 }
ck why(&opener).starts-with("cannot open|No such method 'close'"), True,
   'a LEAVE after a dying statement still runs';
class H { method open { my $r = R.new; LEAVE $r.close; 1 } }
ck why({ H.open }).starts-with("cannot open|No such method 'close'"), True, '…in a method too';
sub late-log { my $n = 1; die 'early' if $n; LEAVE @log.push("late $n") }
@log = ();
ck why(&late-log), 'early', 'a LEAVE written after the statement that dies';
ck @log, ['late 1'], '…runs, and sees the variable';
sub block-leave { { my $k = 2; die 'in block'; LEAVE @log.push("blk $k") } }
@log = ();
ck why(&block-leave), 'in block', 'a LEAVE in a bare block, reading its variable';
ck @log, ['blk 2'], '…runs';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
