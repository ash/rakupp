# Regression: `--exe` compiles the routines of the modules it embeds to native
# bodies (src/AotModules.h), which the interpreter enters after binding each
# call's signature. t/exe/run.raku compiles this file and compares the binary's
# output with the interpreter's, so every line below is checked both ways.
#
# The module (t/regression/lib/RakuppNativeBodies.rakumod) exercises what a
# native body takes from the interpreter's frame — module-level variables and
# constants, the module's own subs over same-named built-ins, `@_`, attributes
# (a private one shadowed in a subclass, `$.x` through an rw accessor, a
# Callable attribute called as `&!f(…)`, one read by a closure that outlives
# its method), `callsame`, private and multi methods, a `return` thrown from a
# block back to the routine it belongs to — and what it hands back to the
# interpreter whole: a qualified method call, a nested-subscript store.
#
# The rest are shapes the C++ backend got wrong in ANY compiled program, found
# while compiling Math::NIntegrate's and the module battery's modules (the
# qualified call is refused now, so it is checked in the module, where that
# keeps one routine interpreted rather than the whole program bundled):
#   `%h .= map(…)`  stored the Seq instead of a Hash built from it
#   `|$h`           did not slip an itemized Hash
#   `f(|@aoa)`      flattened the inner arrays as well
#   `$k => v`       made every computed key a Str
#   `* => v`        was a Pair with a Whatever key, not a WhateverCode
#   `*.value.defined`  asked .defined of the inner WhateverCode (always True)
#   `~ $obj`, `"$obj"` ignored the object's own Str method
#   `given X -> $y` never bound $y; a comma list came back an Array
#   `@a[1 .. Inf]` went on with Nils past the end; `@a Z @b Z @c` made pairs of pairs;
#                   `@a[*-1] = v` stored into element 0; `{ $_ if … }` gave Nil, not
#                   Empty; `$buf.push(…)` could not reach the Buf in the variable
#   `$_ /= 2 for $a, $b`, `@l[0..*-2]`, `$!n = Nil`, `andthen` (now refused or interpreted,
#                   so they are checked in the module)
#   `Int($s)`, a sigilless `my \t` or a `my $*D` inside a routine compiled to
#                   the wrong answer (now refused, so they are checked in the module)
#   `.Base::meth`   ignored the qualifier and recursed into the override
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppNativeBodies;
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

bump() for ^3;
ck(calls(), 3, 'a module variable written by a sub');
ck(doubled-sum(1, 2, 3), 12, "the module's own sum, not the built-in");
ck(scaled(4), 40, 'a module constant');
ck(fib-memo(30), 832040, 'a module hash written through a subscript');
ck(counted(1, 2, 3, 4), 4, '@_ in a sub without a signature');
ck(first-even([1, 3, 6, 8]), 6, 'return from inside a for block');
ck(first-even([1, 3]), Nil, 'falling off the end');
ck(safe-div(6, 3), 2.0, 'try without an error');
ck(safe-div(1, 0), 'error: zero', 'try and $!');
ck(head-of-tail([[1, 2], [3, 4], [5, 6]]), '1,3,5', 'a slipped list of lists into +@');
ck(tag-keys(%(a => {x => 1}, b => {y => 2})), 'a=True b=True', '.= map on a hash, |$h');
ck(with-leave(), 'body', 'an interpreted routine with a phaser');
ck(first-big([1, 20, 3]), 'big 20', 'return from a block inside .map');
ck(first-big([1, 2]), 'none', '…and falling through');
ck(returns-through(), 'returned at 2', 'return from a block through a native routine');
ck(coerce-int('41'), 42, 'a coercion call');
ck(doubled-plus-one(5), 11, 'a sigilless local');
ck(uses-rw(), 2, 'an argument written back through `is rw`');
ck(scale-all(4, 8), '2 4', 'a loop writing the topic writes the variables');
ck(all-but-last(<a b c d>), 'a,b,c', 'a range ending in a WhateverCode');
ck(Holder.new.clear.^name, 'Int', 'Nil assigned to a typed attribute');
ck(why-suffix('x') ~ why-suffix(Str), ': x', '`andthen` / `orelse`');
ck(Holder.new.keep([1, 2, 3]), 1, 'an array in a `$` attribute iterates as one item');
my $*NB-DYN = 1;
ck(dyn-outer(), 2, 'a dynamic declared in a routine');
ck($*NB-DYN, 1, '…shadows the caller\'s and leaves it as it was');

my $d = Derived.new(fn => -> $v { $v * 7 });
ck($d.own-x, 2, "a class's own private attribute");
ck($d.base-x, 1, "the parent's private attribute of the same name");
ck($d.describe, 'derived/base', 'callsame');
ck($d.parent-describe, 'base', 'a qualified method call');
ck($d.call-fn(6), 42, 'a Callable attribute called as &!fn(…)');
my &add = $d.adder;
ck(add(40), 42, 'an attribute read by a closure after its method returned');
ck($d.relabel('z'), 'z', 'an assignment through an rw accessor');
ck($d.set-cell('r', 'c', 9), 9, 'a store through two subscripts of an attribute');
ck($d.reveal, 'hidden', 'a private method');
ck($d.kind(1) ~ $d.kind('s'), 'intstr', 'a multi method');
ck($d.with-defaults(1), '1/3/4', 'parameter defaults');
ck($d.with-defaults(1, 2, :c(5)), '1/2/5', 'parameters passed');
ck($d.copy-param(41), 42, 'an `is copy` parameter');
ck($d.note('a').note('b').log.join, 'ab', 'a method that answers self');

# the same shapes in the program itself
my %h = a => {x => 1};
%h .= map({ .key => %( |.value, y => 2 ) });
ck(%h<a>.keys.sort.join, 'xy', '.= map on a hash in the program');
my %outer = inner => {p => 1};
ck(%( |%outer<inner>, q => 2 ).keys.sort.join, 'pq', '|$h in the program');
sub firsts(+@rows) { @rows.map(*.[0]).join(',') }
my @aoa = [1, 2], [3, 4];
ck(firsts(|@aoa), '1,3', 'f(|@aoa) in the program');
my $k = 7;
ck(($k => 2).key.^name, 'Int', 'a computed Pair key keeps its type');
ck((1, 2).map(* => True).map(*.key).join, '12', '`* => v` curries');
ck((a => 1, b => Str).grep(*.value.defined).map(*.key).join, 'a', 'a method chain in one WhateverCode');
class Strish { method Str { 'S!' } }
ck('x' ~ Strish.new, 'xS!', '`~` asks an object its Str');
ck("<{Strish.new}>", '<S!>', 'interpolation asks an object its Str');
sub given-var($x) { given $x * 2 -> $y { $y + 1 } }
ck(given-var(4), 9, '`given X -> $y` binds $y');
sub three() { 1, 2, 3 }
ck(three().^name, 'List', 'a comma list is a List');
my @short = 1, 2, 3;
ck(@short[1 .. Inf].join, '23', 'an infinite range subscript stops at the end');
ck((("a", "b") Z (1, 2) Z ("x", "y")).map(*.join).join(','), 'a1x,b2y', 'a chained Z makes triples');
my @ends = 1, 2, 3;
@ends[*-1] = 9;
ck(@ends.join, '129', 'a store through `[*-1]` counts from the end');
ck((1..4).map({ $_ if $_ > 2 }).join(','), '3,4', 'a block ending in an `if` that does not fire gives Empty');
my $buf = Buf.new;
$buf.push(7);
$buf.append(8, 9);
ck($buf.list.join(','), '7,8,9', 'push/append on a Buf in a variable');

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
