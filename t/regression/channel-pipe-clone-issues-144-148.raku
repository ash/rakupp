# Regression: issues #144, #145, #148 (2026-10-11).
#
#   #144 `.clone` of an Array held in a scalar came back itemized:
#        `my @b = $a.clone` was one element, the whole Array.
#   #145 `Channel.list` gave nothing until the channel was closed, so a
#        consumer that acts per item (releasing a Semaphore the producer
#        waits on) deadlocked.
#   #148 `.read(N)` on a live process pipe took the rest of the stream: it
#        returned 0 bytes, and only once the child exited, so a request /
#        reply exchange with a child that stays up waited for ever.
#
# Expected answers are Rakudo 2026.09's (2026-10-11).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

# --- #144
my $a = [1, 2, 3];
my @b = $a.clone;
check @b.elems, 3, 'clone of an itemized Array flattens into @b';
@b[0] = 9;
check $a[0], 1, '…and is a copy';
my $h = { a => 1, b => 2 };
my %h2 = $h.clone;
check %h2.elems, 2, 'clone of an itemized Hash';

# --- #145
my $s  = Semaphore.new(2);
my $ch = Channel.new;
my $producer = start { for ^5 { $s.acquire; $ch.send($_) }; $ch.close }
my $consumer = start { my @got; for $ch.list { @got.push: $_; $s.release }; @got }
await Promise.anyof($producer, Promise.in(20));
check $producer.status, Kept, 'the producer is not starved by the consumer';
check (await $consumer), [0, 1, 2, 3, 4], 'the consumer sees every value, in order';

my $c = Channel.new;
start { $c.send(1); sleep 0.5; $c.send(2); $c.close }
my $t0 = now;
my @when;
for $c.list -> $v { @when.push: now - $t0 }
check @when.elems, 2, '.list gives every value';
check @when[0] < 0.4, True, 'the first value arrives as it is sent, not at the close';

my $d = Channel.new;
$d.send($_) for 1..3;
$d.close;
check $d.list.List, (1, 2, 3), '.list of a closed channel drains it';
check $d.closed.status, Kept, '…and keeps .closed';
my $f = Channel.new;
$f.send(1);
$f.fail('boom');
my @seen;
try { for $f.list { @seen.push: $_ } }
check (@seen.List, $!.message), ((1,), 'boom'), 'a failed channel raises its cause once drained';

# --- #148
my $p = run 'sh', '-c', 'printf hi; sleep 3', :out, :bin;
my $t = now;
my $bytes = $p.out.read(65536);
check $bytes.decode, 'hi', '.read answers the bytes there are';
check now - $t < 2, True, '…without waiting for the child to exit';
$p.out.close;

my $q = run 'cat', :in, :out, :bin;
my @replies;
for 'hello', 'again' -> $msg {
    $q.in.write: "$msg\n".encode;
    $q.in.flush;
    @replies.push: $q.out.read(6).decode;
}
$q.in.close;
check @replies, ["hello\n", "again\n"], 'a request / reply exchange with a child that stays up';

my $r = run 'sh', '-c', 'printf abcdef', :out, :bin;
my @chunks;
loop { my $b = $r.out.read(4); last unless $b.elems; @chunks.push: $b.decode }
# (Rakudo hands back the six bytes it has in one go, past the 4 asked
# for; rakupp keeps to "up to N". The data and the end are what both agree on.)
check @chunks.join, 'abcdef', '.read(N) in a loop gets everything, and an empty Buf at the end';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
