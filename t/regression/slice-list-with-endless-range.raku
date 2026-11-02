# Regression: an endless Range inside a slice list is a sub-slice that stops at
# the end of the array.
#
# `@a[1, 4..6, 8..Inf]` is Rakudo's ("2", ("5", "6", "7"), ("9", "10")): each
# member of the index list slices on its own, and an endless one stops where
# the array does. The nesting path took a Range only when both ends were
# integer literals or variables, so `8..Inf` and `8..*` went the flattening
# way, which listed the endless range's 10,000-element prefix and read an
# (Any) for every index past the end. Text::CSV's t/78_fragment.t compares
# `~@exp[1,4..6,8..Inf]` and got "2 5 6 7 9 10" and ten thousand blanks.
# An endless sub-slice wholly past the end is dropped, as Rakudo drops it.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my @a = (1 .. 10).map(~*);
my Str @t = (1 .. 10).map(~*);

for (@a, 'untyped'), (@t, 'typed') -> (@x, $kind) {
    check "$kind: 8..Inf",  @x[1, 4..6, 8..Inf], ("2", ("5", "6", "7"), ("9", "10"));
    check "$kind: 8..*",    @x[1, 4..6, 8..*],   ("2", ("5", "6", "7"), ("9", "10"));
    check "$kind: 8..∞",    @x[1, 8..∞],         ("2", ("9", "10"));
    check "$kind: stringified", ~@x[1, 4..6, 8..Inf], '2 5 6 7 9 10';
    check "$kind: elems",   @x[1, 4..6, 8..Inf].elems, 3;
    check "$kind: past the end", @x[1, 12..Inf],    ("2",);
    check "$kind: past the end, then more", @x[1, 12..Inf, 3], ("2", "4");
    check "$kind: past the end, first", @x[12..Inf, 1], ("2",);
    check "$kind: a finite one is unchanged", @x[1, 2..3], ("2", ("3", "4"));
    check "$kind: on its own", @x[8..Inf], ("9", "10");
}
# a FINITE one past the end reads the holes (Rakudo: a typed array's read its
# type, Str — this nesting path reads Any there, finite ranges included; not
# what this file is about)
check "untyped: a finite one past the end", @a[1, 9..11], ("2", ("10", Any, Any));

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
