# For t/regression/imported-operator-traits.raku: operators whose precedence
# and associativity traits the importing file has to honour, in the shapes
# FunctionalParsers declares its combinators with.
unit module RakuppImportedOpTraits;

sub infix:<⊗⊗>(*@a) is equiv(&infix:<&&>) is assoc<right> is export { "A(" ~ @a.join(",") ~ ")" }
sub infix:<⊕⊕>(*@a) is equiv( &infix:<||> ) is assoc<list> is export { "O(" ~ @a.join(",") ~ ")" }
sub infix:<«&»>(*@a) is equiv(&infix:<&&>) is assoc<right> is export { "D(" ~ @a.join(",") ~ ")" }
sub infix:<«|»>(*@a) is equiv(&infix:<||>) is assoc<list> is export { "E(" ~ @a.join(",") ~ ")" }
sub infix:<(&)>(*@a) is equiv(&infix:<&&>) is assoc<right> is export { "S(" ~ @a.join(",") ~ ")" }
sub infix:<(|)>(*@a) is equiv(&infix:<||>) is assoc<list> is export { "U(" ~ @a.join(",") ~ ")" }
sub infix:<⊲>($a, $b) is equiv( &[**] ) is export { $a - $b }
sub infix:<⊳>($a, $b) is tighter(&infix:<+>) is export { $a * 10 + $b }
sub infix:<⊴>($a, $b) is looser(&infix:<+>) is export { "W($a,$b)" }
sub infix:<⋄>($a, $b) is tighter<*> is export { $a * 10 + $b }
sub infix:<⊶>($a, $b) is assoc<chain> is export { $a < $b }
