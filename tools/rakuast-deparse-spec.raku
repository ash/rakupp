#!/usr/bin/env raku
# The DEPARSE spec for the classes rakupp renders — measured, one line per case.
#
# RUNS UNDER RAKUDO: it is the oracle half. Each case constructs a node with
# `.new` exactly as an ecosystem module does, and prints what Rakudo's own
# `.DEPARSE` answers. That output is the specification `src/RakuAstDeparse.cpp`
# implements and `t/regression/rakuast-deparse.raku` asserts, so when the oracle
# moves, this is re-run and the diff is the work.
#
#     raku tools/rakuast-deparse-spec.raku > docs/dev/findings/rakuast/deparse-2026.08.tsv
#
# The vocabulary is the union of what Needle::Compile 0.0.12,
# Intl::Format::Number 0.2.0 and RakuAST::Utils 0.0.3 name — the grep of their
# REA tarballs, 52 distinct classes (RAKUAST-PLAN Part III, *What each blocked
# dist needs*). Newlines are escaped so a case stays one row.

use experimental :rakuast;

sub d($label, &mk) {
    my $r;
    {
        $r = mk().DEPARSE;
        CATCH { default { $r = "ERR " ~ .^name ~ ": " ~ .message.lines[0] } }
    }
    say $label ~ "\t" ~ $r.subst("\n", '\n', :g);
}

# Shared leaves, so a case shows the one class it is about.
my $topic := RakuAST::Var::Lexical.new('$_');
my $str   := RakuAST::StrLiteral.new('foo');
my $int   := RakuAST::IntLiteral.new(42);
my $name  := RakuAST::Name.from-identifier('foo');
sub stmts(*@e) { RakuAST::StatementList.new(|@e.map({ RakuAST::Statement::Expression.new(expression => $_) })) }
sub blockoid(*@e) { RakuAST::Blockoid.new(stmts(|@e)) }
sub topicsig() {
    RakuAST::Signature.new(parameters =>
      (RakuAST::Parameter.new(target => RakuAST::ParameterTarget::Var.new(name => '$_')),))
}

# ---- literals and terms -------------------------------------------------
d 'IntLiteral',             { RakuAST::IntLiteral.new(42) };
d 'StrLiteral',             { RakuAST::StrLiteral.new('foo') };
d 'Literal.from-value',     { RakuAST::Literal.from-value([1,2]) };
d 'Var::Lexical',           { RakuAST::Var::Lexical.new('$x') };
d 'Var::Dynamic',           { RakuAST::Var::Dynamic.new('$*OUT') };
d 'Var::Lexical::Constant', { RakuAST::Var::Lexical::Constant.new('&infix:<+>') };
d 'Name.from-identifier',   { RakuAST::Name.from-identifier('foo') };
d 'Name.from-identifier-parts', { RakuAST::Name.from-identifier-parts('Foo','Bar') };
d 'Term::Name',             { RakuAST::Term::Name.new($name) };
d 'ColonPair::True',        { RakuAST::ColonPair::True.new('x') };

# ---- operators ----------------------------------------------------------
d 'Infix',           { RakuAST::Infix.new('eq') };
d 'Prefix',          { RakuAST::Prefix.new('not') };
d 'ApplyInfix',      { RakuAST::ApplyInfix.new(left => $topic, infix => RakuAST::Infix.new('eq'), right => $str) };
d 'ApplyListInfix',  { RakuAST::ApplyListInfix.new(infix => RakuAST::Infix.new(','), operands => ($int, $str)) };
d 'ApplyPrefix',     { RakuAST::ApplyPrefix.new(prefix => RakuAST::Prefix.new('not'), operand => $topic) };
d 'ApplyPostfix.postfix',   { RakuAST::ApplyPostfix.new(operand => RakuAST::Var::Lexical.new('$x'), postfix => RakuAST::Postfix.new(operator => '++')) };
# …and the topic invocant, which Rakudo ELIDES: `$_.fc` renders `.fc`, the same
# text `Term::TopicCall` gives. Not a loss — `.fc` means `$_.fc` — but it is a
# normalization our renderer has to make too, or the round trip changes shape.
d 'ApplyPostfix.topic',     { RakuAST::ApplyPostfix.new(operand => $topic, postfix => RakuAST::Call::Method.new(name => RakuAST::Name.from-identifier('fc'))) };
d 'ApplyPostfix.invocant',  { RakuAST::ApplyPostfix.new(operand => $str, postfix => RakuAST::Call::Method.new(name => RakuAST::Name.from-identifier('fc'))) };
d 'Ternary',         { RakuAST::Ternary.new(condition => $topic, then => $int, else => $str) };

# ---- calls --------------------------------------------------------------
d 'Call::Name',        { RakuAST::Call::Name.new(name => $name, args => RakuAST::ArgList.new($topic)) };
d 'Call::Name.noargs', { RakuAST::Call::Name.new(name => $name) };
d 'Call::Method',      { RakuAST::Call::Method.new(name => RakuAST::Name.from-identifier('fc')) };
d 'Call::Method.args', { RakuAST::Call::Method.new(name => RakuAST::Name.from-identifier('match'), args => RakuAST::ArgList.new($str)) };
d 'ArgList',           { RakuAST::ArgList.new($topic, $str) };
d 'Term::TopicCall',   { RakuAST::Term::TopicCall.new(RakuAST::Call::Method.new(name => RakuAST::Name.from-identifier('fc'))) };
d 'Postcircumfix::ArrayIndex', { RakuAST::ApplyPostfix.new(operand => RakuAST::Var::Lexical.new('@a'),
      postfix => RakuAST::Postcircumfix::ArrayIndex.new(index => RakuAST::SemiList.new(RakuAST::Statement::Expression.new(expression => RakuAST::IntLiteral.new(0))))) };

