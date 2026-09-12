# Regression: `'source'.AST` builds the view (RAKUAST-PLAN P1).
#
# Every expectation was measured against Rakudo 2026.08 side by side; where the
# two engines differ the difference is named here rather than hidden.
#
# Contract: exit 0 + last line PASS.
use experimental :rakuast;
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what:\n     got  {$got.raku}\n     want {$want.raku}") unless $got eq $want
}

# ---- what `.AST` answers -----------------------------------------------
check q[say 1].AST.^name, 'RakuAST::StatementList', 'a program is a StatementList';
check q[say 1].AST(:compunit).^name, 'RakuAST::CompUnit', ':compunit wraps it';
# `.AST` is COOL, not Str — measured: both of these answer a StatementList
# upstream, so the arm sits beside the Cool `EVAL` one.
check 42.AST.^name,   'RakuAST::StatementList', '42.AST — Cool, not Str';
check <42>.AST.^name, 'RakuAST::StatementList', 'an allomorph too';

# ---- the round trip, which is the whole point --------------------------
check q[my $x = 41; say $x + 1].AST.DEPARSE,
      "my \$x = 41;\nsay \$x + 1\n", 'parse → view → source, byte for byte';
# A CompUnit terminates EVERY statement; a bare StatementList omits the last.
# Structural, and measured on both engines.
check q[my $x = 1; say 2].AST(:compunit).DEPARSE,
      "my \$x = 1;\nsay 2;\n", 'a CompUnit terminates the last statement';
# A block statement ends in its own newline and takes neither `;` nor a second
# one — getting that wrong put a bare `;` on a line of its own, which is not Raku.
check q[if 1 { 2 }; say 3].AST.DEPARSE,
      "if 1 \{\n    2\n}\nsay 3\n", 'a block statement is not `;`-terminated';
# `multi` is part of the declarator: two candidates that render as two `sub`s of
# one name is a redeclaration error rather than a program.
check q[multi f() { 1 }; multi f($x) { 2 }].AST.DEPARSE,
      "multi sub f \{\n    1\n}\nmulti sub f(\$x) \{\n    2\n}\n", 'multi survives the trip';
# …and a slurpy marker, for the same reason: `(\$p, *@rest)` is a different
# signature from `(\$p, @rest)`.
check q[sub f($p, *@rest) { 1 }].AST.DEPARSE.contains('*@rest'), True,
      'a slurpy parameter keeps its star';
# A reduction metaop needs the space the parser demands of it.
check q[say [+] @a].AST.DEPARSE, "say [+] \@a\n", 'a reduce metaop keeps its space';

# ---- the three StatementList accessors App::Rak drives -----------------
my $cu = q[say 1].AST(:compunit);
check $cu.statement-list.^name, 'RakuAST::StatementList', '.statement-list';
check $cu.statement-list.statements.elems, 1, '.statements';
$cu.statement-list.unshift-statement(
    RakuAST::Statement::Expression.new(expression => RakuAST::IntLiteral.new(42)));
check $cu.DEPARSE, "42;\nsay 1;\n", '.unshift-statement puts one in front';
# …and the regex needle, which is a SOURCE SLICE rather than a regex tree.
check q[/ foo /].AST.statements.head.expression.^name, 'RakuAST::QuotedRegex',
      'a regex literal is a QuotedRegex';

# ---- the gate, and the shapes that refuse ------------------------------
# `.AST` itself is NOT behind the pragma — measured on 2026.08, where
# `Q[say 1].AST` answers a StatementList with no `use experimental` in sight.
# The `RakuAST::` NAMES are what upstream gates, and that is asserted below.
# Gating the method as well was stricter than the thing being matched, and it
# failed every L10N dist's own test, each of which opens with a bare `.AST`.
my $p = run($*EXECUTABLE, '-e', 'say q[say 1].AST.^name', :out, :err);
check ($p.out.slurp(:close) ~ $p.err.slurp(:close)).trim,
      'RakuAST::StatementList', '.AST needs no pragma, as upstream';
$p = run($*EXECUTABLE, '-e', 'say RakuAST::IntLiteral.new(1).DEPARSE', :out, :err);
check ($p.out.slurp(:close) ~ $p.err.slurp(:close)).contains(
          "Use of RakuAST is experimental"), True, '…but a RakuAST:: NAME still is';
# A Buf is not Cool and never reaches the arm.
check (try Buf.new(1,2).AST).defined, False, 'a Buf invocant refuses';
# A construct the builder has no faithful mapping for SAYS so — it never
# answers a wrong tree. The example here has been `class`, then a phaser block,
# then an `enum`, and every one of them is mapped now; this line is the
# FRONTIER, not any particular construct, and it moves each time the frontier
# does. Today it is a substitution — and behind it, the whole `Regex::*`
# subtree, which is what holds the grammar-heavy programs out of the corpus.
check (try q[my $x = "a"; $x ~~ s/a/b/].AST).defined, False,
      'an unmapped construct throws';
check ($! ~~ X::NYI).so, True, '…as X::NYI';
# And a parse error is a Raku exception, not a `===SORRY!===` that takes the
# program with it past every `try` — the round-trip harness found that one.
check (try q[my $x = ].AST).defined, False, 'unparseable source throws';
check ($!.message.contains('could not parse')).so, True, '…catchably';

if @fail {
    note "FAIL: $_" for @fail;
    say "FAIL: $_" for @fail;
    say "FAIL ({+@fail})";
    exit 1;
}
say "PASS";
