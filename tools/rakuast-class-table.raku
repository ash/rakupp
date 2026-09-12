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
    Statement::Default Statement::Loop Statement::Unless
    StatementPrefix::Phaser::Begin StatementPrefix::Phaser::End
    Contextualizer::List Contextualizer::Hash Contextualizer::Item
    ApplyInfix::Chaining Call::Term Call::MaybeMethod Call::PrivateMethod
    Call::MetaMethod Call::Name::WithoutParentheses Circumfix::ArrayComposer
    Circumfix::HashComposer Circumfix::Parentheses ColonPair::Value ColonPair::False
    FatArrow NumLiteral Postcircumfix::HashIndex Postcircumfix::LiteralHashIndex
    QuotedString RatLiteral Statement::Empty Statement::Unless
    Statement::Use Term::Self Term::Whatever Type::Setting
    Assignment MetaInfix::Assign Term::Enum
    StatementModifier::For StatementModifier::While StatementModifier::Until
    StatementModifier::Given StatementModifier::With StatementModifier::Without
    Method Submethod Var::Attribute Type::Enum Type::Subset
    Name::Part::Empty Name::Part::Expression Statement::Import Statement::Need
    Statement::With Statement::Without
    Statement::Loop::RepeatWhile Statement::Loop::RepeatUntil Statement::Whenever
    StatementPrefix::Phaser::Check StatementPrefix::Phaser::Init
    StatementPrefix::Phaser::Enter StatementPrefix::Phaser::Leave
    StatementPrefix::Phaser::First StatementPrefix::Phaser::Next
    StatementPrefix::Phaser::Last StatementPrefix::Phaser::Keep
    StatementPrefix::Phaser::Undo StatementPrefix::Phaser::Pre
    StatementPrefix::Phaser::Post StatementPrefix::Phaser::Close
    StatementPrefix::Phaser::Quit StatementPrefix::Phaser::Catch
    StatementPrefix::Phaser::Control StatementPrefix::Phaser::Doc
>;

# `::("RakuAST::A::B")` answers a Failure for a NESTED name (measured on
# 2026.08: WhateverCode::Argument), while the literal name resolves — so walk
# the package stash for anything qualified.
my @unresolvable;
sub lookup($name) {
    my @parts = $name.split('::');
    my $t = ::("RakuAST::" ~ @parts.shift);
    for @parts -> $p { $t = try $t.WHO{$p} }
    # DEFINEDNESS IS NOT THE TEST. Every one of these is a type object, and a
    # type object is not `.defined` — testing that marked all 491 as missing.
    # A real miss is a Failure out of `::()` or a Nil out of the stash walk.
    # `Nil` has a `.^name` too, so that cannot be the test either. A miss is
    # exactly: a Failure out of `::()`, or Nil out of the stash walk.
    my $ok = !($t ~~ Failure) && !($t =:= Nil);
    @unresolvable.push($name) unless $ok;
    $ok ?? $t !! Nil
}

# …and, since P4, EVERY class the namespace has. The demand list above is kept
# because it documents WHY a name is needed and it is what the seeds are checked
# against — but growing the table one missing name at a time is a losing game:
# ASTQuery alone references most of the namespace by name when it compiles its
# matcher tables, and each miss is an `Undeclared name` at the user's end rather
# than a gap in a walk. The whole set is ~220 rows of text; the registry is
# built once, published once, and consulted only after `classes_` has missed.
sub every-class(Mu $t, $prefix, @out) {
    for $t.WHO.keys.sort -> $k {
        my $c = try $t.WHO{$k};
        # `.defined` is not safe here: some of these are stub type objects whose
        # HOW has no `defined` at all (`DottyInfix`), so the test must be the
        # HOW's NAME and nothing else.
        my $how = (try $c.HOW.^name) // '';
        next unless $how.contains('ClassHOW') || $how.contains('RoleHOW')
                 || $how.contains('ParametricRole');
        @out.push($prefix ~ $k);
        every-class($c, $prefix ~ $k ~ '::', @out);
    }
}
my @all;
every-class(RakuAST, '', @all);

my %chain;                       # name -> its linearization, nearest first
my @queue = (|@head, |@demand, |@all);
my @order;                       # discovery order, for a stable table
while @queue.elems {
    my $n = @queue.shift;
    next if %chain{$n}:exists;
    my $t = lookup($n);
    my @p;
    # A KnowHOW-backed type (`RakuAST::Knowhow`) answers no `.parents` at all —
    # it is not a class in the ordinary sense. It still needs a ROW, because a
    # module may name it; it simply has an empty chain.
    for (try $t.^parents) // () -> $a { @p.push($a.^name.subst("RakuAST::", "")) }
    %chain{$n} = @p;
    @order.push($n);
    @queue.push($_) for @p;
}

# One row per class: the class, then its ancestors nearest-first, then a null.
# Names carry no `RakuAST::` prefix — the C++ side adds it once.
say "// Generated by tools/rakuast-class-table.raku against Rakudo " ~ $*RAKU.compiler.version ~ ".";
say "// " ~ %chain.elems ~ " classes: every class the RakuAST namespace has,";
say "// plus the ancestor closure. Do not edit by hand — regenerate.";
# NAMES THAT DO NOT RESOLVE UPSTREAM get a row with an EMPTY ancestry, and that
# used to happen silently — which is how `Statement::While` sat in the table for
# a month as a class rakupp invented (a plain `while` is `Statement::Loop::While`
# upstream) with no path to `RakuAST::Node` and therefore none of its methods.
# Two different things land here and only one of them is a bug:
#   * a class Rakudo HAS but does not expose by name — `Statement::Elsif` and
#     `Statement::Orwith` are real nodes in a real tree, just not fetchable as
#     symbols. Those belong in the table;
#   * a name nothing upstream answers to at all. That is ours, and wrong.
# Neither is decidable from here, so the list is printed rather than judged.
if @unresolvable {
    note "NOT NAME-RESOLVABLE UPSTREAM ({+@unresolvable}): " ~ @unresolvable.sort.join(', ');
    say "// Not name-resolvable upstream, so their ancestry is empty — some are";
    say "// real-but-unexposed classes (Statement::Elsif), some would be ours to";
    say "// fix: " ~ @unresolvable.sort.join(', ');
}
for @order.sort -> $n {
    my @row = $n, |%chain{$n}.list;
    say '    ' ~ @row.map({ '"' ~ $_ ~ '"' }).join(', ') ~ ', nullptr,';
}
