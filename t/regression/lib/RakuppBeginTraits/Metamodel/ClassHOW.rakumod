# For t/regression/module-trait-exported-from-begin.raku: a metaclass role
# under the dist's own Metamodel:: namespace, as Cro::RPC::JSON has one.
unit role RakuppBeginTraits::Metamodel::ClassHOW;
has %!rbt;
method rbt-add(Mu \type, Str:D $n) { %!rbt{$n} = True }
method rbt-has(Mu \type, Str:D $n) { %!rbt{$n} // False }
