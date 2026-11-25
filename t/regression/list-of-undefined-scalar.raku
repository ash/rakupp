# Regression: `@$x` on an undefined scalar is one item, not nothing (#266).
#
# `my $x; @$x`, `@(%h<missing>)` and `@(@a[5])` answered `()`: the `@`
# contextualizer took any Any or Nil for the empty list. Rakudo makes a
# scalar ONE item, whatever it holds: `(Any,)`, and `(Nil,)` for Nil, a
# missing capture's `@<x>` included. So `for @$x {}` runs once, `my @a = @$x`
# is `[Any]`, and a slurpy receives one argument. A typed `my Int $i` and a
# literal `Any` already answered one item; Empty is still nothing.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
my $x;
check @($x), (Any,), 'my $x; @($x)';
check @$x.elems, 1, '@$x.elems';
my %h;
check @(%h<missing>), (Any,), '@(%h<missing>)';
my $k = 'missing';
check @(%h{$k}).elems, 1, '@(%h{$k})';
my $n = Nil;
check @($n), (Any,), 'my $n = Nil; @($n)';
check @(Nil), (Nil,), '@(Nil)';
my @arr;
check @(@arr[5]), (Any,), '@(@arr[5])';
sub g() { %h<missing> }
check @(g()).elems, 1, 'a routine returning a missed key';
my $c = 0; $c++ for @$x;
check $c, 1, 'for @$x runs once';
my @a = @$x;
check @a, [Any], 'my @a = @$x';
sub f(*@s) { @s.elems }
check f(@$x), 1, 'a slurpy receives one';
"ab" ~~ /a/;
check @<x>, (Nil,), 'a missing capture: @<x>';
my Int $i;
check @($i), (Int,), 'a typed scalar (unchanged)';
check @(Empty).elems, 0, 'Empty is still nothing';
my $l = (1, 2);
check @$l.elems, 2, 'a held list still spreads';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
