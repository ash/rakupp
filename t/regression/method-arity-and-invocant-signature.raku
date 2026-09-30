# Regression: a method with NO parameters takes no positional argument —
# `A.m(1)` dies "Too many positionals" as Rakudo's does, where it used to run
# and ignore the argument — and every method's positional-count refusal counts
# the invocant (`expected 2 arguments but got 3`) and states a range the way
# Rakudo does (`2 or 3`, `1 to 3`). A method's signature leads with its
# invocant: the class, its smiley, its name (`(A:D $me:: *%_)`), `Mu` for an
# anonymous method.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub refusal(&b) { try { b(); return 'lived' }; $!.message }

class A {
    method m { 7 }
    method e() { 42 }
    method one($x) { $x }
    method o2($x, $y?) { 1 }
    method o3($x?, $y?) { 1 }
    method nm(:$k) { $k // 0 }
    method inv(A $s: $y) { $y }
    method d(A:D: ) { 1 }
    method c(::?CLASS:D $me: ) { 1 }
}
module M { class B { method n { } } }

ck(refusal({ A.m(1) }), 'Too many positionals passed; expected 1 argument but got 2', 'a signature-less method takes no positional');
ck(refusal({ A.e(1, 2) }), 'Too many positionals passed; expected 1 argument but got 3', '…nor does an empty one');
ck(A.m(:z), 7, 'a named argument still goes to its *%_');
ck(refusal({ A.one() }), 'Too few positionals passed; expected 2 arguments but got 1', 'the invocant is counted');
ck(refusal({ A.o2() }), 'Too few positionals passed; expected 2 or 3 arguments but got 1', 'a range of two');
ck(refusal({ A.o3(1, 2, 3) }), 'Too many positionals passed; expected 1 to 3 arguments but got 4', 'a wider range');
ck(refusal({ A.nm(1) }), 'Too many positionals passed; expected 1 argument but got 2', 'only nameds declared');

ck(A.^find_method('m').signature.gist, '(A $:: *%_)', 'the invocant leads the signature');
ck(A.^find_method('one').signature.raku, ':(A $:: $x, *%_)', '…in .raku as well');
ck(A.^find_method('inv').signature.gist, '(A $s:: $y, *%_)', 'a named invocant');
ck(A.^find_method('d').signature.gist, '(A:D $:: *%_)', 'its smiley');
ck(A.^find_method('c').signature.gist, '(A:D $me:: *%_)', '::?CLASS is the class');
ck(M::B.^find_method('n').signature.gist, '(M::B $:: *%_)', 'the long class name');
ck((method ($x) { }).signature.gist, '(Mu $:: $x, *%_)', 'an anonymous method is Mu');
ck(A.^find_method('one').signature.count, 2, 'the count is unchanged');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
