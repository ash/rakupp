# The RakuAST:: names need no pragma (rakudo#6134, #6137). Rakudo 2026.08
# refused them under 6.d without `use experimental :rakuast`, and Raku++
# followed it; Rakudo 2026.09 does not refuse, and neither does Raku++ now.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

BEGIN {
    RakuAST::Sub.new(
      name      => RakuAST::Name.from-identifier("nothing"),
      signature => RakuAST::Signature.new(),
      body      => RakuAST::Blockoid.new(RakuAST::StatementList.new())
    ).EVAL
}
check 'BEGIN', 'BEGIN', 'a RakuAST::Sub EVALed in a BEGIN (rakudo#6134)';

my &foo = BEGIN {
  RakuAST::Sub.new(
    name => RakuAST::Name.from-identifier("foo"),
    body => RakuAST::Blockoid.new(
      RakuAST::StatementList.new(
        RakuAST::Statement::Expression.new(
          expression => RakuAST::Call::Name.new(
            name => RakuAST::Name.from-identifier("uc"),
            args => RakuAST::ArgList.new(RakuAST::StrLiteral.new("foo"))
          )
        )
      )
    )
  ).EVAL
}
check foo(), 'FOO', 'a sub built from RakuAST nodes and called (rakudo#6137)';

check RakuAST::IntLiteral.new(42).EVAL, 42, 'an IntLiteral EVALs';
check RakuAST::IntLiteral.^name, 'RakuAST::IntLiteral', 'a RakuAST:: name resolves';
check ::("RakuAST::Node").^name, 'RakuAST::Node', '…and so does the symbolic form';
check RakuAST::IntLiteral.new(7).DEPARSE, '7', 'DEPARSE, too';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
