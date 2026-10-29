# For t/regression/imported-operator-spellings.raku: operators spelled so the
# built-in lexing rules would split them, exported for the importer to lex
# whole — and a `sub tr`, so the re-lex also has a quote form to veto.
unit module RakuppImportedOps;

sub infix:<(\<&)>($a, $b) is export { "L($a,$b)" }
sub infix:<(+&)>($a, $b) is export { "P($a,$b)" }
sub infix:<⨁>($a, $b) is export { "O($a,$b)" }
sub infix:<⨁⨁>($a, $b) is export { "OO($a,$b)" }
sub infix:<+++>($a, $b) is export { "T($a,$b)" }
sub prefix:<√√>($a) is export { "R($a)" }
sub postfix:<‼‼>($a) is export { "F($a)" }
sub circumfix:<[[ ]]>($a) is export { "C($a)" }
sub term:<ℵ₀ℵ₀>() is export { 'aleph' }
sub tr($x) is export { "tr($x)" }
