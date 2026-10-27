# Issue #207: a `%` parameter bound whatever came. `sub g(:%env) { say
# %env<LC_ALL> }; g(:env(LC_ALL => 'C', LANG => 'C'))` printed C, where
# Rakudo dies at the call: a List of Pairs is no Associative, and it builds no
# hash from one. A test file written that way passed here and died partway
# under Rakudo. The named arm coerced every value into a Hash (Int, Str, Nil,
# a Range, a Match, a non-Associative object). The positional arm refused only
# a definite scalar, a default was not checked at all, and `for … -> %h`,
# `given … -> %h` and `with … -> %h` bound by name only, leaving a List in %h.
#
# A `%` parameter now takes an Associative and nothing else, wherever the
# value comes from, with Rakudo's message. What does bind binds as itself: a
# Hash shared with the caller, a Map, a QuantHash, a Pair, an object or type
# object that does Associative.
#
# Contract: exit 0 + last line PASS. Every case is oracle-checked against
# Rakudo 2026.09 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}
my $X = 'X::TypeCheck::Binding::Parameter';
# what a call answers: the result, or the class of what it threw
sub outcome(&c) { my $r = try c(); $! ?? $!.^name !! $r }

# the issue's program, and the spelling Rakudo binds
sub g(:%env) { %env<LC_ALL> }
check outcome({ g(:env(LC_ALL => 'C', LANG => 'C')) }), $X, "the issue's program refuses";
check outcome({ g(:env(%(LC_ALL => 'C', LANG => 'C'))) }), 'C', ':env(%(…)) binds';
check outcome({ g(env => (LC_ALL => 'C', LANG => 'C')) }), $X, 'env => (…) refuses';
check outcome({ g(|{env => (LC_ALL => 'C', LANG => 'C')}) }), $X, '…and a slipped hash holding a List';
check outcome({ g() }), Any, 'nothing passed is an empty hash';

# Rakudo's words
try g(:env(LC_ALL => 'C', LANG => 'C'));
check $!.message,
    "Type check failed in binding to parameter '\%env'; expected Associative but got List. You have to pass an explicitly typed\n"
    ~ "hash, not one that just might happen to contain elements of the correct\ntype.",
    'the message, hint and wrapping as Rakudo has them';
check ($!.got, $!.expected, $!.symbol), ((LC_ALL => 'C', LANG => 'C'), Associative, '%env'),
    'the exception carries the List, Associative and the name';
try g(:env(42));
check $!.message, "Type check failed in binding to parameter '\%env'; expected Associative but got Int",
    'a scalar names its type alone, with no hint';
my @arr = 1, 2;
try g(:env(@arr));
check $!.message,
    "Type check failed in binding to parameter '\%env'; expected Associative but got Array. You have to pass an explicitly\n"
    ~ "typed hash, not one that just might happen to contain elements of the\ncorrect type.",
    'an Array wraps where Rakudo wraps it';
try g(:env((1, 2).Seq));
check $!.message, "Type check failed in binding to parameter '\%env'; expected Associative but got Seq",
    'a Seq has no .of, so no hint';

# everything that is no Associative refuses, named and positional
class Plain { }
class Assocy does Associative { method AT-KEY($) { 'from AT-KEY' } }
sub p(%h) { %h.^name }
sub n(:%h) { %h.^name }
sub pp(\x) { p(x) }   # (a literal argument is refused at compile time, as in Rakudo)
for (a => 1, b => 2), (1, 2), [1, 2], 1..3, (1, 2).Seq, 'x', 42, 1.5, Nil, Any, Int, Plain,
    Plain.new, ('ab' ~~ /a/), \(1, a => 2), *, -> {}, /a/ -> \v {
    my $what = (try v.^name) // v.raku;
    check outcome({ pp(v) }), $X, "%h refuses a $what";
    check outcome({ n(h => v) }), $X, ":%h refuses a $what";
}
check outcome({ p(Failure.new('boom')) }), $X, '%h refuses a Failure';
check outcome({ n(h => Failure.new('boom')) }), $X, ':%h refuses a Failure';

# …and what is one binds as itself
my %hash = a => 1;
my $item = {a => 1};
for %hash, $item, Map.new((a => 1)), bag(<a b>), set(<a>), mix(<a>), BagHash.new(<a>),
    (a => 1), Assocy.new, Hash, Map, Pair, Bag, Assocy, %*ENV -> \v {
    check outcome({ pp(v) }), v.^name, "%h binds a {v.^name} as itself";
    check outcome({ n(h => v) }), v.^name, ":%h binds a {v.^name} as itself";
}
sub through(%h) { %h<a> }
check through(Assocy.new), 'from AT-KEY', 'an Associative object keeps its methods';
sub pair-ops(%p) { (%p.key, %p.value, %p<a>, %p.keys.List) }
check pair-ops((a => 1)), ('a', 1, 1, ('a',)), 'a Pair answers as a Pair';
sub poke(%h) { %h<new> = 1 }
my %caller = a => 1;
poke(%caller);
check %caller<new>, 1, 'a Hash is shared with the caller';

