# Regression: a List made from a literal holds the CONTAINERS of the variables
# in it, as Rakudo's does. `my $m = ($c, 2); $m[0] = 5` writes $c; element 1,
# a plain value, still refuses. rakupp copied every item, so the write was
# refused as X::Assignment::RO (and `$m[0]++` as X::Multi::NoMatch).
#
# Only variables: `(@a[0], 2)` and `(%h<k>, 2)` still hold values here, because
# rakupp has no per-element container a List could keep — a proxy over the
# slot would follow a later `.shift` or `:delete`, which Rakudo's container
# survives (S32-hash/delete-adverb.t).
#
# The native backend has no such container either, so `--exe` bundles a program
# that keeps one and writes through a subscript; it also bundles a write into a
# slurpy parameter's element, which the native Array took where the interpreter
# refuses it. Rakudo passes every check.
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
