# `.^get_attribute_for_usage('$!x')` — the metamodel's lookup of ONE attribute's
# meta-object, by name. It did not exist here, so AttrX::Mooish (which reaches
# every lazy attribute through `self.^get_attribute_for_usage('$!' ~ $name)`)
# stopped on its first test with "No such method 'get_attribute_for_usage'".
# Like Rakudo, the lookup sees the class's OWN attributes only.
use Test;
plan 5;

class Base { has $.b }
class Kid is Base { has $.k; has Int $.n }

is Kid.^get_attribute_for_usage('$!k').name, '$!k',  'an attribute by name';
is Kid.^get_attribute_for_usage('$!n').type.^name, 'Int',
                                                     'the meta-object carries its type';
is Kid.^get_attribute_for_usage('$!k').package.^name, 'Kid',
                                                     '…and its package';
ok Kid.new(k => 1, b => 2).^get_attribute_for_usage('$!k').defined,
                                                     'an instance answers it too';
dies-ok { Kid.^get_attribute_for_usage('$!nope') },  'an unknown attribute throws';
