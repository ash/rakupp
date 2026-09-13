# A slang that replaces a production rakupp has no seam for. Activating it must
# fail by NAME — never silently parse the unit with the stock grammar.
my role Refused::Grammar {
    rule statement-control:sym<unless> { 'unless' <EXPR> <pointy-block> }
}
my role Refused::Legacy {
    rule statement_control:sym<unless> { <sym><.kok> <xblock(2)> }
}
sub EXPORT() {
    my $LANG := $*LANG;
    $LANG.define_slang('MAIN',
      $LANG.slang_grammar('MAIN').^mixin($LANG.^name.starts-with('Raku::') ?? Refused::Grammar !! Refused::Legacy),
      $LANG.slang_actions('MAIN'));
    Map.new
}
