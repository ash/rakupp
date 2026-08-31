# Regression: the gaps `rakupp install BSON::Simple` walked into — the dist, its
# Hash::Ordered / Hash::Agnostic dependencies, and the four dists already in that
# plan. Every expectation is the Rakudo 2026.08 answer, and this file passes under
# Rakudo too, except where a comment says which rakupp-specific relationship is
# being pinned instead of an absolute value.

use lib $?FILE.IO.parent.add('../fixtures/agnostic-lib');
use CircumfixOps;

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# --- `use lib` takes effect DURING the parse ---------------------------------
# The `use lib` above names a directory no default search path holds, and the
# operator below comes from a module in it. Parsing this file at all is the test:
# the operators a `use`d module declares are harvested while this file parses, so
# a parser that ignores `use lib` never sees `circumfix:<⦃ ⦄>` and the next line
# is a syntax error.
ok(⦃ 1, 2 ⦄.list[0].elems == 2, 'an operator from a use lib directory parses');

# --- a circumfix operand is a TERM, not an argument list ---------------------
{
    my $one = ⦃ hello => 'world' ⦄;
    ok($one.list.elems == 1 && $one.hash.elems == 0,
       'a pair between custom brackets is a POSITIONAL part');
    ok($one.list[0] ~~ Pair && $one.list[0].key eq 'hello', 'and it arrives whole');
    ok(⦃ ⦄.list.elems == 1, 'empty brackets still pass one part (the empty list)');
}

# --- a Capture carries its own named/positional split ------------------------
{
    my %h = x => 1, y => 2;
    sub split-of($c) { $c.list.elems ~ '/' ~ $c.hash.elems }
    ok(split-of(\(1, 2, a => 3))     eq '2/1', 'a written pair is a named part');
    ok(split-of(\(1, 'a' => 3))      eq '2/0', 'a QUOTED key is positional');
    ok(split-of(\(1, (a => 3)))      eq '2/0', 'and so is a parenthesised pair');
    ok(split-of(\(@(1, 2, 3)))       eq '1/0', 'a bare array is ONE positional');
    ok(split-of(\(|%h))              eq '0/2', '|%h slips as named parts');
    ok(split-of(\('p', |%h))         eq '1/2', 'beside a positional');

    # …and slipping a Capture hands each part back the way it went in
    sub parts(**@v, *%n) { @v.elems ~ '/' ~ %n.elems }
    ok(parts(|\(1, 2, a => 3)) eq '2/1', 'slipping keeps a named part named');
    ok(parts(|\(1, (a => 3)))  eq '2/0', 'and a positional Pair positional');
}

# --- Buf.splice takes a Blob as its replacement ------------------------------
{
    my $b = buf8.new;
    $b.splice(0, 0, 'hi'.encode('utf8'));
    ok($b.list eqv (104, 105), 'a Blob replacement splices its BYTES in');
    my $c = buf8.new(9, 9, 9);
    $c.splice(1, 1, buf8.new(7, 7));
    ok($c.list eqv (9, 7, 7, 9), 'replacing a window with a longer buffer');
    ok(buf8.new(1, 2, 3, 4).splice(1, 2).raku eq 'Buf[uint8].new(2,3)',
       'the removed bytes keep the buffer type');
}

# --- a class deriving a scalar built-in holds that built-in's VALUE ----------
{
    my role Marker { }
    my class Int64  is Int does Marker { }
    my class Symbol is Str does Marker { }
    ok(Int64.new(-42) == -42 && +Int64.new(7) == 7, 'an Int subclass numifies as its value');
    ok(Int64.new(7).^name eq 'Int64' && Int64.new(7) ~~ Int, 'and keeps its own type');
    ok(Symbol('b') eq 'b' && Symbol('b').chars == 1, 'a Str subclass stringifies as its value');
    ok(Int.new(5) == 5, 'Int.new takes the value it is given');

    # …but a class that adds anything of its own is an ordinary object: roast's
    # trig files define exactly this shape and decide their own numification.
    my class NotComplex is Cool { method Numeric() { 6 } }
    ok(+NotComplex.new == 6, 'a subclass with a method of its own is not boxed');
}

# --- Instant is one clock: `.to-posix` undoes what every producer adds -------
# (The offset itself is this engine's, not Rakudo's 37 leap seconds — what has to
# hold, and did not, is that the producers and `.to-posix` agree.)
{
    ok(DateTime.new('1970-01-01T00:00:00Z').Instant.to-posix[0] == 0,
       'DateTime.Instant round-trips through to-posix');
    ok(DateTime.new('2012-12-24T12:15:30.501Z').Instant.to-posix[0] == 1356351330.501,
       'including the fraction');
    ok(Instant.from-posix(1356351330).DateTime.Str eq '2012-12-24T12:15:30Z',
       'and Instant.DateTime reads the same clock');
    ok((now.to-posix[0] - time).abs < 5, 'now sits on it too');
    ok(!sleep-until(now - 5), 'so an instant already past does not wait');
}

# --- Signature.count: a slurpy HASH takes no positional ----------------------
{
    sub named-slurpy($x, *%o) { }
    sub list-slurpy($x, *@y)  { }
    my class K { method m(Mu $p, *%opts) { } }
    ok(&named-slurpy.signature.count == 1, '*%o does not make the count Inf');
    ok(&list-slurpy.signature.count == Inf, '*@y still does');
    ok(K.^lookup('m').signature.count == 2, 'and a method counts its invocant');
}

