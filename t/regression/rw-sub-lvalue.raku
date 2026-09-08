# Regression: an `is rw` SUB was not assignable — `f($v) = 7` died
# "Target is not assignable" (X::Assignment::RO), and where it did not die the
# write landed in a copy. The same routine spelled as a METHOD worked, which is
# what hid it.
#
# Two halves were missing, both on the SUB side of a pair whose method side was
# already right. `lvalue()` had an arm for a `retRw` method call and none for a
# `retRw` sub call, so a bare `f(...)` on the left of `=` fell through to the
# final throw. And the routine body-runner's tail-return fast path — the one a
# plain `sub` takes — evaluated `return-rw X` in place without ever consulting
# `wantLvalue`, so even once the caller asked for a container the callee handed
# back a copy. The method runner had had both checks for a while.
#
# Found from issue #69 (Crane): `Crane.in(%h, 'a') = 'Sea'` silently left %h
# empty. Crane's whole API is an `is rw` method delegating to `is rw` multi
# subs, which is 12 of its 15 test files. Roast's
# S06-routine-modifiers/native-lvalue-subroutines.t went from partial to
# fully-passing on the fix.
#
# Runs under both engines: Rakudo passes every check natively.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- the minimal case: an rw sub returning an element of a parameter ---------
sub el(%c, $k) is rw { return-rw %c{$k} }
my %a;
el(%a, 'k') = 'A';
check %a, {k => 'A'}, 'is-rw sub returning a hash element is assignable';

sub pos(@c, $i) is rw { return-rw @c[$i] }
my @p;
pos(@p, 0) = 'P';
check @p, ['P'], 'is-rw sub returning an array element is assignable';

# --- a sigilless parameter, the spelling Crane uses --------------------------
sub bare(\c, $k) is rw { return-rw c{$k} }
my %b;
bare(%b, 'k') = 'B';
check %b, {k => 'B'}, 'is-rw sub with a sigilless parameter is assignable';

# --- an IMPLICIT return from an rw sub, not just the explicit return-rw ------
sub implicit(%c, $k) is rw { %c{$k} }
my %c;
implicit(%c, 'k') = 'C';
check %c, {k => 'C'}, 'is-rw sub with an implicit return is assignable';

# --- the container must survive a CHAIN of rw hops (Crane recurses) ----------
sub h1(\c, $k) is rw { return-rw c{$k} }
sub h2(\c, $k) is rw { return-rw h1(c, $k) }
sub h3(\c, $k) is rw { return-rw h2(c, $k) }
my %d;
h3(%d, 'k') = 'D';
check %d, {k => 'D'}, 'three rw sub hops still reach the caller container';

# --- …and a method delegating to an rw sub, which is Crane's actual shape ----
class Facade {
    method put(\c, $k) is rw { return-rw h2(c, $k) }
}
my %e;
Facade.put(%e, 'k') = 'E';
check %e, {k => 'E'}, 'rw method delegating to an rw sub is assignable';

# --- an rw multi: the candidate is only known after dispatch -----------------
multi sub pick(Associative:D \c, $k) is rw { return-rw c{$k} }
multi sub pick(Positional:D  \c, $i) is rw { return-rw c[$i] }
my %f;
pick(%f, 'k') = 'F';
check %f, {k => 'F'}, 'rw multi (Associative candidate) is assignable';
my @g;
pick(@g, 0) = 'G';
check @g, ['G'], 'rw multi (Positional candidate) is assignable';

# --- nesting autovivifies the whole path, as through a plain subscript -------
sub deep(\c, @steps) is rw {
    @steps.elems == 1 ?? return-rw c{@steps[0]} !! return-rw deep(c{@steps[0]}, @steps[1..*])
}
my %h;
deep(%h, ['x', 'y', 'z']) = 'deep';
check %h, {x => {y => {z => 'deep'}}}, 'rw recursion autovivifies the whole path';

# --- a NON-rw sub must still be a plain rvalue: assigning to it dies ---------
sub plain(%c, $k) { %c{$k} }
my %i = k => 1;
my $died = False;
try { plain(%i, 'k') = 'nope'; CATCH { default { $died = True } } }
check $died, True, 'a sub without `is rw` is still not assignable';
check %i, {k => 1}, '…and the non-rw call left the container alone';

# --- a SIGILLESS parameter is the caller's container --------------------------
# `\c` binds the caller's container, so returning it rw must reach the caller's
# variable. rakupp models the binding as a frame copy plus a write-back that
# runs AT RETURN — before the caller's assignment — so the write went nowhere,
# even though `c = 5` INSIDE the routine wrote through fine.
sub raw-id(\c) is rw { return-rw c }
my $r1 = 0;
raw-id($r1) = 1;
check $r1, 1, 'return-rw of a sigilless parameter reaches the caller';

class RawM { method m(\c) is rw { return-rw c } }
my $r2 = 0;
RawM.m($r2) = 1;
check $r2, 1, '…through a method too';

sub raw-hop(\c) is rw { return-rw raw-id(c) }
sub raw-hop2(\c) is rw { return-rw raw-hop(c) }
my $r3 = 0;
raw-hop2($r3) = 1;
check $r3, 1, '…and transitively, three frames up';

# an `is rw` scalar parameter is the same shape
sub rw-id($x is rw) is rw { return-rw $x }
my $r4 = 0;
rw-id($r4) = 7;
check $r4, 7, 'return-rw of an `is rw` parameter reaches the caller';

# the write must not leave the routine's own view of the parameter behind:
# Crane's `set` assigns through the container and then RETURNS it
sub set-and-read(\c, $v) { raw-id(c) = $v; c }
my $r5 = 0;
check set-and-read($r5, 3), 3, 'the routine sees its own write through the parameter';
check $r5, 3, '…and so does the caller';

# …and the `%`/`@` sigil still owns the list: the slot stays a container
sub set-container(\c, $v) { raw-id(c) = $v; c }
my %rh;
set-container(%rh, { :a(1) });
check %rh, { :a(1) }, 'a %-slot written through a parameter stays a Hash';
check %rh.WHAT.gist, '(Hash)', '…with the Hash type, not an itemized copy';
# …and an `@`-slot takes it as LIST assignment, one hop out. (Rakudo's answer
# changes with the number of raw-binding hops between the slot and the
# assignment — two hops keep the List as a single element — so only the direct
# shape, where both engines agree, is asserted here.)
my @ra = 1, 2;
raw-id(@ra) = ('x', 'y');
check @ra, ['x', 'y'], 'an @-slot written through a parameter stays an Array';
check @ra.WHAT.gist, '(Array)', '…with the Array type';

if @fail {
    note $_ for @fail;
    die "{+@fail} check(s) failed";
}
say 'PASS';
