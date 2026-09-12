#!/usr/bin/env raku
# Emit the static RakuAST:: class table that src/RakuAstClasses.cpp includes.
#
# RUNS UNDER RAKUDO, not rakupp: it MEASURES Rakudo's own class hierarchy. The
# table is checked in (src/rakuast-classes.inc) so a rakupp build needs no Raku
# on the machine; this tool regenerates it when the oracle version moves.
#
#     raku tools/rakuast-class-table.raku > src/rakuast-classes.inc
#
# What goes in, and why the seeds are what they are:
#
#   * the classes covering 95% of the nodes in raku-corpus, read from
#     docs/dev/findings/rakuast/oracle-2026.08-classes.tsv (the measured head —
#     see docs/dev/findings/rakuast/README.md), plus
#   * the classes the blocked ecosystem dists construct by name
#     (RAKUAST-PLAN Part III, *What each blocked dist needs*), plus
#   * the ANCESTOR CLOSURE of both, because `~~ RakuAST::Expression` and
#     `.^mro` are what a walker writes and they have to answer.
#
# Each row is a name and Rakudo's own linearization for it (`.^parents`,
# which for these classes already carries the flattened roles — `.^roles` is
# empty on every one of them). rakupp reproduces the list rather than deriving
# it: the chains are C3 over roles and a subclass's tail is NOT its parent's
# chain, so nothing short of the measured list is right.
#
# No ATTRIBUTES here on purpose. Rakudo's nodes carry compiler state beside
# syntax ($!origin, $!sorries, $!resolution…), and a view must not reproduce
# any of it; the syntax-bearing subset arrives with the renderer that needs it.

use experimental :rakuast;

my $root = $*PROGRAM.parent.parent;
my $histogram = $root.add('docs/dev/findings/rakuast/oracle-2026.08-classes.tsv');

# The 95% head, in node-count order (the file is already sorted that way).
my @head;
my $total = 0;
my @rows = $histogram.lines.skip(1).map({ .split("\t") }).grep(*.elems >= 2);
$total += .[1].Int for @rows;
my $seen = 0;
for @rows -> $r {
    @head.push($r[0]);
    $seen += $r[1].Int;
    last if $seen / $total >= 0.95;
}

# What the blocked dists name directly — the grep of their sources, not a
# summary of them: Needle::Compile 0.0.12, Intl::Format::Number 0.2.0 and
# RakuAST::Utils 0.0.3, REA tarballs, 52 distinct names between them. Plus the
# roots a walker tests against, `QuotedRegex`, which P1 needs as a source slice,
# and everything `src/RakuAstView.cpp` can EMIT — the view is the other half of
# the demand, and a name it builds that the table lacks is a throw on real code.
my @demand = <
    Node Statement Expression Literal Term QuotedRegex
    ApplyInfix ApplyListInfix ApplyPostfix ApplyPrefix ArgList Block Blockoid
    Call::Method Call::Name ColonPair::True CompUnit Infix Initializer::Assign
    Initializer::Bind IntLiteral Name Parameter ParameterTarget::Term
    ParameterTarget::Var Parameter::Slurpy::Capture Parameter::Slurpy::Flattened
    Parameter::Slurpy::SingleArgument Parameter::Slurpy::Unflattened
    PointyBlock Postcircumfix::ArrayIndex Postfix Prefix SemiList Signature
    Statement::Elsif Statement::Expression Statement::For Statement::If
    StatementList StatementModifier::If StatementModifier::Unless StrLiteral
    Sub Term::Name Term::TopicCall Ternary Trait::Is Type::Capture
    Type::Coercion Type::Definedness Type::Parameterized Type::Simple
    Var::Dynamic Var::Lexical Var::Lexical::Constant VarDeclaration::Simple
    Class Module Role Grammar Package Statement::Given Statement::When
    Statement::Default Statement::Loop Statement::Unless Statement::Until
    StatementPrefix::Phaser::Begin StatementPrefix::Phaser::End
    Contextualizer::List Contextualizer::Hash Contextualizer::Item
    ApplyInfix::Chaining Call::Term Call::MaybeMethod Call::PrivateMethod
    Call::MetaMethod Call::Name::WithoutParentheses Circumfix::ArrayComposer
    Circumfix::HashComposer Circumfix::Parentheses ColonPair::Value ColonPair::False
    FatArrow NumLiteral Postcircumfix::HashIndex Postcircumfix::LiteralHashIndex
    QuotedString RatLiteral Statement::Empty Statement::Unless Statement::Until
    Statement::Use Statement::While Term::Self Term::Whatever Type::Setting
    Assignment MetaInfix::Assign Term::Enum
    StatementModifier::For StatementModifier::While StatementModifier::Until
    StatementModifier::Given StatementModifier::With StatementModifier::Without
    Method Submethod Var::Attribute
>;

# `::("RakuAST::A::B")` answers a Failure for a NESTED name (measured on
# 2026.08: WhateverCode::Argument), while the literal name resolves — so walk
# the package stash for anything qualified.
sub lookup($name) {
    my @parts = $name.split('::');
    my $t = ::("RakuAST::" ~ @parts.shift);
    for @parts -> $p { $t = $t.WHO{$p} }
    $t
}

my %chain;                       # name -> its linearization, nearest first
my @queue = (|@head, |@demand);
my @order;                       # discovery order, for a stable table
while @queue.elems {
    my $n = @queue.shift;
    next if %chain{$n}:exists;
    my $t = lookup($n);
    my @p;
    for $t.^parents -> $a { @p.push($a.^name.subst("RakuAST::", "")) }
    %chain{$n} = @p;
    @order.push($n);
    @queue.push($_) for @p;
}

# One row per class: the class, then its ancestors nearest-first, then a null.
# Names carry no `RakuAST::` prefix — the C++ side adds it once.
say "// Generated by tools/rakuast-class-table.raku against Rakudo " ~ $*RAKU.compiler.version ~ ".";
say "// " ~ %chain.elems ~ " classes: the 95% head of raku-corpus, what the blocked dists";
say "// name, and the ancestor closure of both. Do not edit by hand — regenerate.";
for @order.sort -> $n {
    my @row = $n, |%chain{$n}.list;
    say '    ' ~ @row.map({ '"' ~ $_ ~ '"' }).join(', ') ~ ', nullptr,';
}
