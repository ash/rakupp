# Regression: the shortcuts a type-dispatching encoder runs through keep their
# answers (issue #217, Mathematica::Serializer's encoders, twice Rakudo's time).
#
# `when Pair` / `$x ~~ Int` against a core type object is answered for a plain
# value from what its kind and the type's name gave the first time, until a
# declaration moves the symbol generation (Interpreter::typeMatchMemo, in
# src/MethodCallEntry.cpp); an Int is two kinds, by sign, for UInt. A name the
# program declares — a subset or a class taking a core type's name — is never
# kept, since its `where` or ACCEPTS asks the value. `--> Str` and the other
# core return types accept a plain value of the type without resolving the name
# on every return (Interpreter::retFastCore). A bare core type name answers
# from its node, and `Buf.new` of machine Ints skips the element walk. Each
# check runs twice: the first answer fills what the second reads.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub kind($x) {
    given $x {
        when Pair       { 'Pair' }
        when Str        { 'Str' }
        when UInt       { 'UInt' }
        when Int        { 'Int' }
        when Rat        { 'Rat' }
        when Numeric    { 'Numeric' }
        when Seq        { 'Seq' }
        when Positional { 'Positional' }
        when Map        { 'Map' }
        default         { 'other' }
    }
}
my @vals = 'a' => 1, 'abc', 7, -7, 2**70, -2**70, 0, 3/4, 1.5e0, (1, 2).map(* + 1), [1, 2], (1, 2),
           $[1], %(a => 1), Map.new, <42>, FatRat.new(1, 3);
for ^2 {
    ck @vals.map(&kind).List,
       <Pair Str UInt Int UInt Int UInt Rat Numeric Seq Positional Positional Positional Map Map Str Numeric>,
       'a type-dispatching given';
    ck (0, 5, -5, 2**70, -2**70).map(* ~~ UInt).List, (True, True, False, True, False), 'UInt asks the sign';
    ck ([1], (1,), $(1, 2), (1, 2).map(* + 1)).map(* ~~ Positional).List, (True, True, True, False),
       'a Seq is no Positional, an itemized list is';
    ck (%(a => 1), $%(a => 1), 'a' => 1, 'x').map(* ~~ Associative).List, (True, True, True, False),
       'Associative';
    ck ('x', 1, 1.5, 1e0).map(* ~~ Cool).List, (True, True, True, True), 'Cool';
    ck (1/2, FatRat.new(1, 2)).map(* ~~ FatRat).List, (False, True), 'a Rat is no FatRat';
    ck <42> ~~ Str, True, 'an allomorph is a Str too';
    ck ('x', 1).grep(Str).List, ('x',), 'grep with a type';
}

# a subset or a class that takes a core type's name answers by the value
subset Method of Str where /^ <[A..Z]>+ $/;
class Duration { method ACCEPTS($x) { $x eq 'long' } }
for ^2 {
    ck ('GET', 'get').map({ $_ ~~ Method }).List, (True, False), 'a subset named Method';
    ck ('long', 'short').map({ $_ ~~ Duration }).List, (True, False), 'a class named Duration with an ACCEPTS';
}

# core return types: a plain value passes, everything else takes the full check
sub r-str(--> Str) { 'x' }
sub r-str-bad(--> Str) { 42 }
sub r-int-bad(--> Int) { 'x' }
sub r-ints(--> Int) { <42> }
sub r-nil(--> Int) { Nil }
sub r-strd(--> Str:D) { Str }
sub r-array-list(--> Array) { (1, 2) }
sub r-array-native(--> Array) { my int @a = 1, 2; @a }
sub r-list-array(--> List) { [1, 2] }
sub r-list-seq(--> List) { (1, 2).Seq }
sub r-blob(--> Blob) { Buf.new(1) }
sub r-rat-num(--> Rat) { 1.5e0 }
sub r-hash(--> Hash) { %(a => 1) }
module M { class Str { }; our sub plain(--> Str) { 'plain' } }
sub outcome(&f) { my $r = try f(); $! ?? $!.^name !! $r.^name }
for ^2 {
    ck (&r-str, &r-str-bad, &r-int-bad, &r-ints, &r-nil, &r-strd, &r-array-list, &r-array-native,
        &r-list-array, &r-list-seq, &r-blob, &r-rat-num, &r-hash, &M::plain).map(&outcome).List,
       <Str X::TypeCheck::Return X::TypeCheck::Return IntStr Any X::TypeCheck::Return X::TypeCheck::Return
        X::TypeCheck::Return Array X::TypeCheck::Return Buf X::TypeCheck::Return Hash X::TypeCheck::Return>,
       'core return types';
}

# a bare core type name, and the forms of Buf.new
for ^2 {
    my @t = Pair, Buf, Int:D, Array[Int];
    ck @t.map(*.^name).List, ('Pair', 'Buf', 'Int:D', 'Array[Int]'), 'type names as terms';
    ck Buf.new(1, 2, 300).list, (1, 2, 44), 'Buf.new keeps the low byte';
    ck Buf.new(Buf.new(1, 2)).list, (1, 2), 'Buf.new of a Buf copies it';
    ck Buf.new(^3, [4, 5]).list, (0, 1, 2, 4, 5), 'Buf.new flattens ranges and arrays';
    ck Blob.new(7).^name, 'Blob', 'Blob.new is a Blob';
    my ($a, $b) = Buf.new(1), Buf.new(1);
    ck ($a === $b, $a eqv $b), (False, True), 'two Bufs are two objects';
    ck Buf.allocate(3, 9).list, (9, 9, 9), 'Buf.allocate fills';
}

say 'PASS';
