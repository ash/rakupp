# For t/regression/lexical-type-alias.raku: the import names the class `Bar`
# in the importing scope only (the shape of roast's packages/RT125715).
class RakuppAliasTarget { }
sub EXPORT(|) { { 'Bar' => RakuppAliasTarget } }
