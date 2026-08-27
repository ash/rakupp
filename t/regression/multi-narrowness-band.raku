# Regression: the multi-dispatch narrowness BAND is max(), not a sum.
#
# The post-3.7.0 bonus for an unfilled subset-typed optional positional was
# added per PARAM and on top of everything else, so `(UInt $size = 1, :$chars)`
# outranked the named-only `(:$chars)` candidate DECLARED FIRST, and
# Data::Generators' random-string() answered a one-element List where its Str
# candidate should have won. Rakudo's sort (probed 2026-08-26 against Rakudo
# 2026.08, every pair in BOTH declaration orders) puts these in one band:
#   - an unfilled optional positional typed by a SUBSET narrows a candidate
#     (`(UInt $v?)` beats `(Str $v?)` and bare `()`); a PLAIN-typed optional
#     narrows nothing (`()` beats `(Int $v?)`);
#   - declaring any plain NAMED parameter narrows the same single band
#     (`(Int $x, :$opt)` beats `(Int $x)` even unsupplied) — named SLURPIES
#     earn nothing (Rakudo calls that pair Ambiguous; rakupp keeps
#     declaration order, its usual softer stance on ties);
#   - two candidates in the band TIE, and declaration order decides
#     (`(:$x)` vs `(UInt $n?)` goes to whichever is written first);
#   - a SUB-SIGNATURE positional suppresses the subset half: `((Int $a))` and
#     `((Int $a), UInt $s = 1)` tie by declaration order — the shape of
#     Data::Generators' random-date-time((DT, DT)).
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want;
}

# the Data::Generators random-string shape: named-only Str candidate first
multi rs(:$chars = Whatever --> Str) { 'one' }
multi rs(UInt $size = 1, :$chars = Whatever --> List) { ('many',) }
check rs().^name, 'Str', 'named-only candidate declared first beats subset-optional sibling';

# subset optional vs bare (): the subset band, both orders
multi b1(UInt $v?) { 'uint' }
multi b1() { 'zero' }
check b1(), 'uint', '(UInt $v?) beats () declared second';
multi b2() { 'zero' }
multi b2(UInt $v?) { 'uint' }
check b2(), 'uint', '(UInt $v?) beats () declared first';

# a PLAIN-typed optional earns no band
multi b3() { 'zero' }
multi b3(Int $v?) { 'int' }
check b3(), 'zero', '() holds against a plain-typed optional';

# subset optional vs class optional, both orders (Date::Event's .etype pair)
multi b4(Str $v?) { 'str' }
multi b4(UInt $v?) { 'uint' }
check b4(), 'uint', '(UInt $v?) beats (Str $v?) declared first';
multi b5(UInt $v?) { 'uint' }
multi b5(Str $v?) { 'str' }
check b5(), 'uint', '(UInt $v?) beats (Str $v?) declared second';

# declaring a plain named param is the same band, both orders
multi n1(Int $x) { 'plain' }
multi n1(Int $x, :$opt) { 'opt' }
check n1(5), 'opt', 'an unsupplied plain named narrows, declared second';
multi n2(Int $x, :$opt) { 'opt' }
multi n2(Int $x) { 'plain' }
check n2(5), 'opt', 'an unsupplied plain named narrows, declared first';
multi n3() { 'empty' }
multi n3(:$x) { 'named' }
check n3(), 'named', '(:$x) beats ()';

# a named SLURPY earns nothing: the tie keeps declaration order
multi s1(Int $x) { 'plain' }
multi s1(Int $x, *%r) { 'slurpy' }
check s1(5), 'plain', '*%r earns no band (declaration order holds)';

# in the band, two candidates TIE and declaration order decides
multi t1(:$x) { 'named' }
multi t1(UInt $n?) { 'uint' }
check t1(), 'named', 'named-band vs subset-band: first declared wins';
multi t2(UInt $n?) { 'uint' }
multi t2(:$x) { 'named' }
check t2(), 'uint', 'named-band vs subset-band: first declared wins (swapped)';

# a SUB-SIGNATURE suppresses the subset half: declaration order, both orders
multi p1((Int $a)) { 'short' }
multi p1((Int $a), UInt $s = 1) { 'long' }
check p1((1,)), 'short', 'sub-sig: shorter candidate declared first wins';
multi p2((Int $a), UInt $s = 1) { 'long' }
multi p2((Int $a)) { 'short' }
check p2((1,)), 'long', 'sub-sig: longer candidate declared first wins';

# …and the random-date-time shape end to end: destructuring candidate first
multi rdt((Str $min, Str $max)) { "$min..$max" }
multi rdt((Str $min = 'a', Str $max = 'z'), UInt $size = 1) { 'sized' }
check rdt(('b', 'y')), 'b..y', 'destructuring candidate beats its sized sibling';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