# --- `self.Mu::meth` reaches the BUILT-IN, not the override that asked -------
{
    my role Stringish {
        proto method Str(|) {*}
        multi method Str(::?ROLE:U:) { self.Mu::Str }
        multi method Str(::?ROLE:D:) { 'instance' }
    }
    my class C does Stringish { }
    ok((quietly C.Str) eq '', 'a built-in qualifier dispatches past the caller');
    ok(C.new.Str eq 'instance', 'while the instance candidate still wins');
}

# --- a type object gists by its SHORT name -----------------------------------
{
    my class Deep::Down { }
    ok(Deep::Down.gist eq '(Down)', 'a nested type gists by its last segment');
    ok(Deep::Down.raku eq 'Deep::Down', 'while .raku keeps the full name');
    ok(Int.gist eq '(Int)' && Array[Int].gist eq '(Array[Int])',
       'an unnested and a parameterized one are unchanged');
}

# --- a `$` scalar HOLDS an object; it is not a container of that type --------
{
    my class Storey { has @.seen; method STORE(*@v) { @!seen = @v; self } }
    my $v = Storey.new;
    $v = 1;
    ok($v == 1, 'assigning to a scalar replaces what it holds');
    my %c is Storey = 1, 2;
    ok(%c.seen.elems == 2, 'while a %-container of that type takes its STORE');
}

# --- binding a hash element: a VALUE is immutable, a container is an alias ---
{
    my %p;
    %p<a> := 137;
    ok(%p<a> == 137, 'a bound element reads back');
    ok(!(try { %p<a> = 666; True }), 'and a bound VALUE cannot be assigned to');
    ok((%p<a>:delete) == 137, 'so :delete still answers what was bound');

    my $cell = 1;
    my %q;
    %q<k> := $cell;
    ok((try { %q<k> = 9; True }) === True, 'binding something that NAMES a container aliases it');
}

# --- an element alias survives what Rakudo's container survives --------------
{
    my @a = 1, 2, 3;
    my \p = @a[1];  p = 9;
    ok(@a eqv [1, 9, 3], 'a sigilless declarator still aliases the element');

    my @b = 1, 2, 3;
    my $x := @b[1];  @b.push(9);  $x = 5;
    ok(@b eqv [1, 5, 3, 9], 'and the alias survives a push');

    my @c = 1, 2, 3, 4;
    my \v = @c[2];
    @c.splice(2, 1);
    ok(v == 3, 'but a SPLICE leaves it holding the value it named');
}

# --- the Associative protocol answers the presentation adverbs ---------------
{
    # the Hash::Agnostic shape: the protocol comes from a composed ROLE
    my role AgLike does Associative does Iterable {
        method AT-KEY($)  { ... }
        method keys()     { ... }
        method pairs()    { self.keys.map({ Pair.new($_, self.AT-KEY($_)) }) }
        method iterator() { self.pairs.iterator }
        method list()     { List.from-iterator(self.iterator) }
        method Hash()     { Hash.new(self) }   # the whole point of the role's .Hash
    }
    my class Assoc does AgLike {
        has %!h;
        method AT-KEY($k)     is raw { %!h.AT-KEY($k)         }
        method ASSIGN-KEY($k, \v)    { %!h.ASSIGN-KEY($k, v)   }
        method EXISTS-KEY($k)        { %!h.EXISTS-KEY($k)     }
        method keys()                { %!h.keys               }
    }
    my $a = Assoc.new;
    $a.ASSIGN-KEY('x', 1); $a.ASSIGN-KEY('y', 2);
    ok(($a{<x y>}:v).sort.List eqv (1, 2), 'a value slice reads through AT-KEY');
    ok(($a{}:v).sort.List      eqv (1, 2), 'the zen slice walks .keys');
    ok(($a{*}:v).sort.List     eqv (1, 2), 'and so does the whatever slice');
    ok(($a{<x y>}:k).sort.List eqv ('x', 'y'), ':k answers the keys');
    ok($a.Hash.sort.List eqv (x => 1, y => 2), 'Hash.new takes an Associative apart');
}

# --- an ANONYMOUS variable takes its container trait -------------------------
{
    my class Counted { has $.n is rw = 0; method STORE(*@v) { $!n = @v.elems; self } }
    my $seen;
    { my % is Counted = 1, 2, 3; }   # the trait was dropped, so STORE never ran
    my %named is Counted = 1, 2, 3;
    ok(%named.n == 3, 'a named container-typed hash runs STORE');
    my @anon-probe = do { my $c = Counted.new; $c.STORE(1, 2, 3); $c.n };
    ok(@anon-probe[0] == 3, 'and the trait itself still works');
}

# --- a coercion call finds a class by the name its own package sees ----------
# `Symbol('b')` inside `unit module BSON::Simple` names BSON::Simple::Symbol; the
# coercion call looked the SHORT name up in the flat class registry and reported
# an undefined routine.
{
    module Pkg {
        class Leaf { has $.v; method COERCE($v) { Leaf.new(:$v) } }
        our sub make($x) { Leaf($x) }
    }
    ok(Pkg::make(7).v == 7, 'a coercion call resolves a package-relative class name');
}

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