# ---- statements and blocks ----------------------------------------------
d 'SemiList',               { RakuAST::SemiList.new(RakuAST::Statement::Expression.new(expression => $int)) };
d 'StatementList',          { stmts($int) };
d 'StatementList.two',      { stmts($int, $str) };
d 'Statement::Expression',  { RakuAST::Statement::Expression.new(expression => $int) };
d 'Blockoid',               { blockoid($int) };
d 'Block',                  { RakuAST::Block.new(body => blockoid($int)) };
d 'PointyBlock',            { RakuAST::PointyBlock.new(signature => topicsig(), body => blockoid($str)) };
d 'Sub',                    { RakuAST::Sub.new(name => $name, body => blockoid($int)) };
d 'Statement::If',          { RakuAST::Statement::If.new(condition => $topic, then => RakuAST::Block.new(body => blockoid($int))) };
d 'Statement::For',         { RakuAST::Statement::For.new(source => $topic,
      body => RakuAST::PointyBlock.new(signature => RakuAST::Signature.new(parameters => ()), body => blockoid($int))) };
d 'StatementModifier::If',     { RakuAST::Statement::Expression.new(expression => $int, condition-modifier => RakuAST::StatementModifier::If.new($topic)) };
d 'StatementModifier::Unless', { RakuAST::Statement::Expression.new(expression => $int, condition-modifier => RakuAST::StatementModifier::Unless.new($topic)) };

# ---- declarations, signatures, types ------------------------------------
d 'VarDeclaration::Simple', { RakuAST::VarDeclaration::Simple.new(sigil => '$', desigilname => RakuAST::Name.from-identifier('x')) };
d 'VarDeclaration.assign',  { RakuAST::VarDeclaration::Simple.new(sigil => '$', desigilname => RakuAST::Name.from-identifier('x'), initializer => RakuAST::Initializer::Assign.new($int)) };
d 'Initializer::Assign',    { RakuAST::Initializer::Assign.new($int) };
d 'Initializer::Bind',      { RakuAST::Initializer::Bind.new($int) };
d 'Signature',              { RakuAST::Signature.new(parameters => (RakuAST::Parameter.new(target => RakuAST::ParameterTarget::Var.new(name => '$a')),)) };
d 'Parameter',              { RakuAST::Parameter.new(target => RakuAST::ParameterTarget::Var.new(name => '$a')) };
d 'ParameterTarget::Var',   { RakuAST::ParameterTarget::Var.new(name => '$a') };
d 'ParameterTarget::Term',  { RakuAST::ParameterTarget::Term.new($name) };
d 'Parameter::Slurpy::Flattened',      { RakuAST::Parameter.new(target => RakuAST::ParameterTarget::Var.new(name => '@a'), slurpy => RakuAST::Parameter::Slurpy::Flattened) };
d 'Parameter::Slurpy::Unflattened',    { RakuAST::Parameter.new(target => RakuAST::ParameterTarget::Var.new(name => '@a'), slurpy => RakuAST::Parameter::Slurpy::Unflattened) };
d 'Parameter::Slurpy::SingleArgument', { RakuAST::Parameter.new(target => RakuAST::ParameterTarget::Var.new(name => '@a'), slurpy => RakuAST::Parameter::Slurpy::SingleArgument) };
d 'Parameter::Slurpy::Capture',        { RakuAST::Parameter.new(target => RakuAST::ParameterTarget::Var.new(name => '$c'), slurpy => RakuAST::Parameter::Slurpy::Capture) };
d 'Type::Simple',        { RakuAST::Type::Simple.new(RakuAST::Name.from-identifier('Int')) };
d 'Type::Definedness',   { RakuAST::Type::Definedness.new(base-type => RakuAST::Type::Simple.new(RakuAST::Name.from-identifier('Int')), definite => True) };
d 'Type::Coercion',      { RakuAST::Type::Coercion.new(base-type => RakuAST::Type::Simple.new(RakuAST::Name.from-identifier('Int')), constraint => RakuAST::Type::Simple.new(RakuAST::Name.from-identifier('Str'))) };
d 'Type::Capture',       { RakuAST::Type::Capture.new($name) };
d 'Type::Parameterized', { RakuAST::Type::Parameterized.new(base-type => RakuAST::Type::Simple.new(RakuAST::Name.from-identifier('Array')), args => RakuAST::ArgList.new(RakuAST::Type::Simple.new(RakuAST::Name.from-identifier('Int')))) };
d 'Trait::Is',           { RakuAST::Trait::Is.new(name => RakuAST::Name.from-identifier('rw')) };

# ---- the whole thing: Needle::Compile's string needle, `rak foo` ---------
# lib/Needle/Compile.rakumod `handle("equal", …)` + `wrap-in-block`, verbatim.
# This is the tree App::Rak's commonest invocation builds, and the first thing
# P2c+P3 has to render and run.
d 'needle.equal', {
    RakuAST::PointyBlock.new(
      signature => topicsig(),
      body => RakuAST::Blockoid.new(
        RakuAST::StatementList.new(
          RakuAST::Statement::Expression.new(
            expression => RakuAST::VarDeclaration::Simple.new(
              sigil => '$', desigilname => RakuAST::Name.from-identifier('/'))),
          RakuAST::Statement::Expression.new(
            expression => RakuAST::ApplyInfix.new(
              left  => RakuAST::Var::Lexical.new('$_'),
              infix => RakuAST::Infix.new('eq'),
              right => RakuAST::StrLiteral.new('foo'))))))
};
