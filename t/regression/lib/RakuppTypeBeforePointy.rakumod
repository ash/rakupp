# For t/regression/module-type-before-pointy-block.raku: types a module
# declares, under plain and qualified names, the way Cro::Iri is declared.
class RakuppTBP { }
class RakuppTBP::Inner { }
role RakuppTBPRole { }
grammar RakuppTBP::Grammar { token TOP { 'x' } }