# `is copy` checks first, then copies
sub cp(%h is copy) { %h<new> = 1; %h.raku }
sub ncp(:%h is copy) { %h.^name }
check outcome({ cp((a => 1, b => 2)) }), $X, '%h is copy refuses a List';
check outcome({ ncp(h => (a => 1, b => 2)) }), $X, ':%h is copy refuses a List';
check outcome({ my $i = 3; cp($i) }), $X, '%h is copy refuses an Int';
check cp((a => 1)), '{:a(1), :new(1)}', 'a Pair copies into a Hash';
my %orig = a => 1;
cp(%orig);
check %orig<new>:exists, False, 'a copied Hash leaves the caller alone';

# a default binds as an argument would
sub d1(%h = (a => 1, b => 2)) { %h.^name }
sub d2(:%h = (a => 1, b => 2)) { %h.^name }
sub d3(:%h = ()) { %h.^name }
sub d4(:%h = Empty) { %h.^name }
sub d5(:%h = 5) { %h.^name }
sub d6(:%h is copy = ()) { %h.^name }
for &d1, &d2, &d3, &d4, &d5, &d6 -> &d {
    check outcome({ d() }), $X, "a non-Associative default refuses ({&d.name})";
}
sub d7(:%h = {a => 1}) { %h<a> }
sub d8(%a, :%h = %a) { %h<x> }
sub d9(:%h = Map.new((a => 1))) { %h.^name }
sub d10(:%h = (a => 1)) { %h.^name }
check d7(), 1, 'a hash default binds';
check d8({x => 2}), 2, 'a default naming another parameter binds';
check d9(), 'Map', 'a Map default binds as itself';
check d10(), 'Pair', 'a Pair default binds as itself';
check d2(h => {b => 2}), 'Hash', 'a passed hash never looks at the default';

# pointy blocks, loops, given/with, methods, multis, destructuring
check outcome({ -> %h { %h.^name }((a => 1, b => 2)) }), $X, 'a pointy block refuses';
check outcome({ for ((a => 1, b => 2),) -> %h { } }), $X, 'for … -> %h refuses a List';
check outcome({ for (1,) -> %h { } }), $X, '…and an Int';
my @seen;
for ({a => 1}, {b => 2}) -> %h { @seen.push: %h.keys.head }
check @seen.List, <a b>, 'for … -> %h over hashes';
my @recs = {a => 1}, {b => 2};
for @recs -> %h { %h<z> = 9 }
check @recs.map(*<z>).List, (9, 9), 'for … -> %h writes through to the element';
@seen = ();
for {a => 1, b => 2}.pairs.sort -> %p { @seen.push: %p.^name }
check @seen.List, <Pair Pair>, 'for %h.pairs -> %p holds each Pair';
@seen = ();
for ({a => 1}, {b => 2}) -> %h, %g { @seen.push: %h.keys.head ~ %g.keys.head }
check @seen.List, ('ab',), 'two hashes a turn';
check outcome({ ((a => 1, b => 2),).map(-> %h { %h.^name }).join }), $X, 'map -> %h refuses';
check ({a => 1}, {b => 2}).map(-> %h { %h.keys.head }).List, <a b>, 'map -> %h over hashes';
check outcome({ given (a => 1, b => 2) -> %h { %h.^name } }), $X, 'given … -> %h refuses';
check outcome({ with (a => 1, b => 2) -> %h { %h.^name } }), $X, 'with … -> %h refuses';
check outcome({ with Any { 1 } else -> %h { %h.^name } }), $X, 'with … else -> %h refuses Any';
check outcome({ with Any { 1 } orwith (a => 1, b => 2) -> %h { %h.^name } }), $X, 'orwith … -> %h refuses';
check outcome({ if (a => 1, b => 2) -> %h { %h.^name } }), $X, 'if … -> %h refuses';
check (given {a => 1} -> %h { %h<a> }), 1, 'given a hash -> %h';
check (with (a => 1) -> %h { %h.^name }), 'Pair', 'with a Pair -> %h';
class K {
    method m(%h) { %h.^name }
    method n(:%h) { %h.^name }
}
check outcome({ K.m((a => 1, b => 2)) }), $X, 'a method refuses';
check outcome({ K.n(h => (a => 1, b => 2)) }), $X, '…and a named one';
class B { has %.h; submethod BUILD(:%!h) { } }
check outcome({ B.new(h => (a => 1, b => 2)) }), $X, 'an attributive :%!h refuses';
class A2 { has %.h }
check A2.new(h => (a => 1, b => 2)).h, {a => 1, b => 2}, '…where an attribute assigned by new coerces';
sub ds(%h (:$a, *%)) { $a }
check outcome({ ds((a => 1, b => 2)) }), $X, 'a destructuring %h (…) refuses';
check ds({a => 1, b => 2}), 1, '…and destructures a hash';
multi m(%h) { 'hash' }
multi m(@a) { 'array' }
check m((a => 1, b => 2)), 'array', 'a multi still picks the Positional candidate';
check m({a => 1}), 'hash', '…and the Associative one';

# a Junction autothreads before it binds
check outcome({ p(any({a => 1}, {b => 2})).raku }), 'any("Hash", "Hash")', 'a Junction of hashes autothreads';
check outcome({ p(any((1, 2), {b => 2})) }), $X, 'a Junction holding a List refuses';

# the placeholder %^h was already strict
sub ph { %^h.^name }
check outcome({ pp(1) }), $X, 'placeholder baseline';
check outcome({ ph((a => 1, b => 2)) }), $X, '%^h refuses a List';

.note for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
