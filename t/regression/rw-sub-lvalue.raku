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

if @fail {
    note $_ for @fail;
    die "{+@fail} check(s) failed";
}
say 'PASS';
