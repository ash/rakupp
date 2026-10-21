# Regression: a core enum member, a qualified enum member and a type name
# are TERMS before an infix (2026-10-21). A bare capitalized name the parser
# could not place was read as a listop, so its right side became the call's
# argument: `Less R+ More` was `Less(R + More)` ("Undeclared name 'R'"),
# `Order::Less Z+ More` and `E::a R+ b` the same, `Less < More, More > Same`
# called `Less` with the word list `<More, More>`, and `Int < 2, 3 > 2` was
# `Int(<2, 3>) 2` ("Two terms in a row"). A user enum's bare member and a
# constant already parsed as terms. Rakudo reads a spaced `<` after a type
# as the infix whatever follows; the tight `Int<a>` stays the postcircumfix.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
# the core names go through EVAL, so one that does not parse fails its check
# instead of the file; a user declaration is not visible to rakupp's EVAL
# parser, so those are written out
sub parses(Str $code) { my $r = try EVAL $code; $! ?? $!.^name !! $r }

enum E <a b>;
ck(parses('Less R+ More'), 0, '`Less R+ More` reverses a `+`');
ck(parses('Order::Less R+ More'), 0, '…as does the qualified `Order::Less`');
ck(parses('(Less Z+ More).List'), (0,), '`Less Z+ More` zips');
ck(parses('(Less X+ More).List'), (0,), '`Less X+ More` crosses');
ck(parses('More Rcmp Less'), Less, '`More Rcmp Less` (it already worked)');
ck(parses('Bool::True R+ Bool::False'), 1, 'a qualified Bool member');
ck(parses('SIGINT R+ 1'), 3, 'a Signal member');
ck(E::a R+ b, 1, 'a qualified user enum member');
ck(a R+ b, 1, 'a bare user enum member (it already worked)');
ck(parses('(Less < More, More > Same)'), (True, True), '`Less < More, More > Same` is two comparisons');

class C { method Numeric { 1 } }
ck((quietly C < 2, 3 > 2), (True, True), '`C < 2, 3 > 2` after a class is two comparisons');
ck(so(parses('Int <a b>') ~~ /^ 'X::'/), True, '`Int <a b>` is an infix missing its right side, no coercion');
ck(parses('Int<a>'), Any, '…while a tight `Int<a>` is the postcircumfix');
ck(parses('Array[Int].^name'), 'Array[Int]', '`Array[Int]` still parses');
ck(parses('my Int %h; %h<a>'), Int, '`%h<a>` still parses');
ck(parses('<a b>.List'), <a b>.List, 'a `<…>` term still parses');

# the term reading leaves the enum value itself alone
ck(Less.raku, 'Order::Less', 'a bare `Less` is Order::Less');
ck((Less, More).List, (Less, More), '`Less, More` is a list of two');
ck((given Less { when Less { 'yes' } }), 'yes', '`when Less` still matches');
ck(Less ?? 1 !! 2, 1, '`Less ?? 1 !! 2`');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
