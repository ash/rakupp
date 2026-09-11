#!/usr/bin/env raku
# The `.EVAL`-on-a-tree spec — measured, one line per case.
#
# ENGINE-NEUTRAL, like tools/rakuast-deparse-spec.raku: run under Rakudo it
# produces docs/dev/findings/rakuast/eval-2026.08.tsv, run under rakupp it has
# to produce the same file, and t/regression/rakuast-eval.raku is that diff.
#
#     raku tools/rakuast-eval-spec.raku > docs/dev/findings/rakuast/eval-2026.08.tsv
#
# The first four cases are Part I's scope probes, rewritten from `EVAL q[…]` to
# `.EVAL` on a constructed tree. They are the whole argument for the design: a
# deparsed tree is mostly NAMES from wherever it came from, so if the text
# compiled in a fresh empty scope the bridge could only ever carry literals.
# Each answer below is reachable only if the fragment saw the enclosing scope —
# and case 3 is the strong one, because the `say` runs OUTSIDE and still sees
# 99, which means the fragment wrote to the real container rather than a copy.
#
# Case 5 is the other half: a live value the text cannot carry. Both engines
# hand back the SAME object, which for Rakudo is free (it compiles the tree) and
# here is the side table doing its job.

use experimental :rakuast;
# The SUB form needs this on Rakudo (the method form on a node does not), and
# it is what Intl::Format::Number itself writes. Here it stays the accepted
# no-op it has always been.
use MONKEY-SEE-NO-EVAL;

sub lex($n)  { RakuAST::Var::Lexical.new($n) }
sub ilit($v) { RakuAST::IntLiteral.new($v) }      # not `int`: that is a native type name
sub binop($l, $o, $r) {
    RakuAST::ApplyInfix.new(left => $l, infix => RakuAST::Infix.new($o), right => $r)
}
sub d($label, $value) { say $label ~ "\t" ~ $value }

# 1. reads an outer lexical — an empty scope would throw "undeclared", not 42
my $x = 41;
d 'outer-lexical', binop(lex('$x'), '+', ilit(1)).EVAL;

# 2. resolution reaches ROUTINES too, not just variables — a deparsed tree is
#    mostly calls, so this is the case that carries the weight
sub f($n) { $n * 2 }
d 'outer-routine', RakuAST::Call::Name.new(
    name => RakuAST::Name.from-identifier('f'),
    args => RakuAST::ArgList.new(ilit(21))).EVAL;

# 3. the strong one: the `say` runs OUTSIDE the EVAL and still sees 99, so the
#    fragment wrote to the real container — the scope is shared, not a snapshot
my $y = 1;
binop(lex('$y'), '=', ilit(99)).EVAL;
d 'writes-real-container', $y;

# 4. `$p` is a parameter, a lexical in g's call frame: 7 × 3 proves the capture
#    works at depth, which is where `.EVAL` gets called from in real code
sub g($p) { binop(lex('$p'), '*', ilit(3)).EVAL }
d 'parameter-at-depth', g(7);

# 5. a live object has no source behind it, and comes back as ITSELF
my $obj = class { method greet { 'hi' } }.new;
my $back = RakuAST::Literal.from-value($obj).EVAL;
d 'identity-survives', $back === $obj;
d 'and-still-works',   $back.greet;

# 6. the sub form, which is how Intl::Format::Number runs what it builds
d 'sub-form', EVAL RakuAST::IntLiteral.new(42);

# 7. the whole string needle, built and run — App::Rak's commonest invocation
my &needle = RakuAST::PointyBlock.new(
  signature => RakuAST::Signature.new(parameters =>
    (RakuAST::Parameter.new(target => RakuAST::ParameterTarget::Var.new(name => '$_')),)),
  body => RakuAST::Blockoid.new(RakuAST::StatementList.new(
    RakuAST::Statement::Expression.new(
      expression => binop(lex('$_'), 'eq', RakuAST::StrLiteral.new('foo')))))).EVAL;
d 'needle-matches',    needle('foo');
d 'needle-rejects',    needle('bar');
