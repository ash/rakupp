# Regression: the second round of issue #217's shortcuts keep what the long
# way answers — Mathematica::Serializer's encoders went from 1.3 times
# Rakudo's time to under it.
#
# A plain method call (no `.^`/`».`/`.?`/`.=`, no computed or qualified name,
# none of the names evalMethodCallExpr's arms test) skips that function's
# frame (fatalCheckedMethodCall's lean path); an invocant one of its arms is
# written for — a Seq, a junction, a Whatever, a Buf — is handed to it
# already evaluated, and a lazy chain, an active CATCH or a kept list's
# containers still take or reach their arms. A `:D` or `is copy` parameter
# binds on bindParams' simple path. methodCall answers `Buf.new` of machine
# Ints, `.Str` of an Int, a Pair's `.key`/`.value` and a `.subst` whose
# pattern does not occur before the chain (MethodCallEntry.cpp). A bare core
# type name past evalRareExpr's list (`WhateverCode`) answers from its node
# while no scope has bound that name. Each check runs twice: the first call
# settles what the second reuses.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class P {
    has $.x = 3;
    has @.list = 1, 2;
    method !hidden($n) { $n * 2 }
    method twice($n) { self!hidden($n) }
    method boom { die 'boom' }
    method seq { (1, 2, 3).map(* * 2) }
}
my $o = P.new;
for ^2 {
    ck $o.x, 3, 'an accessor';
    ck $o.twice(4), 8, 'a private call from a method';
    ck (try EVAL '$o!P::hidden(1)') // $!.^name, 'X::Method::Private::Permission', 'a private call from outside';
    ck $o.seq.elems, 3, 'a Seq answer';
    my $s = $o.seq;
    ck ($s.elems, $s.sum), (3, 12), 'a Seq invocant, asked twice';
    ck ('a' | 'b').uc.WHAT.^name, 'Junction', 'a junction invocant autothreads';
    ck (*.uc).('x'), 'X', 'a Whatever invocant curries';
    ck (* + 1).WHAT.^name, 'WhateverCode', '…and so does a composed one';
    ck Buf.new(1, 2).elems, 2, 'a Buf invocant';
    ck Buf.new(104, 105).decode, 'hi', '…decoded';
    my $caught;
    { CATCH { default { $caught = .message } }; $o.boom }
    ck $caught, 'boom', 'a method dying under a CATCH';
    ck (1 .. *).map(* + 1).grep(* %% 2).first, 2, 'a lazy chain';
    my @a = 1 .. 3;
    my \h = @a.head(2);
    @a[0] = 42;
    ck h.List, (42, 2), 'a kept .head(n) holds the containers';
    ck 'abc'.uc.lc.chars, 3, 'a chain of plain calls';
}

# `:D`, `:U` and `is copy` bind as the general binder binds them
sub f($y is copy) { $y = $y ~ '!'; $y }
sub g($x) { f($x) }
sub h(Int:D $v is copy) { $v = $v +> 1; $v }
sub k(Str:D $s, Int:U $t) { "$s {$t.^name}" }
sub n($x is copy) { $x.push(3); $x.elems }
for ^2 {
    ck (g('a'), f(5), h(10)), ('a!', '5!', 5), 'is copy';
    ck (try h(Int)) // $!.^name, 'X::Parameter::InvalidConcreteness', 'Int:D refuses a type object';
    ck k('a', Int), 'a Int', 'Str:D and Int:U';
    ck (try k('a', 5)) // $!.^name, 'X::Parameter::InvalidConcreteness', 'Int:U refuses an instance';
    my @b = 1, 2;
    ck (n(@b), @b.elems), (3, 3), 'is copy of an Array is the same Array';
    my $c = 5;
    sub q($x is copy) { $x++; $x }
    ck (q($c), $c), (6, 5), 'is copy leaves the argument alone';
}

# the plain-value answers before the chain
for ^2 {
    ck (0, -7, -2**63, 2**62).map(*.Str).List, ('0', '-7', '-9223372036854775808', '4611686018427387904'), 'Int.Str';
    ck (Less.Str, <42>.Str, True.Str), ('Less', '42', 'True'), 'an enum, an allomorph, a Bool';
    my %oh{Any} = (1) => 'a';
    ck %oh.pairs.map({ .key.^name }).List, ('Int',), 'an object hash pair keeps its key';
    ck (('a' => 1).key, ('a' => 1).value, (a => Nil).value), ('a', 1, Nil), 'Pair .key / .value';
    'xyz' ~~ /y/;
    ck 'abc'.subst(:g, '"', '\\"'), 'abc', 'a subst that finds nothing';
    ck ~$/, 'y', '…leaves $/ alone';
    ck 'a"b"c'.subst(:g, '"', '\\"'), 'a\\"b\\"c', 'a subst that finds';
    ck "a\r\nb".subst("\n", 'N'), "a\r\nb", 'a LF inside a CR LF is no match';
    ck 'abc'.subst(:x(2), 'q', 'z'), 'abc', 'another adverb';
    ck <42>.subst('q', 'z').^name, 'Str', 'an allomorph substitutes to a Str';
    ck (Buf.new(1, 2, 300).list, Buf.allocate(2, 7).list, Blob.new(65).^name), ((1, 2, 44), (7, 7), 'Blob'),
       'Buf.new and .allocate';
}

# a core type name past evalRareExpr's list answers from its node
# (NameTerm::typeMemo) while no scope binds the name, and a lexical still wins
sub wc() { WhateverCode }
for ^2 {
    ck wc().^name, 'WhateverCode', 'a bare core type name';
    my $plus = * + 1;
    ck (5.isa(WhateverCode), 5.isa(HyperWhatever), $plus.isa(WhateverCode)), (False, False, True),
       '…as an .isa argument';
    ck (Instant, Duration, FatRat, UInt).map(*.^name).List, <Instant Duration FatRat UInt>, '…and others';
}
{
    my \WhateverCode = 42;
    ck WhateverCode, 42, 'a lexical of the name wins';
    ck wc().^name, 'WhateverCode', '…only where it is in scope';
}
ck EVAL('my \Duration = "mine"; Duration'), 'mine', 'a lexical of the name in an EVAL';

say 'PASS';
