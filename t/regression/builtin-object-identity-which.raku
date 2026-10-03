# Every Promise had the same WHICH.
#
# Promise, Channel, Supplier, Lock and the other built-in objects that keep
# their state in a Hash payload took their identity from that payload's
# RENDERING, so every fresh Promise was `Promise|status Planned`. `.unique`
# kept one of three, a Set of two Promises had one element, and a Promise's
# WHICH changed when it was kept. Rakudo answers an ObjAt for each, by
# identity. Each fixture below builds distinct objects with identical fields,
# which is what the broken engine merged.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

check((Promise.new xx 3).unique.elems,   3, 'three Promises stay three');
check((Channel.new xx 3).unique.elems,   3, 'three Channels stay three');
check((Supplier.new xx 2).unique.elems,  2, 'two Suppliers stay two');
check((Lock.new xx 2).unique.elems,      2, 'two Locks stay two');
check((Lock::Async.new xx 2).unique.elems, 2, 'two Lock::Asyncs stay two');
check((Semaphore.new(1) xx 2).unique.elems, 2, 'two Semaphores stay two');
check((Proc::Async.new('true') xx 2).unique.elems, 2, 'two Proc::Asyncs stay two');
check(Promise.new.WHICH.^name, 'ObjAt', 'a Promise WHICH is an ObjAt');

my $p = Promise.new;
check(set(Promise.new, Promise.new, $p, $p).elems, 3, 'a Set keys Promises by identity');
my %h{Any}; %h{$p} = 1; %h{Promise.new} = 2; %h{Promise.new} = 3; %h{$p} = 4;
check(%h.elems, 3, 'an object hash keys Promises by identity');

# the identity holds still while the state moves, and copies share it
my $w = $p.WHICH; $p.keep(1);
check($p.WHICH eq $w, True, 'keeping a Promise does not move its WHICH');
my $c = Channel.new; my $cw = $c.WHICH; $c.close;
check($c.WHICH eq $cw, True, 'closing a Channel does not move its WHICH');
my @a = $p;
check(@a[0].WHICH eq $p.WHICH, True, 'a copy is the same Promise');
my $q = Promise.new;
check($q.then({ 1 }).WHICH ne $q.WHICH, True, '.then makes a different Promise');
my $sup = Supplier.new.Supply;
check($sup.tap.WHICH ne $sup.tap.WHICH, True, 'two taps are two Taps');

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
