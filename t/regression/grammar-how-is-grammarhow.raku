# Regression: a grammar's HOW is a Metamodel::GrammarHOW, and that is a
# Metamodel::ClassHOW, as in Rakudo (found checking issue #214).
#
# Every user type but a role answered the one metaobject class named
# Metamodel::ClassHOW, so `G.HOW ~~ Metamodel::GrammarHOW` was False and
# `G.HOW.^name` said ClassHOW. A module that picks its metaclass handling by
# HOW (Cro::RPC::JSON's HOW-onto-type, `when Metamodel::ClassHOW`) must find
# both. A grammar's metaobject class is GrammarHOW now, with ClassHOW its
# parent.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

grammar G { token TOP { 'a' } }
class C { }

ck G.HOW.^name, 'Perl6::Metamodel::GrammarHOW', 'a grammar has a GrammarHOW';
ck (G.HOW ~~ Metamodel::GrammarHOW), True, '…which smartmatches GrammarHOW';
ck (G.HOW ~~ Metamodel::ClassHOW), True, '…and ClassHOW';
ck (C.HOW ~~ Metamodel::GrammarHOW), False, 'a class has no GrammarHOW';
ck (C.HOW ~~ Metamodel::ClassHOW), True, '…but a ClassHOW';
ck G.parse('a').Str, 'a', 'the grammar still parses';

# a role mixed into it, by a trait, as Cro::RPC::JSON does
role Marked { method marked(Mu \type) { True } }
multi trait_mod:<is>(Method:D \meth, :$marked!) {
    my $how := $*PACKAGE.HOW;
    $how does Marked if $how ~~ Metamodel::GrammarHOW && $how !~~ Marked;
}
grammar H { method m() is marked { 1 } }
ck H.HOW.marked(H), True, 'a role mixed into a grammar\'s HOW';
ck (H.HOW ~~ Metamodel::GrammarHOW), True, '…which stays a GrammarHOW';

say 'PASS';
