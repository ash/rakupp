# For t/regression/module-trait-exported-from-begin.raku: the shape of
# Cro::RPC::JSON — `is export` trait multis declared inside a BEGIN block,
# which mix the dist's own Metamodel::ClassHOW role into a class's HOW and
# tell the setting's Metamodel::ClassHOW from it by the short name.
unit module RakuppBeginTraits;
use RakuppBeginTraits::Metamodel::ClassHOW;

sub onto(Mu \pkg, Str:D $name) {
    my $how := pkg.HOW;
    given $how {
        when RakuppBeginTraits::Metamodel::ClassHOW { }
        when Metamodel::ClassHOW { $how does RakuppBeginTraits::Metamodel::ClassHOW }
        default { die "not a class: " ~ $how.^name }
    }
    pkg.^rbt-add($name);
}

BEGIN {
    multi trait_mod:<is>(Method:D \m, Bool:D :$rbt-rpc!) is export { onto($*PACKAGE, m.name) }
    multi trait_mod:<is>(Method:D \m, Str:D :$rbt-rpc!) is export { onto($*PACKAGE, $rbt-rpc) }
    sub rbt-begin-sub() is export { "from a BEGIN" }
}
