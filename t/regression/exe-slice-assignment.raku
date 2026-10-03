# Regression: assigning to a SLICE in a program compiled with --exe.
#
# The native backend took a slice subscript as one key: `@a[0, 1] = 7, 8` put
# the whole list into the element the list's COUNT named, `[1 2 (7 8)]`, with
# and without -O alike, so the -O/no-O gate never saw it. A slice target is now
# refused, which bundles the interpreter. Found beside issue #122
# (`@$h[0, 1] = …`).
#
# t/exe/run.raku compiles this file and compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

my @a = 1, 2, 3;
@a[0, 1] = 7, 8;          ck @a, [7, 8, 3], '@a[0, 1] = …';
@a[^2] = 4, 5;            ck @a, [4, 5, 3], '@a[^2] = …';
@a[1..2] = 0, 0;          ck @a, [4, 0, 0], '@a[1..2] = …';
my @i = 0, 2;
@a[@i] = 9, 9;            ck @a, [9, 0, 9], '@a[@i] = …';
my $h = [1, 2, 3];
$h[0, 2] = 'x', 'y';      ck $h, ['x', 2, 'y'], '$h[0, 2] = …';
@$h[1, 2] = 'p', 'q';     ck $h, ['x', 'p', 'q'], '@$h[1, 2] = …';
my %h;
%h<a b> = 1, 2;           ck %h, {a => 1, b => 2}, '%h<a b> = …';
%h{'c', 'd'} = 3, 4;      ck %h<c d>.List, (3, 4), '%h{…, …} = …';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
