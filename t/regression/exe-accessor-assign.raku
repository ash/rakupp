# Regression: `$obj.name = v` in a program compiled with --exe. The binary
# stored straight into the attribute (RT.accessorRef), which knows none of what
# the interpreter's method-call lvalue does: a `%` attribute took the list as
# one Array, so `$g.map<a>` died "Type Array does not support associative
# indexing."; a typed attribute took a Str; an `is rw` method (`.calc`) and an
# Array's `.tail`/`.head` were "Target is not assignable". The store now goes
# through the interpreter's own assignment, and a class with an `is rw` method
# (whose native body would hand back a copy, not the container) bundles
# (t/regression/dynamic-method-lvalue.raku has that shape).
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

class C {
    has $.path is rw;
    has @.list is rw;
    has %.map  is rw;
    has Int $.typed is rw;
    has $.ro;
}
my $g = C.new; $g.map = (a => 1, b => 2);
ck $g.map<a>, 1, 'a % attribute takes the pairs';
ck $g.map.elems, 2, '…every one of them';
my $e = C.new; $e.list = (1, 2);
ck $e.list.List, (1, 2), 'a @ attribute takes the list';
my $t = C.new; $t.typed = 7;
ck $t.typed, 7, 'a typed attribute takes a good value';
ck (try { my $x = C.new; $x.typed = 'nope'; 'no throw' } // 'threw'), 'threw', '…and refuses a bad one';
ck (try { my $x = C.new; $x.ro = 5; 'no throw' } // 'threw'), 'threw', 'a read-only one refuses';
my $p = C.new; $p."path"() = '/x';
ck $p.path, '/x', 'a dynamic name';

my @s = [1, 2], [3];
@s.tail = (5, 6);
ck @s[1], (5, 6), '.tail is assignable';
my @a = 1, 2;
@a.head = 9;
ck @a, [9, 2], '…and so is .head';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
