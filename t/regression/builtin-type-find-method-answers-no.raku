# Regression: `.^find_method` on a built-in type object answers no for a
# name the type does not have (issue #77, FCO's item 4).
#
# A built-in type is probed through an empty instance of it, and only Str,
# Int, Num, Bool, Array, Hash and Date had one: every other type answered
# a method object for ANY name. Red asks a column type
# `.^find_method("red-type-db-methods")` and calls what it gets, which died
# on a Set column. Set, Bag, Mix (and their mutable forms), Range, Pair, Seq,
# Rat, Complex, Buf, Blob, Version, Duration, Instant, IO::Path and Uni now
# have one, `.can` shares them, and Cool, Real, Numeric, Stringy and
# Iterable are judged by a type that does them.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @none = <Set SetHash Bag BagHash Mix MixHash Range Pair Seq Rat FatRat Complex
            Buf Blob Version Duration Instant IO::Path Uni Cool Real Numeric Stringy Iterable>;
for @none -> $t {
    ck so(::($t).^find_method('red-compat-nonexistent-method')), False, "$t has no such method";
}
for <Set Bag Range Pair Rat Buf Version Duration Instant Uni> -> $t {
    ck so(::($t).can('red-compat-nonexistent-method')), False, "$t.can says no too";
}

# …while what they have is still found
for <Set:elems Bag:total Mix:roll SetHash:grabpairs Range:min Pair:key Seq:cache
     Rat:numerator Complex:re Buf:bytes Blob:decode Version:parts Duration:tai
     Instant:to-posix Uni:NFC Cool:chars Real:floor Stringy:Stringy Iterable:map> -> $p {
    my ($t, $m) = $p.split(':');
    ck so(::($t).^find_method($m)), True, "$t has $m";
}
for <Set:elems Bag:total Range:min Pair:key Rat:numerator Buf:bytes Version:parts Duration:tai> -> $p {
    my ($t, $m) = $p.split(':');
    ck so(::($t).can($m)), True, "$t can $m";
}
ck so(IO::Path.^find_method('basename')), True, 'IO::Path has basename';
ck Set.^find_method('elems')(set(1, 2)), 2, 'the method object found calls the method';

say 'PASS';
