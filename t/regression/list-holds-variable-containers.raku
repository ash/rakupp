# Regression: a List made from a literal holds the CONTAINERS of the variables
# in it, as Rakudo's does. `my $m = ($c, 2); $m[0] = 5` writes $c; element 1,
# a plain value, still refuses. rakupp copied every item, so the write was
# refused as X::Assignment::RO (and `$m[0]++` as X::Multi::NoMatch).
#
# An ELEMENT item, `(@a[0], 2)` or `(%h<k>, 2)`, holds the element's container
# too: a write through the List reaches the element, and a later `.shift` or
# `:delete` leaves the List holding the container it had (as
# S32-hash/delete-adverb.t expects). A List PUSHED (`@o.push(($w, 0))`) or stored
# in an element (`@o[0] = ($w, 0)`) keeps its containers as well.
#
# The native backend has no such container, so `--exe` bundles a program that
# keeps one and writes through a subscript, or writes what one holds; it also
# bundles a write into a slurpy parameter's element, which the native Array
# took where the interpreter refuses it. Rakudo passes every check.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub throws(&c, $type, $desc) {
    my $got = (try { c(); 'no throw' }) // $!.^name;
    @fail.push("$desc: got $got, want $type") unless $got eq $type;
}

# --- assigned to a scalar ---------------------------------------------------
my $c = 1;
my $m = ($c, 2);
$m[0] = 5;
check $c, 5, 'a write through the List reaches the variable';
$m[0]++;
check $c, 6, '…and so does a step';
check $m, (6, 2), 'the List reads the variable';
$c = 7;
check $m[0], 7, '…as it is now';
throws { $m[1] = 9 }, 'X::Assignment::RO', 'a value element still refuses';
throws { $m[1]++ },   'X::Multi::NoMatch', '…a step too';
throws { $m[0] := 9 }, 'X::Bind', 'binding into the List refuses, container or not';

# --- bound, declared inline, passed as an argument --------------------------
my $a = 1;
my $b = 2;
my $l := ($a, $b);
$l[0] = 10;
check $a, 10, 'a bound List writes through';
my $n = ($a, $b);
$n[1] = 20;
check $b, 20, 'the second variable as well';
sub f($x) { $x[0] = 30 }
f(($a, $b));
check $a, 30, 'a List passed as an argument writes through';
sub g(@x) { @x[1] = 40 }
g(($a, $b));
check $b, 40, '…into an @ parameter too';
my $s = (my $ = 7, 8);
$s[0] = 9;
check $s, (9, 8), 'an anonymous `my $` item is a container';

# --- elements -----------------------------------------------------------------
my @arr = 1, 2;
my $la = (@arr[0], 3);
$la[0] = 40;
check @arr, [40, 2], 'a write through the List reaches an Array element';
@arr[0] = 41;
check $la, (41, 3), '…and the List reads the element as it is now';
my $gone = @arr.shift;
@arr.unshift(0);
check $la, (41, 3), 'after a shift the List keeps the container it had';
my %h = k => 1, j => 2;
my $lh = (%h<k>, 3);
$lh[0] = 50;
check %h<k>, 50, 'a write through the List reaches a Hash element';
%h<k>:delete;
check $lh, (50, 3), 'after a :delete the List keeps the container it had';
my $kk = 'j';
my $lv = (%h{$kk}, 4);
$lv[0] = 60;
check %h<j>, 60, 'a key in a variable names the element too';

# --- stored by a push, or into an element ------------------------------------
my $w = 0;
my @o;
for 1..3 { $w = $_; @o.push(($w, 0)) }
check @o, [(3, 0), (3, 0), (3, 0)], 'a pushed List shows its variable as it is now';
@o[0][0] = 9;
check $w, 9, '…and a write through it reaches the variable';
my @e;
@e[0] = ($w, 1);
$w = 10;
check @e[0], (10, 1), 'a List stored into an element keeps its variable';
check blob8.new(@o[0]), blob8.new(10, 0), 'read whole from an element, it gives values';

# --- what still holds values ------------------------------------------------
my $lit = (1, 2);
throws { $lit[0] = 5 }, 'X::Assignment::RO', 'a List of literals refuses';
check $lit, (1, 2), '…and is unchanged';
my @copy = $m;
check @copy.elems, 1, 'a List in a scalar is one item';
my @flat = @$m;
$c = 100;
check @flat, [7, 2], 'an Array assigned from the List copies the values';

# --- a slurpy parameter's elements ------------------------------------------
sub sl(*@a) { @a[0] = 9; @a }
throws { sl(1, 2) }, 'X::AdHoc', 'a slurpy of values refuses an element write';
my $v = 1;
check sl($v), [9], 'a slurpy of a variable takes it';

if @fail { .say for @fail; say "{+@fail} check(s) failed"; exit 1 }
say 'PASS';
