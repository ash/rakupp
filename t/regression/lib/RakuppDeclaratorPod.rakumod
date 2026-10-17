# Fixture for t/regression/exe-declarator-pod.raku: declarator pod in a MODULE
# file, which the module loader used to parse without it (no .WHY at all).
unit module RakuppDeclaratorPod;

#| a module sub's lead
sub documented is export {
    #= …and its trailer
    'ran'
}

class Documented is export {
    has $.size; #= a module attribute
}
