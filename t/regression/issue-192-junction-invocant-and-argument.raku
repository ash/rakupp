# Issue #192: `so @paths.all.starts-with('/etc/' | '/var/lib/')` was False
# where every path starts with one of the two prefixes, and Rakudo says True.
# With a junction on both sides of a method call, methodCall threaded the
# ARGUMENT first, so the answer was an any of alls — any(all(True, False),
# all(False, True)), False — where Rakudo's is an all of anys. Rakudo's
# dispatcher threads a method a Junction does not have over the INVOCANT
# first, whatever the two junctions' kinds, and the arguments thread inside
# each call. Among the arguments themselves an all/none junction threads
# before an any/one, whatever its position, as it already did for a sub;
# methodCall took the first junction it met.
#
# Every answer below is Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

# the issue's program
my @paths = '/etc/pay/pay.conf', '/var/lib/pay/pay.conf';
check "the issue's program", so(@paths.all.starts-with('/etc/' | '/var/lib/')), True;
check "…and its map form", so(@paths.map({ .starts-with('/etc/' | '/var/lib/') }).all), True;
check 'a path with neither prefix', so(('/etc/a', '/usr/b').all.starts-with('/etc/' | '/var/lib/')), False;
check 'the junction it collapses', @paths.all.starts-with('/etc/' | '/var/lib/').gist,
      'all(any(True, False), any(False, True))';

# the invocant threads first, whatever the kinds
check 'an any invocant, an all argument', ('ab' | 'cd').contains('a' & 'b').gist,
      'any(all(True, True), all(False, False))';
check 'a one invocant', ('ab' ^ 'cd').contains('a' | 'c').gist, 'one(any(True, False), any(False, True))';
check 'a none invocant', none('ab', 'cd').contains('a' | 'c').gist, 'none(any(True, False), any(False, True))';
check '.fmt', (1 | 2).fmt('%d' & '%x').gist, 'any(all(1, 1), all(2, 2))';
check '.index', ('a' & 'b').index('a' | 'b').gist, 'all(any(0, Nil), any(Nil, 0))';
check '.round', (1 | 2).round(1 & 0.5).gist, 'any(all(1, 1), all(2, 2))';
check '.base', (1 & 2).base(2 | 16).gist, 'all(any(1, 1), any(10, 2))';
check '.substr-eq', ('ab' | 'cd').substr-eq('b' & 'd', 1).gist, 'any(all(True, False), all(False, True))';
check '.?contains', ('ab' | 'cd').?contains('a' & 'b').gist, 'any(all(True, True), all(False, False))';
{
    my class C { has $.n; method m($x) { $!n ~ $x } }
    check 'a user method', (C.new(:n<a>) | C.new(:n<b>)).m(1 & 2).gist, 'any(all(a1, a2), all(b1, b2))';
    # a parameter that takes the junction whole still does, in each thread
    my class D { method m(Mu $x) { $x.^name } }
    check 'a Mu parameter', (D.new & D.new).m(1 | 2).gist, 'all(Junction, Junction)';
}
# a WhateverCode calls methodCall directly (issue #22's path)
my &starts = *.starts-with('/etc/' | '/var/lib/');
check 'a curried call', starts(@paths.all).gist, 'all(any(True, False), any(False, True))';
check '…which is True', so(starts(@paths.all)), True;

# among the arguments, all/none before any/one
check 'any, then all', 'abc'.substr(0 | 1, 1 & 2).gist, 'all(any(a, b), any(ab, bc))';
check 'all, then any', 'abc'.substr(0 & 1, 1 | 2).gist, 'all(any(a, ab), any(b, bc))';
check 'any, then any', 'abc'.substr(0 | 1, 1 | 2).gist, 'any(any(a, ab), any(b, bc))';
check 'one, then none', 'abc'.substr(0 ^ 1, none(1, 2)).gist, 'none(one(a, b), one(ab, bc))';
check 'all three', ('abc' & 'xyz').substr(0 | 1, 1 & 2).gist,
      'all(all(any(a, b), any(ab, bc)), all(any(x, y), any(xy, yz)))';
check 'a junction invocant, two junction arguments', ('ab', 'cd').any.contains('a' | 'c', 0 & 1).gist,
      'any(all(any(True, False), any(False, False)), all(any(False, True), any(False, False)))';

# what threaded one junction already is unchanged
check 'a plain argument', @paths.all.starts-with('/etc/').gist, 'all(True, False)';
check 'a plain invocant', '/etc/x'.starts-with('/etc/' | '/var/lib/').gist, 'any(True, False)';
check 'a matcher argument', (1 | 2).grep(any(1, 3)).gist, 'any((1), ())';
check 'a hyper call', <ab cd>».contains('a' | 'c').gist, '(any(True, False) any(False, True))';
check "a Junction's own method", (3 & 4).Str.gist, 'all(3, 4)';

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
