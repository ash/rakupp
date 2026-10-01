# Regression: RakuAST::Name::Part::EmptyEdge is accepted beside ::Empty.
#
# rakudo/rakudo#6771 renames the empty name part (the nothing before a leading
# `::`, or after a trailing one) to EmptyEdge. Until the oracle ships it, a tree
# built by code written for either Rakudo has to deparse and EVAL the same,
# while `.AST` keeps emitting the name Rakudo 2026.09 emits: ::Empty.
#
# Contract: exit 0 + last line PASS.
use experimental :rakuast;
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what:\n     got  {$got.raku}\n     want {$want.raku}") unless $got eqv $want
}

for <Empty EmptyEdge> -> $k {
    my \E = ::("RakuAST::Name::Part::$k");
    my $sym = RakuAST::Term::Name.new(RakuAST::Name.new(
        E.new,
        RakuAST::Name::Part::Expression.new(RakuAST::StrLiteral.new("Int")),
    ));
    check $sym.DEPARSE, '::("Int")', "$k: leading part of a symbolic name";
    check $sym.EVAL.^name, 'Int', "$k: the symbolic name EVALs";
    check RakuAST::Name.new(RakuAST::Name::Part::Simple.new("Foo"), E.new).DEPARSE,
        'Foo::', "$k: trailing part";
    check E.new ~~ RakuAST::Name::Part, True, "$k: is a Name::Part";
    check E.^mro.map(*.^name).List,
        ("RakuAST::Name::Part::$k", 'RakuAST::Name::Part', 'Any', 'Mu'), "$k: .^mro";
    CATCH { default { @fail.push("$k: died: {.message}") } }
}
check Q{::("Int")}.AST.statements[0].expression.name.parts[0].^name,
    'RakuAST::Name::Part::Empty', '.AST emits the name the oracle emits';

if @fail {
    say "FAIL: $_" for @fail;
    say "FAIL ({+@fail})";
    exit 1;
}
say "PASS";
