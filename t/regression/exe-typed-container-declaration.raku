# Regression: typed and trait-carrying variable declarations in a program
# compiled with --exe. The backend built every `@`/`%` variable as a plain
# Array/Hash and ignored the declaration's traits: `my Int @b = 1, 2` was an
# `Array` that took a Str, `my %s is Set` a Hash, `my $x is default(7)` (Any),
# `my Int(Str) $c = "3"` a Str, and the `:D` / `where` constraints never ran.
# Such a declaration is refused now and the program bundles the interpreter.
#
# t/exe/run.raku compiles this file and compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

sub typed-array() { my Int @b = 1, 2; @b.^name }
sub typed-push()  { my Int @b = 1, 2; (try { @b.push('s'); 'pushed' }) // 'refused' }
sub typed-hash()  { my Str %h; (try { %h<a> = 1; 'stored' }) // 'refused' }
sub native-array() { my int @n = 1, 2; @n.^name }

ck typed-array(),  'Array[Int]', 'my Int @b in a sub';
ck typed-push(),   'refused',    'a typed array refuses a Str';
ck typed-hash(),   'refused',    'a typed hash refuses an Int';
ck native-array(), 'array[int]', 'my int @n';

my Int @top = 3, 4;
ck @top.^name, 'Array[Int]', 'my Int @top at the top level';
ck (try { @top.push('x'); 'pushed' }) // 'refused', 'refused', '…which refuses a Str';

my %s is Set = <a b>;
ck %s.^name, 'Set', 'my %s is Set';
my $d is default(7);
ck $d, 7, 'is default';
my Int(Str) $c = '3';
ck $c, 3, 'a coercion type coerces';
my Int:D $s = 1;
ck (try { $s = Nil; 'stored' }) // 'refused', 'refused', 'a :D variable refuses Nil';
my $w where * > 0 = 1;
ck (try { $w = -1; 'stored' }) // 'refused', 'refused', 'a `where` variable checks';

if @fail { .say for @fail; say 'FAIL' }
else     { say 'PASS' }
